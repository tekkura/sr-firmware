"""Exercise firmware framing on the host without a Pico SDK or device."""

import binascii
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GET_LOG, SET_MOTOR, RESET, GET_STATE, GET_VERSION = 0, 1, 2, 3, 6
NACK, ACK = 0xFC, 0xFD

STUB_HEADER = r"""
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned int uint;
typedef struct i2c_inst i2c_inst_t;
#define PICO_ERROR_TIMEOUT (-1)
#define auto_init_mutex(name) static int name
static inline void mutex_enter_blocking(int *mutex) {
    assert(*mutex == 0);
    *mutex = 1;
}
static inline void mutex_exit(int *mutex) {
    assert(*mutex == 1);
    *mutex = 0;
}
int getchar_timeout_us(uint32_t timeout);
uint64_t time_us_64(void);
"""

HARNESS = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "robot.h"
#include "rp2040_log.h"

unsigned char output[131072];
size_t output_size;
unsigned motor_calls, state_calls;
static unsigned char input[2048];
static size_t input_size, input_offset;

int test_putchar(int value) {
    assert(output_size < sizeof(output));
    output[output_size++] = (unsigned char)value;
    return (unsigned char)value;
}

int test_printf(const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof(buffer));
    for (int i = 0; i < length; ++i) test_putchar(buffer[i]);
    return length;
}

int getchar_timeout_us(uint32_t timeout) {
    (void)timeout;
    return input_offset < input_size ? input[input_offset++] : PICO_ERROR_TIMEOUT;
}

uint64_t time_us_64(void) { return 1234; }

void set_motor_levels(RP2040_STATE *state) {
    (void)state;
    motor_calls++;
}

void get_state(RP2040_STATE *state) {
    (void)state;
    state_calls++;
}

void test_reset(void) {
    rp2040_log_init();
    RP2040_STATE state = {0};
    serial_comm_manager_init(&state);
    output_size = motor_calls = state_calls = 0;
    input_size = input_offset = 0;
}

void test_log(const char *message) {
    rp2040_log(LOG_LEVEL_ERROR, "%s", message);
}

void test_request(const unsigned char *bytes, size_t length) {
    assert(length <= sizeof(input));
    memcpy(input, bytes, length);
    input_size = length;
    input_offset = 0;
    output_size = 0;
    get_block();
}
"""


def frame(command, payload=b""):
    body = (len(payload) + 1).to_bytes(2, "little") + bytes([command]) + payload
    return b"\xfe" + body + binascii.crc_hqx(body, 0xFFFF).to_bytes(2, "little")


class ProtocolTests(unittest.TestCase):
    telemetry = False

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="firmware-protocol-")
        cls.addClassCleanup(cls.temp.cleanup)
        build = Path(cls.temp.name)
        (build / "stub.h").write_text(STUB_HEADER)
        for name in (
            "pico/types.h", "pico/mutex.h", "pico/multicore.h",
            "pico/stdio.h", "pico/time.h", "hardware/uart.h",
            "hardware/gpio.h", "hardware/platform_defs.h", "hardware/i2c.h",
        ):
            header = build / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "stub.h"\n')
        harness = build / "harness.c"
        harness.write_text(HARNESS)
        library = build / "protocol.so"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
            "-Dprintf=test_printf", "-Dputchar=test_putchar",
            *(["-DENABLE_LATENCY_BENCHMARK=1"] if cls.telemetry else []),
            "-I", str(build), "-I", str(ROOT / "include"), str(harness),
            *(str(ROOT / "src" / name) for name in
              ("crc.c", "rp2040_log.c", "serial_comm_manager.c")),
            "-o", str(library),
        ], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.test_log.argtypes = [ctypes.c_char_p]
        cls.lib.test_request.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
        cls.lib.rp2040_get_byte_count.restype = ctypes.c_uint16
        for name in ("test_log", "test_request", "test_reset"):
            getattr(cls.lib, name).restype = None

    def setUp(self):
        self.lib.test_reset()

    def request_bytes(self, packet):
        self.lib.test_request(packet, len(packet))
        size = ctypes.c_size_t.in_dll(self.lib, "output_size").value
        return bytes((ctypes.c_ubyte * size).in_dll(self.lib, "output"))

    def request(self, command, payload=b""):
        return self.request_bytes(frame(command, payload))

    def test_repeated_log_reads(self):
        for message in (b"first\n", b"second\n"):
            self.lib.test_log(message)
        self.assertEqual(self.request(GET_LOG), frame(GET_LOG, b"first\nsecond\n"))
        self.assertEqual(self.request(GET_LOG), frame(GET_LOG))
        self.lib.test_log(b"new\n")
        self.assertEqual(self.request(GET_LOG), frame(GET_LOG, b"new\n"))

    def test_ring_wrap(self):
        messages = [f"line {i}\n".encode() for i in range(1100)]
        for message in messages:
            self.lib.test_log(message)
        self.assertEqual(self.request(GET_LOG), frame(GET_LOG, b"".join(messages[-1028:])))
        self.assertEqual(self.request(GET_LOG), frame(GET_LOG))

    def test_log_length_boundary(self):
        # 516 full lines leave two bytes before the largest legal log payload.
        for _ in range(516):
            self.lib.test_log(b"x" * 127)
        self.lib.test_log(b"ab")
        self.assertEqual(self.lib.rp2040_get_byte_count(), 65534)
        self.assertEqual(self.request(GET_LOG), frame(GET_LOG, b"x" * 65532 + b"ab"))

    def test_log_command_byte_reserved(self):
        for _ in range(516):
            self.lib.test_log(b"x" * 127)
        self.lib.test_log(b"abc")
        expected = b"x" * (515 * 127) + b"abc"
        self.assertEqual(self.lib.rp2040_get_byte_count(), len(expected))
        self.assertEqual(self.request(GET_LOG), frame(GET_LOG, expected))

    def test_invalid_payloads_have_no_side_effects(self):
        self.lib.test_log(b"retained\n")
        for command in (GET_LOG, GET_STATE, GET_VERSION, RESET):
            for payload in (b"x", b"xyz", b"x" * 1023):
                with self.subTest(command=command, length=len(payload)):
                    self.assertEqual(self.request(command, payload), frame(NACK))
        for payload in (b"", b"x", b"xyz"):
            self.assertEqual(self.request(SET_MOTOR, payload), frame(NACK))
        for counter in ("motor_calls", "state_calls"):
            self.assertEqual(ctypes.c_uint.in_dll(self.lib, counter).value, 0)
        self.assertEqual(self.request(GET_LOG), frame(GET_LOG, b"retained\n"))

    def test_valid_commands(self):
        self.assertEqual(self.request(RESET), frame(ACK))
        self.assertEqual(self.request(GET_VERSION), frame(GET_VERSION, b"\x01\x02\x00"))
        size = 45 if self.telemetry else 29
        for command, payload in ((GET_STATE, b""), (SET_MOTOR, b"\x12\x34")):
            response = self.request(command, payload)
            self.assertEqual(len(response), size + 6)
            self.assertEqual(response, frame(command, response[4:-2]))
            if command == SET_MOTOR:
                self.assertEqual(response[4:6], payload)
        self.assertEqual(ctypes.c_uint.in_dll(self.lib, "motor_calls").value, 1)
        self.assertEqual(ctypes.c_uint.in_dll(self.lib, "state_calls").value, 2)

    def test_bad_crc_and_length(self):
        corrupted = bytearray(frame(SET_MOTOR, b"\x12\x34"))
        corrupted[-1] ^= 1
        for packet in (bytes(corrupted), b"\xfe\x00\x00", b"\xfe\x01\x04"):
            self.assertEqual(self.request_bytes(packet), frame(NACK))
        self.assertEqual(self.request(0x99), frame(NACK))
        self.assertEqual(ctypes.c_uint.in_dll(self.lib, "motor_calls").value, 0)

    def test_timeout_then_valid_request(self):
        self.assertEqual(self.request_bytes(b""), b"")
        packet = frame(SET_MOTOR, b"\x12\x34")
        for length in range(1, len(packet)):
            with self.subTest(length=length):
                self.assertEqual(self.request_bytes(packet[:length]), frame(NACK))
                self.assertEqual(self.request(RESET), frame(ACK))


class TelemetryProtocolTests(ProtocolTests):
    telemetry = True


if __name__ == "__main__":
    unittest.main(verbosity=2)
