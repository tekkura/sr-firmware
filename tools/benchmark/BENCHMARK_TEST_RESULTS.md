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

## Direct CDC writes

| Success Rate (%) | Min (ms) | Max (ms) | Avg (ms) | Jitter (ms) |
|:-----------------|:---------|:---------|:---------|:------------|
| 100.000          | 2.524    | 12.691   | 10.785   | 1.025       |
| 100.000          | 4.753    | 16.398   | 10.807   | 1.189       |
| 100.000          | 7.399    | 14.330   | 10.858   | 1.017       |
| 100.000          | 10.288   | 21.461   | 10.964   | 1.081       |
| 100.000          | 5.922    | 15.936   | 10.807   | 1.093       |
| 100.000          | 5.149    | 16.528   | 10.816   | 1.147       |

A big improvement in speed

## Waiting for a full payload

| Success Rate (%) | Min (ms) | Max (ms) | Avg (ms) | Jitter (ms) |
|:-----------------|:---------|:---------|:---------|:------------|
| 100.000          | 1.360    | 14.240   | 11.301   | 1.692       |
| 100.000          | 3.820    | 38.904   | 11.821   | 3.333       |
| 100.000          | 8.147    | 13.913   | 11.508   | 0.895       |
| 100.000          | 3.424    | 31.191   | 11.754   | 2.748       |
| 100.000          | 2.488    | 25.722   | 11.622   | 2.312       |
| 100.000          | 1.128    | 29.015   | 11.356   | 2.693       |

After reworking CDC so that it waits until all data is sent, the average and minimum time slightly increased, but it's still noticeably faster than legacy method. Jitter has also substantially increased