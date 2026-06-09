# Implementation Plan: Latency Benchmark Measurements (T4 & T5)

This document describes the implementation plan to capture granular latency measurement points (**T4** and **T5**) on the Raspberry Pi Pico (RP2040) firmware side during round-trip time (RTT) tests. 

---

## 1. Objective and Requirements

Currently, the Android host-side application conducts end-to-end RTT tests, but the firmware does not measure or record any internal processing milestones. To enable accurate, microsecond-level debugging of the communication loop under the `LATENCY_BENCHMARK` build configuration, we will introduce:

1. **T4 (Firmware Receipt)**: Measured inside `get_block()` immediately after the `END_MARKER` of a packet is successfully received.
2. **T5 (Firmware Processing Time)**: Measured inside `handle_packet()` immediately before a response packet begins transmitting (i.e., before writing response bytes/characters to standard output).
3. **In-Memory Storage**: The measurements will be stored in a dedicated memory structure and updated upon receiving and processing each new packet.
4. **Conditional Compilation**: Collection of these timestamps must only be compiled when the `LATENCY_BENCHMARK` option is enabled, ensuring zero overhead in normal production builds.

---

## 2. Proposed Architecture & Data Structures

We will use the Pico SDK's high-resolution timer (`time_us_64()`) from `pico/time.h` or `hardware/timer.h` to fetch microsecond-level timestamps since boot.

### Data Structure Definition

In `include/serial_comm_manager.h`, declare a conditional structure and utility function:

```c
#ifdef LATENCY_BENCHMARK
#include <stdint.h>

typedef struct {
    uint64_t t4_timestamp_us;  // Firmware Receipt (after END_MARKER receive)
    uint64_t t5_timestamp_us;  // Firmware processing time (before sending response)
} LatencyMeasurements;

// Getter function to retrieve current measurements for debug/test verification
LatencyMeasurements get_latency_measurements(void);
#endif
```

---

## 3. Step-by-Step Code Modifications

### 3.2. Update Header (`include/serial_comm_manager.h`)

Insert the conditional `LatencyMeasurements` struct and getter declaration right before `#endif`.

---

### 3.3. Update Source (`src/serial_comm_manager.c`)

Define the static variable holding the latest measurements and implement the getter function:

```c
#ifdef LATENCY_BENCHMARK
#include "pico/time.h"

static LatencyMeasurements g_latency_measurements = {0, 0};

LatencyMeasurements get_latency_measurements(void) {
    return g_latency_measurements;
}
#endif
```

#### A. Capturing T4 in `get_block()`
Record `T4` as soon as the closing `END_MARKER` is parsed, just before packet validation and processing:

```c
        c = getchar_timeout_us(100);
        if (c != PICO_ERROR_TIMEOUT && c == END_MARKER){
#ifdef LATENCY_BENCHMARK
            g_latency_measurements.t4_timestamp_us = time_us_64();
#endif
            // Calculate the length of the packet
            uint16_t packet_length = end_idx - start_idx;
            ...
```

#### B. Capturing T5 in `handle_packet()`
Record `T5` in `handle_packet()` right before printing any response. To ensure accurate measurement of processing time ending immediately before transmission, we will place the timestamp capture immediately before the first `putchar()` call in each command case block.

```c
void handle_packet(IncomingPacketFromAndroid *packet){
    uint8_t* bytes;
    
    // Assign the same packet type to the outgoing packet
    switch (packet->packet_type){
    	case GET_LOG:
#ifdef LATENCY_BENCHMARK
            g_latency_measurements.t5_timestamp_us = time_us_64();
#endif
            outgoing_log_packet_to_android.packet_type = packet->packet_type;
            putchar(outgoing_log_packet_to_android.start_marker);
            ...
	case SET_MOTOR_LEVEL:
            outgoing_packet_to_android.packet_type = packet->packet_type;
            ...
            get_state(&rp2040_state_);
	    outgoing_packet_to_android.data = rp2040_state_;

#ifdef LATENCY_BENCHMARK
            g_latency_measurements.t5_timestamp_us = time_us_64();
#endif
	    // Print the outgoing packet chars
            bytes = (uint8_t*)&outgoing_packet_to_android;
            for (int i = 0; i < sizeof(outgoing_packet_to_android); i++){
                putchar(bytes[i]);
            }
	    break;
        ...
```
*(This ensures that `T5` captures the total time spent preparing the response, immediately before serialization to the standard output begins).*

---

## 4. Verification and Testing Plan

To ensure correctness and compile-safety, we will use the following verification tasks:

1. **Verify Compilation Without Option (Production Mode)**:
   - Run `make firmware LATENCY_BENCHMARK=OFF`.
   - Ensure it compiles successfully.
   - Use `grep` or inspect the disassembled code (e.g. `build/robot.dis`) to confirm that `time_us_64` is not called and the latency measurements code is excluded.

2. **Verify Compilation With Option (Benchmark Mode)**:
   - Run `make firmware LATENCY_BENCHMARK=ON`.
   - Ensure it compiles successfully.

3. **Runtime Verification**:
   - Run the firmware in GDB (`make debug`).
   - Place a breakpoint at `handle_packet` or after response transmission.
   - Print `g_latency_measurements` using GDB to verify that `t4_timestamp_us` and `t5_timestamp_us` are being properly set and updated after each packet is sent from the mock host or the phone.
   - Assert that `t5_timestamp_us >= t4_timestamp_us`.
