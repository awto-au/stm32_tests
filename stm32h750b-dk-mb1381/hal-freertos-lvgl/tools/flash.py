#!/usr/bin/env python3
"""Flash STM32H750B-DK via SWD and capture boot log on /dev/ttyACM4."""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

DEFAULT_BOARD_SN   = "003400223137510E33333639"
HEX_PATH   = "build/Debug/stm32h750_lvgl.hex"
DEFAULT_SERIAL_DEV = "/dev/ttyACM4"
BAUD       = 2_000_000
LOG_FILE   = "boot.tmp"


_CUBE_FALLBACK_DIRS  = ["~/.vscode-insiders/extensions", "~/.vscode/extensions"]
_CUBE_BINARY_GLOB    = "stmicroelectronics.stm32cube-ide-core-*/resources/binaries/linux/x86_64/cube"
_CUBE_FALLBACK_PATHS = ["~/.local/stm32cube/bin/cube"]

_EXTLOADER_NAME = "MT25TL01G_STM32H750B-DISCO.stldr"
_EXTLOADER_GLOBS = [
    # Standalone STM32CubeProgrammer install (version-agnostic)
    str(Path.home() / "STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader" / _EXTLOADER_NAME),
    # CubeIDE embedded (any version)
    str(Path.home() / "STMicroelectronics/STM32CubeIDE/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_*/tools/bin/ExternalLoader" / _EXTLOADER_NAME),
    str(Path.home() / ".local/opt/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_*/tools/bin/ExternalLoader" / _EXTLOADER_NAME),
]


def _find_cube() -> str:
    found = shutil.which("cube")
    if found:
        return found
    for p in _CUBE_FALLBACK_PATHS:
        cand = Path(p).expanduser()
        if cand.is_file() and os.access(cand, os.X_OK):
            return str(cand)
    for base in _CUBE_FALLBACK_DIRS:
        matches = sorted(glob.glob(str(Path(base).expanduser() / _CUBE_BINARY_GLOB)))
        if matches:
            return matches[-1]
    sys.stderr.write(
        "FATAL: 'cube' binary not found on PATH or in known fallback locations.\n"
        "       Install STM32CubeIDE or the cube CLI, or add it to PATH.\n"
    )
    sys.exit(2)


def _find_extloader() -> str:
    for pattern in _EXTLOADER_GLOBS:
        matches = sorted(glob.glob(str(Path(pattern).expanduser())))
        if matches:
            return matches[-1]
    sys.exit(
        f"{_EXTLOADER_NAME} not found.\n"
        "Install STM32CubeProgrammer standalone or STM32CubeIDE."
    )


def detect_board_and_uart() -> tuple[str, str]:
    """Detect target ST-Link SN and matching UART port from --list output."""
    cube = _find_cube()
    try:
        result = subprocess.run(
            [cube, "programmer", "--list"],
            capture_output=True,
            text=True,
            check=False,
        )
    except Exception as exc:
        print(f"warn: failed to query programmer list ({exc}), using defaults", file=sys.stderr)
        return DEFAULT_BOARD_SN, DEFAULT_SERIAL_DEV

    text = result.stdout or ""

    # Parse ST-LINK probes first.
    probes = []
    for m in re.finditer(
        r"ST-LINK SN\s*:\s*([0-9A-Fa-f]+)\s*\n"
        r"\s*ST-LINK FW\s*:[^\n]*\n"
        r"\s*Access Port Number\s*:[^\n]*\n"
        r"\s*Board Name\s*:\s*([^\n]*)",
        text,
        flags=re.IGNORECASE,
    ):
        probes.append((m.group(1).upper(), m.group(2).strip()))

    selected_sn = DEFAULT_BOARD_SN
    for sn, board_name in probes:
        if board_name.upper() == "STM32H750B-DK":
            selected_sn = sn
            break
    else:
        for sn, _ in probes:
            if sn == DEFAULT_BOARD_SN:
                selected_sn = sn
                break

    # Parse UART mapping blocks and match by ST-Link SN.
    uart_by_sn: dict[str, str] = {}
    for m in re.finditer(
        r"ST-LINK SN:\s*([0-9A-Fa-f]+)\s*\n"
        r"Port:\s*(tty\S+)",
        text,
        flags=re.IGNORECASE,
    ):
        uart_by_sn[m.group(1).upper()] = f"/dev/{m.group(2)}"

    serial_dev = uart_by_sn.get(selected_sn, DEFAULT_SERIAL_DEV)
    if selected_sn == DEFAULT_BOARD_SN and selected_sn not in uart_by_sn:
        print("warn: could not map ST-Link SN to UART port, using default serial device", file=sys.stderr)

    return selected_sn, serial_dev


def flash(hex_path: str, board_sn: str) -> bool:
    cmd = [
        _find_cube(), "programmer",
        "--connect", f"port=SWD", f"sn={board_sn}", "freq=4000", "reset=HWrst",
        "--extload", _find_extloader(),
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


def reset_only(board_sn: str) -> None:
    cmd = [
        _find_cube(), "programmer",
        "--connect", f"port=SWD", f"sn={board_sn}", "freq=4000", "reset=HWrst",
        "--hardRst",
    ]
    subprocess.run(cmd, capture_output=True)


def capture_log(duration_s: int, serial_dev: str) -> tuple[list[str], threading.Thread]:
    try:
        import serial
    except ImportError:
        print("pyserial not available, skipping log capture", file=sys.stderr)
        return []

    lines: list[str] = []

    def read_loop() -> None:
        try:
            s = serial.Serial(serial_dev, BAUD, timeout=0.5)
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
    board_sn, serial_dev = detect_board_and_uart()
    print(f"Using ST-Link SN: {board_sn}")
    print(f"Using UART port:  {serial_dev}")

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
            lines_ref, log_thread = capture_log(args.log_seconds, serial_dev)
            time.sleep(0.1)
        reset_only(board_sn)
    else:
        ok = flash(args.hex, board_sn)
        if not ok:
            sys.exit("Flash failed")
        print("Flash OK", flush=True)
        # Open port first, then reset — same pattern as --no-flash so we don't miss boot
        if has_serial:
            lines_ref, log_thread = capture_log(args.log_seconds, serial_dev)
            time.sleep(0.1)
        reset_only(board_sn)

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
