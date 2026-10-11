#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# config.sh — manage the ONE active TinyUSB config, and generate the
# per-example configs.
#
# WHY THIS SCRIPT EXISTS
# ----------------------
# The RA4M1 core reads its TinyUSB config from
# <core>/variants/<BOARD>/tusb_config.h. arduino-cli compiles the core's
# vendored TinyUSB sources as CORE translation units, and those do NOT get the
# library's src/ on their include path (only the library's own sources and the
# sketch do — verified with --only-compilation-database). So the active config
# cannot live in src/ and be #included: it has to be COPIED into the variant
# dir. src/tusb_config_unor4tinyusb.h is the source of truth; this script
# keeps the two in sync and keeps the 54 per-example copies from drifting.
#
# The per-example configs are GENERATED from the one source of truth, not
# hand-written: this file owns the class-selection table below and rewrites only
# the lines between the BEGIN/END GENERATED SECTION markers in the active
# config. Everything outside those markers is shared verbatim.
#
# Usage:
#   ./scripts/config.sh union           restore the full union (default)
#                                    (copies scripts/tusb_config.union.h back
#                                     over the active config, then installs it)
#   ./scripts/config.sh set <Example>   activate examples/<Example>'s config
#   ./scripts/config.sh gen             regenerate every per-example config
#   ./scripts/config.sh install         copy src/ config into the core variants
#   ./scripts/config.sh list            show each example's class signature
#   ./scripts/config.sh verify          check each sketch's API against its config
#
# After `set` or `union` the config is installed into the core automatically.
# Pass --no-install to skip that (useful when the core is not patched yet).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ACTIVE="$PROJ_ROOT/src/tusb_config_unor4tinyusb.h"
EXAMPLES_DIR="$PROJ_ROOT/examples"
# Pristine copy of the union. src/tusb_config_unor4tinyusb.h is BOTH the
# active config and the file `set` overwrites with an example's config, so
# without this copy `union` would just re-install whatever example ran last
# -- which is exactly the bug that shipped in b118f18.
UNION_CFG="$SCRIPT_DIR/tusb_config.union.h"
MARK_BEGIN="// ==== BEGIN GENERATED SECTION (scripts/config.sh) ===="
MARK_END="// ==== END GENERATED SECTION ===="
OUTDIR_NOTE="${TMPDIR:-/tmp}/r4tinyusb-config-$$"

DO_INSTALL=1
[[ "${1:-}" == "--no-install" ]] && { DO_INSTALL=0; shift; }

# ---------------------------------------------------------------------------
# Class-selection table.
#
#   <ExampleDir>            <role> <class>...
#
# <role> is DEVICE, HOST or BOTH. A single-role config sets the other stack's
# CFG_*_ENABLED to 0, which is what makes the UnoR4TinyUSB.h role guards fire
# (a device config compiled against a Host example is a compile error naming
# the fix, not an undefined reference at link time).
#
# Class names map to the CFG_<role>_<NAME> macro. "AUDIO1" is the special case
# of the single mono-IN audio function; "AUDIO" is the host audio driver.
#
# "CLASS=N" caps that side BELOW its default in count_for() below (the device
# and host counts are separate — a host CDC slot count says nothing about the
# device one). Omit it unless you need fewer instances than the default; the
# defaults are already the maximum any example needs.
# ---------------------------------------------------------------------------
SIGNATURES='
r4_DeviceCDC                    DEVICE VENDOR
r4_HostCDC                      HOST   CDC CDC_FTDI CDC_CP210X CDC_PL2303 CDC_CH34X HUB
r4_HostCP2102                   HOST   CDC CDC_CP210X HUB
r4_HostDualSense                HOST   HID HUB
r4_HostFTDI                     HOST   CDC CDC_FTDI HUB
r4_HostGamepad                  HOST   HID HUB
r4_HostHID                      HOST   HID HUB
r4_HostHID_Boot                 HOST   HID HUB
r4_HostInfo                     HOST   CDC HID MSC HUB
r4_HostJoystick                 HOST   HID HUB
r4_HostKeyboard                 HOST   HID HUB
r4_HostMSC                      HOST   MSC HUB
r4_HostMouse                    HOST   HID HUB
r4_HostPL2303                   HOST   CDC CDC_PL2303 HUB
r4_HostTriCDC                   HOST   CDC CDC_FTDI CDC_CP210X CDC_PL2303 CDC_CH34X HUB
r4_msc_file_explorer            HOST   MSC HUB
Device_audio_test               DEVICE AUDIO1
Device_CDC                      DEVICE CDC=1
Device_cdc_dual_ports           DEVICE CDC CDC
Device_cdc_msc                  DEVICE CDC=1 MSC
Device_cdc_msc_throughput       DEVICE CDC=1 MSC
Device_cdc_uac2                 DEVICE CDC=1 AUDIO1
Device_dfu_runtime              DEVICE DFU_RUNTIME
Device_dynamic_configuration    DEVICE CDC=1 MIDI MSC   # mode 1 switches to MSC
Device_HID                      DEVICE HID=1
Device_hid_boot_interface       DEVICE HID HID
Device_hid_composite            DEVICE HID HID
Device_hid_generic_inout        DEVICE HID=1
Device_hid_multiple_interface   DEVICE HID HID
Device_Keyboard                 DEVICE HID=1
Device_midi_test                DEVICE MIDI
Device_MIDI_Seq                 DEVICE MIDI
Device_MSC                      DEVICE MSC
Device_msc_dual_lun             DEVICE MSC
Device_NET                      DEVICE NCM
Device_usbtmc                   DEVICE USBTMC
Device_Vendor                   DEVICE VENDOR
Device_webusb_serial            DEVICE CDC=1 VENDOR
Host_audio_host                 HOST   AUDIO HUB
Host_bare_api                   HOST   HUB
Host_cdc_msc_hid                HOST   CDC CDC_FTDI CDC_CP210X CDC_PL2303 CDC_CH34X HID MSC HUB
Host_device_info                HOST   HUB
Host_HID_Boot                   HOST   HID HUB
Host_hid_controller             HOST   HID HUB
Host_MIDI                       HOST   MIDI HUB
Host_midi_rx                    HOST   MIDI HUB
Host_msc_file_explorer          HOST   MSC HUB
Host_Printer                    HOST   PRINTER HUB
'

# Per-class instance counts. The count is a COUNT, not a flag: the union needs
# 2 CDC and 2 HID device instances for the dual/composite examples, and 4 host
# CDC + 4 host HID slots for the bridge and DualSense cases.
count_for() {
  local role="$1" cls="$2"
  case "$role/$cls" in
    # DEVICE role
    DEVICE/CDC)     echo 2 ;;   # Device_cdc_dual_ports
    DEVICE/HID)     echo 2 ;;   # Device_hid_composite
    DEVICE/NCM)     echo 1 ;;
    # HOST role
    HOST/CDC)       echo 4 ;;   # hub + 3 bridges
    HOST/HID)       echo 4 ;;   # DualSense reports are large
    *)              echo 1 ;;
  esac
}

# Devices with a count > 1 need the matching _ENABLE_INT_EP only where the
# driver wants it (USBTMC). Everything else is a plain count.
# Classes that have no host-side driver at all. Upstream removed the vendor
# host driver entirely ("vendor: remove the obsolete host vendor driver"), and
# NCM/ECM-RNDIS/USBTMC/DFU-RT are device-only by specification. Emitting
# CFG_TUH_<X> for them would define a macro nothing reads and, worse, make
# tusb.h try to include a host header that does not exist.
host_capable() {
  case "$1" in
    AUDIO1|VENDOR|NCM|USBTMC|DFU_RUNTIME) return 1 ;;
    *) return 0 ;;
  esac
}

# The four UART-bridge drivers are host-side serial adapters
# (class/cdc/serial/*.h, reached through cdc_host.c). There is no device-side
# CFG_TUD_* for them, and CFG_TUH_HUB has no device counterpart either.
device_capable() {
  case "$1" in
    CDC_FTDI|CDC_CP210X|CDC_PL2303|CDC_CH34X|HUB) return 1 ;;
    *) return 0 ;;
  esac
}

emit_generated_section() {
  local role="$1"; shift
  local classes=("$@")

  local tuh=0
  if [[ "$role" == HOST || "$role" == BOTH ]]; then tuh=1; fi

  # Resolve "CLASS" / "CLASS=N" tokens into one entry per class, per side.
  # A repeated token ("HID HID") means "this example needs every instance";
  # "CLASS=N" pins the count for the examples that only need one. The device
  # and host counts are separate tables: a host CDC slot count says nothing
  # about the device one.
  local -A dtud=() dtuh=()
  local tok base val d
  for tok in "${classes[@]}"; do
    if [[ "$tok" == *=* ]]; then base="${tok%%=*}"; val="${tok##*=}"
    else base="$tok"; val=""; fi
    if device_capable "$base"; then
      d="$(count_for DEVICE "$base")"
      if [[ -n "$val" ]]; then d="$val"; fi
      dtud[$base]="$d"
    fi
    if host_capable "$base"; then
      d="$(count_for HOST "$base")"
      if [[ -n "$val" ]]; then d="$val"; fi
      dtuh[$base]="$d"
    fi
  done

  # $1 = table name, $2 = CFG prefix
  # NOTE: the declarations must be separate `local` statements — the combined
  # `local -n tbl=$1 prefix="$2" b macro` form mis-parses in bash 5.3 and leaves
  # b unbound, which trips `set -u` on the first `case "$b"`.
  emit_table() {
    local -n tbl=$1
    local prefix="$2" b macro
    for b in $(printf '%s\n' "${!tbl[@]}" | sort); do
      case "$b" in
        AUDIO1)
          if [[ "$prefix" == TUD ]]; then
            printf '#define %-36s %s  // mono IN only\n' CFG_TUD_AUDIO 1
          fi
          continue ;;
      esac
      macro="$(printf 'CFG_%s_%s' "$prefix" "$(printf '%s' "$b" | tr '[:lower:]' '[:upper:]')")"
      printf '#define %-36s %s\n' "$macro" "${tbl[$b]}"
      if [[ "$b" == USBTMC && "$prefix" == TUD ]]; then
        printf '#define %-36s %s\n' CFG_TUD_USBTMC_ENABLE_INT_EP 1
      fi
    done
    # The core always needs a device CDC instance; see the note at the caller.
    if [[ "$prefix" == TUD && -z "${tbl[CDC]:-}" ]]; then
      printf '#define %-36s %s  // floor: SerialUSB.cpp needs it\n' CFG_TUD_CDC 1
    fi
  }

  echo "// Board check. board_auto.h is NOT reachable from here: the core's TinyUSB"
  echo "// translation units get no -I for the library, so derive the board from the"
  echo "// core's own command-line macro, which is present in every TU. The library's"
  echo "// own sources get the same value from UnoR4TinyUSB/board_auto.h, and an"
  echo "// identical redefinition is benign."
  echo "#if !defined(ARDUINO_ARCH_RENESAS)"
  echo "  #error \"Unsupported board: this config is for the Arduino Renesas RA4M1 core (renesas_uno). No ARDUINO_ARCH_RENESAS on the command line.\""
  echo "#endif"
  echo "#ifndef UNOR4_TINYUSB_BOARD_R4"
  echo "  #define UNOR4_TINYUSB_BOARD_R4 1"
  echo "#endif"
  echo ""
  echo "// ---- Device stack ----"
  # CFG_TUD_ENABLED and CFG_TUD_CDC are NOT trimmed, not even for host-only
  # examples, because two core files require them unconditionally:
  #   cores/arduino/usb/USB.cpp       __SetupUSBDescriptor() builds a
  #                                   TUD_CDC_DESCRIPTOR outside any #if, and
  #                                   __USBStart() calls tud_rhport_init().
  #   cores/arduino/usb/SerialUSB.cpp  the _SerialUSB object calls
  #                                   tud_cdc_connected/peek/available/
  #                                   read_char/write/write_available/
  #                                   write_flush.
  # So CFG_TUD_CDC 0 fails to COMPILE THE CORE, not just the sketch. What a
  # per-example config really saves is the other device classes and the whole
  # host stack (~8 KB).
  printf '#define %-36s %s  // required by the core: USB.cpp / SerialUSB.cpp\n' CFG_TUD_ENABLED 1
  emit_table dtud TUD
  echo ""
  echo "// ---- Host stack ----"
  printf '#define %-36s %s\n' CFG_TUH_ENABLED "$tuh"
  if [[ $tuh -eq 1 ]]; then emit_table dtuh TUH; fi
}


# Split the active config into prologue / generated section / epilogue, rebuild
# with a new generated section, and write it to $1.
render_config() {
  local out="$1" role="$2"; shift 2
  # One pass: prologue = everything before BEGIN, epilogue = everything after
  # END. The generated section itself is dropped and regenerated below.
  #
  # The body is read from UNION_CFG, never from $ACTIVE. $ACTIVE is whichever
  # config was installed last, so reading it here made a union-body edit need
  # `union` before `gen`: running `gen` alone spliced the OLD body into all 48
  # example configs while verify compared them against the NEW union, i.e. 48
  # "hand-edited?" FAILs from a script that had just written them. The union is
  # the pristine source by definition, so take the body from it.
  awk -v b="$MARK_BEGIN" -v e="$MARK_END" \
      -v prologue="$out.pro" -v epilogue="$out.epi" '
    $0 == b { inb = 1; next }
    $0 == e { inb = 0; ine = 1; next }
    ine { print > epilogue; next }
    inb { next }
    { print > prologue }
  ' "$UNION_CFG"
  {
    cat "$out.pro"
    echo "$MARK_BEGIN"
    emit_generated_section "$role" "$@"
    echo "$MARK_END"
    cat "$out.epi"
  } > "$out"
  rm -f "$out.pro" "$out.epi"
}

lookup() {
  local want="$1" line role classes
  while read -r name role classes; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    if [[ "$name" == "$want" ]]; then
      # shellcheck disable=SC2086
      printf '%s\n' "$role"
      # shellcheck disable=SC2086
      printf '%s\n' $classes
      return 0
    fi
  done <<< "$SIGNATURES"
  return 1
}

# ---------------------------------------------------------------------------
# verify_configs — every class API an example CALLS must be enabled by that
# example's config.
#
# Weak callback DEFINITIONS (identifiers ending in _cb) are excluded: the
# library supplies weak defaults, so defining one compiles whether or not the
# class is enabled, and requiring the class for them would force every sketch
# to link classes it never uses.
#
# This exists because the class-selection table and the sketches are edited by
# hand and separately. Two real gaps were caught this way in 0.3.0:
# r4_HubLSDebug called tuh_hid_mounted() with CFG_TUH_HID absent, and
# Device_dynamic_configuration implemented the MSC callbacks with CFG_TUD_MSC absent
# (both compiled against the union and failed against their own config).
# ---------------------------------------------------------------------------
CLASS_MAP="
tud_cdc:CFG_TUD_CDC
tud_hid:CFG_TUD_HID
tud_msc:CFG_TUD_MSC
tud_midi:CFG_TUD_MIDI
tud_audio:CFG_TUD_AUDIO
tud_vendor:CFG_TUD_VENDOR
tud_usbtmc:CFG_TUD_USBTMC
tud_network:CFG_TUD_NCM
tud_dfu:CFG_TUD_DFU_RUNTIME
tuh_cdc:CFG_TUH_CDC
tuh_hid:CFG_TUH_HID
tuh_msc:CFG_TUH_MSC
tuh_midi:CFG_TUH_MIDI
tuh_audio:CFG_TUH_AUDIO
tuh_printer:CFG_TUH_PRINTER
tuh_hub:CFG_TUH_HUB
"

# Every config must share a byte-identical body outside the generated section.
# `gen` now takes that body from the union, so a FAIL means either somebody hand
# edited an example config (which happened once already) or the examples were
# never regenerated after a union edit -- run ./scripts/config.sh gen.
check_shared_body() {
  local d name problems=0
  mkdir -p "$OUTDIR_NOTE"
  # Remove ONLY the between-markers section (inclusive) and keep the rest. The
  # shared config is split AROUND the markers -- header before BEGIN, MCU /
  # buffer / queue settings after END -- so dropping "everything from BEGIN on"
  # would compare only the header and miss any edit to the tail.
  strip_generated() {
    awk -v b="$MARK_BEGIN" -v e="$MARK_END" '
      index($0, b) == 1 { inb = 1; next }
      index($0, e) == 1 { inb = 0; next }
      !inb { print }
    ' "$1"
  }
  strip_generated "$UNION_CFG" > "$OUTDIR_NOTE/union.body"
  for d in "$EXAMPLES_DIR"/*/; do
    name="$(basename "$d")"
    [[ -f "$d/tusb_config.h" ]] || continue
    strip_generated "$d/tusb_config.h" > "$OUTDIR_NOTE/x.body"
    if ! cmp -s "$OUTDIR_NOTE/union.body" "$OUTDIR_NOTE/x.body"; then
      echo "[config] FAIL $name: shared body differs from the union (hand-edited?)" >&2
      problems=$((problems + 1))
    fi
  done
  rm -rf "$OUTDIR_NOTE"
  return $problems
}

verify_configs() {
  local problems=0 checked=0
  local pair prefix macro name src cfg val id
  for d in "$EXAMPLES_DIR"/*/; do
    name="$(basename "$d")"
    cfg="$d/tusb_config.h"
    if [[ ! -f "$cfg" ]]; then
      echo "[config] FAIL $name: no tusb_config.h" >&2
      problems=$((problems + 1))
      continue
    fi
    for pair in $CLASS_MAP; do
      prefix="${pair%%:*}"
      macro="${pair##*:}"
      val="$(sed -n "s/^[[:space:]]*#define[[:space:]]\+$macro[[:space:]]\+\([0-9]\+\).*/\1/p" "$cfg" | head -1)"
      for src in "$d"*.ino "$d"*.cpp; do
        [[ -f "$src" ]] || continue
        # Identifiers the sketch actually CALLS, minus weak-callback definitions.
        while read -r id; do
          [[ -n "$id" ]] || continue
          case "$id" in *_cb) continue ;; esac
          checked=$((checked + 1))
          if [[ -z "$val" ]]; then
            echo "[config] FAIL $name/$(basename "$src"): calls $id() but $macro is not defined" >&2
            problems=$((problems + 1))
          elif [[ "$val" -eq 0 ]]; then
            echo "[config] FAIL $name/$(basename "$src"): calls $id() but $macro == 0" >&2
            problems=$((problems + 1))
          fi
        done < <(grep -ohE "\b${prefix}_[a-z0-9_]+[[:space:]]*\(" "$src" 2>/dev/null \
                 | sed 's/[[:space:]]*($//' | sort -u)
      done
    done
  done
  check_shared_body || problems=$((problems + 1))
  echo "[config] verify: $checked class-API call sites checked"
  return $problems
}

install_config() {
  local src="$1"
  local data_dir core_dir
  data_dir="$(arduino-cli config get directories.data 2>/dev/null || true)"
  [[ -n "$data_dir" ]] || data_dir="$HOME/.arduino15"
  local root="$data_dir/packages/arduino/hardware/renesas_uno"
  [[ -d "$root" ]] || { echo "[config] ERROR: no renesas_uno core at $root" >&2; return 1; }
  local versions=("$root"/*)
  core_dir="${versions[-1]}"
  local ok=0
  for v in MINIMA NANOR4; do
    # The variant config is only safe to overwrite if this library put it
    # there: the marker is inside the generated section of the config we
    # install, and never appears in a stock core file.
    if [[ -f "$core_dir/variants/$v/tusb_config.h" ]] && \
       grep -qF 'scripts/config.sh' "$core_dir/variants/$v/tusb_config.h"; then
      cp -f "$src" "$core_dir/variants/$v/tusb_config.h"
      echo "[config] installed -> variants/$v/tusb_config.h"
      ok=1
    else
      echo "[config] WARNING: variants/$v/tusb_config.h was not installed by this" >&2
      echo "[config]          library; refusing to overwrite it. Run" >&2
      echo "[config]          ./scripts/patch_renesas_core.sh once, then retry." >&2
    fi
  done
  return $((1 - ok))
}

cmd="${1:-union}"
case "$cmd" in
union)
  # Restore the pristine union, THEN install it. Copying $ACTIVE onto itself
  # would be a no-op whenever `set` (or a build script) has left an example's
  # config there.
  [[ -f "$UNION_CFG" ]] || { echo "[config] ERROR: $UNION_CFG not found" >&2; exit 1; }
  cp -f "$UNION_CFG" "$ACTIVE"
  echo "[config] active config <- scripts/tusb_config.union.h"
  install_config "$ACTIVE"
  ;;

set)
  ex="${2:-}"
  [[ -n "$ex" ]] || { echo "usage: config.sh set <Example>" >&2; exit 2; }
  src_cfg="$EXAMPLES_DIR/$ex/tusb_config.h"
  [[ -f "$src_cfg" ]] || { echo "[config] ERROR: $src_cfg not found" >&2; exit 1; }
  cp -f "$src_cfg" "$ACTIVE"
  echo "[config] active config <- examples/$ex/tusb_config.h"
  install_config "$ACTIVE"
  ;;

gen)
  while read -r name role classes; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    out="$EXAMPLES_DIR/$name/tusb_config.h"
    [[ -d "$EXAMPLES_DIR/$name" ]] || { echo "[config] skip $name (no example dir)" >&2; continue; }
    # shellcheck disable=SC2086
    render_config "$out" "$role" $classes
    echo "[config] generated examples/$name/tusb_config.h"
  done <<< "$SIGNATURES"
  verify_configs
  ;;

verify)
  verify_configs
  ;;

install)
  install_config "$ACTIVE"
  ;;

list)
  printf '%-24s %-7s %s\n' EXAMPLE ROLE "CLASSES"
  while read -r name role classes; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    printf '%-24s %-7s %s\n' "$name" "$role" "$classes"
  done <<< "$SIGNATURES"
  ;;

*)
  sed -n '2,30p' "$0"
  exit 2
  ;;
esac