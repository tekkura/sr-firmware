# Milestone 3: Length-Prefix + CRC Framing Implementation Plan

This document outlines the plan for implementing a custom lightweight framing protocol for the RP2040 firmware to replace the legacy start/end marker system.

## 1. Objectives
- Implement a custom framing protocol using a length prefix and CRC16 checksum.
- Remove dependence on the complex TinyFrame library for comparison.
- Minimize memory footprint while ensuring data integrity.
- Measure Round-Trip Time (RTT) and compare performance with legacy and TinyFrame implementations.

## 2. Proposed Protocol Format
Each packet will follow this structure:

| Byte Offset    | Field          | Size    | Description                                          |
|:---------------|:---------------|:--------|:-----------------------------------------------------|
| 0              | `START_MARKER` | 1 byte  | Constant `0x01` (SOH - Start of Header)              |
| 1-2            | `LENGTH`       | 2 bytes | Total length of Payload + Command ID (Little-Endian) |
| 3              | `COMMAND_ID`   | 1 byte  | Command Type (e.g., `GET_LOG`, `SET_MOTOR`)          |
| 4 to (N+3)     | `PAYLOAD`      | N bytes | Actual data bytes                                    |
| (N+4) to (N+5) | `CRC16`        | 2 bytes | CRC16-CCITT checksum over bytes 1 to (N+3)           |

## 3. Implementation Steps

### Step 1: Implementation of CRC16 Utility
- Add a lightweight CRC16-CCITT calculation function to a new `src/crc.c`.
- Ensure the algorithm is efficient for the RP2040 (M0+ core).

### Step 2: Protocol Refactoring (`src/serial_comm_manager.c`)
- **Data Reception**:
    - Refactor `get_block()` to wait for the `START_MARKER`.
    - Read the 2-byte `LENGTH` prefix.
    - Read the remaining bytes (Command + Payload) based on the length.
    - Calculate the CRC of the received data and verify against the trailing 2 bytes.
    - If CRC fails, send a `NACK`.
- **Data Transmission**:
    - Create a helper function `send_framed_packet(uint8_t cmd, uint8_t* data, uint16_t len)`.
    - Calculate and append the CRC automatically before sending via `putchar`.

### Step 3: Migration of Packet Handlers
- Update `handle_packet` to work with the new raw buffer and validated length.
- Ensure `RP2040_STATE` is correctly packed into the new frame format.

### Step 4: Benchmark Tool Update (`tools/benchmark/main.cpp`)
- Update the host-side harness to generate and verify the new Length-Prefix + CRC frames.
- Maintain compatibility with the existing RTT measurement logic.

### Step 5: Validation & Benchmarking
- Run the RTT benchmark loop.
- Compare results against the baseline (Legacy) and previous TinyFrame metrics.
- Record findings in `BENCHMARK_TEST_RESULTS.md`.

## 4. Risks & Considerations
- **Sync Issues**: If a packet is interrupted, the receiver needs a way to re-sync (usually by looking for the next `START_MARKER`).
- **Endianness**: Ensure the `uint16_t` length and CRC are handled consistently (Little-Endian) between the Pico and the Host.
- **Buffer Limits**: Ensure the firmware's reception buffer is large enough for the maximum possible log packet.
