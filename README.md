# UnoR4-tinyusb

TinyUSB device + host library for the **Arduino Uno R4 Minima** and **Arduino Nano R4** (Renesas RA4M1).

Upgrades the core's 0.15-era TinyUSB tree to an upstream **master** snapshot, which is what makes host mode possible at all. Device **or** host per session on the single `RUSB2` full-speed port (`P914`/`P915`) — not simultaneous OTG.

It ships two ways: as the **`unor4tinyusb` board package** (recommended — nothing on your machine is modified, and each sketch carries its own TinyUSB config), or as a **patch** applied to the stock `arduino:renesas_uno` core. The library API is identical either way.

The API is the same shape as [`TinyUSB_Arduino`](https://github.com/controllercustom/TinyUSB_Arduino), so sketches port across with an include-line change.

```cpp
#include <UnoR4TinyUSB.h>

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);  // Serial1, 115200
  tuh_arduino_init();      // or tud_arduino_init()
}

void loop() {
  tuh_arduino_poll();      // or tud_arduino_task();
}
```

### API at a glance

| TinyUSB_Arduino | UnoR4-tinyusb | Notes |
|---|---|---|
| `tud_arduino_init()` | same | R4: bookkeeping only — the core already brought DEVICE up in `_init()` |
| `tud_arduino_task()` | same | `tud_task()` + drains debug output queued from the USB ISR |
| `tuh_arduino_init()` | same | re-inits rhport 0 as HOST, re-latches `DCFM`/`DRPD`, re-enables `USBFS_IRQn` |
| `tuh_arduino_poll()` | same | keeps RUSB2 in host mode, then `tuh_task()` |
| `arduino_tinyusb_probe()` | same | RUSB2 register snapshot (`SYSCFG`/`DVSTCTR0`/`SYSSTS0`/…) |
| `arduino_tinyusb_get_mode()` | same | `UNOR4_TINYUSB_MODE_{UNINITIALIZED,DEVICE,HOST}` |
| `UNOR4_TINYUSB_CONSOLE` | `Serial1` | R4 has no USB-serial chip; its own USB port is RUSB2 |
| `UNOR4_TINYUSB_BOARD_*` | `UNOR4_TINYUSB_BOARD_R4` | auto-detected; override with `-D` |
| `src/tusb_config_unor4tinyusb.h` | route B: same. Route A: `tusb_config.h` **in the sketch** | see [Configuration](#configuration) |

Removed, with what replaces them:

| Name | Replaced by |
|---|---|
| `tuh_arduino_init_forced_fs()` (0.4.0) | `tuh_arduino_init()` — the forced-FS path wrote `DVSTCTR0` bit 1, which is `RHST[2:0]`, a read-only status field; `USBRST` is bit 6, so it never asserted a bus reset |
| `arduino_tinyusb_set_mode()` (0.4.0) | nothing: the role is set by the `*_init()` entry points and only read out by `arduino_tinyusb_get_mode()` |
| `R4TUSBDevice_begin(vid,pid)` | supply your own descriptors; see [Device descriptors](#device-descriptors) |
| `R4TUSBDevice_vendor*` | `tud_vendor_read/write/available()` |
| `R4TUSBHost_*` | `tuh_arduino_init()` / `tuh_arduino_poll()` |

`src/R4TinyUSB.h`, the one-release shim that forwarded the `R4*` and `r4tusb_*` names, is gone in 0.4.0: pre-0.3.0 sketches must move to the calls above.

## Install (Arduino IDE 2.x — Windows, macOS, Linux)

You need Arduino IDE 2.x, an Uno R4 Minima or Nano R4, and a browser. **No
GitHub account, no git, no `arduino-cli`, no administrator rights.**

Two things get installed, because Arduino separates them: a **board package**
(the RA4M1 core with the upgraded TinyUSB tree inside it) and a **library**
(`UnoR4TinyUSB.h`, the examples). Installing one without the other gets you a
board that compiles a sketch that cannot find its header.

### A. Board package — the supported route

Nothing under `packages/arduino/` is written, so ordinary `arduino:renesas_uno`
projects keep working side by side.

1. **Add the board manager URL.** File → Preferences → *Additional boards
   manager URLs*, add:

   ```
   https://raw.githubusercontent.com/controllercustom/UnoR4-tinyusb/refs/heads/main/package_unor4tinyusb_index.json
   ```

   The IDE downloads the index when you save Preferences and again at startup.

2. **Install the board package.** Tools → Board → Boards Manager, search
   `UnoR4 TinyUSB`, install **UnoR4 TinyUSB Boards**. The compiler and uploader
   are Arduino's own: the package *declares* them as dependencies instead of
   shipping copies, so Boards Manager pulls them the same way it does for the
   stock R4 board.

3. **Install the library.** Grab the repo ZIP (the **Code → Download ZIP**
   button — no account needed) and use Sketch → Include Library → **Add
   .ZIP Library…** . If you unzip by hand instead, put the folder in
   `~/Arduino/libraries/` and keep the folder name `UnoR4-tinyusb`; the include
   path is the folder name, so `UnoR4-tinyusb-main` will not resolve
   `#include <UnoR4TinyUSB.h>`.

4. **Pick the board.** Tools → Board → **UnoR4 TinyUSB Boards** →
   **UNO R4 Minima (TinyUSB)** or **Nano R4 (TinyUSB)**. The parenthetical is
   the whole difference from the stock entry: same board, this package's core.

5. **Open an example and verify.** File → Examples → **UnoR4-tinyusb** →
   `Device_CDC` (device) or `Host_cdc_msc_hid` (host) → **Verify**. Upload uses
   the stock DFU path (`dfu-util`, ports `2341:0069` / `2341:0369`), unchanged
   from Arduino's own package.

Each example carries its own TinyUSB config next to its `.ino`, so there is
nothing to copy and no global setting to remember — see
[Configuration](#configuration).

The package carries **Minima and Nano R4 only**. The UNO R4 WiFi is excluded:
it is untested here, and its USB-C port sits behind a hardware multiplexer,
which makes its TinyUSB path not comparable to the other two.

### B. Patch the stock core — alternative, not the default

Use this only if `arduino:renesas_uno` must stay the only R4 package on the
machine. It edits Arduino's own core directory, which is why it is not the
default: **an IDE update to `arduino:renesas_uno` silently reverts it**, and
switching an example's class set means running a script (see
[Configuration](#configuration)).

1. **Install the library** as in A3.
2. **Install the stock platform:** Tools → Board → Boards Manager, search
   `Uno R4`, install **Arduino UNO R4 Boards** (`arduino:renesas_uno`,
   exactly `1.6.0`).
3. **Upgrade the core (once).** Run the script for your OS from the installed
   library's `scripts/` folder. Each uses only utilities your OS already ships:
   - **Linux:** `bash patch_renesas_core.sh` (uses `patch`, `sha256sum`)
   - **macOS:** `sh patch_renesas_core.macos.sh` in Terminal (uses `patch`, `shasum`)
   - **Windows:** double-click `patch_renesas_core.windows.bat` (stock PowerShell 5.1)

   What it does: verifies the vendored tree against `SHA256SUMS`, backs up
   `cores/arduino/tinyusb/`, `USB.cpp` and both variant configs, installs the
   pinned upstream snapshot, writes the active config into `variants/MINIMA` and
   `variants/NANOR4`, and applies `renesas_core.patch` (weak descriptor
   callbacks + `tusb_rhport_init`).
   Check it took: `variants/MINIMA/.r4tinyusb_patched` and
   `variants/NANOR4/.r4tinyusb_patched` contain `master-e2048238`.
   Revert: re-run with `--restore` (`-Restore` on Windows), or reinstall the
   core from Boards Manager.
4. **Board and example:** Tools → Board → **Arduino Uno R4 Minima** (or
   **Arduino Nano R4**), then File → Examples → **UnoR4-tinyusb** → …

### Console wiring (both routes)

The R4 has no USB-serial chip, so `UNOR4_TINYUSB_CONSOLE` is **`Serial1`**: **pin 0 = RX**, **pin 1 = TX**, 115200 8N1, 3.3 V. Wire an external USB-UART adapter — adapter RX to pin 1, adapter TX to pin 0, ground to ground. Every example prints there, not to `Serial`, so **the IDE Serial Monitor shows nothing**; on host examples it can't, since the board is the host and not a USB device.

> J-Link `erase` wipes the bootloader at `0x00000000`. Use `r; h; loadfile` (`upload_r4.sh`) for SWD, or IDE upload via `dfu`.
>
> **VBUS:** the R4 Minima and Nano R4 do **not** source VBUS (5 V) from their USB connector. Self-powered and full-/low-speed devices (Logitech joystick, CP2102, FT232R, LS keyboard, DualSense, DS4) detect host presence and work direct. Bus-powered devices need backpowering or a powered hub.

## Configuration

TinyUSB is configured by one header, `tusb_config.h`. Which copy applies depends on the install route:

| route | config in force | how to change it |
|---|---|---|
| **A. board package** | `tusb_config.h` **in your sketch folder** | every example ships one next to its `.ino`. Edit it and rebuild — nothing else to run |
| **A**, sketch has no config | the package's `variants/<BOARD>/tusb_config.h`, which is the union | — |
| **B. patched stock core** | `variants/<BOARD>/tusb_config.h`, installed from `src/tusb_config_unor4tinyusb.h` | `./scripts/config.sh set <Example>`, or `union` |

Route A works because the package's `platform.txt` adds `-iquote{build.source.path}`, and `-iquote` directories are searched **before** `-I` — so the copy beside your `.ino` is seen by every translation unit, the core's own vendored TinyUSB included. That is not merely header lookup: enabling `CFG_TUH_AUDIO 1` in a sketch config makes `class/audio/audio_host.c` compile to a 257 KB driver, where the union leaves the same translation unit a 41 KB object with no symbols in it. On the stock core the sketch folder is on nobody's include path, which is exactly why route B installs a copy into the variant.

Either way the config is **self-contained by necessity**: a config that `#include`s a helper header resolves for the sketch and fails with "No such file or directory" inside `cdc_device.c`.

The union — device and host stacks, every class that fits — is the default, and it is **not** universal: 43 of the 48 examples build against it. `Device_MSC` overflows RAM; `Device_NET`, `Host_audio_host`, `Host_Printer` need classes the union deliberately leaves out (`CFG_TUD_NCM`, `CFG_TUH_AUDIO`, `CFG_TUH_PRINTER`); and `Device_usbtmc` needs one the union *must* leave out so ordinary sketches keep linking (see below). On route B that means running `config.sh set <Example>` before those five; on route A it means nothing, because every sketch already carries its own.

**A sketch that never mentions this library is unaffected.** Installing the board definition must not change what an ordinary sketch does, so the default config deliberately leaves `CFG_TUD_USBTMC` out: unlike CDC, HID, MSC, audio and vendor — where TinyUSB supplies weak default callbacks — that class requires the *sketch* to implement its callbacks, and the RA core links its core archive with `-Wl,--whole-archive`, so enabling it makes even `Blink.ino` fail to link. To use USBTMC, enable it in your own sketch config (route A) or run `config.sh set Device_usbtmc` (route B). `./scripts/build.sh` compiles the IDE's Blink for both boards before anything else, precisely to keep this from regressing.

Route B's config is `src/tusb_config_unor4tinyusb.h`, managed by:

```bash
./scripts/config.sh union            # full device+host, every class (default)
./scripts/config.sh set Device_CDC   # activate examples/Device_CDC's lean config
./scripts/config.sh gen              # regenerate all 48 per-example configs
./scripts/config.sh list             # show each example's class signature
./scripts/config.sh verify           # check each sketch's API + config drift
```

`src/tusb_config_unor4tinyusb.h` is BOTH the active config *and* the file
`set` overwrites with an example's config, so the union lives separately in
`scripts/tusb_config.union.h` and `union` restores from there before
installing. (`b118f18` shipped without that copy, so `union` silently
re-installed whichever example had run last — the commit's active config
contained only MSC. Fixed in the follow-up commit.)

`set` copies the example's config into `src/` **and** into both variant dirs. A per-example config still compiles the **device** stack, because the core requires it:

- `CFG_TUD_ENABLED` is always `1` and `CFG_TUD_CDC` is at least `1` in every config. Two core files are compiled unconditionally and need them: `USB.cpp`'s `__SetupUSBDescriptor()` builds a `TUD_CDC_DESCRIPTOR` outside any `#if`, and `__USBStart()` calls `tud_rhport_init()`; `SerialUSB.cpp`'s `_SerialUSB` object calls `tud_cdc_connected/peek/available/read_char/write/write_available/write_flush`. Setting `CFG_TUD_CDC 0` fails to compile the **core**, not just the sketch.
- What a per-example config actually saves is the host stack (~8 KB) and the device classes the sketch does not use.

`./scripts/build.sh` builds the matrix on route B: it activates each example's own config before building it and restores the union afterwards, so nothing else may compile while it runs. `--union` builds everything against the full union instead. `--fork` switches to route A's package, where there is no install step at all — and which is therefore the only mode safe to parallelise (`--fork -j 4`).

Per-example configs are **generated**, not hand-written: `scripts/config.sh` takes the shared body from `scripts/tusb_config.union.h` and rewrites the lines between the `BEGIN/END GENERATED SECTION` markers from its class table. Everything outside those markers is shared verbatim across all 48 copies, so they cannot drift. Run `./scripts/config.sh gen` after editing the shared part.

**RAM is the binding constraint.** 32 KB, statically linked, and TinyUSB sizes its buffers and slot arrays for every enabled class in every sketch:

| sketch | static RAM |
|---|---|
| device (CDC only) | 5.8 KB (17%) |
| device (CDC + Vendor) | 6.3 KB (19%) |
| host (CDC + 4 UART-bridge drivers) | 12.9 KB (39%) |
| host + MSC + FatFS | 12.3 KB (37%) |
| device MSC, single 8 MB RAM disk | 22.2 KB (67%) — the worst case |

`src/tusb_config_unor4tinyusb.h` carries the full per-class cost table.

## Device descriptors

The library no longer supplies a default descriptor. Two options:

- **Own them** (what every `Device_*` example does): put `tud_descriptor_device_cb()`, `tud_descriptor_configuration_cb()` and `tud_descriptor_string_cb()` in a second `.cpp` tab. The core patch makes all three `weak`, so your strong definitions win. Keep them in a `.cpp`, not the `.ino` — the Arduino prototype generator mangles `extern "C"` callbacks.
- **Use the core's**: supply none and the RA4M1 core's own descriptor is used — CDC at `2341:0069` (Nano R4: `2341:0074`), the identity the Arduino IDE expects, which keeps `Serial` owning interface 0. The core has no vendor interface (`TUD_VENDOR_DESCRIPTOR` does not appear in `USB.cpp`); its DFU-RT/HID/MSC blocks install only conditionally. See `r4_DeviceCDC`.

Never define `tud_cdc_line_state_cb`, `tud_cdc_line_coding_cb` or `tud_dfu_runtime_reboot_to_dfu_cb` in a sketch: the core's `SerialUSB.cpp` owns all three strongly, and overriding one is a link error, not a silent replacement. The core's DFU path already does the right thing — detach, then reboot into the stock bootloader at `0x00000000`, which `dfu-util -e` picks up as `2341:0369`.

## Examples

48 sketches in two families.

### Upstream-aligned (32)

Where upstream TinyUSB has an example for it, the name is upstream's, prefixed
`Device_` / `Host_` so the role is legible in the IDE menu: `Device_cdc_msc` is
`examples/device/cdc_msc`. Where upstream has no example, the older
`TinyUSB_Arduino` name stands (`Device_CDC`, `Host_Printer`, …) rather than
inventing a name that looks upstream and is not.

**Device:** `Device_audio_test`, `Device_CDC`, `Device_cdc_dual_ports`, `Device_cdc_msc`, `Device_cdc_msc_throughput`, `Device_cdc_uac2`, `Device_dfu_runtime`, `Device_dynamic_configuration`, `Device_HID`, `Device_hid_boot_interface`, `Device_hid_composite`, `Device_hid_generic_inout`, `Device_hid_multiple_interface`, `Device_Keyboard`, `Device_midi_test`, `Device_MIDI_Seq`, `Device_MSC`, `Device_msc_dual_lun`, `Device_NET`, `Device_usbtmc`, `Device_Vendor`, `Device_webusb_serial`

**Host:** `Host_audio_host`, `Host_bare_api`, `Host_cdc_msc_hid`, `Host_device_info`, `Host_HID_Boot`, `Host_hid_controller`, `Host_MIDI`, `Host_midi_rx`, `Host_msc_file_explorer`, `Host_Printer`

`Host_cdc_msc_hid` is upstream's one combined host example: CDC, MSC and HID
bound in a single sketch, because a composite device mounts several drivers at
once and that is the case worth demonstrating. It replaces this repo's three
single-class host sketches, which had grown into near-copies of each other.

Compile-gated, not silently missing — each ships the config that enables its class, and its header comment says why it is marginal on 32 KB:

| Example | Why it is tight |
|---|---|
| `Device_NET` | `CFG_TUD_NCM` needs ~6.4 KB of NTB buffers plus a 1514-byte MTU buffer |
| `Host_audio_host` | ~1.3 KB static for the audio stream, plus ISO bandwidth on a shared FS bus |
| `Host_Printer` | the PRINTER host driver is a local backport (absent upstream) |

### Not ported, and why

Upstream has 33 device and 10 host examples. The `*_freertos` variants are out
by design: these sketches are bare `setup()` / `loop()`. The rest of the
omissions are deliberate and each has a reason, because "it is not here" and "it
cannot work here" are different statements:

| Upstream example | Why there is no R4 version |
|---|---|
| `video_capture`, `video_capture_2ch` | the `video` class is not in the vendored TinyUSB tree — `scripts/tinyusb-master/class/` has `audio cdc dfu hid midi msc net printer usbtmc vendor`, nothing else. Adding it means re-vendoring the class, not writing a sketch |
| `mtp` | same: no `mtp` class in the vendored tree |
| `net_lwip_webserver` | needs the lwIP stack, which is not vendored. `Device_NET` is CDC-NCM with no IP stack, which is what the class actually enables here |
| `audio_4_channel_mic` | cannot fit full speed. RUSB2 is FS: 1024 B per 1 ms frame, ~102 KB/s. Four channels at 16 bit need 384 KB/s @48 kHz, and 128 KB/s even @16 kHz |
| `usbtest` | the USB-IF compliance test harness; needs a test host, and there is none on this bench |
| `board_test` | upstream drives it through BSP LED/button macros that TinyUSB does not provide for RA4M1 |
| `dfu` (DFU mode, not runtime) | the RA4M1 bootloader at `0x00000000` is Arduino's, and the core owns the detach-to-DFU path. `Device_dfu_runtime` is the supported shape |
| `printer_to_cdc`, `uac2_headset`, `uac2_speaker_fb`, `audio_test_multi_rate`, `midi2_device`, `midi2_host` | feasible — the classes are vendored. Not yet ported; each costs config lines, RAM, and a row in the class table |

### R4-specific (`r4_*`, 16)

The bench-verified originals, migrated onto the new API. These encode per-device knowledge the generic set does not have — specific VID/PIDs, report offsets, baudrate quirks, register dumps.

| Example | Mode | Device / purpose |
|---|---|---|
| `r4_HostInfo` | Host | attached-device debugger: port transitions, slot table, config-descriptor walk, RUSB2 registers |
| `r4_HostCDC` | Host | CDC bridge `Serial1 ↔ tuh_cdc`, FTDI/CP210x/PL2303/CH34x via `CFG_TUH_CDC_*` |
| `r4_HostCP2102` | Host | CP2102 `10C4:EA60` echo, 115200 |
| `r4_HostFTDI` | Host | FT232R `0403:6001` echo / cross / tx-flood, 9600–115200 |
| `r4_HostPL2303` | Host | PL2303 — needs the 8N1 line-coding init, see AGENTS.md |
| `r4_HostTriCDC` | Host | 3 × CDC concurrent through a hub |
| `r4_HostHID` / `r4_HostHID_Boot` | Host | HID report dump / boot kbd+mouse |
| `r4_HostKeyboard` / `r4_HostMouse` | Host | boot-protocol key / 10-px circle + left-click |
| `r4_HostJoystick` | Host | multi-interface joystick reports |
| `r4_HostGamepad` | Host | DS4 `054C:09CC` sticks/buttons/LED/rumble |
| `r4_HostDualSense` | Host | DualSense PS5 `054C:0CE6` — report-offset corrections |
| `r4_HostMSC` | Host | BOT `READ10` bench + RUSB2 register dump on stall |
| `r4_msc_file_explorer` | Host | FatFS R0.15 (`ff.c` vendored) `ls/cat/dd` CLI, multi-LUN |
| `r4_DeviceCDC` | Device | core's `2341:0069` identity (Nano R4 `2341:0074`), CDC echo |

### Emulators (RP2040)

`emulators/` from `Giga_USBH` for host testing (`rp2040:rp2040:rpipico:usbstack=tinyusb` + Debug Probe):

| Emulator | PID | Host | Notes |
|---|---|---|---|
| `CDCEcho` | `239A:CAFD` | `r4_HostCDC` echo | CDC-only, host-driven `seq` verify, `ok/bad/err` |
| `CDCBulkSource` | `239A:CAFE` | `r4_HostCDC` tx-flood | bulk TX flood |
| `KeyA_Loop` | `239A:CAFE` | `r4_HostHID`/`r4_HostKeyboard` | `HID_KEY_A`, 250/500 ms |
| `MouseMove` | — | `r4_HostHID`/`r4_HostMouse` | 10-px circle + `LEFT` every 4 laps |
| `MIDIEcho` | `239A:CAFB` | `Host_MIDI` | 4-byte echo |
| `MSCRamDisk` | `239A:CAFC` | `r4_HostMSC` | 256×512 = 128 KiB RAM disk, ~910 KiB/s `READ10` |

Flash via `emulators/upload.sh key|mouse|cdc-echo|cdc-bulk|midi-echo|msc`. After a PID change, reset the host (`J-Link r; g`).

## Build (maintainers)

```bash
./scripts/build.sh                          # all 48 examples, minima + nanor4 (stock core)
./scripts/build.sh --fork -j 4              # all 48 × 2 against the fork package, 4 at a time
./scripts/build.sh examples/Host_cdc_msc_hid                        # one example, both boards
./scripts/build.sh examples/Host_cdc_msc_hid arduino:renesas_uno:nanor4
./scripts/build_fork_package.sh --install   # rebuild + reinstall the board package
./scripts/upload_r4.sh r4_HostInfo          # compile + J-Link flash (SWD 4000 kHz)
./scripts/upload_r4.sh --fork r4_HostInfo   # same, built against the fork package
./scripts/hw_test.sh r4_HostFTDI 25 c "ready"   # flash + capture Serial1 25 s
R4_BOARD=nanor4 ./scripts/hw_test.sh r4_HostFTDI 25 c "ready"
SERIAL_PORT=/dev/ttyUSB0 SEND_AT=14 ./scripts/hw_test.sh --fork r4_HostInfo 40 i "MOUNT addr=1"
```

Requires `arduino-cli` + the `arduino:renesas_uno` core. `build/` is per-target and gitignored. The stock-core matrix installs one variant config at a time, so nothing else may compile while it runs; the fork matrix has no shared state and is the only one that takes `-j`.

Build matrices, both re-run most recently on this tree: **board package 48 × 2 = 96 builds, 0 failures** and **stock core 48 × 2 = 96 builds, 0 failures**, with every example building on its own sketch-local `tusb_config.h` and no `config.sh` step in the package run. The 96 route-A/route-B `.hex` pairs are byte-identical. Peak flash `Host_msc_file_explorer` 73,544 B of 262,144 B; peak static RAM `Device_MSC` 22,200 B of 32,768 B (67%).

## Acknowledgements

- **Ha Thach and the TinyUSB contributors** — the stack this runs on. Almost
  everything here *is* upstream TinyUSB; the R4-specific changes are listed
  under [Upstream](#upstream) instead of being quietly folded in, and the three
  HCD fixes among them are still absent upstream.
- **The Arduino `renesas_uno` core team** — the RA4M1 core, its USB bring-up
  (`__USBStart()`, `SerialUSB.cpp`), and the DFU bootloader at `0x00000000` that
  this library deliberately never touches.
- **ChaN** — FatFs (`ff.c`), vendored under its own license in
  `Host_msc_file_explorer` and `r4_msc_file_explorer`.
- **Adafruit** — `Adafruit_TinyUSB_Arduino`, which established the
  Arduino-shaped way of driving TinyUSB that this library and its sibling follow.
- **[`TinyUSB_Arduino`](https://github.com/controllercustom/TinyUSB_Arduino)** —
  the multi-board sibling (GIGA, Pico, ESP32, Due, SAMD) sharing this API and
  example set, so a sketch moves between them by changing an include line rather
  than its logic.

## Upstream

`arduino:renesas_uno@1.6.0`, patched to upstream [hathach/tinyusb](https://github.com/hathach/tinyusb) **master** `e2048238` (2026-10-08). The trimmed tree is vendored at `scripts/tinyusb-master/` and `SHA256SUMS`-verified; `scripts/tinyusb-master/PIN` records the exact commit and every local deviation.

Local deviations, all bench-proven on R4 hardware and **still absent upstream**:

| File | Deviation |
|---|---|
| `portable/renesas/rusb2/hcd_rusb2.c` | re-assert `DCPCFG.DIR=1` on the OUT data stage; `PID=NAK` in the SACK ISR; `PID=NAK` before the status-stage flip |
| `host/usbh.c` | drop control-transfer completions whose endpoint does not match the outstanding stage; PRINTER driver table entry |
| `class/printer/printer_host.{c,h}` | whole driver backported (upstream has only the device side) |
| `class/msc/msc_host.c` | per-LUN TUR + capacity walk, bounded to 5 sense retries, `max_lun` clamped to `CFG_TUH_MSC_MAXLUN`, READ_CAPACITY refresh, IDLE-stage guard |
| `class/cdc/cdc_host.c` | line coding defaults to 8N1 @9600 (a zero `data_bits` is silently rejected by the PL2303 driver) |

Host CDC serial drivers: FTDI, CP210x, PL2303, CH34x. FatFS R0.15 (`ff.c`, ChaN license) is vendored inside `examples/Host_msc_file_explorer` and `examples/r4_msc_file_explorer`.

License MIT (`LICENSE`).