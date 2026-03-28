/**
 * EdgeForge Labs - Milestone 2: Host-Side Test Harness
 * USB CDC RTT Benchmark Tool
 * * Purpose: Emulates Android host packets using TinyFrame protocol
 * Protocol: TinyFrame (SOF=0x01, Checksum=CRC16-ARC)
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

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

// --- CONFIGURATION ---
const char* DEFAULT_PORT = "/dev/ttyACM0";
const uint8_t TF_SOF = 0x01;
const uint8_t CMD_SET_MOTOR = 0x01;
const int BAUDRATE = B115200;
const int TEST_ITERATIONS = 100;
const int EXPECTED_PAYLOAD_SIZE = 29; // RP2040_STATE size
const int EXPECTED_RESPONSE_SIZE = 9 + EXPECTED_PAYLOAD_SIZE; // 9 bytes TF overhead + 29 bytes RP2040_STATE

// --- CRC16-ARC Implementation (Matches TinyFrame's Reflected CRC16) ---
uint16_t crc16_update(uint16_t crc, uint8_t data) {
    static const uint16_t table[256] = {
        0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
        0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
        0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
        0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
        0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
        0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
        0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
        0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
        0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
        0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
        0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
        0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
        0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
        0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
        0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
        0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
        0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
        0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
        0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
        0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
        0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
        0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
        0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
        0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
        0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
        0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
        0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
        0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
        0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
        0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
        0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
        0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
    };
    return (crc >> 8) ^ table[(crc ^ data) & 0xFF];
}

std::vector<uint8_t> wrap_tinyframe(uint8_t type, const std::vector<uint8_t>& data, uint8_t& id_out) {
    static uint8_t next_id = 1;
    std::vector<uint8_t> packet;
    packet.push_back(TF_SOF);

    uint16_t head_cksum = 0;
    head_cksum = crc16_update(head_cksum, TF_SOF);

    // ID (1 byte)
    uint8_t id = (next_id++ & 0x7F) | 0x80; // Master bit set
    id_out = id;
    packet.push_back(id);
    head_cksum = crc16_update(head_cksum, id);

    // LEN (2 bytes, BIG ENDIAN in TinyFrame)
    uint16_t len = (uint16_t)data.size();
    packet.push_back((len >> 8) & 0xFF);
    head_cksum = crc16_update(head_cksum, (len >> 8) & 0xFF);
    packet.push_back(len & 0xFF);
    head_cksum = crc16_update(head_cksum, len & 0xFF);

    // TYPE (1 byte)
    packet.push_back(type);
    head_cksum = crc16_update(head_cksum, type);

    // Header Checksum (2 bytes, BIG ENDIAN)
    packet.push_back((head_cksum >> 8) & 0xFF);
    packet.push_back(head_cksum & 0xFF);

    // Data
    uint16_t data_cksum = 0;
    for (uint8_t b : data) {
        packet.push_back(b);
        data_cksum = crc16_update(data_cksum, b);
    }

    // Data Checksum (2 bytes, BIG ENDIAN)
    if (len > 0) {
        packet.push_back((data_cksum >> 8) & 0xFF);
        packet.push_back(data_cksum & 0xFF);
    }

    return packet;
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

bool validate_tinyframe(const std::vector<uint8_t>& rx, uint8_t expected_type, uint8_t expected_id, bool debug) {
    if (rx.size() < 9) return false;
    if (rx[0] != TF_SOF) {
        if (debug) std::cout << "\n[DEBUG] Invalid SOF: 0x" << std::hex << (int)rx[0] << std::dec << std::endl;
        return false;
    }

    // Header checksum validation
    uint16_t head_cksum_calc = 0;
    for (int i = 0; i < 5; ++i) head_cksum_calc = crc16_update(head_cksum_calc, rx[i]);
    uint16_t head_cksum_recv = (rx[5] << 8) | rx[6];
    if (head_cksum_calc != head_cksum_recv) {
        if (debug) std::cout << "\n[DEBUG] Header checksum mismatch. Calc: 0x" << std::hex << head_cksum_calc << " Recv: 0x" << head_cksum_recv << std::dec << std::endl;
        return false;
    }

    // ID validation
    if (rx[1] != expected_id) {
        if (debug) std::cout << "\n[DEBUG] ID mismatch. Expected: 0x" << std::hex << (int)expected_id << " Recv: 0x" << (int)rx[1] << std::dec << std::endl;
        return false;
    }

    if (!(rx[1] & 0x80)) {
        if (debug) std::cout << "\n[DEBUG] Missing response bit in ID: 0x" << std::hex << (int)rx[1] << std::dec << std::endl;
        return false;
    }

    // Type validation
    if (rx[4] != expected_type) {
        if (debug) std::cout << "\n[DEBUG] Type mismatch. Expected: 0x" << std::hex << (int)expected_type << " Recv: 0x" << (int)rx[4] << std::dec << std::endl;
        return false;
    }

    // Length validation
    uint16_t len = (rx[2] << 8) | rx[3];
    if (len != EXPECTED_PAYLOAD_SIZE) {
        if (debug) std::cout << "\n[DEBUG] Payload length mismatch. Expected: " << EXPECTED_PAYLOAD_SIZE << " Recv: " << len << std::endl;
        return false;
    }

    if (rx.size() < (size_t)(9 + len)) return false;

    // Data checksum validation
    uint16_t data_cksum_calc = 0;
    for (int i = 7; i < 7 + len; ++i) data_cksum_calc = crc16_update(data_cksum_calc, rx[i]);
    uint16_t data_cksum_recv = (rx[7 + len] << 8) | rx[8 + len];
    if (data_cksum_calc != data_cksum_recv) {
        if (debug) std::cout << "\n[DEBUG] Data checksum mismatch. Calc: 0x" << std::hex << data_cksum_calc << " Recv: 0x" << data_cksum_recv << std::dec << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    const char* port = (argc > 1) ? argv[1] : DEFAULT_PORT;
    bool debug = false;
    for(int i=1; i<argc; i++) if(std::string(argv[i]) == "--debug") debug = true;

    std::cout << "--- USB CDC RTT Benchmark (TinyFrame Host) ---" << std::endl;

    try {
        SerialPort serial(port);
        std::vector<double> results;
        results.reserve(TEST_ITERATIONS);

        std::cout << "Waiting 5 seconds for Pico to boot..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        serial.flush();

        std::vector<uint8_t> payload = {0x00, 0x00}; // Left, Right motor levels
        uint8_t last_id = 0;
        std::vector<uint8_t> tx_packet = wrap_tinyframe(CMD_SET_MOTOR, payload, last_id);

        if (debug) {
            std::cout << "[DEBUG] Sending packet (" << tx_packet.size() << " bytes): ";
            for(auto b : tx_packet) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
            std::cout << std::dec << std::endl;
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
                        if (validate_tinyframe(rx, CMD_SET_MOTOR, last_id, debug)) {
                            status = 1;
                        }
                        break;
                    }
                }
            }

            if (status == 1) {
                std::chrono::duration<double, std::milli> rtt = t_end - t_start;
                results.push_back(rtt.count());
                if (i % 10 == 0) std::cout << "." << std::flush;
            } else {
                if (debug) {
                    if (rx.empty()) {
                         std::cout << "\n[DEBUG] No data received within timeout." << std::endl;
                    } else if (rx.size() != EXPECTED_RESPONSE_SIZE) {
                        std::cout << "\n[DEBUG] Wrong RX size: " << rx.size() << " bytes: ";
                        for(auto b : rx) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
                        std::cout << std::dec << std::endl;
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