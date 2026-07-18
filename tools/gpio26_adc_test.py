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
DEFAULT_OUTPUT = "/tmp/gpio26-adc-test.log"
DEFAULT_BAUD = 115200

BAUD_RATES = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
}

SAMPLE_RE = re.compile(
    r"GPIO26_ADC_TEST sample=(?P<sample>\d+) "
    r"raw=(?P<raw>\d+) adc_mv=(?P<adc_mv>\d+)"
)
SUMMARY_RE = re.compile(
    r"GPIO26_ADC_TEST summary min_raw=(?P<min_raw>\d+) "
    r"max_raw=(?P<max_raw>\d+) avg_raw=(?P<avg_raw>\d+) "
    r"avg_adc_mv=(?P<avg_adc_mv>\d+)"
)
RESULT_RE = re.compile(r"GPIO26_ADC_TEST result=(?P<result>[A-Z_]+)")


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
    print("Running make flash while GPIO26 capture is active...", flush=True)
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
                    if "GPIO26_ADC_TEST END" in text:
                        return text

            if flash_process is not None and flash_process.poll() is not None:
                pass

    return b"".join(chunks).decode("utf-8", "replace")


def parse_results(text):
    samples = []
    summary = None
    result = None

    for line in text.splitlines():
        sample_match = SAMPLE_RE.search(line)
        if sample_match:
            samples.append({
                "sample": int(sample_match.group("sample")),
                "raw": int(sample_match.group("raw")),
                "adc_mv": int(sample_match.group("adc_mv")),
            })
            continue

        summary_match = SUMMARY_RE.search(line)
        if summary_match:
            summary = {
                "min_raw": int(summary_match.group("min_raw")),
                "max_raw": int(summary_match.group("max_raw")),
                "avg_raw": int(summary_match.group("avg_raw")),
                "avg_adc_mv": int(summary_match.group("avg_adc_mv")),
            }
            continue

        result_match = RESULT_RE.search(line)
        if result_match:
            result = result_match.group("result")

    return samples, summary, result


def validate(samples, summary, result):
    errors = []
    warnings = []

    if not samples:
        errors.append("missing GPIO26 sample lines")
    if summary is None:
        errors.append("missing GPIO26 summary line")
    if result is None:
        errors.append("missing GPIO26 result line")

    if summary is not None and summary["max_raw"] <= 32:
        warnings.append(
            "GPIO26 stayed near zero; this matches the suspicious 16-17 raw range"
        )
    if result == "LOW_RAW":
        warnings.append("firmware reported LOW_RAW")

    return errors, warnings


def print_summary(samples, summary, result, errors, warnings, output_path, flash_process):
    print(f"Log written to: {output_path}")

    if flash_process is not None:
        status = flash_process.poll()
        if status is None:
            status = flash_process.wait()
        print(f"make flash exit code: {status}")

    print(f"samples captured: {len(samples)}")
    if summary is None:
        print("summary: not found")
    else:
        print(
            "summary: "
            f"min_raw={summary['min_raw']} "
            f"max_raw={summary['max_raw']} "
            f"avg_raw={summary['avg_raw']} "
            f"avg_adc_mv={summary['avg_adc_mv']}"
        )

    print(f"firmware result: {result or 'not found'}")

    if errors:
        print("GPIO26 ADC diagnostic: FAIL")
        for error in errors:
            print(f"  - {error}")
        return

    if warnings:
        print("GPIO26 ADC diagnostic: WARN")
        for warning in warnings:
            print(f"  - {warning}")
        return

    print("GPIO26 ADC diagnostic: PASS")


def main():
    parser = argparse.ArgumentParser(
        description="Capture and summarize GPIO26 ADC diagnostic firmware logs."
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

    samples, summary, result = parse_results(text)
    errors, warnings = validate(samples, summary, result)
    print_summary(samples, summary, result, errors, warnings, args.output, flash_process)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
