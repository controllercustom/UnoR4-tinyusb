#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# upload_r4.sh — flash UnoR4-tinyusb example via SEGGER J-Link.
#
# Usage:
#   ./scripts/upload_r4.sh <example-name> [board]
#   ./scripts/upload_r4.sh --fork <example-name> [board]
#   e.g. ./scripts/upload_r4.sh r4_HostInfo
#   e.g. ./scripts/upload_r4.sh r4_HostInfo nanor4
#   e.g. ./scripts/upload_r4.sh --fork r4_HostInfo
#
# --fork builds against the unor4tinyusb package (route A): the sketch carries
# its own tusb_config.h, so nothing is installed into a variant and the stock
# core is not involved. Without it, route B: this example's config is installed
# into the stock core's variants first.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJ_ROOT/build"

fork=0
if [[ "${1:-}" == "--fork" ]]; then fork=1; shift; fi

name="${1:?usage: upload_r4.sh [--fork] <example-name> [board]}"
board="${2:-minima}"
if [[ "$fork" -eq 1 ]]; then
  fqbn="unor4tinyusb:renesas_uno:$board"
  builddir="$BUILD_DIR/fork-$board/$name"
else
  fqbn="arduino:renesas_uno:$board"
  builddir="$BUILD_DIR/$board/$name"
fi

# Activate this example's own config first (build.sh does the same): a sketch
# may not fit in 32 KB against the full union, and Device_NET / Host_audio_host /
# Host_Printer need classes the union deliberately leaves off. Route A only --
# under the fork the sketch folder is on the include path and the copy next to
# the .ino already wins, so installing anything would be pointless mutation.
if [[ "$fork" -eq 0 && -f "$PROJ_ROOT/examples/$name/tusb_config.h" ]]; then
  cp -f "$PROJ_ROOT/examples/$name/tusb_config.h" \
        "$PROJ_ROOT/src/tusb_config_unor4tinyusb.h"
  "$SCRIPT_DIR/config.sh" install >/dev/null
fi

# Compile
echo "[upload] compiling examples/$name for $fqbn..."
rm -rf "$builddir"
arduino-cli compile --fqbn "$fqbn" --build-path "$builddir" "$PROJ_ROOT/examples/$name"
buildhex="$builddir/$name.ino.hex"
[[ -f "$buildhex" ]] || { echo "no .hex file at $buildhex" >&2; exit 1; }

echo "[upload] flashing $buildhex via J-Link..."
TMPSCRIPT="$(mktemp /tmp/jlink_flash_XXXX.jlink)"
cat > "$TMPSCRIPT" <<JLINK_EOF
si SWD
device R7FA4M1AB
speed 4000
connect
r
h
loadfile $buildhex
r
g
q
JLINK_EOF
if ! JLinkExe -CommanderScript "$TMPSCRIPT" 2>&1 | tee /tmp/jlink_r4.log; then
  echo "[upload] J-Link failed, trying OpenOCD..." >&2
  openocd \
    -f /usr/share/openocd/scripts/interface/jlink.cfg \
    -c "transport select swd" \
    -f /usr/share/openocd/scripts/target/renesas_ra4m1.cfg \
    -c "adapter speed 4000; init; reset halt; program $buildhex verify; reset run; exit" 2>&1 | tee -a /tmp/jlink_r4.log || true
fi
rm -f "$TMPSCRIPT"
echo "[upload] done."
