# Implementation Strategy: Firmware Logging & Telemetry Separation (Option B)

This document details the implementation strategy for isolating diagnostic firmware logging and latency benchmark telemetry from the primary USB CDC command-and-response channel. 

Based on project decisions, we adopt **Option B (Dedicated Out-of-Band Hardware UART)** as our general physical architecture. This provides absolute physical isolation, ensuring zero interference with the primary USB command-and-response protocol.

We address two different operational workflows under this architecture:
1. **Option 1: Real-Time Binary Streaming & On-The-Fly Parsing** (for active, live-monitored debugging with sub-millisecond overhead).
2. **Option 2: Post-Run File-Merging & Offline Alignment** (for non-real-time file aggregation).

---

## General Physical Architecture

We decouple control traffic and debug/telemetry data physically by utilizing one of the RP2040's hardware UART peripherals mapped to dedicated GPIO pins.

```
       +--------------------+               +--------------------+
       |   Android Phone    | --[ADB]------>|   Development PC   |
       +---------+----------+ (Test results)+---------+----------+
                 |                                    ^
                 | USB CDC                            | USB
                 | (Time-Critical Commands)           | (Logging / Telemetry)
                 v                                    |
       +---------+----------+               +---------+----------+
       |     RP2040 Board   | --[UART GPIO]-->|   Pico Debugger/   |
       |  (Control Loop)    |   (Pins 16/17)  |  USB-UART Bridge   |
       +--------------------+               +--------------------+
```

* **Control Transport (Primary USB CDC):** Standard command packets (e.g., motor drive, status requests) continue to run over `pico_stdio_usb`.
* **Logging/Telemetry Transport (Dedicated UART):** Raw logs, execution timestamps, and diagnostic output are routed through the RP2040's `uart0` or `uart1` peripheral connected via a Pico Debugger / USB-to-UART converter to the host PC.

---

## Workflow Option 1: Real-Time Binary Streaming & On-The-Fly Parsing

This option achieves real-time, low-overhead telemetry alignment on the PC while maintaining control loop frequency. It eliminates the two main sources of micro-controller latency overhead: string formatting (`printf`) and blocking/waiting serial writes.

### Core Concept:
1. **Latency Benchmark build mode:** Add build option for firmware that modifies the default packet structure, adding the telemetry packet.
2. **Binary Telemetry Packet:** Instead of converting integers to characters (which takes up to $300\,\mu\text{s}$ due to the lack of hardware division on Cortex-M0+), the firmware logs telemetry as raw binary structs.
   ```c
   #pragma pack(1)
   typedef struct {
       uint32_t timestamp_us;  // 32-bit microsecond clock (wraps after 71 mins)
       uint16_t command_id;    // Unique hash/ID for mapping to Android command
       uint16_t event_id;      // Event type (e.g., RX_COMPLETE = 1, TX_START = 2)
   } TelemetryEvent;
   #pragma pack()
   ```
3. **On-The-Fly Parsing:** Android phone reads the binary UART stream in real-time, parses the binary structs into readable strings, and matches command IDs with its data. Results can be streamed concurrently over wireless ADB or stored for later retrieval.

---

## Workflow Option 2: Post-Run File-Merging & Offline Alignment

This option prioritizes simplicity and robust data capture over live visualization. The system stores high-precision timestamps locally on both ends during active test execution and aligns the files after the benchmark completes.

### Core Concept:
1. **Local Recording:** During the test run, both the Android Phone and the RP2040 write execution telemetry locally with common command identifiers (`command_id` / transaction hash).
2. **Pico Local Storage:** The RP2040 firmware writes simple ASCII telemetry lines (e.g., `cmd_id:event_id:timestamp_us\n`) or binary chunks directly to its UART line. A simple PC logging utility (e.g., `tio`, `minicom`, or `putty`) captures the incoming UART stream into a raw file (`pico_telemetry.log`).
3. **Android Local Storage:** The RTT test app on the phone records transit timings and command IDs to a local file.
4. **Offline Alignment:** Once the test run ends, the developer runs a local script on the PC. The script fetches phone and firmware files, matches transaction IDs, and computes granular latency differences.