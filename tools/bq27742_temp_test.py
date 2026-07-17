#!/usr/bin/env python3
import argparse
import os
import re
import select
import subprocess
import sys
import termios
import time


DEFAULT_DEVICE = "/dev/ttyACM0"
DEFAULT_OUTPUT = "/tmp/bq27742-temp-test.log"
DEFAULT_BAUD = 115200

BAUD_RATES = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
}

TEMP_RE = re.compile(
    r"BQ27742_TEMP_TEST (?P<name>selected_temp|internal_temp) "
    r"raw=0x(?P<raw>[0-9a-fA-F]{4}) "
    r"lsb=0x(?P<lsb>[0-9a-fA-F]{2}) "
    r"msb=0x(?P<msb>[0-9a-fA-F]{2}) "
    r"c_x10=(?P<c_x10>-?\d+)"
)
API_RE = re.compile(
    r"BQ27742_TEMP_TEST api_temp_c=(?P<api>\d+) "
    r"voltage_mv=(?P<voltage>\d+) "
    r"safety=0x(?P<safety>[0-9a-fA-F]{2}) "
    r"flags=0x(?P<flags>[0-9a-fA-F]{4})"
)


def configure_serial(fd, baud):
    if baud not in BAUD_RATES:
        raise ValueError(f"unsupported baud rate: {baud}")

    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = BAUD_RATES[baud]
    attrs[5] = BAUD_RATES[baud]
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)


def open_serial(device, baud):
    fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    configure_serial(fd, baud)
    return fd


def drain_serial(fd, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], 0.05)
        if readable:
            try:
                os.read(fd, 4096)
            except BlockingIOError:
                pass


def start_flash(enabled):
    if not enabled:
        return None
    print("Running make flash while BQ27742 capture is active...", flush=True)
    return subprocess.Popen(["make", "flash"])


def capture(fd, output_path, timeout, flash_process):
    deadline = time.monotonic() + timeout
    chunks = []

    with open(output_path, "wb") as output:
        while time.monotonic() < deadline:
            readable, _, _ = select.select([fd], [], [], 0.1)
            if readable:
                try:
                    chunk = os.read(fd, 4096)
                except BlockingIOError:
                    chunk = b""

                if chunk:
                    output.write(chunk)
                    output.flush()
                    chunks.append(chunk)
                    text = b"".join(chunks).decode("utf-8", "replace")
                    if "BQ27742_TEMP_TEST END" in text:
                        return text

            if flash_process is not None and flash_process.poll() is not None:
                pass

    return b"".join(chunks).decode("utf-8", "replace")


def decode_temp(raw):
    return (raw / 10.0) - 273.15


def parse_results(text):
    results = {}
    api = None

    for line in text.splitlines():
        temp_match = TEMP_RE.search(line)
        if temp_match:
            name = temp_match.group("name")
            raw = int(temp_match.group("raw"), 16)
            results[name] = {
                "raw": raw,
                "lsb": int(temp_match.group("lsb"), 16),
                "msb": int(temp_match.group("msb"), 16),
                "c_x10": int(temp_match.group("c_x10")),
                "c": decode_temp(raw),
            }
            continue

        api_match = API_RE.search(line)
        if api_match:
            api = {
                "api_temp_c": int(api_match.group("api")),
                "voltage_mv": int(api_match.group("voltage")),
                "safety": int(api_match.group("safety"), 16),
                "flags": int(api_match.group("flags"), 16),
            }

    return results, api


def validate(results, api):
    errors = []

    for name in ("selected_temp", "internal_temp"):
        if name not in results:
            errors.append(f"missing {name} diagnostic line")
            continue

        raw = results[name]["raw"]
        temp_c = results[name]["c"]
        if raw == 0:
            errors.append(f"{name} raw register is 0x0000")
        if temp_c < -40.0 or temp_c > 100.0:
            errors.append(f"{name} decoded temperature is implausible: {temp_c:.2f} C")

    if api is None:
        errors.append("missing api_temp_c diagnostic line")
    elif "selected_temp" in results:
        decoded_whole_c = int(results["selected_temp"]["c"])
        if api["api_temp_c"] != decoded_whole_c:
            errors.append(
                "firmware API temperature does not match selected raw register "
                f"(api={api['api_temp_c']} C, decoded={decoded_whole_c} C)"
            )

    return errors


def print_summary(results, api, errors, output_path, flash_process):
    print(f"Log written to: {output_path}")

    if flash_process is not None:
        status = flash_process.poll()
        if status is None:
            status = flash_process.wait()
        print(f"make flash exit code: {status}")

    for name in ("selected_temp", "internal_temp"):
        value = results.get(name)
        if value is None:
            print(f"{name}: not found")
            continue
        print(
            f"{name}: raw=0x{value['raw']:04x} "
            f"lsb=0x{value['lsb']:02x} msb=0x{value['msb']:02x} "
            f"decoded={value['c']:.2f} C c_x10={value['c_x10']}"
        )

    if api is None:
        print("api/context: not found")
    else:
        print(
            f"api/context: api_temp_c={api['api_temp_c']} "
            f"voltage_mv={api['voltage_mv']} "
            f"safety=0x{api['safety']:02x} flags=0x{api['flags']:04x}"
        )

    if errors:
        print("BQ27742 temperature diagnostic: FAIL")
        for error in errors:
            print(f"  - {error}")
    else:
        print("BQ27742 temperature diagnostic: PASS")


def main():
    parser = argparse.ArgumentParser(
        description="Capture and validate BQ27742 temperature diagnostic firmware logs."
    )
    parser.add_argument("--device", default=DEFAULT_DEVICE, help=f"serial device, default {DEFAULT_DEVICE}")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"baud rate, default {DEFAULT_BAUD}")
    parser.add_argument("--timeout", type=float, default=20.0, help="capture timeout in seconds, default 20")
    parser.add_argument("--output", default=DEFAULT_OUTPUT, help=f"log output path, default {DEFAULT_OUTPUT}")
    parser.add_argument("--flash", action="store_true", help="run make flash after opening serial capture")
    parser.add_argument("--no-drain", action="store_true", help="do not discard stale bytes before capture")
    args = parser.parse_args()

    try:
        fd = open_serial(args.device, args.baud)
    except OSError as exc:
        print(f"ERROR: could not open {args.device}: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    flash_process = None
    try:
        if not args.no_drain:
            drain_serial(fd, 0.25)
        flash_process = start_flash(args.flash)
        text = capture(fd, args.output, args.timeout, flash_process)
    finally:
        os.close(fd)

    results, api = parse_results(text)
    errors = validate(results, api)
    print_summary(results, api, errors, args.output, flash_process)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
