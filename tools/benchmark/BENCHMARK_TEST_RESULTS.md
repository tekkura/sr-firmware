This document contains the results of the benchmark tests for measuring Round-Trip Time (RTT) latency between a PC (Linux/WSL) and the board (Raspberry PI Pico or custom PCB) running the Tekkura robot firmware

Each row represents a separate benchmark test run

## Measurement scope

The PC tables below are historical experiments, not measurements of the
current firmware. Their firmware/host commit hashes were not recorded; the
100 us run used a local timeout change. Current firmware uses
`FRAME_BYTE_TIMEOUT_US=5000` (5 ms). The Android run at the end identifies
the tested revisions and used that 5 ms timeout.

These RTT measurements exercise only `SET_MOTOR_LEVEL`. They do not validate
variable-length logs, malformed requests, or parser recovery. Host protocol
regression tests are documented in `tests/protocol/README.md`; they are not
USB latency measurements. No hardware results for the PR review fixes are
recorded here.

## Initial measurements

| Success Rate (%) | Min (ms) | Max (ms) | Avg (ms) | Jitter (ms) |
|:-----------------|:---------|:---------|:---------|:------------|
| 100.000          | 7.419    | 27.384   | 13.760   | 1.693       |
| 100.000          | 8.774    | 32.602   | 13.933   | 2.121       |
| 100.000          | 11.226   | 15.672   | 13.772   | 0.607       |
| 100.000          | 8.995    | 17.126   | 13.650   | 1.033       |
| 100.000          | 7.701    | 24.562   | 13.790   | 1.383       |
| 100.000          | 6.443    | 29.112   | 13.903   | 1.847       |

## TinyFrame

| Success Rate (%) | Min (ms) | Max (ms) | Avg (ms) | Jitter (ms) |
|:-----------------|:---------|:---------|:---------|:------------|
| 100.000          | 10.945   | 38.025   | 18.985   | 2.654       |
| 100.000          | 8.383    | 39.824   | 19.394   | 4.315       |
| 100.000          | 9.262    | 29.980   | 18.980   | 1.981       |
| 100.000          | 10.786   | 26.849   | 18.837   | 1.937       |
| 100.000          | 9.668    | 38.551   | 19.121   | 2.756       |
| 100.000          | 8.766    | 29.441   | 18.801   | 2.779       |

While TinyFrame is higher-level and way easier to use and maintain than direct byte transfer, it is considerably slower.

## Length prefix + CRC Framing (1ms timeout)

| Success Rate (%) | Min (ms) | Max (ms) | Avg (ms) | Jitter (ms) |
|:-----------------|:---------|:---------|:---------|:------------|
| 100.000          | 6.779    | 27.058   | 15.520   | 4.474       |
| 100.000          | 6.231    | 26.748   | 11.672   | 5.125       |
| 100.000          | 5.895    | 20.386   | 10.366   | 4.696       |
| 100.000          | 6.403    | 41.395   | 11.244   | 6.198       |
| 100.000          | 6.470    | 37.672   | 11.572   | 5.673       |
| 100.000          | 4.937    | 19.595   | 10.933   | 4.920       |

After seeing those results, I have noticed that the AI increased the timeout window in get_byte, so I have reverted this (locally) and ran tests again.

## Length prefix + CRC Framing (100µs timeout)

| Success Rate (%) | Min (ms) | Max (ms) | Avg (ms) | Jitter (ms) |
|:-----------------|:---------|:---------|:---------|:------------|
| 100.000          | 13.287   | 31.074   | 17.012   | 2.141       |
| 100.000          | 11.656   | 21.859   | 16.700   | 1.357       |
| 100.000          | 12.917   | 29.143   | 17.032   | 2.150       |
| 100.000          | 10.096   | 39.760   | 17.076   | 2.815       |
| 100.000          | 10.381   | 27.337   | 16.866   | 1.815       |
| 100.000          | 11.403   | 20.655   | 16.719   | 1.202       |

Interestingly, the average time skyrocketed after the revert. I think that the cause is that usb serial can't reliably pass bytes this quickly and this might be my testing setup issue, but this is something worth looking into.

As for the length prefix + CRC framing, this solution is slightly faster than TinyFrame and, in comparison with legacy, it allows for the variable length commands. It is still a major latency increase though, not sure that the trade-off is worth it

## Rebased CRC: Android hardware validation

2026-09-05 04:44:10 UTC, Pixel 3a / Android 12 + RP2040 over USB serial;
100 warm-up and 1000 measured iterations. This Android run uses a different
benchmark/setup from the historical PC measurements above.

- Android: `tekkura/CommunicationFraming+crc-length-prefix` at
  `c01c50bfe74894e862817aed8833a43c7e5705c6` (embedded in the report).
- Firmware: `tekkura/feature/milestone-3-crc-framing` at
  `9bfeef30fbd91745275104a4a887046650797cdd` (operator run history and branch
  ancestry; the device report did not record the firmware hash).
- Responses: **1000/1000**; RTT mean **22.705 ms**, p95 **26.253 ms**,
  min **18.607 ms**, max **35.097 ms**. Gradle failed the mean RTT <20 ms assertion.

These are the tested rebased revisions, before Android `720a67dc` (RESET_STATE
ACK alignment) and firmware `8d3e3c9` (protocol documentation only).
