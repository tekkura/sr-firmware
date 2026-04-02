# Milestone 1: Host-Side Test Harness

Associated branch:
- `feature/milestone-1-benchmark`

Goal
- Add a host-side test harness to emulate Android packets without requiring a phone.

Deliverables
- Host-side benchmark or test harness capable of sending the current firmware packet format over USB CDC.
- Documented procedure for running RTT measurements without Android.

Review checklist
- The PR is limited to the host-side harness and the minimum supporting build/documentation changes needed to run it.
- The harness can exercise the current firmware protocol used on `main`.
- The procedure to run RTT measurements is documented and reproducible.
- Build instructions for the harness are present and consistent with the repo.

Out of scope
- Protocol migrations
- Firmware framing rewrites
- Android integration
