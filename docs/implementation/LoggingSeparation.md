# Implementation Strategy: Firmware Logging & Telemetry Separation (Option B)

This document details the definitive implementation plan for isolating diagnostic firmware logging from the primary USB CDC command-and-response channel and adding benchmark logging results to the USB stream.

---

## General Physical Architecture

We decouple control traffic and diagnostic logs physically by utilizing one of the RP2040's hardware UART peripherals mapped to dedicated GPIO pins. Additionally, the Android phone maintains a concurrent connection to the development PC to stream phone-side metrics and real-time logs.

```
       +--------------------+  --[ADB over Wi-Fi / USB]-->  +--------------------+
       |   Android Phone    |    (Phone Logs & Metrics)     |   Development PC   |
       +---------+----------+                               +---------+----------+
                 |                                                    ^
                 | USB CDC                                            | USB
                 | (Time-Critical Commands)                           | (Logging)
                 v                                                    |
       +---------+----------+                               +---------+----------+
       |     RP2040 Board   | -----------[UART GPIO]------->|   Pico Debugger/   |
       |  (Control Loop)    |         (Pins 16/17)          |  USB-UART Bridge   |
       +--------------------+                               +--------------------+
```

* **Control Transport (Primary USB CDC):** Standard command packets (e.g., motor drive, status requests) run over `pico_stdio_usb`.
* **Logging Transport (Dedicated UART):** General logs and diagnostic strings are routed through the RP2040's `uart0` or `uart1` peripheral mapped to pins `GPIO16`/`GPIO17`.
* **Host Debug Transport (Phone to PC):** Phone-side logs are sent directly to the development PC via ADB (USB or Wireless Debugging).

---

## Technical Implementation Plan

The implementation is split into two distinct, high-performance tasks: redirecting logging to the hardware UART interface, and conditionally extending the USB packet structure for active benchmarks.

### Part 1: Redirecting Logging to UART

To ensure that verbose logs never corrupt or collide with binary command frames on the USB CDC channel (`stdio`), the firmware logging library must be configured to output exclusively to the RP2040’s hardware UART controller.

#### 1.1 Compile-Time Selection
Configure the build system to enable UART-based logging. In `CMakeLists.txt`, compile-time flags are added to conditionally define `LOGGER_UART` and bind the desired UART peripheral:

```cmake
# In CMakeLists.txt
add_compile_definitions(LOGGER_UART)
add_compile_definitions(PICO_DEFAULT_UART=0)
add_compile_definitions(PICO_DEFAULT_UART_TX_PIN=16)
add_compile_definitions(PICO_DEFAULT_UART_RX_PIN=17)
```

#### 1.2 Firmware Initialization (`src/rp2040_log.c`)
Ensure that `rp2040_log_init()` initializes the hardware UART peripheral and configures the multiplexing functions on the targeted GPIO pins:

```c
void rp2040_log_init() {
    log_buffer.head = 0;
    log_buffer.tail = 0;
    
    #ifdef LOGGER_UART
    // Initialize UART0 at 115200 (or up to 921600 baud for ultra-high throughput)
    uart_init(LOG_UART, 115200);
    
    // Set up GPIO pins 16 and 17 to operate as UART pins
    gpio_set_function(LOG_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(LOG_UART_RX_PIN, GPIO_FUNC_UART);
    #endif
}
```

#### 1.3 Asynchronous Logging to UART
To maintain non-blocking behavior, the logging routine should populate the existing circular buffer and trigger an asynchronous DMA-backed UART transfer (or an interrupt-driven drain). The implementation should ensure that the primary control loop never waits for the UART hardware peripheral to complete transmission.

```c
void rp2040_log(const char* format, ...) {
    #ifdef LOGGER_UART
    // 1. Format into local buffer
    // 2. Add to circular buffer (non-blocking)
    // 3. Trigger UART interrupt or DMA to drain buffer in background
    #else
    // Legacy behavior
    #endif
}
```

#### 1.4 Eliminating Log Flushing on primary CDC
Any background routines or command handlers (such as `GET_LOG` 0x00 inside `serial_comm_manager.c`) that trigger `rp2040_log_flush()` must be disabled or ignored when `LOGGER_UART` is active. This guarantees that `printf` never writes unexpected characters to the primary USB interface.

---

### Part 2: Build-Time Conditional Packet Extension for Latency Benchmarks

To measure latency bottleneck points (such as command decoding time and state machine processing overhead) under real-world conditions, we append timing markers directly to the primary command-response packets.

#### 2.1 Conditional Packet Structs (`include/serial_comm_manager.h`)
Modify the packet definitions using `#ifdef` preprocessor blocks. If `ENABLE_LATENCY_BENCHMARK` is enabled, the outgoing response packet structure is extended with a nested binary `TelemetryData` block. In production builds, this block is omitted entirely, keeping the packet structure compact and optimized:

```c
#pragma pack(1)

#ifdef ENABLE_LATENCY_BENCHMARK
typedef struct {
    uint32_t rx_arrival_us;    // Timestamp when start_marker was read from CDC
    uint32_t command_start_us; // Timestamp when packet handling began
    uint32_t response_ready_us;// Timestamp just before transmitting the packet
} TelemetryData;
#endif

typedef struct
{
    uint8_t start_marker;
    uint8_t packet_type;
    uint16_t data_size;
    RP2040_STATE data;
    
#ifdef ENABLE_LATENCY_BENCHMARK
    TelemetryData telemetry;   // Injected only during benchmark builds
#endif

    uint8_t end_marker;
} OutgoingPacketToAndroid;

#pragma pack()
```

#### 2.2 Populating Timing Metrics (`src/serial_comm_manager.c`)
Modify the command execution and serial parsing loop to capture precise RP2040 system clock microseconds (`time_us_32()`). The measurements are conditionally recorded and transmitted back:

```c
void get_block() {
    // 1. Record exact time when incoming START_MARKER is received
    #ifdef ENABLE_LATENCY_BENCHMARK
    uint32_t rx_time = time_us_32();
    #endif

    ...
    
    if (c != PICO_ERROR_TIMEOUT && c == START_MARKER){
        #ifdef ENABLE_LATENCY_BENCHMARK
        // Inject the arrival timestamp into a temporary buffer
        current_rx_timestamp = rx_time;
        #endif
        
        ...
    }
}

void handle_packet(IncomingPacketFromAndroid *packet){
    #ifdef ENABLE_LATENCY_BENCHMARK
    uint32_t processing_start = time_us_32();
    #endif

    switch (packet->packet_type){
        case SET_MOTOR_LEVEL:
            ... // Process command
            
            #ifdef ENABLE_LATENCY_BENCHMARK
            // Populate the telemetry timestamps
            outgoing_packet_to_android.telemetry.rx_arrival_us = current_rx_timestamp;
            outgoing_packet_to_android.telemetry.command_start_us = processing_start;
            outgoing_packet_to_android.telemetry.response_ready_us = time_us_32();
            #endif
            
            // Send packet to Android over stdio
            uint8_t* bytes = (uint8_t*)&outgoing_packet_to_android;
            for (int i = 0; i < sizeof(outgoing_packet_to_android); i++){
                putchar(bytes[i]);
            }
            break;
            
        ...
    }
}
```

Note to reviewer: code examples in the implementation plan are rough drafts and might differ from actual implementation.

---

## Omitted From Scope

* **Android-Side Implementation Plan:** The implementation, integration, and mapping logic of the Android client's high-precision timers, USB endpoints, or Wireless ADB configuration are omitted from the scope of this firmware documentation.
