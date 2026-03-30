# Milestone 4: CDC Write Path Latency Optimization Implementation Plan

This document outlines the plan for evaluating and optimizing the USB CDC write path to reduce Round-Trip Time (RTT) latency between the RP2040 and the Android host.

## 1. Objectives
- Compare the latency of the standard `pico_stdio_usb` (`putchar`) against direct TinyUSB CDC calls (`tud_cdc_n_write`).
- Evaluate the impact of explicit FIFO flushing (`tud_cdc_n_write_flush`) on RTT.
- Document performance gains and conditions where direct CDC is preferable.

## 2. Technical Evaluation
- **`pico_stdio_usb`**: High-level, thread-safe, but introduces overhead via mutexes and a secondary software buffer before reaching the TinyUSB stack.
- **Direct TinyUSB**: Low-level, bypasses the stdio layer, allowing direct insertion into the USB hardware endpoints and immediate frame triggering via flush.

## 3. Implementation Steps

### Step 1: Implement Direct CDC Write Wrapper
- Update `src/serial_comm_manager.c` to include `tusb.h`.
- Create a conditional macro or a helper function `cdc_write_optimized(const uint8_t* buf, uint32_t len)`.
- Use `tud_cdc_n_write()` to load the data and `tud_cdc_n_write_flush()` to force immediate transmission.

### Step 2: Benchmarking & Comparison
- Build the firmware with the optimized write path.
- Run the benchmark tool again under identical conditions.
- Compare:
    - **Legacy** (`putchar` loop)
    - **Optimized** (`tud_cdc_n_write` + `flush`)

### Step 3: Documentation
- Record results in `BENCHMARK_TEST_RESULTS.md`.
- Provide a technical summary of why the optimization worked (or didn't) based on the RP2040 USB hardware behavior.

## 4. Risks & Considerations
- **Concurrency**: Direct TinyUSB calls are not thread-safe in the same way `pico_stdio` is. We must ensure no other part of the code (like `printf` logs) is writing to USB simultaneously if we bypass the stdio lock.
- **Buffer Overflows**: `tud_cdc_n_write` can fail if the endpoint FIFO is full. The implementation must handle checking available space using `tud_cdc_n_write_available()`.
