This document contains the results of the benchmark tests for measuring Round-Trip Time (RTT) latency between a PC (Linux/WSL) and the board (Raspberry PI Pico or custom PCB) running the Tekkura robot firmware

Each row represents a separate benchmark test run

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