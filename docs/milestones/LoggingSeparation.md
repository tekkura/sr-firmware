## Goal
Establish a dedicated, live firmware logging channel isolated from the primary USB CDC communication / command transport channel. This separation ensures that real-time execution telemetry and diagnostic logs do not corrupt the control protocol stream, allowing concurrent debugging and high-resolution latency benchmarking.

## Current Setup
* **Physical Link:** An Android mobile device is physically connected to the robot's RP2040-based firmware controller via a single USB CDC interface.
* **Shared Communication Channel:** Command packets (e.g., motor level adjustments, state requests) and diagnostic logging currently share the default USB stdio channel (`pico_stdio_usb`).
* **Telemetry Acquisition:** Benchmarking granular latency relies on temporal markers on both the Android library side and the firmware side.

## The Problem
1. **Protocol Stream Corruption:** Because logging and control commands share the same serial transport layer, raw ASCII log strings printed during command execution are interfering with the work of the program and are not easily parceable.
2. **Measurement Bias and Interference:** Polling for logs (`GET_LOG`) or flushing circular buffers over the command channel introduces significant latency overhead, distorting benchmark measurements.
3. **Simultaneity Constraints:** Developers cannot monitor real-time debug output while running active control benchmarks. This constraint severely hinders troubleshooting of state machines, motor control loops, and sensor processing.

## Deliverables
- **Isolated Logging Transport:** Implement a dedicated physical or logical channel for firmware logging that is completely separate from the primary USB CDC command channel.
- **Firmware Logging Library Integration:** Update `rp2040_log.h` and `rp2040_log.c` to route logging messages to this new channel.
- **Telemetry Mapping & Benchmark Harness:** Develop or extend the host-side benchmark harness to collect logs concurrently with control requests, enabling automated, accurate alignment of host-side and firmware-side latency metrics.

## Review Checklist
- **No Channel Cross-talk:** Verify that active logging does not corrupt primary USB CDC packets, and command execution remains 100% reliable with verbose logs enabled.
- **Non-blocking Execution:** Ensure that logging calls do not block critical real-time execution paths (e.g., by utilizing circular ring buffers, DMA, or non-blocking hardware writes).
- **Accurate Temporal Correlation:** Demonstrate that firmware-side measure points can be retrieved and temporally correlated with host-side benchmarks without affecting control loop frequency.