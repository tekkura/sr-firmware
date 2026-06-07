# Implementation Strategy: Firmware Logging & Telemetry Separation

This document analyzes different architectural approaches to isolate diagnostic firmware logging and latency benchmark telemetry from the primary USB CDC command-and-response transport. The primary objective is to prevent protocol stream corruption and eliminate measurement bias/overhead during high-rate, time-critical control loops.

---

## Comparative Analysis of Proposed Options

| Metric                          | Option A: Dual USB CDC (Logical Isolation)        | Option B: Dedicated Hardware UART            | Option C: In-Band TinyFrame Multiplexing (Logical Separation) | Option D: Local Memory Buffering & Post-Benchmark Retrieval (Deferred Dump) | Option E: SEGGER RTT via SWD           |
|:--------------------------------|:--------------------------------------------------|:---------------------------------------------|:--------------------------------------------------------------|:----------------------------------------------------------------------------|:---------------------------------------|
| **Physical Complexity**         | **None** (Existing USB cable)                     | **Medium** (GPIO wiring + USB-TTL converter) | **None** (Existing USB cable)                                 | **None** (Existing USB cable)                                               | **High** (Requires debug probe/SWD)    |
| **Firmware Complexity**         | **Medium** (TinyUSB descriptor tuning)            | **Low** (Simple hardware UART writes)        | **Medium** (TinyFrame packet wrappers)                        | **Low-Medium** (RAM storage + dump handler)                                 | **Low** (Include RTT library)          |
| **Android Complexity**          | **Medium** (Secondary serial thread)              | **High** (Separate hardware/accessory)       | **Medium** (Async stream parser)                              | **Low-Medium** (Post-run retrieval cmd)                                     | **N/A** (PC-only)                      |
| **Throughput / Latency Impact** | **High** (Native USB speed, no command bus block) | **Medium** (Limited by hardware baud rate)   | **Low-Medium** (Shares bus; adds framing tax)                 | **Zero Runtime Overhead** (Writes directly to RAM)                          | **Extremely High** (Direct RAM access) |
| **Suitability**                 | **Best for Mobile / Field Diagnostics**           | **Best for Lab Bench Testing**               | **Alternative if USB CDC remains single**                     | **Best for Latency-Sensitive Benchmarks**                                   | **Developer-only debugging**           |

---

## Detailed Propositions

### Option A: Dual USB CDC Interfaces (Logical Physical Isolation)

This approach configures the RP2040's TinyUSB stack to present **two** separate virtual serial ports (CDC 0 and CDC 1) over a single physical USB connection.

```
                          +-------------------+
                          |    Android Phone  |
                          |                   |
                          |  +-------------+  |
                          |  | Control App |  |
                          |  +------+------+  |
                          |         |         |
                          +---------|---------+
                                    | USB Cable (Composite Device)
                                    v
                 +------------------+------------------+
                 |  CDC 0 (Commands)   CDC 1 (Logging) |
                 +------------------+------------------+
                 |                  |                  |
                 v                  v                  v
         [serial_comm_manager]  [rp2040_log]       [RP2040 MCU]
```

#### Technical Plan:
1. **TinyUSB Configuration:**
   * Modify the USB descriptors (`tusb_config.h` or configuration files) to expose multiple CDC interfaces:
     ```c
     #define CFG_TUD_CDC 2
     ```
2. **Pico SDK Mapping:**
   * Direct the standard command stream through `CDC 0` (via standard `pico_stdio_usb`).
   * Bind the `rp2040_log` functions to write raw ASCII log messages directly to `CDC 1` using TinyUSB's port-indexed write commands:
     ```c
     tud_cdc_n_write(1, buffer, len);
     tud_cdc_n_write_flush(1);
     ```
3. **Android Client Integration:**
   * Use an Android USB-Serial library that supports composite devices.
   * Open both logical ports concurrently: one thread drives control cycles on `CDC 0`, while a secondary daemon thread continuously drains log messages from `CDC 1`.

---

### Option B: Out-of-Band Hardware UART (Physical Isolation)

This approach leverages one of the RP2040's on-chip UART peripherals to route logs entirely away from the USB CDC interface onto physical GPIO pins. This mode is partially supported in `src/rp2040_log.c` via the `-DLOGGER_UART` compiler flag.

```
    +-------------------+                 +-------------------+
    |    Android Phone  |                 |   Development PC  |
    +---------+---------+                 +---------+---------+
              |                                     ^
              | USB CDC (Commands Only)             | USB (Log stream)
              v                                     |
    +---------+---------+                 +---------+---------+
    |   RP2040 MCU      | --(UART GPIO)--> |   USB-to-UART     |
    |                   |  TX:16, RX:17   |    Converter      |
    +-------------------+                 +-------------------+
```

#### Technical Plan:
1. **Hardware Hookup:**
   * Route firmware logs through `uart0` on `GPIO16` (TX) and `GPIO17` (RX).
   * Connect these pins to an external USB-to-TTL UART adapter (e.g., CP2102, FT232R) plugged into a development machine or testing workstation.
2. **Firmware Routing:**
   * Configure `rp2040_log_init()` to initialize the UART driver at a high baud rate (e.g., `921600` or `115200`).
   * When `LOGGER_UART` is active, routing bypassed standard `stdio` entirely, writing directly to UART registers:
     ```c
     uart_puts(LOG_UART, buffer);
     ```

---

### Option C: In-Band TinyFrame Multiplexing (Framed Separation)

This approach keeps a single USB CDC interface but uses a logical framing engine (**TinyFrame**) to multiplex both command structures and logging messages over the same physical byte stream.

```
+-------------------------------------------------------------+
| USB CDC physical packet stream                              |
|  +--------------------+  +-------------------------------+  |
|  | TF Header: TYPE_CMD |  | TF Header: TYPE_LOG           |  |
|  | Payload: Motor level|  | Payload: [DEBUG] Encoder high |  |
|  +--------------------+  +-------------------------------+  |
+-------------------------------------------------------------+
```

#### Technical Plan:
1. **Message Classification:**
   * Define unique TinyFrame type identifiers (e.g., `0x01` for Control Commands, `0x03` for Robot State, and `0x80` for Log Messages).
2. **Buffering & Dispatch:**
   * Rather than emitting raw log text inline (which disrupts protocol synchronicity), log statements are formatted and pushed into a thread-safe circular ring buffer.
   * A low-priority background thread or scheduler loop checks the circular buffer, packages accumulated log lines into TinyFrame packets, and calls `TF_Send` with `TF_TYPE_LOG`.
3. **Host-Side De-multiplexing:**
   * The Android driver continuously feeds incoming USB bytes to the TinyFrame engine.
   * Frame listeners route packets with command responses to the main loop, while log frames (`0x80`) are pushed into an asynchronous logging file/view handler.

---

### Option D: Local Memory Buffering & Post-Benchmark Retrieval (Deferred Dump)

This approach completely eliminates runtime serial bus overhead during active benchmarks. Instead of sending logs/telemetry markers *during* execution, the firmware records high-precision event sequences directly to the RP2040's local memory (RAM) and transmits them to the Android host *on-demand after* the benchmark run is complete.

```
   [ ACTIVE RUN PHASE ]
   Android Command ----(USB CDC)----> RP2040 Core (Fast Control Loop)
                                        | (Write microsecond record to RAM)
                                        v
                                  [ Static RAM Buffer Array ]

   [ RETRIEVAL PHASE (After test ends) ]
   Android GET_LOG ----(USB CDC)----> RP2040 MCU
                                        | (Read records sequentially from RAM)
                                        v
   Android Logger <---(Dump Stream)---- RP2040 MCU
```

#### Technical Plan:
1. **Optimized Binary Telemetry Record Layout:**
   * To maximize memory efficiency, the firmware records microsecond-resolution events using a compact binary layout instead of verbose ASCII strings:
     ```c
     #pragma pack(1)
     typedef struct {
         uint64_t timestamp_us; // High-resolution Pico system absolute microsecond clock
         uint16_t event_id;     // Enumerated event code (e.g. 0x01 = RX_CMD, 0x02 = TX_RESP, 0x03 = PIO_INT)
         uint32_t metadata;     // Auxiliary contextual metric (e.g. motor level, encoder tick count)
     } TelemetryRecord;
     #pragma pack()
     ```
2. **SRAM Buffer Allocation:**
   * The RP2040 features 264KB of low-latency on-chip SRAM.
   * Allocate a static block (e.g., 64KB - 128KB) for telemetry. A 64KB buffer can store up to 4,096 high-resolution telemetry records (each taking 16 bytes), which is more than enough for a typical 30-second benchmark execution:
     ```c
     #define MAX_TELEMETRY_RECORDS 4096
     static TelemetryRecord telemetry_buffer[MAX_TELEMETRY_RECORDS];
     static volatile uint16_t record_count = 0;
     ```
3. **Ultra-Low Overhead Logging Macro:**
   * Implement an optimized, non-blocking telemetry macro that records timestamps in less than a microsecond:
     ```c
     inline void record_telemetry_event(uint16_t event_id, uint32_t metadata) {
         if (record_count < MAX_TELEMETRY_RECORDS) {
             uint16_t idx = record_count++; // Atomic-like increment
             telemetry_buffer[idx].timestamp_us = time_us_64();
             telemetry_buffer[idx].event_id = event_id;
             telemetry_buffer[idx].metadata = metadata;
         }
     }
     ```
4. **Post-Run Dump Command (`GET_LOCAL_BENCHMARK_DATA` / 0x05):**
   * During the active test, absolutely zero print statements are sent over the USB CDC channel.
   * Once the Android host stops sending control packets, it sends a dedicated retrieval command.
   * The firmware intercepts this command, suspends real-time interrupts or background telemetry acquisition, and flushes the entire binary log array over the USB CDC channel.
   * Once transmission completes, the firmware resets `record_count` for the next run.
5. **Post-Processing Alignment:**
   * The Android benchmark app saves the retrieved binary file locally.
   * An offline script maps host-side transmit timestamps (captured on Android) directly against the retrieved device-side absolute microsecond events using a standard baseline synchronization offset.

---

### Option E: SEGGER RTT via SWD (Direct Debug Probe)

This approach isolates logging at the lowest physical layer by using the SWD debug pins. This requires no USB bandwidth and is highly invisible to the RP2040 core execution loop.

#### Technical Plan:
1. **RTT Integration:**
   * Integrate the SEGGER Real-Time Transfer (RTT) sources into the firmware.
   * Re-route `rp2040_log` to write directly to RTT control blocks in RAM.
2. **Capture:**
   * A debugging probe (e.g., Raspberry Pi Picoprobe, J-Link) reads the RTT buffers concurrently over the SWD interface.
   * Logs are displayed live in real-time in the developer's console without affecting the USB stack or timing metrics.

---

## Telemetry Synchronization & Timestamp Alignment

Regardless of the selected real-time transport or deferred local-storage option, host-side and firmware-side logs require a robust clock-synchronization mechanism to reconstruct a combined chronological timeline.

### Proposed Synchronization Protocol:
1. **Baseline Sync Command (`TIME_SYNC` / 0x04):**
   * At the beginning of a benchmark sequence, the Android host sends a `TIME_SYNC` frame containing its current high-precision epoch timestamp ($T_{\text{host\_start}}$) in microseconds.
   * Upon receipt of this packet, the RP2040 captures its internal high-resolution microsecond timer value ($T_{\text{pico\_start}}$) via `time_us_64()`.
   * The RP2040 replies with a confirmation packet containing $T_{\text{pico\_start}}$.
2. **Offset Calculation:**
   * The host calculates the Round-Trip Time (RTT) of the sync operation.
   * The host then establishes a temporal mapping offset:
     $$\text{Offset} = T_{\text{host\_start}} - T_{\text{pico\_start}} - \frac{\text{RTT}}{2}$$
3. **Timeline Merging:**
   * After the run is complete, every log/telemetry timestamp retrieved from the RP2040 is shifted by the computed offset:
     $$T_{\text{aligned}} = T_{\text{firmware}} + \text{Offset}$$
   * This yields a unified chronological trace of the entire command/response sequence.
