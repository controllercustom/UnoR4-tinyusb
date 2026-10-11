#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# hw_test.sh — flash an example via J-Link, capture Serial1, check patterns.
#
# Usage:
#   ./scripts/hw_test.sh [--fork] <example> [seconds] [send-chars] [expect-pattern ...]
#
#   --fork      build against the unor4tinyusb package (route A): the sketch
#               carries its own tusb_config.h, so no variant config is installed
#               and the stock core is untouched. Default is route B, the patched
#               stock core, which installs the example's config first.
#   seconds     capture window starting at the flash (default 20)
#   send-chars  chars written to Serial1 5 s after capture start (default none);
#               override the delay with SEND_AT=<seconds> -- 5 s is too early for
#               a command that needs the device already mounted
#   expect-pattern  all must appear in the capture (default: example banner)
#
# Examples:
#   ./scripts/hw_test.sh r4_HostInfo
#   ./scripts/hw_test.sh r4_HostFTDI 25 c "ready"
#   ./scripts/hw_test.sh r4_HostCP2102 25 e "ready"
#   SERIAL_PORT=/dev/ttyUSB0 ./scripts/hw_test.sh r4_HostFTDI 60
#   SERIAL_PORT=/dev/ttyUSB0 SEND_AT=14 ./scripts/hw_test.sh --fork r4_HostInfo 40 i "MOUNT"
#
# Debug port: /dev/ttyACM0 by default, override with SERIAL_PORT=. On this bench
# the console is a CP210x UART bridge on the R4's Serial1 pins, so it is
# /dev/ttyUSB0 -- and note that when the R4 is the USB HOST it is not on the
# host's bus at all, so /dev/ttyACM* will not exist.
# Target board: minima by default, override with R4_BOARD=nanor4.
#
# `seconds` is the window from the FLASH onward. The compile happens before the
# capture starts, so it does not eat into this budget; it used to, which made
# every short window capture only the previous sketch's leftover UART output.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

fork=0
if [[ "${1:-}" == "--fork" ]]; then fork=1; shift; fi

name="${1:?usage: hw_test.sh [--fork] <example> [seconds] [send-chars] [expect-pattern ...]}"
seconds="${2:-20}"
send="${3:-}"
shift $(( $# < 3 ? $# : 3 ))
patterns=("$@")

SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM0}"
SEND_AT="${SEND_AT:-5}"
if [[ "$fork" -eq 1 ]]; then
  fqbn="unor4tinyusb:renesas_uno:${R4_BOARD:-minima}"
  builddir="$PROJ_ROOT/build/fork-${R4_BOARD:-minima}/$name"
else
  fqbn="arduino:renesas_uno:${R4_BOARD:-minima}"
  builddir="$PROJ_ROOT/build/${R4_BOARD:-minima}/$name"
fi
restore_union() {
  # Route B only: it installed an example config, so put the shipped default back.
  if [[ "$fork" -eq 0 ]]; then "$SCRIPT_DIR/config.sh" union >/dev/null 2>&1 || true; fi
}
CAPTURE="/tmp/r4tusb_hw_${name}_$(date +%s).cap"
mkdir -p /tmp

# ---- 1. Build FIRST ---------------------------------------------------------
# The compile takes ~40 s. Doing it inside the capture window meant the flash
# landed after the capture had already closed, and the "capture" was really the
# PREVIOUS sketch's trailing UART output.
echo "[hw] building examples/$name for $fqbn..."
if [[ "$fork" -eq 0 && -f "$PROJ_ROOT/examples/$name/tusb_config.h" ]]; then
  cp -f "$PROJ_ROOT/examples/$name/tusb_config.h" \
        "$PROJ_ROOT/src/tusb_config_unor4tinyusb.h"
  "$SCRIPT_DIR/config.sh" install >/dev/null
fi
rm -rf "$builddir"
if ! arduino-cli compile --fqbn "$fqbn" \
      --build-path "$builddir" "$PROJ_ROOT/examples/$name"; then
  echo "[hw] BUILD FAILED" >&2
  restore_union
  exit 1
fi
buildhex="$builddir/$name.ino.hex"
if [[ ! -f "$buildhex" ]]; then
  echo "[hw] no .hex at $buildhex" >&2
  restore_union
  exit 1
fi

# ---- 2. Capture, starting before the flash so the banner is not lost -------
echo "[hw] capturing $SERIAL_PORT for ${seconds}s (send='${send}')"

# Capture (start before flash to catch as much setup output as possible)
python3 - "$SERIAL_PORT" "$CAPTURE" "$seconds" "$send" "$SEND_AT" <<'PYEOF' &
import os, sys, time, termios, tty, select
port, out, seconds, send = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
send_at = float(sys.argv[5])
fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
attrs = list(termios.tcgetattr(fd))
attrs[4] = termios.B115200
attrs[5] = termios.B115200
attrs[3] = attrs[3] & ~(termios.ECHO | termios.ICANON | termios.ISIG)
attrs[2] = attrs[2] & ~(termios.ECHO | termios.ICANON)
attrs[6][termios.VMIN] = 0
attrs[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, attrs)
t_end = time.time() + seconds
t_send = time.time() + send_at
sent = False
data = bytearray()
try:
    while time.time() < t_end:
        if not sent and time.time() >= t_send and send:
            os.write(fd, send.encode())
            sent = True
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                break
            if chunk:
                data.extend(chunk)
finally:
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    os.close(fd)
with open(out, "wb") as f:
    f.write(bytes(data))
print(f"[hw] captured {len(data)} bytes -> {out}")
PYEOF
CAP_PID=$!

sleep 1
echo "[hw] flashing $buildhex via J-Link..."
# "r; h; loadfile" -- NEVER "erase": it wipes the bootloader at 0x00000000.
jlink_script="$(mktemp /tmp/jlink_flash_XXXXXX.jlink)"
printf 'si SWD\ndevice R7FA4M1AB\nspeed 4000\nconnect\nr\nh\nloadfile %s\nr\ng\nq\n' \
  "$buildhex" > "$jlink_script"
flash_ok=1
JLinkExe -CommanderScript "$jlink_script" 2>&1 | tee /tmp/jlink_r4.log || flash_ok=0
rm -f "$jlink_script"
if [[ "$flash_ok" -ne 1 ]]; then
  echo "[hw] FLASH FAILED (see /tmp/jlink_r4.log)" >&2
  wait "$CAP_PID" 2>/dev/null || true
  restore_union
  exit 1
fi

wait "$CAP_PID" 2>/dev/null || true
restore_union

echo "=================== capture (${seconds}s) ==================="
cat -v "$CAPTURE" 2>/dev/null || echo "(no capture)"
echo "============================================================="

rc=0
if [[ ${#patterns[@]} -eq 0 ]]; then
  echo "[hw] no patterns given — inspect capture manually ($CAPTURE)"
else
  for p in "${patterns[@]}"; do
    if grep -qF "$p" "$CAPTURE" 2>/dev/null; then
      echo "[hw] PASS: found '$p'"
    else
      echo "[hw] FAIL: missing '$p'"
      rc=1
    fi
  done
fi
exit $rc
