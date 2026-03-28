# Milestone 2: TinyFrame Integration & RTT Benchmark Implementation Plan

This document outlines the plan for integrating the [TinyFrame](https://github.com/MightyPork/TinyFrame) library into the RP2040 firmware to replace the current custom serial framing, and the subsequent benchmarking process.

## 1. Objectives
- Replace manual framing in `serial_comm_manager.c` with TinyFrame.
- Ensure reliable communication between the RP2040 and the Android host.
- Measure and compare Round-Trip Time (RTT) latency against the baseline.
- Maintain compatibility with existing commands (GET_LOG, SET_MOTOR_LEVEL, etc.).

## 2. Proposed Changes

### A. Library Integration
- **Add TinyFrame**: Add `TinyFrame.c` and `TinyFrame.h` to the project (suggested: `src/tinyframe/`).
- **Configuration**: Create `TF_Config.h` in `include/` with optimized settings for the RP2040:
  - `TF_ID_BYTES`: 1 (Small number of concurrent requests)
  - `TF_LEN_BYTES`: 2 (To accommodate log packets)
  - `TF_TYPE_BYTES`: 1 (For command IDs)
  - `TF_CKSUM_TYPE`: `TF_CKSUM_CRC16` or `TF_CKSUM_XOR` for better reliability.

### B. Firmware Refactoring (`src/serial_comm_manager.c`)
- **Initialization**: 
  - Initialize a global `TinyFrame` instance in `serial_comm_manager_init`.
  - Register listener functions for each command type using `TF_AddTypeListener`.
- **Data Reception (`get_block`)**:
  - Replace the manual state machine with a loop feeding `getchar_timeout_us` results into `TF_AcceptChar`.
- **Data Transmission**:
  - Implement `TF_WriteImpl` using `putchar` or `uart_putc`.
  - Use `TF_Send` or `TF_Respond` for sending state and log data.
- **Packet Handling**:
  - Refactor `handle_packet` logic into individual TF listener callbacks.

### C. Protocol Definition (`include/serial_comm_manager.h`)
- Remove legacy `IncomingPacketFromAndroid` and `OutgoingPacketToAndroid` structs.
- Define `TF_TYPE` constants matching current command IDs.
- Ensure `RP2040_STATE` remains packed for efficient transmission.

### D. Benchmark Tool Update (`tools/benchmark/`)
- Update `main.cpp` to support TinyFrame:
  - Integrate TinyFrame (C++ or C version) into the host-side harness.
  - Wrap existing `CMD_SET_MOTOR` requests in TinyFrame packets.
  - Update expected response handling to decode TinyFrame frames.

## 3. Implementation Steps

| Step | Task                                                                  | Files Affected                |
|:-----|:----------------------------------------------------------------------|:------------------------------|
| 1    | Import TinyFrame source and create `TF_Config.h`                      | `src/`, `include/TF_Config.h` |
| 2    | Update `CMakeLists.txt` to include new source files                   | `CMakeLists.txt`              |
| 3    | Implement `TF_WriteImpl` and initialize TF in `serial_comm_manager.c` | `src/serial_comm_manager.c`   |
| 4    | Refactor `get_block` to use `TF_AcceptChar`                           | `src/serial_comm_manager.c`   |
| 5    | Migrate command handling to TF Type Listeners                         | `src/serial_comm_manager.c`   |
| 6    | Refactor benchmark tool to use TinyFrame                              | `tools/benchmark/main.cpp`    |
| 7    | Run benchmarks and update `BENCHMARK_TEST_RESULTS.md`                 | `BENCHMARK_TEST_RESULTS.md`   |

## 4. Risks & Considerations
- **Overhead**: TinyFrame adds a small header/checksum overhead per packet. We need to verify if this significantly impacts RTT.
- **Memory**: Ensure the TF buffer sizes in `TF_Config.h` are sufficient for the largest packets (e.g., `RP2040_STATE` and logs) but don't exceed SRAM limits.
- **Synchronization**: TinyFrame handles frame boundaries, reducing the risk of "stuck" states seen in the current manual implementation.
