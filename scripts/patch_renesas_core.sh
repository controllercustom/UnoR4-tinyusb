#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# patch_renesas_core.sh — Upgrade the Arduino Renesas core (Uno R4) to the
# TinyUSB master snapshot vendored in scripts/tinyusb-master/.
#
# Linux version: uses only stock utilities (bash, patch, grep, sha256sum,
# cp/rm/mv). No Python, no arduino-cli required (arduino-cli is used only to
# locate the core when present).
# Sibling scripts: patch_renesas_core.macos.sh, patch_renesas_core.windows.ps1
# (.windows.bat double-click wrapper). All three do the same steps:
#   1. verify the vendored tree (scripts/tinyusb-master/SHA256SUMS)
#   2. back up the core's tinyusb tree + tusb_config.h + USB.cpp
#   3. replace cores/arduino/tinyusb/ with the vendored tree (RUSB2 HCD fixes
#      already applied; see scripts/tinyusb-master/PIN)
#   4. install the device+host tusb_config.h for MINIMA and NANOR4
#      (stock NANOR4 config is byte-identical to stock MINIMA)
#   5. apply scripts/renesas_core.patch (USB.cpp weak descriptor callbacks +
#      tusb_rhport_init)
#
# Usage:
#   ./scripts/patch_renesas_core.sh            # upgrade core to the pinned master
#   ./scripts/patch_renesas_core.sh --restore  # restore the backed-up core files
#
# Pinned core version: 1.6.0 (override with R4TUSB_ALLOW_ANY_VERSION=1).
# Test hook: set R4TUSB_CORE_DIR to patch a copy of the core instead.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_FILE="$SCRIPT_DIR/renesas_core.patch"
CONFIG_SRC="$SCRIPT_DIR/../src/tusb_config_unor4tinyusb.h"
VENDORED="$SCRIPT_DIR/tinyusb-master"
PINNED_VERSION="1.6.0"

[[ -f "$PATCH_FILE" ]] || { echo "[patch] ERROR: $PATCH_FILE not found" >&2; exit 1; }
[[ -f "$CONFIG_SRC" ]] || { echo "[patch] ERROR: $CONFIG_SRC not found" >&2; exit 1; }
[[ -d "$VENDORED" ]] || { echo "[patch] ERROR: vendored tree not found: $VENDORED" >&2; exit 1; }

TARGET_TUSB_VERSION="$(cat "$VENDORED/VERSION" 2>/dev/null || echo unknown)"

find_core_dir() {
  if [[ -n "${R4TUSB_CORE_DIR:-}" ]]; then
    echo "$R4TUSB_CORE_DIR"
    return
  fi
  local data_dir=""
  if command -v arduino-cli >/dev/null 2>&1; then
    data_dir="$(arduino-cli config get directories.data 2>/dev/null || true)"
  fi
  [[ -n "$data_dir" ]] || data_dir="$HOME/.arduino15"
  local root="$data_dir/packages/arduino/hardware/renesas_uno"
  [[ -d "$root" ]] || {
    echo "[patch] ERROR: no renesas_uno core installed at $root." >&2
    echo "[patch] Install via Boards Manager: Arduino UNO R4 Boards" >&2
    exit 1
  }
  # Highest-versioned install wins.
  local versions=("$root"/*)
  local core_dir="${versions[-1]}"
  [[ -d "$core_dir" ]] || { echo "[patch] ERROR: no version directory under $root" >&2; exit 1; }
  echo "$core_dir"
}

CORE_DIR="$(find_core_dir)"
CORE_DIR="${CORE_DIR%/}"
VER="$(basename "$CORE_DIR")"

if [[ "$VER" != "$PINNED_VERSION" && "${R4TUSB_ALLOW_ANY_VERSION:-0}" != "1" ]]; then
  echo "[patch] ERROR: core version $VER found, but this patch is pinned to $PINNED_VERSION." >&2
  echo "[patch] Re-run with R4TUSB_ALLOW_ANY_VERSION=1 to try anyway." >&2
  exit 1
fi

echo "[patch] core $CORE_DIR"

USB_CPP="$CORE_DIR/cores/arduino/usb/USB.cpp"
TUSB_TREE="$CORE_DIR/cores/arduino/tinyusb"
HCD="$TUSB_TREE/portable/renesas/rusb2/hcd_rusb2.c"
MINI_CFG="$CORE_DIR/variants/MINIMA/tusb_config.h"
MINI_MARKER="$CORE_DIR/variants/MINIMA/.r4tinyusb_patched"
NANO_CFG="$CORE_DIR/variants/NANOR4/tusb_config.h"
NANO_MARKER="$CORE_DIR/variants/NANOR4/.r4tinyusb_patched"
BAK_DIR="$CORE_DIR/.r4tinyusb"
TREE_BAK="$BAK_DIR/tinyusb.bak"

# ---- State checks -----------------------------------------------------------
# All markers present + marker version match  => already upgraded (no-op).
# No markers at all                            => clean stock (upgrade OK).
# Anything in between                          => partial (refuse; restore first).
mark_usb=0; grep -qF '__attribute__((weak)) const uint8_t *tud_descriptor_device_cb' "$USB_CPP" 2>/dev/null && mark_usb=1
mark_mini=0; grep -qF 'scripts/config.sh' "$MINI_CFG" 2>/dev/null && mark_mini=1
mark_nano=0; grep -qF 'scripts/config.sh' "$NANO_CFG" 2>/dev/null && mark_nano=1
mark_hcd=0; { [[ -f "$HCD" ]] && grep -qF 'rusb->DCPCFG_b.DIR != 1' "$HCD"; } 2>/dev/null && mark_hcd=1
mark_str=0; grep -qF '__attribute__((weak)) const uint16_t *tud_descriptor_string_cb' "$USB_CPP" 2>/dev/null && mark_str=1
mark_ver_mini=""
[[ -f "$MINI_MARKER" ]] && mark_ver_mini="$(cat "$MINI_MARKER" 2>/dev/null || true)"
mark_ver_nano=""
[[ -f "$NANO_MARKER" ]] && mark_ver_nano="$(cat "$NANO_MARKER" 2>/dev/null || true)"
TOTAL=$((mark_usb + mark_mini + mark_nano + mark_hcd + mark_str))

do_restore() {
  echo "[patch] restoring core $CORE_DIR"
  restored=0
  for cfg in "$MINI_CFG" "$NANO_CFG"; do
    if [[ -f "$cfg.r4tinyusb_orig" ]]; then
      cp -f "$cfg.r4tinyusb_orig" "$cfg"
      rm -f "$cfg.r4tinyusb_orig"
      echo "[patch] restored $cfg"
      restored=1
    fi
  done
  if [[ -f "$USB_CPP.r4tinyusb_orig" ]]; then
    cp -f "$USB_CPP.r4tinyusb_orig" "$USB_CPP"
    rm -f "$USB_CPP.r4tinyusb_orig"
    echo "[patch] restored $USB_CPP"
    restored=1
  fi
  if [[ -d "$TREE_BAK" ]]; then
    rm -rf "$TUSB_TREE"
    mv "$TREE_BAK" "$TUSB_TREE"
    # A stale in-tree hcd sidecar from older scripts must not survive.
    rm -f "$TUSB_TREE"/rusb2/hcd_rusb2.c.r4tinyusb_orig \
          "$TUSB_TREE"/portable/renesas/rusb2/hcd_rusb2.c.r4tinyusb_orig
    echo "[patch] restored $TUSB_TREE"
    restored=1
  fi
  for marker in "$MINI_MARKER" "$NANO_MARKER"; do
    if [[ -f "$marker" ]]; then
      rm -f "$marker"
      restored=1
    fi
  done
  rmdir "$BAK_DIR" 2>/dev/null || true
  if [[ "$restored" -eq 0 ]]; then
    echo "[patch] nothing to restore (no backups found)"
    return 1
  fi
  echo "[patch] restore done."
}

if [[ "${1:-}" == "--restore" ]]; then
  do_restore
  exit $?
fi

if [[ "$mark_ver_mini" == "$TARGET_TUSB_VERSION" && "$mark_ver_nano" == "$TARGET_TUSB_VERSION" && "$TOTAL" -eq 5 ]]; then
  echo "[patch] already upgraded to TinyUSB $TARGET_TUSB_VERSION, skipping."
  exit 0
fi
if [[ "$TOTAL" -ne 0 || -n "$mark_ver_mini" || -n "$mark_ver_nano" ]]; then
  echo "[patch] ERROR: core appears PARTIALLY patched (markers: usb=$mark_usb minima=$mark_mini nano=$mark_nano hcd=$mark_hcd str=$mark_str)." >&2
  echo "[patch] Restore first: $0 --restore" >&2
  exit 1
fi

# ---- Verify vendored tree ----------------------------------------------------
echo "[patch] verifying vendored tree checksums..."
(cd "$VENDORED" && sha256sum -c SHA256SUMS) || {
  echo "[patch] ERROR: vendored tree checksum mismatch" >&2
  exit 1
}

# ---- Back up -----------------------------------------------------------------
# Backup lives at the core root (.r4tinyusb/): arduino-cli compiles every .c
# file under cores/arduino/, so a backup tree inside it would double-compile.
mkdir -p "$BAK_DIR"
cp -f "$MINI_CFG" "$MINI_CFG.r4tinyusb_orig"
cp -f "$NANO_CFG" "$NANO_CFG.r4tinyusb_orig"
cp -f "$USB_CPP" "$USB_CPP.r4tinyusb_orig"
rm -rf "$TREE_BAK"
cp -a "$TUSB_TREE" "$TREE_BAK"
echo "[patch] backups in $BAK_DIR + *.r4tinyusb_orig"

# ---- Replace tree -------------------------------------------------------------
# Delete contents (keep the directory: the backup move on --restore recreates it).
find "$TUSB_TREE" -mindepth 1 -delete
for entry in "$VENDORED"/*; do
  name="$(basename "$entry")"
  [[ "$name" == "SHA256SUMS" || "$name" == "VERSION" || "$name" == "PIN" ]] && continue
  cp -a "$entry" "$TUSB_TREE/$name"
done
echo "[patch] replaced $TUSB_TREE with TinyUSB $TARGET_TUSB_VERSION"

# ---- Install config (MINIMA + NANOR4; stock files are identical) ---------------
cp -f "$CONFIG_SRC" "$MINI_CFG"
echo "[patch] wrote $MINI_CFG (active config from the library)"
cp -f "$CONFIG_SRC" "$NANO_CFG"
echo "[patch] wrote $NANO_CFG (active config from the library)"

# ---- Patch USB.cpp + hcd --------------------------------------------------------
if ! (cd "$CORE_DIR" && patch -p1 --dry-run --silent < "$PATCH_FILE" >/dev/null); then
  echo "[patch] ERROR: renesas_core.patch does not apply cleanly to core $VER." >&2
  echo "[patch] Restore first: $0 --restore" >&2
  exit 1
fi
(cd "$CORE_DIR" && patch -p1 --silent < "$PATCH_FILE")
echo "[patch] applied renesas_core.patch"

# ---- Post-checks ------------------------------------------------------------------
fail=0
grep -qF '__attribute__((weak)) const uint8_t *tud_descriptor_device_cb' "$USB_CPP" || { echo "[patch] POSTCHECK FAIL: USB.cpp weak hooks" >&2; fail=1; }
grep -qF '__attribute__((weak)) const uint16_t *tud_descriptor_string_cb' "$USB_CPP" || { echo "[patch] POSTCHECK FAIL: USB.cpp weak string descriptor" >&2; fail=1; }
grep -qF 'tud_rhport_init(BOARD_TUD_RHPORT' "$USB_CPP" || { echo "[patch] POSTCHECK FAIL: USB.cpp tusb_rhport_init" >&2; fail=1; }
grep -qF 'scripts/config.sh' "$MINI_CFG" || { echo "[patch] POSTCHECK FAIL: MINIMA tusb_config.h install" >&2; fail=1; }
grep -qF 'scripts/config.sh' "$NANO_CFG" || { echo "[patch] POSTCHECK FAIL: NANOR4 tusb_config.h install" >&2; fail=1; }
grep -qF 'rusb->DCPCFG_b.DIR != 1' "$HCD" || { echo "[patch] POSTCHECK FAIL: hcd_rusb2.c fixes" >&2; fail=1; }
[[ $fail -eq 0 ]] || exit 1

printf '%s\n' "$TARGET_TUSB_VERSION" > "$MINI_MARKER"
printf '%s\n' "$TARGET_TUSB_VERSION" > "$NANO_MARKER"
echo "[patch] done. TinyUSB $TARGET_TUSB_VERSION installed."
echo "[patch] the config source of truth is src/tusb_config_unor4tinyusb.h"
echo "[patch] change it with ./scripts/config.sh set <Example> | union"
