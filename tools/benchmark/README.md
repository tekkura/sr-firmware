# EdgeForge Benchmark Tool (Milestone 1)

This is a high-performance, native C++ host-side tool designed to measure the Round-Trip Time (RTT) latency between a PC (Linux/WSL) and the Raspberry Pi Pico RP2040 running the Tekkura robot firmware.

It establishes a baseline for USB CDC communication efficiency using the custom binary protocol implemented in the firmware.

## Features

* **Fully Integrated Pipeline:** The benchmark is completely integrated into the project's Dockerized Make targets, ensuring zero host-side dependency issues.
* **Robust C++ Design:** Implements RAII principles via a custom `SerialPort` class to ensure safe resource management, buffer flushing, and automatic port closure.
* **Precise Timing:** Uses `std::chrono::high_resolution_clock` for microsecond-level measurement precision.
* **Statistical Reporting:** Automates 100 measurement loops and generates a detailed statistical report (Mean, Jitter/StdDev, Min, Max, and Success Rate).

## Prerequisites

* **OS:** Linux or WSL2 (Windows Subsystem for Linux).
* **Hardware:** CustomPCB or Raspberry Pi Pico (RP2040) connected via USB (typically recognized as `/dev/ttyACM0`).
* **Environment:** Docker must be installed and running.

## Build and Run Instructions

Per the Milestone 1 requirements, there is no separate ad-hoc pipeline. Building the firmware, flashing the board, and running the host benchmark tool are all handled by a single Make target.

### Option A: Testing on a standard Raspberry Pi Pico (Dev/Local)
To test on a standard Pico, the build system uses board gating to safely skip custom-PCB-only initialization (I2C, motor drivers) while preserving the main loop behavior.

```bash
make test TEST=rtt BOARD=pico

```

*Note: If you do not have a hardware debug probe (CMSIS-DAP) connected, the automated OpenOCD flash step will fail to prevent benchmarking stale firmware. In this case, you must manually flash the generated `build/robot.uf2` to the Pico in BOOTSEL mode, and append `SKIP_FLASH=1` to bypass the hardware flasher:*

```bash
make test TEST=rtt BOARD=pico SKIP_FLASH=1

```

### Option B: Testing on the Custom PCB (Production)

To run the benchmark on the actual robot hardware with all sensors, battery ICs, and motor drivers enabled, use:

```bash
make test TEST=rtt BOARD=customPCB

```

*(Note: Since `customPCB` is the default board in the Makefile, running `make test TEST=rtt` will yield the same result).*

*Hardware Requirement: This target expects a CMSIS-DAP debug probe to be connected to the custom board's SWD pins for automated flashing via OpenOCD. It will compile, flash the board, and immediately run the host-side benchmark.*

### Developer Tip: Fast Iteration (Run Benchmark Only)

If you have already flashed the firmware and only want to modify or re-run the C++ host benchmark tool without rebuilding the firmware, you can bypass the `make` pipeline and run the benchmark directly inside the Docker environment:

```bash
docker run --rm -it --device /dev/ttyACM0:/dev/ttyACM0 -v $(pwd):/project -w /project topher217/smartphone-robot-firmware:latest bash -c "g++ -O2 -std=c++17 tools/benchmark/main.cpp -o tools/benchmark/benchmark && ./tools/benchmark/benchmark"

```

## Note about `robot.c` and Hardware Gating

To test locally on a standard Raspberry Pi Pico while maintaining support for the `customPCB`, proper board gating was implemented.
Using `#ifndef BOARD_PICO` macros, custom-PCB-only initializations (I2C, battery ICs, motor unit tests) are conditionally skipped to prevent hardware hard-faults.

## Protocol Details (SET_MOTOR_LEVEL)

The tool targets the specific `SET_MOTOR_LEVEL` path for Milestone 1:

* **Baud Rate:** 115200 (Virtual CDC)
* **Start Marker:** `0xFE`
* **Command:** `0x01` (SET_MOTOR_LEVEL)
* **End Marker:** `0xFF`
* **TX Packet:** 5 Bytes `[START, CMD, 0x00, 0x00, END]`
* **RX Expected Response:** 34 Bytes (The Pico responds by packing the entire `RP2040_STATE` structure).

## Expected Output

Typical results:

```text
--- USB CDC RTT Benchmark (Host Harness) ---
Waiting 5 seconds for Pico to boot...
Starting measurement loop...
..........

=== BENCHMARK RESULTS (ms) ===
Success Rate: 100.000%
Min: 8.182 ms
Max: 26.316 ms
Avg: 14.820 ms
Jitter: 1.638 ms
==============================

```