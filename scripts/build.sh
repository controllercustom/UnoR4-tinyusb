#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# build.sh — compile UnoR4-tinyusb examples using arduino-cli.
#
# Each example is built against ITS OWN generated config (examples/<Name>/
# tusb_config.h), installed into the core's variant dirs first.
# That is the workflow the per-example configs exist for, and it is required:
# a few sketches (Device_MSC, Device_dfu_runtime) do not fit in 32 KB against the
# full union, and Device_NET / Host_audio_host / Host_Printer need classes the union
# deliberately leaves off.
#
# Every run starts by building scripts/vanilla/Blink -- a sketch that includes
# nothing. It is the only check that installing this board definition still
# leaves an ordinary sketch compiling; see the comment at its call site for the
# failure that got through 96/96.
#
# Usage:
#   ./scripts/build.sh                    # all examples, minima + nanor4, own configs
#   ./scripts/build.sh <example-dir>      # one example, both boards
#   ./scripts/build.sh <example-dir> <fqbn>   # one example, one board
#   ./scripts/build.sh --union            # all examples against the full union
#   ./scripts/build.sh --union <example>  # one example against the full union
#   ./scripts/build.sh --fork             # all examples against the fork package
#   ./scripts/build.sh --fork -j 4        # same, 4 compiles at a time
#
# --fork is a different thing, not just a different FQBN: with the unor4tinyusb
# package every example already carries its own tusb_config.h, so nothing is
# installed into a variant and no build shares state with another. That is why
# -j is only allowed there. The stock path swaps one variant config between
# builds, so it must stay serial -- see AGENTS.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

R4_FQBNS=("arduino:renesas_uno:minima" "arduino:renesas_uno:nanor4")
USE_UNION=0
FORK=0
JOBS=1

# Print the header comment (line 3 up to the first non-comment line) rather than
# a hardcoded range, so editing this file's header cannot silently truncate help.
usage() { awk 'NR >= 3 && /^#/ { sub(/^# ?/, ""); print; next } NR >= 3 { exit }' "$0"; exit 2; }

POS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --union) USE_UNION=1 ;;
    --fork)  FORK=1 ;;
    -j|--jobs) shift; JOBS="${1:?usage: -j needs a count}" ;;
    -h|--help) usage ;;
    *) POS+=("$1") ;;
  esac
  shift
done

if [[ "$FORK" -eq 1 ]]; then
  if [[ "$USE_UNION" -eq 1 ]]; then
    echo "[build] --fork and --union are mutually exclusive: the fork ships the" >&2
    echo "[build] union in its variants, and a sketch-local config overrides it." >&2
    exit 2
  fi
  R4_FQBNS=("unor4tinyusb:renesas_uno:minima" "unor4tinyusb:renesas_uno:nanor4")
fi

ensure_core() {
  echo "[build] checking renesas_uno core..."
  arduino-cli core list 2>&1 | grep -q "renesas_uno" || arduino-cli core install arduino:renesas_uno
}

ensure_project_lib_linked() {
  local libdir="$HOME/Arduino/libraries"
  mkdir -p "$libdir"
  local link="$libdir/UnoR4-tinyusb"
  if [[ ! -e "$link" ]]; then
    ln -s "$PROJ_ROOT" "$link"
    echo "[build] linked $link -> $PROJ_ROOT"
  fi
}

# Discover all example sketches
EXAMPLES=()
for d in "$PROJ_ROOT"/examples/*/; do
  [[ -d "$d" ]] || continue
  sketch=$(basename "$d")
  if ls "$d"/*.ino &>/dev/null; then
    EXAMPLES+=("examples/$sketch")
  fi
done

if [[ ${#POS[@]} -ge 1 ]]; then
  EXAMPLES=("${POS[0]}")
fi

if [[ ${#POS[@]} -ge 2 ]]; then
  R4_FQBNS=("${POS[1]}")
fi

if [[ "$FORK" -eq 0 && "$JOBS" -gt 1 ]]; then
  echo "[build] -j is only safe with --fork: the stock path installs one variant" >&2
  echo "[build] config between builds, so parallel builds would race on it." >&2
  exit 2
fi

ensure_fork_package() {
  if ! arduino-cli core list 2>&1 | grep -q "unor4tinyusb"; then
    echo "[build] the unor4tinyusb package is not installed; building and installing it"
    "$SCRIPT_DIR/build_fork_package.sh" --install
  fi
}

ensure_core
ensure_project_lib_linked
if [[ "$FORK" -eq 1 ]]; then ensure_fork_package; fi

rc=0

# A board definition must not change what an ordinary sketch does. This builds
# the IDE's Blink -- which includes nothing, so it links against the core and
# the shipped variant config alone -- for every FQBN of the route being built,
# before anything else runs.
#
# The example matrix structurally cannot catch this class of failure: every
# example includes UnoR4TinyUSB.h, so it always links the weak callbacks in
# bsp_common.cpp. A default config that enables a class whose application
# callbacks TinyUSB does NOT provide a default for therefore passes 96/96 and
# still breaks the user's Blink. That is what shipped: CFG_TUD_USBTMC on made
# plain Blink fail with 17 undefined tud_usbtmc_* references under BOTH routes,
# because the RA core links core.a with -Wl,--whole-archive and so drags
# usbtmc_device.c.o into every image. Audio and Vendor are enabled in the union
# and do not break Blink, because audio_device.c / vendor_device.c carry
# TU_ATTR_WEAK defaults. See AGENTS.md.
#
# It must run against the config we SHIP, hence the union install for the stock
# path (the fork has the union baked into its variants).
VANILLA="$SCRIPT_DIR/vanilla/Blink"
check_vanilla() {
  local fqbn failed=0
  for fqbn in "${R4_FQBNS[@]}"; do
    echo "==================== vanilla Blink [${fqbn}] ===================="
    if ! arduino-cli compile --fqbn "$fqbn" \
         --build-path "$PROJ_ROOT/build/vanilla/$(basename "$fqbn")" "$VANILLA"; then
      echo "[build] FAILED: plain Blink does not link for $fqbn" >&2
      echo "[build] The shipped default config must not enable a class whose" >&2
      echo "[build] application callbacks TinyUSB has no default for (USBTMC)." >&2
      failed=1
    fi
  done
  return $failed
}

if [[ "$FORK" -eq 0 ]]; then "$SCRIPT_DIR/config.sh" union >/dev/null; fi
check_vanilla || rc=1

activate_config() {
  local ex
  ex="$(basename "$1")"
  if [[ "$FORK" -eq 1 ]]; then
    # Nothing to install: the sketch folder is on the include path and carries
    # its own tusb_config.h -- which is also what makes concurrent builds safe.
    return 0
  fi
  if [[ "$USE_UNION" -eq 1 ]]; then
    "$SCRIPT_DIR/config.sh" union >/dev/null
    return
  fi
  local cfg="$PROJ_ROOT/examples/$ex/tusb_config.h"
  if [[ ! -f "$cfg" ]]; then
    echo "[build] ERROR: $cfg missing. Run ./scripts/config.sh gen" >&2
    return 1
  fi
  cp -f "$cfg" "$PROJ_ROOT/src/tusb_config_unor4tinyusb.h"
  "$SCRIPT_DIR/config.sh" install >/dev/null
}

build_one() {
  local fqbn="$1" ex="$2" bdir
  bdir="$PROJ_ROOT/build/$(basename "$fqbn")/$(basename "$ex")"
  activate_config "$ex" || return 1
  rm -rf "$bdir"
  arduino-cli compile --fqbn "$fqbn" --build-path "$bdir" "$PROJ_ROOT/$ex"
}

if [[ "$JOBS" -gt 1 ]]; then
  # Parallel mode is only reachable with --fork, where activate_config is a
  # no-op and no two builds touch the same file.
  LOGDIR="$PROJ_ROOT/build/logs"
  rm -rf "$LOGDIR"
  mkdir -p "$LOGDIR"
  pids=()
  for fqbn in "${R4_FQBNS[@]}"; do
  for ex in "${EXAMPLES[@]}"; do
    tag="$(basename "$fqbn")-$(basename "$ex")"
    # errexit-safe AND stdout-safe: the rc write must not swallow the build's
    # stdout (fi > rc applies the redirect to the whole if-comPOUND, including
    # the condition's stdout -- that turned rc into "build log + 0" and made
    # every passing build look failed). Capture into a var (|| context defeats
    # errexit), redirect only the echo.
    { r4tusb_rc=0; build_one "$fqbn" "$ex" || r4tusb_rc=$?; echo "$r4tusb_rc" > "$LOGDIR/$tag.rc"; } > "$LOGDIR/$tag.log" 2>&1 &
    pids+=("$!")
    if [[ ${#pids[@]} -ge "$JOBS" ]]; then
      wait "${pids[0]}" || true
      pids=("${pids[@]:1}")
    fi
  done
  done
  for pid in "${pids[@]}"; do wait "$pid" || true; done
  total=0
  bad=0
  shopt -s nullglob
  rcfiles=("$LOGDIR"/*.rc)
  shopt -u nullglob
  # A missing rc file IS a failure (job died before recording); without the
  # count guard an empty set reads as "0/1" or under-reports real failures.
  want=$(( ${#EXAMPLES[@]} * ${#R4_FQBNS[@]} ))
  if [[ ${#rcfiles[@]} -ne "$want" ]]; then
    echo "[build] FAILED: expected $want rc files, got ${#rcfiles[@]}" >&2
    rc=1
  fi
  for f in "${rcfiles[@]}"; do
    total=$((total + 1))
    if [[ "$(cat "$f")" != "0" ]]; then
      bad=$((bad + 1))
      echo "[build] FAILED: $(basename "${f%.rc}")  log: $f" >&2
    fi
  done
  echo "[build] $((total - bad))/$total builds succeeded (jobs=$JOBS)"
  if [[ "$bad" -gt 0 ]]; then rc=1; fi
else
for fqbn in "${R4_FQBNS[@]}"; do
for ex in "${EXAMPLES[@]}"; do
  echo "==================== building $ex [$fqbn] ===================="
  if ! build_one "$fqbn" "$ex"; then
    echo "[build] FAILED: $ex [$fqbn]" >&2
    rc=1
  fi
done
done
fi

# Leave the shipped default (the union) installed rather than whatever the last
# example happened to need. Only the stock path installs anything at all.
if [[ "$FORK" -eq 0 ]]; then "$SCRIPT_DIR/config.sh" union >/dev/null; fi

exit $rc