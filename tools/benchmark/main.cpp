/**
 * EdgeForge Labs - Milestone 1: Host-Side Test Harness
 * USB CDC RTT Benchmark Tool
 * * Purpose: Emulates Android host packets to measure Round-Trip Time (RTT)
 * Protocol: Tekkura (Start=0xFE, End=0xFF, Cmd=0x01)
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <numeric>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

// --- CONFIGURATION ---
const char* DEFAULT_PORT = "/dev/ttyACM0";
const uint8_t START_MARKER = 0xFE;
const uint8_t END_MARKER   = 0xFF;
const uint8_t CMD_SET_MOTOR = 0x01;
const int BAUDRATE = B115200;
const int TEST_ITERATIONS = 100;
const int EXPECTED_RESPONSE_SIZE = 34;

struct BenchmarkOptions {
    std::string port = DEFAULT_PORT;
    bool debug = false;
    bool show_help = false;
};

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [port] [--debug]" << std::endl;
    std::cout << "  port      Optional serial device path (default: " << DEFAULT_PORT << ")" << std::endl;
    std::cout << "  --debug   Print extra serial benchmark diagnostics" << std::endl;
}

BenchmarkOptions parse_args(int argc, char* argv[]) {
    BenchmarkOptions options;
    bool port_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            options.debug = true;
        } else if (arg == "--help" || arg == "-h") {
            options.show_help = true;
        } else if (arg.rfind("--", 0) == 0) {
            throw std::invalid_argument("Unknown option: " + arg);
        } else if (!port_set) {
            options.port = arg;
            port_set = true;
        } else {
            throw std::invalid_argument("Unexpected extra positional argument: " + arg);
        }
    }

    return options;
}

class SerialPort {
public:
    int fd;
    SerialPort(const char* portName) {
        fd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
        if (fd < 0) throw std::runtime_error("Failed to open serial port.");
        
        struct termios tty;
        tcgetattr(fd, &tty);
        cfsetospeed(&tty, BAUDRATE);
        cfsetispeed(&tty, BAUDRATE);
        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
        tty.c_cflag |= (CREAD | CLOCAL);
        tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
        tty.c_oflag &= ~OPOST;
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 1; 
        tcsetattr(fd, TCSANOW, &tty);
        
        int flags;
        ioctl(fd, TIOCMGET, &flags);
        flags |= TIOCM_DTR | TIOCM_RTS;
        ioctl(fd, TIOCMSET, &flags);
        tcflush(fd, TCIOFLUSH);
    }
    ~SerialPort() { if (fd >= 0) close(fd); }

    void flush() { tcflush(fd, TCIOFLUSH); }

    void write_data(const std::vector<uint8_t>& data) {
        if (write(fd, data.data(), data.size()) < 0) {
            throw std::runtime_error("Failed to write to serial port.");
        }
    }
};

struct Stats {
    double min = 0, max = 0, avg = 0, std_dev = 0;
    int lost = 0;
};

Stats calculate_stats(const std::vector<double>& latencies, int total_sent) {
    Stats s;
    if (latencies.empty()) { s.lost = total_sent; return s; }
    s.lost = total_sent - latencies.size();
    auto [min_it, max_it] = std::minmax_element(latencies.begin(), latencies.end());
    s.min = *min_it; s.max = *max_it;
    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    s.avg = sum / latencies.size();
    double sq_sum = std::inner_product(latencies.begin(), latencies.end(), latencies.begin(), 0.0);
    s.std_dev = std::sqrt(sq_sum / latencies.size() - s.avg * s.avg);
    return s;
}

int main(int argc, char* argv[]) {
    BenchmarkOptions options;
    try {
        options = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    std::cout << "--- USB CDC RTT Benchmark (Host Harness) ---" << std::endl;
    
    try {
        if (options.debug) {
            std::cout << "[DEBUG] Using serial port: " << options.port << std::endl;
        }

        SerialPort serial(options.port.c_str());
        std::vector<double> results;
        results.reserve(TEST_ITERATIONS);

        std::cout << "Waiting 5 seconds for Pico to boot..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        serial.flush();
        
        std::vector<uint8_t> tx_packet = {START_MARKER, CMD_SET_MOTOR, 0x00, 0x00, END_MARKER};

        if (options.debug) {
            std::cout << "[DEBUG] TX packet:";
            for (uint8_t byte : tx_packet) {
                std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
            }
            std::cout << std::dec << std::setfill(' ') << std::endl;
        }

        std::cout << "Starting measurement loop..." << std::endl;
        
        for (int i = 0; i < TEST_ITERATIONS; i++) {
            serial.flush();
            auto t_start = std::chrono::high_resolution_clock::now();
            serial.write_data(tx_packet);
            
            std::vector<uint8_t> rx;
            uint8_t c;
            int status = 0;
            auto t_end = std::chrono::high_resolution_clock::now();
            
            while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t_start).count() < 200) {
                if (read(serial.fd, &c, 1) > 0) {
                    rx.push_back(c);
                    if (rx.size() == EXPECTED_RESPONSE_SIZE) {
                        t_end = std::chrono::high_resolution_clock::now();
                        if (rx[0] == START_MARKER && rx[1] == CMD_SET_MOTOR) status = 1;
                        break;
                    }
                }
            }

            if (status == 1) {
                std::chrono::duration<double, std::milli> rtt = t_end - t_start;
                results.push_back(rtt.count());
                if (i % 10 == 0) std::cout << "." << std::flush;
            } else {
                if (options.debug) {
                    if (rx.empty()) {
                        std::cout << "\n[DEBUG] No response received before timeout." << std::endl;
                    } else {
                        std::cout << "\n[DEBUG] Unexpected response (" << rx.size() << " bytes):";
                        for (uint8_t byte : rx) {
                            std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
                        }
                        std::cout << std::dec << std::setfill(' ') << std::endl;
                    }
                }
                std::cout << "X" << std::flush;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        Stats stats = calculate_stats(results, TEST_ITERATIONS);
        std::cout << "\n\n=== BENCHMARK RESULTS (ms) ===" << std::endl;
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "Success Rate: " << (100.0 - (stats.lost * 100.0 / TEST_ITERATIONS)) << "%" << std::endl;
        std::cout << "Min: " << stats.min << " ms" << std::endl;
        std::cout << "Max: " << stats.max << " ms" << std::endl;
        std::cout << "Avg: " << stats.avg << " ms" << std::endl;
        std::cout << "Jitter: " << stats.std_dev << " ms" << std::endl;
        std::cout << "==============================" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
