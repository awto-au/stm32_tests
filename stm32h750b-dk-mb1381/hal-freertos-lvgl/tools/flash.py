#!/usr/bin/env python3
"""Flash STM32H750B-DK via SWD and capture boot log on /dev/ttyACM4."""

import argparse
import subprocess
import sys
import threading
import time

PROGRAMMER = (
    "/home/dan/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"
)
EXTLOADER = (
    "/home/dan/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/"
    "ExternalLoader/MT25TL01G_STM32H750B-DISCO.stldr"
)
BOARD_SN   = "0043002C3133510A36303739"
HEX_PATH   = "build/Debug/stm32h750_lvgl.hex"
SERIAL_DEV = "/dev/ttyACM4"
BAUD       = 2_000_000
LOG_FILE   = "boot.tmp"


def flash(hex_path: str) -> bool:
    cmd = [
        PROGRAMMER,
        "--connect", f"port=SWD", f"sn={BOARD_SN}", "freq=4000", "reset=HWrst",
        "--extload", EXTLOADER,
        "--download", hex_path,
        "--verify", "--run",
    ]
    print(f"Flashing {hex_path} ...", flush=True)
    result = subprocess.run(cmd, capture_output=True, text=True)
    ok = "Download verified successfully" in result.stdout or result.returncode == 0
    if not ok:
        sys.stderr.write(result.stdout[-2000:])
        sys.stderr.write(result.stderr[-500:])
    return ok


def reset_only() -> None:
    cmd = [
        PROGRAMMER,
        "--connect", f"port=SWD", f"sn={BOARD_SN}", "freq=4000", "reset=HWrst",
        "--hardRst",
    ]
    subprocess.run(cmd, capture_output=True)


def capture_log(duration_s: int) -> list[str]:
    try:
        import serial
    except ImportError:
        print("pyserial not available, skipping log capture", file=sys.stderr)
        return []

    lines: list[str] = []

    def read_loop() -> None:
        try:
            s = serial.Serial(SERIAL_DEV, BAUD, timeout=0.5)
            deadline = time.monotonic() + duration_s
            while time.monotonic() < deadline:
                raw = s.readline()
                if raw:
                    lines.append(raw.decode("ascii", errors="replace").rstrip())
            s.close()
        except Exception as exc:
            print(f"serial error: {exc}", file=sys.stderr)

    t = threading.Thread(target=read_loop, daemon=True)
    t.start()
    return lines, t


def main() -> None:
    parser = argparse.ArgumentParser(description="Flash STM32H750B-DK and capture boot log")
    parser.add_argument("hex", nargs="?", default=HEX_PATH, help="HEX file to flash")
    parser.add_argument("--log-seconds", type=int, default=12, help="Seconds to capture log")
    parser.add_argument("--no-flash", action="store_true", help="Skip flashing, just reset and log")
    parser.add_argument("--output", default=LOG_FILE, help="Output .tmp file for log")
    args = parser.parse_args()

    lines: list[str] = []
    log_thread = None
    lines_ref = lines

    try:
        import serial as _  # noqa: F401
        has_serial = True
    except ImportError:
        has_serial = False
        print("pyserial not available, skipping log capture", file=sys.stderr)

    if args.no_flash:
        # Start capture before reset so we catch early boot lines
        if has_serial:
            lines_ref, log_thread = capture_log(args.log_seconds)
            time.sleep(0.1)
        reset_only()
    else:
        ok = flash(args.hex)
        if not ok:
            sys.exit("Flash failed")
        print("Flash OK", flush=True)
        # Open port first, then reset — same pattern as --no-flash so we don't miss boot
        if has_serial:
            lines_ref, log_thread = capture_log(args.log_seconds)
            time.sleep(0.1)
        reset_only()

    if log_thread is not None:
        log_thread.join()
        lines = lines_ref

    if lines:
        with open(args.output, "w") as f:
            f.write("\n".join(lines))
        print(f"Log ({len(lines)} lines) saved to {args.output}")
        # Print last 30 lines for quick review
        tail = lines[-30:] if len(lines) > 30 else lines
        print("\n--- boot log tail ---")
        print("\n".join(tail))
    else:
        print("No log lines captured")


if __name__ == "__main__":
    main()
