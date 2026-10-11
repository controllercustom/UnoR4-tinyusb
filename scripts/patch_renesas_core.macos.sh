#!/bin/sh
# SPDX-License-Identifier: MIT
# patch_renesas_core.macos.sh — Upgrade the Arduino Renesas core (Uno R4) to the
# TinyUSB master snapshot vendored in scripts/tinyusb-master/
#
# macOS version: uses only the default shell (/bin/sh) and stock utilities
# (patch, grep, shasum, cp/rm/mv). No Python, no arduino-cli required
# (arduino-cli is used only to locate the core when present).
# See patch_renesas_core.sh for the Linux version and
# patch_renesas_core.windows.ps1 for Windows. All three do the same steps;
# the upgrade flow is documented in patch_renesas_core.sh.
#
# Usage:
#   sh scripts/patch_renesas_core.macos.sh            # upgrade core to the pinned master
#   sh scripts/patch_renesas_core.macos.sh --restore  # restore the backed-up core files
#
# Pinned core version: 1.6.0 (override with R4TUSB_ALLOW_ANY_VERSION=1).
# Test hook: set R4TUSB_CORE_DIR to patch a copy of the core instead.

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PATCH_FILE="$SCRIPT_DIR/renesas_core.patch"
CONFIG_SRC="$SCRIPT_DIR/../src/tusb_config_unor4tinyusb.h"
VENDORED="$SCRIPT_DIR/tinyusb-master"
PINNED_VERSION="1.6.0"

[ -f "$PATCH_FILE" ] || { echo "[patch] ERROR: $PATCH_FILE not found" >&2; exit 1; }
[ -f "$CONFIG_SRC" ] || { echo "[patch] ERROR: $CONFIG_SRC not found" >&2; exit 1; }
[ -d "$VENDORED" ] || { echo "[patch] ERROR: vendored tree not found: $VENDORED" >&2; exit 1; }

TARGET_TUSB_VERSION=$(cat "$VENDORED/VERSION" 2>/dev/null || echo unknown)

if [ -n "${R4TUSB_CORE_DIR:-}" ]; then
    CORE_DIR="$R4TUSB_CORE_DIR"
else
    DATA_DIR=$(arduino-cli config get directories.data 2>/dev/null || true)
    [ -n "$DATA_DIR" ] || DATA_DIR="$HOME/Library/Arduino15"
    CORE_ROOT="$DATA_DIR/packages/arduino/hardware/renesas_uno"
    if [ ! -d "$CORE_ROOT" ]; then
        echo "[patch] ERROR: no renesas_uno core installed at $CORE_ROOT." >&2
        echo "[patch] Install via Boards Manager: Arduino UNO R4 Boards" >&2
        exit 1
    fi
    # Highest-versioned install wins (glob sorts lexically; good for x.y.z).
    CORE_DIR=""
    for v in "$CORE_ROOT"/*; do
        [ -d "$v" ] && CORE_DIR="$v"
    done
    if [ -z "$CORE_DIR" ]; then
        echo "[patch] ERROR: no version directory under $CORE_ROOT" >&2
        exit 1
    fi
fi
CORE_DIR=${CORE_DIR%/}
VER=${CORE_DIR##*/}

if [ "$VER" != "$PINNED_VERSION" ] && [ "${R4TUSB_ALLOW_ANY_VERSION:-0}" != "1" ]; then
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

# ---- State checks --------------------------------------------------------
mark_usb=0; grep -qF '__attribute__((weak)) const uint8_t *tud_descriptor_device_cb' "$USB_CPP" 2>/dev/null && mark_usb=1 || true
mark_mini=0; grep -q 'HOST CONFIGURATION' "$MINI_CFG" 2>/dev/null && mark_mini=1 || true
mark_nano=0; grep -q 'HOST CONFIGURATION' "$NANO_CFG" 2>/dev/null && mark_nano=1 || true
mark_hcd=0; { [ -f "$HCD" ] && grep -qF 'rusb->DCPCFG_b.DIR != 1' "$HCD"; } 2>/dev/null && mark_hcd=1 || true
mark_str=0; grep -qF '__attribute__((weak)) const uint16_t *tud_descriptor_string_cb' "$USB_CPP" 2>/dev/null && mark_str=1 || true
mark_ver_mini=""; [ -f "$MINI_MARKER" ] && mark_ver_mini=$(cat "$MINI_MARKER" 2>/dev/null || true)
mark_ver_nano=""; [ -f "$NANO_MARKER" ] && mark_ver_nano=$(cat "$NANO_MARKER" 2>/dev/null || true)
TOTAL=$((mark_usb + mark_mini + mark_nano + mark_hcd + mark_str))

do_restore() {
    echo "[patch] restoring core $CORE_DIR"
    restored=0
    for cfg in "$MINI_CFG" "$NANO_CFG"; do
        if [ -f "$cfg.r4tinyusb_orig" ]; then
            cp -f "$cfg.r4tinyusb_orig" "$cfg"
            rm -f "$cfg.r4tinyusb_orig"
            echo "[patch] restored $cfg"
            restored=1
        fi
    done
    if [ -f "$USB_CPP.r4tinyusb_orig" ]; then
        cp -f "$USB_CPP.r4tinyusb_orig" "$USB_CPP"
        rm -f "$USB_CPP.r4tinyusb_orig"
        echo "[patch] restored $USB_CPP"
        restored=1
    fi
    if [ -d "$TREE_BAK" ]; then
        rm -rf "$TUSB_TREE"
        mv "$TREE_BAK" "$TUSB_TREE"
        rm -f "$TUSB_TREE/rusb2/hcd_rusb2.c.r4tinyusb_orig" \
              "$TUSB_TREE/portable/renesas/rusb2/hcd_rusb2.c.r4tinyusb_orig"
        echo "[patch] restored $TUSB_TREE"
        restored=1
    fi
    for marker in "$MINI_MARKER" "$NANO_MARKER"; do
        if [ -f "$marker" ]; then
            rm -f "$marker"
            restored=1
        fi
    done
    rmdir "$BAK_DIR" 2>/dev/null || true
    if [ "$restored" -eq 0 ]; then
        echo "[patch] nothing to restore (no backups found)"
        return 1
    fi
    echo "[patch] restore done."
}

if [ "${1:-}" = "--restore" ]; then
    do_restore
    exit $?
fi

if [ "$mark_ver_mini" = "$TARGET_TUSB_VERSION" ] && [ "$mark_ver_nano" = "$TARGET_TUSB_VERSION" ] && [ "$TOTAL" -eq 5 ]; then
    echo "[patch] already upgraded to TinyUSB $TARGET_TUSB_VERSION, skipping."
    exit 0
fi
if [ "$TOTAL" -ne 0 ] || [ -n "$mark_ver_mini" ] || [ -n "$mark_ver_nano" ]; then
    echo "[patch] ERROR: core appears PARTIALLY patched (markers: usb=$mark_usb minima=$mark_mini nano=$mark_nano hcd=$mark_hcd str=$mark_str)." >&2
    echo "[patch] Restore first: sh $0 --restore" >&2
    exit 1
fi

# ---- Verify vendored tree (shasum ships with macOS; sha256sum does not) ---
echo "[patch] verifying vendored tree checksums..."
(cd "$VENDORED" && shasum -a 256 -c SHA256SUMS) || {
    echo "[patch] ERROR: vendored tree checksum mismatch" >&2
    exit 1
}

# ---- Back up (at the core root: a backup under cores/arduino/ would
# ---- double-compile, arduino-cli builds every .c file found there) -------
mkdir -p "$BAK_DIR"
cp -f "$MINI_CFG" "$MINI_CFG.r4tinyusb_orig"
cp -f "$NANO_CFG" "$NANO_CFG.r4tinyusb_orig"
cp -f "$USB_CPP" "$USB_CPP.r4tinyusb_orig"
rm -rf "$TREE_BAK"
cp -R "$TUSB_TREE" "$TREE_BAK"
echo "[patch] backups in $BAK_DIR + *.r4tinyusb_orig"

# ---- Replace tree ----------------------------------------------------------
for entry in "$TUSB_TREE"/* "$TUSB_TREE"/.*; do
    [ -e "$entry" ] || continue
    case "$entry" in
        */.|*/..) continue ;;
    esac
    rm -rf "$entry"
done
for entry in "$VENDORED"/*; do
    name=${entry##*/}
    [ "$name" = "SHA256SUMS" ] && continue
    [ "$name" = "VERSION" ] && continue
    [ "$name" = "PIN" ] && continue
    cp -R "$entry" "$TUSB_TREE/$name"
done
echo "[patch] replaced $TUSB_TREE with TinyUSB $TARGET_TUSB_VERSION"

# ---- Install config (MINIMA + NANOR4; stock files are identical) -------------
cp -f "$CONFIG_SRC" "$MINI_CFG"
echo "[patch] wrote $MINI_CFG (active config from the library)"
cp -f "$CONFIG_SRC" "$NANO_CFG"
echo "[patch] wrote $NANO_CFG (active config from the library)"

# ---- Patch USB.cpp + hcd (dry run, then apply) ------------------------------
# Dry-run flag: GNU patch knows --dry-run, Apple/BSD patch uses -C.
DRYRUN="--dry-run"
if ! patch --help 2>&1 | grep -q -- '--dry-run'; then
    DRYRUN="-C"
fi

(cd "$CORE_DIR" && patch -p1 $DRYRUN -s < "$PATCH_FILE") || {
    echo "[patch] ERROR: renesas_core.patch does not apply cleanly to core $VER." >&2
    echo "[patch] Restore first: sh $0 --restore" >&2
    exit 1
}

(cd "$CORE_DIR" && patch -p1 -s < "$PATCH_FILE")
echo "[patch] applied renesas_core.patch"

# ---- Post-checks ---------------------------------------------------------------
fail=0
grep -qF '__attribute__((weak)) const uint8_t *tud_descriptor_device_cb' "$USB_CPP" || { echo "[patch] POSTCHECK FAIL: USB.cpp weak hooks" >&2; fail=1; }
grep -qF '__attribute__((weak)) const uint16_t *tud_descriptor_string_cb' "$USB_CPP" || { echo "[patch] POSTCHECK FAIL: USB.cpp weak string descriptor" >&2; fail=1; }
grep -qF 'tud_rhport_init(BOARD_TUD_RHPORT' "$USB_CPP" || { echo "[patch] POSTCHECK FAIL: USB.cpp tusb_rhport_init" >&2; fail=1; }
grep -qF 'scripts/config.sh' "$MINI_CFG" || { echo "[patch] POSTCHECK FAIL: MINIMA tusb_config.h install" >&2; fail=1; }
grep -qF 'scripts/config.sh' "$NANO_CFG" || { echo "[patch] POSTCHECK FAIL: NANOR4 tusb_config.h install" >&2; fail=1; }
grep -qF 'rusb->DCPCFG_b.DIR != 1' "$HCD" || { echo "[patch] POSTCHECK FAIL: hcd_rusb2.c fixes" >&2; fail=1; }
[ "$fail" -eq 0 ] || exit 1

printf '%s\n' "$TARGET_TUSB_VERSION" > "$MINI_MARKER"
printf '%s\n' "$TARGET_TUSB_VERSION" > "$NANO_MARKER"
echo "[patch] done. TinyUSB $TARGET_TUSB_VERSION installed."
echo "[patch] the config source of truth is src/tusb_config_unor4tinyusb.h"
echo "[patch] change it with ./scripts/config.sh set <Example> | union"
