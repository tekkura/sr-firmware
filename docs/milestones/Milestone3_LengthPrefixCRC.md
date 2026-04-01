# Milestone 3: Length-Prefix + CRC Framing Fallback

Associated branch:
- `feature/milestone-3-crc-framing`

Goal
- Implement a length-prefix + CRC framing implementation and benchmark it.

Deliverables
- Length-prefix + CRC framing implemented in firmware.
- RTT measured against the current implementation and TinyFrame.
- Summary comparison of RTT results.

Review checklist
- The framing format implemented in firmware matches the documented protocol.
- CRC calculation and verification are correct on both firmware and benchmark sides.
- Benchmarks validate the framing integrity they claim to measure.
- RTT results are documented in a way that permits comparison to legacy and TinyFrame implementations.
- Commands and responses that remain unsupported or intentionally deferred are documented accurately.

Out of scope
- CDC write-path optimization beyond what is necessary to make this framing work
- Android app/library versioning policy beyond protocol facts needed for this milestone
