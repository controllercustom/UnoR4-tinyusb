#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# upload.sh — flash an RP2040/RP2350 USB device emulator via the Raspberry Pi
# Debug Probe, for testing the R4 in host mode.
#
# Usage:
#   ./upload.sh <target> [fqbn]
#   e.g. ./upload.sh cdc-echo
#   e.g. ./upload.sh msc
#   e.g. ./upload.sh key "rp2040:rp2040:rpipico:usbstack=tinyusb,uploadmethod=picoprobe_cmsis_dap"
#
# Targets (VID:PID is set by the sketch, not the FQBN):
#   key        KeyA_Loop     239A:CAFE  boot-protocol keyboard, HID_KEY_A
#   mouse      MouseMove     239A:CAFE  boot-protocol mouse, 10-px circle + LEFT
#   cdc-echo   CDCEcho       239A:CAFD  CDC echo, host verifies seq/CRC
#   cdc-bulk   CDCBulkSource 239A:CAFE  bulk TX flood
#   midi-echo  MIDIEcho      239A:CAFB  4-byte MIDI echo
#   msc        MSCRamDisk    239A:CAFC  256x512 = 128 KiB RAM disk
#
# The emulators speak USB device on the Pico's native USB port, which is
# plugged into the R4's host port through a C-to-A adapter. The R4 sources no
# VBUS, so the Pico needs power injection on its 5V/VBUS pin. The R4 console
# stays on its own CP210x bridge (/dev/ttyUSB0); scripts/hw_test.sh reads it.
#
# After changing an emulator's PID, reset the R4 so it re-enumerates:
#   JLinkExe -CommanderScript <(printf 'si SWD\ndevice R7FA4M1AB\nspeed 4000\nconnect\nr\nh\ng\nq\n')
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Pico 2 by default; pass a full FQBN to override. uploadmethod=picoprobe_cmsis_dap
# drives the Debug Probe's CMSIS-DAP end over OpenOCD, so no BOOTSEL dance.
DEFAULT_FQBN="rp2040:rp2040:rpipico2:usbstack=tinyusb,uploadmethod=picoprobe_cmsis_dap"

target="${1:?usage: upload.sh <target> [fqbn]  (key|mouse|cdc-echo|cdc-bulk|midi-echo|msc)}"
fqbn="${2:-$DEFAULT_FQBN}"

case "$target" in
  key)       sketch="KeyA_Loop"     ;;
  mouse)     sketch="MouseMove"     ;;
  cdc-echo)  sketch="CDCEcho"       ;;
  cdc-bulk)  sketch="CDCBulkSource" ;;
  midi-echo) sketch="MIDIEcho"      ;;
  msc)       sketch="MSCRamDisk"    ;;
  *) echo "unknown target '$target' (key|mouse|cdc-echo|cdc-bulk|midi-echo|msc)" >&2; exit 1 ;;
esac

src="$SCRIPT_DIR/$sketch"
[[ -f "$src/$sketch.ino" ]] || { echo "no sketch at $src" >&2; exit 1; }

echo "[emu] compiling $sketch ($fqbn)..."
rm -rf "$PROJ_ROOT/build/emu/$sketch"
arduino-cli compile --fqbn "$fqbn" --build-path "$PROJ_ROOT/build/emu/$sketch" "$src"

echo "[emu] flashing $sketch via Raspberry Pi Debug Probe..."
arduino-cli upload -p /dev/ttyACM0 --fqbn "$fqbn" \
  --input-dir "$PROJ_ROOT/build/emu/$sketch" "$src"

echo "[emu] done — reset the R4 (J-Link 'r; h; g') so it re-enumerates $sketch."
