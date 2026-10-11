#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# build_fork_package.sh — assemble the UnoR4 TinyUSB CORE FORK and its package
# index, so IDE users install a board package instead of patching the stock
# core.
#
# WHY A FORK
#   The stock install path copies files into the user's installed core
#   (~/.arduino15/packages/arduino/hardware/renesas_uno/<ver>). That is
#   reversible but hostile: it mutates a package the IDE can update out from
#   under you, it needs bash/PowerShell, and the per-example TinyUSB configs
#   cannot be selected from the IDE at all. A fork publishes the same content
#   as a normal board package: nothing on the user's machine is modified, and
#   `arduino:renesas_uno` projects keep working side by side (separate
#   packages/<name>/ directory).
#
# WHAT MAKES A FORK WORTH HAVING (measured, not assumed)
#   platform.txt is ours in a fork, so we can set build.extra_flags. The stock
#   core leaves it empty and sets it nowhere else (checked: boards.txt has no
#   extra_flags at all). With
#
#     build.extra_flags=-iquote{build.source.path}
#
#   a `tusb_config.h` sitting in the SKETCH folder wins over the variant copy
#   for the core's own TinyUSB translation units. Verified two ways: the flag
#   appears in USB.cpp's and usbh.c's compile lines, and a probe tusb_config.h
#   containing #error fired from inside tusb_option.h. That is what removes the
#   last file-copy step: each example ships its own config as a sketch tab, so
#   per-example class selection works in the IDE with one config per sketch
#   instead of one global union. (The union cannot serve all 50 examples: with
#   everything enabled it passes 92/100 builds at most.)
#
# SCOPE
#   Minima + Nano R4 only. The UNO R4 WiFi variant is dropped from boards.txt
#   and variants/: it is untested here, and its USB-C port goes through a
#   hardware multiplexer, so its TinyUSB path is not comparable.
#
# The overlay itself is NOT reimplemented here. scripts/patch_renesas_core.sh
# already knows how to replace the TinyUSB tree from the vendored snapshot,
# install the active config into both variants, and weaken the USB.cpp
# callbacks; its R4TUSB_CORE_DIR hook points it at a staged copy instead of the
# installed core. Using it keeps the fork and the patched-stock path identical
# by construction -- if they ever drift, the fork silently fixes bugs that
# stock-core users still have.
#
# Usage:
#   ./scripts/build_fork_package.sh                # build + index into dist/
#   ./scripts/build_fork_package.sh --install      # also install for local test
#   FORK_VERSION=1.0.0 BASE_URL=https://host/x ./scripts/build_fork_package.sh
#
# Requires only stock tools: curl, tar, sha256sum, patch, sed, find.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---- Identity -----------------------------------------------------------------
PKG_NAME="unor4tinyusb"
PLATFORM_NAME="UnoR4 TinyUSB Boards"
ARCH="renesas_uno"           # must stay renesas_uno: library.properties says
                              # architectures=renesas_uno, and a mismatch means
                              # the IDE refuses to load the library at all.
MAINTAINER="controllercustom"
WEBSITE="https://github.com/controllercustom/UnoR4-tinyusb"
HELP_URL="https://github.com/controllercustom/UnoR4-tinyusb/issues"

FORK_VERSION="${FORK_VERSION:-0.5.0}"

# ---- Pinned upstream ---------------------------------------------------------
# The fork is upstream + overlay, never a hand-maintained copy: rebasing onto a
# new core release means updating these three values and re-running.
UPSTREAM_VERSION="1.6.0"
UPSTREAM_URL="http://downloads.arduino.cc/cores/staging/ArduinoCore-renesas_uno-1.6.0.tar.bz2"
UPSTREAM_SHA256="71aa5d81f9455dbc2e8977b594963494e41466a8ed439a841b9d029b4aa31c8b"

# Tools are NOT redistributed: they are declared with packager=arduino, so the
# IDE fetches them from Arduino and they are shared with the stock core.
TOOL_DEPS=(
  "arm-none-eabi-gcc|7-2017q4"
  "bossac|1.9.1-arduino5"
  "dfu-util|0.11.0-arduino5"
  "openocd|0.11.0-arduino2"
)

# Where the archive will live once published. The index is small and lives in
# this repo; the archive is a release asset.
BASE_URL="${BASE_URL:-https://github.com/controllercustom/UnoR4-tinyusb/releases/download/v${FORK_VERSION}}"

DROP_VARIANTS=("UNOWIFIR4")
REMOVED_IDS=()

# Fixed timestamp for the reproducible archive: every entry gets this mtime, so
# the same core content always yields the same bytes. Bump it with FORK_VERSION,
# or set SOURCE_DATE_EPOCH for an official build.
PACKAGE_DATE="${PACKAGE_DATE:-2026-10-10T00:00:00Z}"
KEEP_SVD="${KEEP_SVD:-1}"      # svd/ is 9.6 MB but the debug config needs it

INSTALL=0
for arg in "$@"; do
  case "$arg" in
    --install) INSTALL=1 ;;
    --help|-h) sed -n '1,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "[fork] unknown argument: $arg" >&2; exit 1 ;;
  esac
done

STAGE="$PROJ_ROOT/build/fork"
CACHE="$PROJ_ROOT/build/cache"
DIST="$PROJ_ROOT/dist"
UPSTREAM_TARBALL="$CACHE/$(basename "$UPSTREAM_URL")"
ARCHIVE_NAME="unor4tinyusb-${ARCH}-${FORK_VERSION}.tar.gz"
TOPDIR="unor4tinyusb-${ARCH}-${FORK_VERSION}"

log() { echo "[fork] $*"; }
die() { echo "[fork] ERROR: $*" >&2; exit 1; }

# ---- 1. Upstream archive, verified -------------------------------------------
mkdir -p "$CACHE" "$DIST"
if [[ ! -f "$UPSTREAM_TARBALL" ]]; then
  log "downloading upstream core ${UPSTREAM_VERSION}..."
  curl -fsSL -o "$UPSTREAM_TARBALL" "$UPSTREAM_URL" ||
    die "could not fetch $UPSTREAM_URL"
fi
echo "$UPSTREAM_SHA256  $UPSTREAM_TARBALL" | sha256sum -c - ||
  die "upstream archive checksum mismatch (has the pinned URL been superseded?)"

# ---- 2. Stage -----------------------------------------------------------------
log "staging $UPSTREAM_URL"
rm -rf "$STAGE"
mkdir -p "$STAGE"
tar -xjf "$UPSTREAM_TARBALL" -C "$STAGE"

# The upstream tarball may be flat or wrapped in one directory; normalise.
mapfile -t top < <(find "$STAGE" -mindepth 1 -maxdepth 1)
if [[ -f "$STAGE/boards.txt" || -f "$STAGE/platform.txt" ]]; then
  CORE="$STAGE/core"
  mkdir -p "$CORE"
  for entry in "$STAGE"/*; do
    [[ "$entry" == "$CORE" ]] && continue
    mv "$entry" "$CORE/"
  done
elif [[ ${#top[@]} -eq 1 && -d "${top[0]}" ]]; then
  CORE="${top[0]}"
else
  die "cannot find the core root in the extracted archive"
fi
[[ -f "$CORE/platform.txt" && -f "$CORE/boards.txt" ]] ||
  die "staged core is missing platform.txt/boards.txt: $CORE"

# patch_renesas_core.sh derives the core version from the directory name, and
# this tarball unpacks to "ArduinoCore-renesas" with no version in it.
if [[ "$(basename "$CORE")" != "$UPSTREAM_VERSION" ]]; then
  mv "$CORE" "$STAGE/$UPSTREAM_VERSION"
  CORE="$STAGE/$UPSTREAM_VERSION"
fi

# ---- 3. Overlay, via the very script stock-core users run ---------------------
log "applying overlay (vendored TinyUSB + USB.cpp weaks + variant config)"
R4TUSB_CORE_DIR="$CORE" "$SCRIPT_DIR/patch_renesas_core.sh" ||
  die "overlay failed on the staged core"

# Backups are for the reversible stock-core path; they must not ship. They also
# cost 2.8 MB and would confuse anyone reading the package.
rm -rf "$CORE/.r4tinyusb"
find "$CORE" -name '*.r4tinyusb_orig' -delete
log "stripped backup tree and *.r4tinyusb_orig sidecars"

# ---- 4. Fork-only core edits --------------------------------------------------
log "editing platform.txt (name, version, sketch-dir include path)"
sed -i \
  -e "s|^name=.*|name=${PLATFORM_NAME}|" \
  -e "s|^version=.*|version=${FORK_VERSION}|" \
  -e "s|^build.extra_flags=.*|build.extra_flags=-iquote{build.source.path}|" \
  "$CORE/platform.txt"
grep -q "^build.extra_flags=-iquote" "$CORE/platform.txt" ||
  die "platform.txt has no build.extra_flags line to override"

# Drop unsupported boards entirely: a board in the menu that nobody has tested
# is worse than an absent one.
for variant in "${DROP_VARIANTS[@]}"; do
  # boards.txt carries <id>.build.variant=<VARIANT>; find the id by it rather
  # than hard-coding, so a rename upstream fails the build instead of quietly
  # leaving an unsupported board in the menu.
  board_id="$(grep -E "^[a-zA-Z0-9_]+\.build\.variant=${variant}\$" "$CORE/boards.txt" | head -1 | cut -d. -f1)"
  [[ -n "$board_id" ]] || die "no board id in boards.txt declares variant $variant (renamed upstream?)"
  log "removing board '$board_id' and variant $variant"
  sed -i "/^${board_id}\.[^=]*=/d" "$CORE/boards.txt"
  rm -rf "$CORE/variants/$variant"
  REMOVED_IDS+=("$board_id")
done
for id in "${REMOVED_IDS[@]}"; do
  grep -q "^${id}\." "$CORE/boards.txt" && die "board '$id' still present in boards.txt"
done

# Display names. The Tools -> Board menu comes from boards.txt, NOT from the
# index's boards[] array, so without this the menu shows two indistinguishable
# "Arduino Uno R4 Minima" entries once both packages are installed. The FQBN ids
# and build.board macros stay untouched -- only .name changes.
BOARD_NAMES=("minima|UNO R4 Minima (TinyUSB)" "nanor4|Nano R4 (TinyUSB)")
for pair in "${BOARD_NAMES[@]}"; do
  id="${pair%%|*}"; disp="${pair#*|}"
  grep -q "^${id}\.name=" "$CORE/boards.txt" ||
    die "board id '$id' has no .name line (renamed upstream?)"
  sed -i "s|^${id}\.name=.*|${id}.name=${disp}|" "$CORE/boards.txt"
done

if [[ "$KEEP_SVD" != "1" ]]; then
  log "dropping svd/ ($(du -sh "$CORE/svd" 2>/dev/null | cut -f1))"
  rm -rf "$CORE/svd"
fi

# ---- 5. Pack -----------------------------------------------------------------
# Reproducible: the same core content must always produce the same bytes.
# Otherwise tar stamps every entry with its real mtime, the overlay copies
# (vendored tree, patch output, sed edits) all carry the build clock, every
# rebuild yields a new archive, and the committed index stops describing it --
# which also makes "is this the published artifact?" unanswerable without
# re-hashing. The knobs:
#   --sort=name            deterministic entry order
#   --mtime=@N --utc       one fixed timestamp for every entry
#   --owner/--group 0      no build-machine usernames or uids in the archive
#   gzip -n                no filename or timestamp in the gzip header
#   -9                     fixed level (gzip's default could vary by version)
# The mtime spelling is not cosmetic: --mtime="UTC @$EPOCH" is NOT a valid date
# string, tar silently substitutes INT64_MIN, and every entry lands at
# -9223372036854775808 while the log below prints the real PACKAGE_DATE. That is
# what this script shipped as until it was caught -- still byte-reproducible
# (which is why the reproducibility check passed), but a user who runs
# `tar -tzvf` on the release artifact gets an "implausibly old time stamp"
# warning for all ~1130 entries. "@N" is the epoch form; --utc keeps rendering
# deterministic. Verified here: "UTC @1791590400" -> INT64_MIN,
# "@1791590400" --utc -> 2026-10-10T00:00:00Z.
# GNU tar only: bsdtar (macOS) has neither --sort nor --mtime=@N, and failing
# quietly into a non-reproducible archive is worse than refusing.
tar --help 2>/dev/null | grep -q -- '--sort' ||
  die "reproducible packing needs GNU tar -- $(tar --version 2>&1 | head -1)"
EPOCH="${SOURCE_DATE_EPOCH:-$(date -u -d "$PACKAGE_DATE" +%s)}"
# GNU date for the label, with a fallback that is merely ugly: bash's
# printf '%(...)T' renders in LOCAL time (this box is UTC-10, so it printed a
# "Z"-suffixed timestamp that was 10 hours off), and its %!flag form is not
# parsed -- both were tried.
MTIME_LABEL="$(TZ=UTC date -u -d "@$EPOCH" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null ||
  echo "epoch $EPOCH")"
log "packing $ARCHIVE_NAME (reproducible; every entry mtime $MTIME_LABEL)"
STAMP="$STAGE/$TOPDIR"
mkdir -p "$STAMP"
for entry in "$CORE"/*; do
  mv "$entry" "$STAMP/"
done
tar --sort=name --mtime="@$EPOCH" --utc --owner=0 --group=0 --numeric-owner \
    -cf - -C "$STAGE" "$TOPDIR" | gzip -n -9 > "$DIST/$ARCHIVE_NAME"
rm -rf "$STAMP"

# The failure mode this guards is a mis-parsed --mtime: tar substitutes
# INT64_MIN and still writes a byte-reproducible archive, so the reproducibility
# property alone cannot detect it. Look for the underflow sentinel in the listing
# instead -- it does not depend on which date we intended.
if tar -tzvf "$DIST/$ARCHIVE_NAME" 2>/dev/null | grep -q -- '-922337203685477580'; then
  die "archive entries have underflowed mtimes -- the --mtime argument was not parsed"
fi

ARCHIVE_SHA="$(sha256sum "$DIST/$ARCHIVE_NAME" | cut -d' ' -f1)"
ARCHIVE_SIZE="$(stat -c%s "$DIST/$ARCHIVE_NAME" 2>/dev/null || stat -f%z "$DIST/$ARCHIVE_NAME")"
log "archive: $DIST/$ARCHIVE_NAME ($ARCHIVE_SIZE bytes)"
log "sha256 : $ARCHIVE_SHA"

# ---- 6. Package index --------------------------------------------------------
# The index is small text and lives in the repo, so the Boards Manager URL is a
# stable raw.githubusercontent link and the JSON diff is reviewable. The archive
# never goes in git -- it is a release asset.
INDEX="$PROJ_ROOT/package_${PKG_NAME}_index.json"
# Join with a real comma. (The first version of this joined with a bare newline
# and produced invalid JSON that no shell check noticed.)
tools_json=""
for dep in "${TOOL_DEPS[@]}"; do
  tname="${dep%%|*}"; tver="${dep##*|}"
  [[ -n "$tools_json" ]] && tools_json+=","$'\n'
  tools_json+="            { \"packager\": \"arduino\", \"name\": \"${tname}\", \"version\": \"${tver}\" }"
done

# The index's boards[] names are generated from the same table as boards.txt,
# so the two cannot disagree about what a board is called.
boards_json=""
for pair in "${BOARD_NAMES[@]}"; do
  disp="${pair#*|}"
  [[ -n "$boards_json" ]] && boards_json+=","$'\n'
  boards_json+="            { \"name\": \"${disp}\" }"
done

cat > "$INDEX" <<JSON
{
  "packages": [
    {
      "name": "${PKG_NAME}",
      "maintainer": "${MAINTAINER}",
      "website": "${WEBSITE}",
      "email": "${MAINTAINER}@users.noreply.github.com",
      "help": { "online": "${HELP_URL}" },
      "platforms": [
        {
          "name": "${PLATFORM_NAME}",
          "architecture": "${ARCH}",
          "version": "${FORK_VERSION}",
          "category": "contributed",
          "url": "${BASE_URL}/${ARCHIVE_NAME}",
          "archiveFileName": "${ARCHIVE_NAME}",
          "checksum": "SHA-256:${ARCHIVE_SHA}",
          "size": "${ARCHIVE_SIZE}",
          "help": { "online": "${HELP_URL}" },
          "boards": [
${boards_json}
          ],
          "toolsDependencies": [
${tools_json}
          ]
        }
      ],
      "tools": []
    }
  ]
}
JSON

log "index: $INDEX"

# A stale index sitting in the build-output dir is a live footgun: it looks like
# a publishable artifact, and its checksum points at an archive whose bytes no
# longer match. The committed file at $PROJ_ROOT is the only index there is.
# (This is not hypothetical -- an earlier revision of this script wrote the index
# here, and the old copy survived the move, gitignored and therefore invisible.)
if [[ "$INDEX" != "$DIST/package_${PKG_NAME}_index.json" &&
      -f "$DIST/package_${PKG_NAME}_index.json" ]]; then
  log "removing stale index from $DIST/ (canonical copy: $INDEX)"
  rm -f "$DIST/package_${PKG_NAME}_index.json"
fi

# Validate before it can be published: a malformed index fails every install
# with a message that never says which field was wrong. python3/jq are not
# needed to build the package, only to check this.
if command -v python3 >/dev/null 2>&1; then
  python3 -m json.tool "$INDEX" >/dev/null 2>&1 && log "index validated (json.tool)" ||
    die "generated index is NOT valid JSON"
elif command -v jq >/dev/null 2>&1; then
  jq . "$INDEX" >/dev/null 2>&1 && log "index validated (jq)" ||
    die "generated index is NOT valid JSON"
else
  log "WARNING: neither python3 nor jq present -- index NOT validated"
fi
log
log "Users add this URL in Preferences -> Additional boards manager URLs:"
log "  <raw URL of $(basename "$INDEX") in this repo>"
log "then install '$PLATFORM_NAME' in Boards Manager, and pick"
log "  '$PKG_NAME:${ARCH}:minima' or '$PKG_NAME:${ARCH}:nanor4'."
log "Publish: upload $DIST/$ARCHIVE_NAME as a release asset for v${FORK_VERSION},"
log "and commit the index so its URL points at that release."

# ---- 7. Optional local install, for testing the fork end to end --------------
if [[ "$INSTALL" == "1" ]]; then
  # arduino-cli's ACTUAL data dir, which is NOT necessarily ~/.arduino15: on a
  # box with directories.data overridden (measured: /mnt/projects/arduino15),
  # an install under $HOME lands where the very next compile cannot see it --
  # "Platform 'unor4tinyusb:renesas_uno' not found" right after "[fork]
  # installed." Same resolution as patch_renesas_core.sh's find_core_dir.
  DATA_DIR="$(arduino-cli config get directories.data 2>/dev/null || true)"
  [[ -n "$DATA_DIR" ]] || DATA_DIR="${HOME}/.arduino15"
  DEST="${DATA_DIR}/packages/${PKG_NAME}/hardware/${ARCH}/${FORK_VERSION}"
  log "installing to $DEST"
  rm -rf "$DEST"
  mkdir -p "$(dirname "$DEST")"
  tar -xzf "$DIST/$ARCHIVE_NAME" -C "$(dirname "$DEST")"
  mv "$(dirname "$DEST")/$TOPDIR" "$DEST"
  log "installed. Test with:"
  log "  arduino-cli compile --fqbn ${PKG_NAME}:${ARCH}:minima examples/Device_CDC"
fi
