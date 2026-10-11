#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# dev_test.sh — verify a DEVICE example functionally on the Linux host.
#
# hw_test.sh only proves a host-mode example works, because in host mode the R4
# is not on the Linux bus. In device mode it is, so the check that matters is
# host-side: does the class actually come up and carry data. This script flashes
# the example over J-Link, waits for the sketch's VID:PID on the Linux bus, and
# runs a class-specific functional check.
#
# Usage:
#   ./scripts/dev_test.sh <example>            # one example
#   ./scripts/dev_test.sh --all                # every example with a check
#   ./scripts/dev_test.sh --list               # table of examples + checks
#   BOARD=nanor4 ./scripts/dev_test.sh Device_HID
#
# Node numbers (card1, event7, ttyACM0, /dev/sdX) are NOT stable and are never
# hardcoded here: the R4 is located by the VID:PID its own descriptor declares,
# and nodes are resolved from sysfs at run time. An earlier "pcmC1D0c" note is
# exactly this trap — card 1 on this machine is the
# laptop's HDA codec, not the R4.
#
# Bench: R4 USB-C -> this Linux host. Console (Serial1/CP210x) is optional and
# only used to show the sketch's banner on failure.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BOARD="${BOARD:-minima}"
FQBN="arduino:renesas_uno:$BOARD"
SERIAL_PORT="${SERIAL_PORT:-/dev/ttyUSB0}"

# example | vid:pid | check function | what it proves
# Check functions are defined below and dispatched by name.
#
# CONTROL first. Device_CDC / Device_msc_dual_lun were functionally verified before
# this script existed, so re-running them here proves the harness itself works
# before any of the 17 results below are trusted.
EXAMPLES=(
  "Device_CDC|2341:8001|check_cdc_echo|CONTROL: known-good CDC echo"
  "Device_HID|2341:8002|check_mouse|boot mouse: EV_REL events arrive"
  "Device_Keyboard|2341:8043|check_keyboard|boot kbd: EV_KEY spells hello"
  "Device_hid_boot_interface|cafe:400e|check_kbd_mouse|composite kbd + mouse nodes"
  "Device_hid_multiple_interface|cafe:4013|check_kbd_mouse|two HID interfaces, kbd + mouse"
  "Device_hid_generic_inout|cafe:4012|check_hid_echo|vendor HID report OUT->IN echo"
  "Device_MSC|2341:8004|check_msc_pattern|RAM disk reads, pattern i&0xFF"
  "Device_cdc_msc|cafe:4003|check_cdc_msc|CDC tty + MSC disk together"
  "Device_cdc_msc_throughput|cafe:4013|check_cdc_bench|CDC bulk loopback throughput"
  "Device_cdc_uac2|cafe:4011|check_uac2|UAC2 ALSA card + CDC tty"
  "Device_midi_test|2341:8003|check_midi_notes|MIDI note-on/off reaches ALSA"
  "Device_MIDI_Seq|cafe:4014|check_midi_notes|MIDI sequencer notes reach ALSA"
  "Device_usbtmc|cafe:401c|check_usbtmc_idn|*IDN? query round trip"
  "Device_webusb_serial|cafe:401f|check_webusb|BOS/WebUSB cap + vendor echo"
  "Device_Vendor|2341:0058|check_vendor_echo|vendor bulk OUT->IN echo"
  "r4_DeviceCDC|2341:0069|check_cdc_echo|core identity CDC echo (Minima PID)"
  "Device_dfu_runtime|2341:8005|check_dfu_runtime|dfu-util sees DFU runtime ifc"
)

# ---- helpers ---------------------------------------------------------------

say() { echo "[dev] $*"; }

# Find USB device nodes for a vid:pid. Prints /sys/bus/usb/N-1.2 style paths.
find_usb_sysfs() {
  local vp="$1" d
  for d in /sys/bus/usb/devices/*; do
    [[ -f "$d/idVendor" ]] || continue
    if [[ "$(cat "$d/idVendor" 2>/dev/null):$(cat "$d/idProduct" 2>/dev/null)" == "$vp" ]]; then
      basename "$d"
    fi
  done
}

# Wait until the vid:pid shows up. Prints sysfs name; fails after timeout.
wait_for_usb() {
  local vp="$1" timeout="${2:-25}" waited=0 found
  while (( waited < timeout )); do
    found="$(find_usb_sysfs "$vp" | head -1)"
    if [[ -n "$found" ]]; then echo "$found"; return 0; fi
    sleep 1; waited=$((waited + 1))
  done
  return 1
}

# Remove the board from the bus so the next example starts clean. A DFU detach
# or a reset both work; the J-Link flash at the start of the next run resets it
# anyway, so this is only about not racing the previous app.
unconfigure_r4() {
  local d
  for d in $(find_usb_sysfs "2341:0369"; find_usb_sysfs "cafe:4003"; find_usb_sysfs "cafe:400e";
             find_usb_sysfs "cafe:4011"; find_usb_sysfs "cafe:4012"; find_usb_sysfs "cafe:4013";
             find_usb_sysfs "cafe:4014"; find_usb_sysfs "cafe:401c"; find_usb_sysfs "cafe:401f";
             find_usb_sysfs "2341:0058"; find_usb_sysfs "2341:8002"; find_usb_sysfs "2341:8003";
             find_usb_sysfs "2341:8004"; find_usb_sysfs "2341:8005"; find_usb_sysfs "2341:8043"); do
    echo -n >/sys/bus/usb/devices/"$d"/remove 2>/dev/null || true
  done
}

flash() {
  local name="$1"
  local builddir="$PROJ_ROOT/build/$BOARD/$name"
  if [[ -f "$PROJ_ROOT/examples/$name/tusb_config.h" ]]; then
    cp -f "$PROJ_ROOT/examples/$name/tusb_config.h" \
          "$PROJ_ROOT/src/tusb_config_unor4tinyusb.h"
    "$SCRIPT_DIR/config.sh" install >/dev/null
  fi
  rm -rf "$builddir"
  arduino-cli compile --fqbn "$FQBN" --build-path "$builddir" "$PROJ_ROOT/examples/$name" || return 1
  local hex="$builddir/$name.ino.hex"
  [[ -f "$hex" ]] || { say "no .hex at $hex"; return 1; }
  # NEVER "erase" -- it wipes the bootloader at 0x00000000.
  local js; js="$(mktemp /tmp/jlink_dev_XXXXXX.jlink)"
  printf 'si SWD\ndevice R7FA4M1AB\nspeed 4000\nconnect\nr\nh\nloadfile %s\nr\ng\nq\n' "$hex" > "$js"
  if ! JLinkExe -CommanderScript "$js" >/tmp/jlink_dev.log 2>&1; then
    say "J-Link flash failed (see /tmp/jlink_dev.log)"; rm -f "$js"; return 1
  fi
  rm -f "$js"
}

# ---- checks ---------------------------------------------------------------

# Find driver-bound nodes under a usb sysfs device. Depth is not fixed -- HID
# inserts a 0003:VID:PID.NNNN layer, storage inserts host/target/LUN, so a
# fixed-depth glob silently matches nothing.
#
# -H is required, not cosmetic: /sys/bus/usb/devices/<N> is itself a symlink,
# and find's default -P reports only the link and descends no further. Symlinks
# BELOW it are still not followed, which is what keeps this from looping on the
# parent links sysfs is full of. (A recursive glob follows those and hangs.)
usb_nodes() { find -H "/sys/bus/usb/devices/$1" -path "$2" -type d 2>/dev/null; }

# EV_REL movement from a boot mouse.
check_mouse() {
  local dev="$1"
  python3 "$SCRIPT_DIR/input_probe.py" "$dev" rel && { say "mouse: movement events seen"; return 0; }
  return 1
}

# EV_KEY press+release from a boot keyboard.
check_keyboard() {
  local dev="$1"
  python3 "$SCRIPT_DIR/input_probe.py" "$dev" key && { say "keyboard: key press/release events seen"; return 0; }
  return 1
}

# Composite keyboard + mouse: keys and movement, from the same composite device.
check_kbd_mouse() {
  local dev="$1"
  python3 "$SCRIPT_DIR/input_probe.py" "$dev" both && { say "composite: keyboard + mouse events seen"; return 0; }
  return 1
}

# Vendor HID report: write an OUT report on hidraw, expect it echoed on IN.
check_hid_echo() {
  local dev="$1" hr payload
  for hr in $(usb_nodes "$dev" "*/hidraw/hidraw*"); do
    [[ -e "$hr" ]] || continue
    local node="/dev/hidraw$(cat "$hr"/dev | cut -d: -f2)"
    [[ -c "$node" ]] || continue
    payload=$(python3 - "$node" <<'PY'
import sys, os, time, select
node = sys.argv[1]
fd = os.open(node, os.O_RDWR)
probe = bytes([0] + list(b"R4ECHO19") + [0] * (64 - 9))   # report id 0 + payload
os.write(fd, probe)
data = b""
end = time.time() + 3.0
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.2)
    if not r:
        continue
    try:
        chunk = os.read(fd, 64)
    except (BlockingIOError, OSError):
        break
    if chunk:
        data += chunk
        break
os.close(fd)
print(data[:12].hex())
PY
)
    if [[ "$payload" == *"5234454348"* ]]; then   # "R4ECH"
      say "hid echo: $node returned the written report ($payload)"
      return 0
    fi
    say "hid echo: $node returned ${payload:-nothing}, retrying"
  done
  return 1
}

# MSC RAM disk: block device appears, read it, verify the i&0xFF ramp.
#
# Needs root: reading a raw block device is a disk-group permission, and
# without it blockdev/dd fail with EACCES -- which the first version reported
# as "no block device", a false negative on working firmware.
# -prune stops find descending into queue/mq/power/trace under the disk dir.
check_msc_pattern() {
  local dev="$1" sd sz
  for sd in $(find -H "/sys/bus/usb/devices/$dev" -path "*/block/*" -type d -prune 2>/dev/null); do
    local node="/dev/$(basename "$sd")"
    [[ -b "$node" ]] || continue
    sz=$(sudo blockdev --getsize64 "$node" 2>/dev/null) || continue
    (( sz > 0 )) || continue
    if sudo dd if="$node" bs=512 count=8 iflag=direct 2>/dev/null | python3 -c '
import sys
d = sys.stdin.buffer.read()
ok = bool(d) and all(d[i] == (i & 0xFF) for i in range(len(d)))
sys.exit(0 if ok and len(d) >= 4096 else 1)
'; then
      say "msc: $node $sz bytes, ramp pattern verified"
      return 0
    fi
    say "msc: $node $sz bytes present but pattern mismatch"
  done
  say "msc: no readable block device under $dev"
  return 1
}

# CDC ACM tty echo: open, write, expect it back.
check_cdc_echo() {
  local dev="$1" acm
  for acm in $(usb_nodes "$dev" "*/tty/ttyACM*"); do
    [[ -e "$acm" ]] || continue
    local node="/dev/${acm##*/}"
    if python3 "$SCRIPT_DIR/cdc_probe.py" "$node" echo; then
      say "cdc echo: $node round-tripped the probe"
      return 0
    fi
    say "cdc echo: $node did not echo"
  done
  return 1
}

# CDC + MSC both present (composite).
check_cdc_msc() {
  local dev="$1" ok=0
  ls /sys/bus/usb/devices/$dev/*/tty/ttyACM* >/dev/null 2>&1 && ok=$((ok+1))
  ls -d /sys/bus/usb/devices/$dev/*/host*/target*/*/block/* >/dev/null 2>&1 && ok=$((ok+2))
  (( ok == 3 )) && { say "cdc+msc: both tty and block device present"; return 0; }
  say "cdc+msc: mask=$ok (1=tty 2=block)"
  return 1
}

# CDC bulk throughput meter: the sketch loops back CDC data; measure with it.
check_cdc_bench() {
  local dev="$1" acm
  for acm in $(usb_nodes "$dev" "*/tty/ttyACM*"); do
    [[ -e "$acm" ]] || continue
    local node="/dev/${acm##*/}"
    if python3 "$SCRIPT_DIR/cdc_probe.py" "$node" bench; then return 0; fi
  done
  say "cdc bench: no data returned"
  return 1
}

# UAC2: an ALSA card whose number maps to this usb device.
check_uac2() {
  local dev="$1" card
  for card in $(usb_nodes "$dev" "*/sound/card*"); do
    [[ -e "$card" ]] || continue
    # find -path also matches below the card dir; keep only the card dir itself
    [[ "$(basename "$(dirname "$card")")" == "sound" ]] || continue
    local num=${card##*card} name numpcm
    name=$(cat "$card/name" 2>/dev/null)
    numpcm=$(ls -d "$card"/pcm*/ 2>/dev/null | wc -l)
    if (( numpcm > 0 )); then
      say "uac2: ALSA card $num ($name) with pcm devices"
      # The device declares 48000 Hz mono S16 (TUD_AUDIO20_MIC_ONE_CH). Asking
      # for another rate makes arecord produce nothing at all, which reads as a
      # device fault.
      if timeout 12 arecord -D "plughw:$num,0" -f S16_LE -r 48000 -c 1 -t raw -d 2 /tmp/r4tusb_uac2.raw >/dev/null 2>&1 &&
         [[ $(stat -c%s /tmp/r4tusb_uac2.raw 2>/dev/null || echo 0) -gt 1000 ]]; then
        say "uac2: captured $(stat -c%s /tmp/r4tusb_uac2.raw) bytes"
        return 0
      fi
      say "uac2: card present, capture produced no data (check mic/alt setting)"
      return 1
    fi
  done
  say "uac2: no sound card under $dev"
  return 1
}

# MIDI: the sketch streams note-on/off. Read the ALSA rawmidi node that belongs
# to this usb device (a sequencer port name is not stable, rawmidi is derivable
# from sysfs) and look for running-status note bytes 0x9n / 0x8n.
check_midi_notes() {
  local dev="$1" card midi
  for card in $(usb_nodes "$dev" "*/sound/card*"); do
    [[ -e "$card" ]] || continue
    # find -path also matches below the card dir; keep only the card dir itself
    [[ "$(basename "$(dirname "$card")")" == "sound" ]] || continue
    local num=${card##*card}
    midi="/dev/snd/midiC${num}D0"
    [[ -c "$midi" ]] || continue
    if python3 - "$midi" <<'PY'
import sys, os, time, select
node = sys.argv[1]
fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
buf = b""
end = time.time() + 6.0
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            buf += os.read(fd, 4096)
        except (BlockingIOError, OSError):
            break
os.close(fd)
notes_on = sum(1 for b in buf if (b & 0xF0) == 0x90)
notes_off = sum(1 for b in buf if (b & 0xF0) == 0x80)
print(f"[dev] midi: {len(buf)} B, note-on={notes_on} note-off={notes_off}")
sys.exit(0 if notes_on and notes_off else 1)
PY
    then
      say "midi: notes arriving on $midi"
      return 0
    fi
    say "midi: no note events on $midi"
  done
  say "midi: no rawmidi node under $dev"
  return 1
}

# USBTMC: *IDN? via the kernel usbtmc class.
#
# Node is /dev/usbtmcN on this kernel (flat), NOT /dev/usb/usbtmc/N; both are
# globbed. The node is 0600 root:root, so this needs sudo.
#
# Do NOT drive this with select(): poll() on the usbtmc char device never
# reports readable, so a select-driven probe never calls read() -- and the
# kernel only submits the bulk-IN URB from inside read(). The device then has
# a correct reply queued that nobody ever fetches, which looks exactly like a
# device that never answers. Call read() directly and tolerate EAGAIN.
check_usbtmc_idn() {
  local dev="$1" node
  for node in /dev/usbtmc* /dev/usb/usbtmc/*; do
    [[ -c "$node" ]] || continue
    if sudo timeout 15 python3 - "$node" <<'PY'
import sys, os, time
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NONBLOCK)
os.write(fd, b"*IDN?\n")
buf = b""
end = time.time() + 8
while time.time() < end and not buf:
    try:
        chunk = os.read(fd, 512)
    except BlockingIOError:
        time.sleep(0.05)
        continue
    except OSError:
        break
    if chunk:
        buf += chunk
os.close(fd)
print(f"[dev] usbtmc idn: {buf!r}")
sys.exit(0 if b"TinyUSB," in buf else 1)
PY
    then return 0; fi
  done
  say "usbtmc: no reply (nodes: $(ls /dev/usbtmc* /dev/usb/usbtmc/ 2>/dev/null | tr '\n' ' '))"
  return 1
}

# WebUSB: the BOS must carry the two platform capabilities the sketch declares.
#
# GUIDs are compared against the vendored macros in device/usbd.h, which are
# what the firmware actually emits -- NOT against hand-typed constants. The
# first version hardcoded a GUID from memory and reported a working device as
# broken. Whether TinyUSB's WebUSB UUID matches the WebUSB spec's GUID (and so
# whether Chrome actually claims the device) is a separate question this bench
# cannot answer; only a real browser can.
check_webusb() {
  local dev="$1"
  python3 - "$dev" "$SCRIPT_DIR" <<'PY'
import sys, re

dev = sys.argv[1]
hdr = open(f"{sys.argv[2]}/tinyusb-master/device/usbd.h").read()

def macro_uuid(name):
    # Anchor on the #define: the name also appears earlier, as an argument to
    # TUD_BOS_PLATFORM_DESCRIPTOR inside the descriptor macro. Matching the bare
    # name captures that macro's bytes (0x0100 etc.) instead of the UUID.
    txt = re.search(r"#define\s+" + re.escape(name) + r"\b(.*?)(?=\n#define)", hdr, re.S).group(1)
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})", txt))

webusb_uuid = macro_uuid("TUD_BOS_WEBUSB_UUID")
msos_uuid = macro_uuid("TUD_BOS_MS_OS_20_UUID")

try:
    b = open(f"/sys/bus/usb/devices/{dev}/bos_descriptors", "rb").read()
except OSError as e:
    print(f"[dev] webusb: cannot read bos_descriptors ({e})")
    sys.exit(1)

if len(b) < 5 or b[1] != 0x0F:
    print(f"[dev] webusb: {len(b)} B is not a BOS descriptor")
    sys.exit(1)

total, caps = int.from_bytes(b[2:4], "little"), b[4]
print(f"[dev] webusb: BOS wTotalLength={total} bDeviceCapabilityCount={caps}")

found = []
i = 5
while i < len(b):
    ln = b[i]
    if ln == 0:
        break
    cap = b[i:i + ln]
    # bLength, bDescriptorType(0x10), bDevCapabilityType(0x05 PLATFORM),
    # bReserved, then the 16-byte UUID at cap[4:20].
    if len(cap) == 24 and cap[2] == 0x05:
        found.append(("webusb" if cap[4:20] == webusb_uuid else "unknown-24B"))
    elif len(cap) == 28 and cap[2] == 0x05:
        found.append(("ms_os_20" if cap[4:20] == msos_uuid else "unknown-28B"))
    else:
        found.append(f"cap(len={ln})")
    i += ln

print(f"[dev] webusb: caps parsed: {found}")
if "webusb" in found and "ms_os_20" in found:
    sys.exit(0)
sys.exit(1)
PY
  local bos_rc=$?
  if (( bos_rc != 0 )); then return 1; fi
  if check_vendor_echo "$dev"; then
    say "webusb: BOS capabilities + vendor echo both good"
  else
    say "webusb: BOS capabilities confirmed (vendor echo unavailable)"
  fi
  return 0
}

# Vendor bulk echo via pyusb on the sketch's vendor endpoints.
#
# Matches on sys_name (the sysfs device name) rather than bus/port: this
# pyusb build has no `.port` attribute, and the first version died with an
# AttributeError that surfaced as a bare FAIL.
check_vendor_echo() {
  local dev="$1"
  python3 - "$dev" <<'PY'
import sys
import usb.core
import usb.util

want = sys.argv[1]

def sysfs_name(c):
    # This pyusb has no `.port`/`.sys_name`; port_numbers is the port path,
    # which is exactly the sysfs device name suffix ("1-3.2" == bus 1, 3.2).
    try:
        return f"{c.bus}-{'.'.join(str(p) for p in c.port_numbers)}"
    except Exception:
        return None

d = next((c for c in usb.core.find(find_all=True) if sysfs_name(c) == want), None)
if d is None:
    print("[dev] vendor: device not found via pyusb")
    sys.exit(1)

try:
    cfg = d.get_active_configuration()
except usb.core.USBError as e:
    print(f"[dev] vendor: no active configuration ({e})")
    sys.exit(1)

for itf in cfg:
    if itf.bInterfaceClass != 0xFF:
        continue
    ep_in = ep_out = None
    for ep in itf:
        if usb.util.endpoint_type(ep.bmAttributes) != usb.util.ENDPOINT_TYPE_BULK:
            continue
        if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN:
            ep_in = ep
        else:
            ep_out = ep
    if not (ep_in and ep_out):
        continue
    probe = bytes(list(b"R4VEND12") + [0] * (64 - 8))
    try:
        ep_out.write(probe, 1000)
        data = ep_in.read(64, 1000)
    except Exception as e:
        print(f"[dev] vendor: itf {itf.bInterfaceNumber} xfer failed ({e})")
        continue
    if bytes(data[:8]) == b"R4VEND12":
        print(f"[dev] vendor: itf {itf.bInterfaceNumber} echoed the 64B probe")
        sys.exit(0)
    print(f"[dev] vendor: itf {itf.bInterfaceNumber} returned {bytes(data[:8])!r}")

print("[dev] vendor: no vendor bulk echo")
sys.exit(1)
PY
}

# DFU Runtime: dfu-util lists a DFU interface while the app is running.
check_dfu_runtime() {
  local dev="$1" out
  out=$(dfu-util -l 2>&1)
  # dfu-util prints "Found Runtime:" for a runtime-DFU interface and
  # "Found DFU:" for a device sitting in DFU mode. Matching only the latter
  # reported this example as broken while dfu-util was listing it fine.
  if echo "$out" | grep -qiE "Found (Runtime|DFU):"; then
    say "dfu-runtime: $(echo "$out" | grep -iE "Found (Runtime|DFU):" | head -1)"
    return 0
  fi
  say "dfu-runtime: dfu-util did not list it ($out)"
  return 1
}

# ---- dispatch -------------------------------------------------------------

run_one() {
  local name="$1" entry n="" v="" c="" d="" vp="" check="" desc=""
  for entry in "${EXAMPLES[@]}"; do
    IFS='|' read -r n v c d <<<"$entry"
    if [[ "$n" == "$name" ]]; then vp="$v"; check="$c"; desc="$d"; break; fi
  done
  if [[ -z "${check:-}" ]]; then say "no check registered for $name"; return 2; fi

  echo "=================================================================="
  say "$name  expect $vp  —  $desc"
  echo "  build+flash..."
  flash "$name" || { say "FAIL: build/flash"; return 1; }
  echo "  waiting for $vp on the bus..."
  local dev
  if ! dev=$(wait_for_usb "$vp" 30); then
    say "FAIL: $vp never enumerated (lsusb: $(lsusb | grep -i 2341 | tr '\n' ' '))"
    return 1
  fi
  say "enumerated as $dev"
  sleep 2   # let kernel drivers bind their nodes (storage scans slower than 1 s)
  if $check "$dev"; then say "PASS: $name"; return 0; fi
  say "FAIL: $name"
  return 1
}

case "${1:-}" in
  --list) printf '%-22s %-12s %-20s %s\n' EXAMPLE VID:PID CHECK PROVES
          for e in "${EXAMPLES[@]}"; do
            IFS='|' read -r n v c d <<<"$e"
            printf '%-22s %-12s %-20s %s\n' "$n" "$v" "$c" "$d"
          done; exit 0 ;;
  --all)  rc=0; declare -a results=()
          for e in "${EXAMPLES[@]}"; do
            n="${e%%|*}"
            if run_one "$n"; then results+=("PASS $n"); else results+=("FAIL $n"); rc=1; fi
          done
          echo; echo "================= summary ================="
          printf '%s\n' "${results[@]}"
          "$SCRIPT_DIR/config.sh" union >/dev/null 2>&1 || true
          exit $rc ;;
  "")     echo "usage: dev_test.sh <example> | --all | --list" >&2; exit 1 ;;
  *)      run_one "$1"; rc=$?; "$SCRIPT_DIR/config.sh" union >/dev/null 2>&1 || true; exit $rc ;;
esac
