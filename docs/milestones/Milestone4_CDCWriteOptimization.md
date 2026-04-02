# Milestone 4: CDC Write Path Latency Optimization

Associated branch:
- `feature/milestone-4-cdc`

Goal
- Evaluate CDC write-path options to reduce latency.

Deliverables
- Compare `pico_stdio_usb` versus direct TinyUSB CDC writes (`tud_cdc_n_write` + flush) for RTT.
- Document whether direct CDC improves latency and under what conditions.

Review checklist
- The PR is focused on write-path latency work rather than introducing unrelated protocol changes.
- The direct CDC path handles partial writes, ordering, and buffering correctly.
- RTT comparison methodology is documented and comparable to earlier milestones.
- Any latency claims are tied to measured results and to the specific write path under test.

Out of scope
- Replacing protocol framing again unless it is strictly necessary to evaluate the write path
