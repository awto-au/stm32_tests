#!/usr/bin/env bash
set -euo pipefail

# Capture early boot UART logs, trigger reset, and decode HardFault info.
# Usage examples:
#   tools/hardfault_capture.sh
#   tools/hardfault_capture.sh --port /dev/ttyACM8 --seconds 12
#   tools/hardfault_capture.sh --no-reset

PORT="/dev/ttyACM5"
BAUD="2000000"
SECONDS_CAPTURE="10"
DO_RESET="1"
LOG_DIR="${HOME}/logs"
RESET_CMD_DEFAULT='PROG=/opt/st/stm32cubeide_1.18.0/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.200.202503041107/tools/bin/STM32_Programmer_CLI; EL=/opt/st/stm32cubeide_1.18.0/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.200.202503041107/tools/bin/ExternalLoader/MT25TL01G_STM32H750B-DISCO.stldr; "$PROG" -c port=SWD sn=003400223137510E33333639 -el "$EL" -rst'
RESET_CMD="$RESET_CMD_DEFAULT"

usage() {
  cat <<'EOF'
hardfault_capture.sh

Capture UART boot logs and decode HardFault/BusFault details.

Options:
  --port <device>      Serial device (default: /dev/ttyACM5)
  --baud <rate>        Baud rate (default: 2000000)
  --seconds <n>        Capture duration after reset (default: 10)
  --log-dir <dir>      Output log directory (default: ~/logs)
  --reset-cmd <cmd>    Command used to reset target before capture
  --no-reset           Do not reset, just capture
  -h, --help           Show this help

Output:
  - Writes full UART capture to a timestamped log file
  - Prints last BOOT/START lines and fault summary
  - Decodes CFSR/HFSR bits when a HardFault panic line is present
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      PORT="$2"; shift 2 ;;
    --baud)
      BAUD="$2"; shift 2 ;;
    --seconds)
      SECONDS_CAPTURE="$2"; shift 2 ;;
    --log-dir)
      LOG_DIR="$2"; shift 2 ;;
    --reset-cmd)
      RESET_CMD="$2"; shift 2 ;;
    --no-reset)
      DO_RESET="0"; shift 1 ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2 ;;
  esac
done

mkdir -p "$LOG_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="${LOG_DIR}/stm32h750-hardfault-${STAMP}.log"

if [[ ! -e "$PORT" ]]; then
  echo "Serial port not found: $PORT" >&2
  exit 1
fi

echo "[info] logging to: $LOG_FILE"
echo "[info] port=$PORT baud=$BAUD duration=${SECONDS_CAPTURE}s"

: > "$LOG_FILE"

if [[ "$DO_RESET" == "1" ]]; then
  (
    sleep 0.2
    echo "[info] resetting target..."
    bash -lc "$RESET_CMD" >/dev/null 2>&1 || {
      echo "[warn] reset command failed, continuing capture" >&2
    }
  ) &
fi

if command -v tio >/dev/null 2>&1 && command -v script >/dev/null 2>&1; then
  # tio expects a TTY; run it under script so terminal output is captured.
  script -q /dev/null -c "timeout ${SECONDS_CAPTURE}s tio -b ${BAUD} ${PORT}" | tee "$LOG_FILE" >/dev/null || true
elif command -v tio >/dev/null 2>&1; then
  timeout "${SECONDS_CAPTURE}s" tio -b "$BAUD" "$PORT" 2>&1 | tee "$LOG_FILE" >/dev/null || true
else
  stty -F "$PORT" "$BAUD" cs8 -cstopb -parenb -ixon -ixoff -echo -icanon min 0 time 1
  timeout "${SECONDS_CAPTURE}s" stdbuf -o0 cat "$PORT" | tee "$LOG_FILE" >/dev/null || true
fi

print_recent_stages() {
  echo
  echo "[summary] recent BOOT/START lines:"
  grep -E "\[(BOOT|START|QSPI|LTDC|LVGL|UI)\]" "$LOG_FILE" | tail -n 30 || true
}

decode_cfsr() {
  local cfsr_hex="$1"
  local cfsr=$((16#${cfsr_hex#0x}))

  echo "[decode] CFSR=${cfsr_hex}"
  (( cfsr & 0x00000001 )) && echo "  - IACCVIOL: instruction access violation"
  (( cfsr & 0x00000002 )) && echo "  - DACCVIOL: data access violation"
  (( cfsr & 0x00000008 )) && echo "  - MUNSTKERR: MemManage fault on unstack"
  (( cfsr & 0x00000010 )) && echo "  - MSTKERR: MemManage fault on stacking"
  (( cfsr & 0x00000080 )) && echo "  - MMARVALID: MMFAR holds valid fault address"

  (( cfsr & 0x00000100 )) && echo "  - IBUSERR: instruction bus error"
  (( cfsr & 0x00000200 )) && echo "  - PRECISERR: precise data bus error"
  (( cfsr & 0x00000400 )) && echo "  - IMPRECISERR: imprecise data bus error"
  (( cfsr & 0x00000800 )) && echo "  - UNSTKERR: BusFault on unstack"
  (( cfsr & 0x00001000 )) && echo "  - STKERR: BusFault on stacking"
  (( cfsr & 0x00008000 )) && echo "  - BFARVALID: BFAR holds valid fault address"

  (( cfsr & 0x00010000 )) && echo "  - UNDEFINSTR: undefined instruction"
  (( cfsr & 0x00020000 )) && echo "  - INVSTATE: invalid EPSR state"
  (( cfsr & 0x00040000 )) && echo "  - INVPC: invalid PC load by EXC_RETURN"
  (( cfsr & 0x00080000 )) && echo "  - NOCP: no coprocessor"
  (( cfsr & 0x01000000 )) && echo "  - UNALIGNED: unaligned access"
  (( cfsr & 0x02000000 )) && echo "  - DIVBYZERO: divide by zero"
}

decode_hfsr() {
  local hfsr_hex="$1"
  local hfsr=$((16#${hfsr_hex#0x}))

  echo "[decode] HFSR=${hfsr_hex}"
  (( hfsr & 0x00000002 )) && echo "  - VECTTBL: BusFault during vector table read"
  (( hfsr & 0x40000000 )) && echo "  - FORCED: escalated configurable fault"
  (( hfsr & 0x80000000 )) && echo "  - DEBUGEVT: debug event"
}

print_fault_summary() {
  local hf_line
  hf_line="$(grep -E "\[E\]\[PANIC\] HardFault" "$LOG_FILE" | tail -n 1 || true)"

  if [[ -z "$hf_line" ]]; then
    echo
    echo "[summary] no HardFault panic line found"
    grep -E "\[E\]\[PANIC\]|HardFault|BusFault|UsageFault|MemManage" "$LOG_FILE" | tail -n 20 || true
    return
  fi

  echo
  echo "[summary] last HardFault line:"
  echo "$hf_line"

  local pc lr cfsr hfsr
  pc="$(echo "$hf_line" | sed -n 's/.*pc=\(0x[0-9A-Fa-f]\+\).*/\1/p')"
  lr="$(echo "$hf_line" | sed -n 's/.*lr=\(0x[0-9A-Fa-f]\+\).*/\1/p')"
  cfsr="$(echo "$hf_line" | sed -n 's/.*cfsr=\(0x[0-9A-Fa-f]\+\).*/\1/p')"
  hfsr="$(echo "$hf_line" | sed -n 's/.*hfsr=\(0x[0-9A-Fa-f]\+\).*/\1/p')"

  echo "[summary] registers: pc=${pc:-n/a} lr=${lr:-n/a} cfsr=${cfsr:-n/a} hfsr=${hfsr:-n/a}"

  [[ -n "$cfsr" ]] && decode_cfsr "$cfsr"
  [[ -n "$hfsr" ]] && decode_hfsr "$hfsr"
}

print_recent_stages
print_fault_summary

echo
echo "[done] full log: $LOG_FILE"
