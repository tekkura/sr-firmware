# Host Protocol Regression Tests

Run from the repository root with Python 3 and a C compiler (`cc`):

```sh
python3 tests/protocol/test_protocol.py
```

The tests compile the actual CRC, logger, and serial manager sources into
temporary shared libraries, with latency telemetry both disabled and enabled.
Pico hardware, time, mutexes, and motor/sensor operations are stubbed. Serial
output is captured and checked against Python's independent CRC implementation.

Coverage includes repeated and empty log reads, ring wrapping, the maximum
frame length, valid commands, invalid payload sizes without side effects,
bad CRCs, invalid frame lengths, and a truncated request followed by a valid
request. Temporary build files are removed after the run.

These tests do not validate USB timing, multicore synchronization, hardware
behavior, or every corrupt-stream recovery pattern. The RTT benchmark remains
separate and measures the happy-path motor command.
