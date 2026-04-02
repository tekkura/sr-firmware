# Milestone 2: TinyFrame Integration + RTT Benchmark

Associated branch:
- `feature/milestone-2-tinyframe`

Goal
- Integrate TinyFrame into the RP2040 firmware and validate functionality.

Deliverables
- TinyFrame integrated into the firmware in C/Pico SDK.
- Existing framing replaced with TinyFrame RX/TX.
- RTT measured against the current implementation.
- Documentation of TinyFrame configuration and frame format.

Review checklist
- The firmware RX/TX path is actually using TinyFrame rather than keeping a parallel legacy framing path.
- Any new third-party TinyFrame dependency is wired consistently with the repo’s dependency model.
- The benchmark and protocol documentation reflect the TinyFrame framing actually implemented in code.
- RTT results are documented clearly enough to compare against the previous implementation.
- Unsupported or partially implemented commands are either disabled cleanly or documented accurately.

Out of scope
- Length-prefix + CRC fallback framing
- CDC write-path micro-optimizations
