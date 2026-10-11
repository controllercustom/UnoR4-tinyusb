# AGENTS.md — UnoR4-tinyusb

## What this project is

TinyUSB device + host library for the Arduino Uno R4 Minima and Arduino Nano R4
(Renesas RA4M1). Enables USB device and USB host on the single RUSB2 Full-Speed
peripheral. Device OR host per session — not simultaneous OTG.

Upgrades the core's 0.15-era TinyUSB tree in place to upstream **master**
`e2048238` (trimmed copy vendored at `scripts/tinyusb-master/`,
SHA256-verified). The one-time upgrade replaces `cores/arduino/tinyusb/`,
installs the active config into both variants, and weakens the `USB.cpp`
callback symbols. Original core files are backed up; `--restore` reverts.

The Arduino-facing API is `TinyUSB_Arduino`'s (v0.3.0), so sketches port
between the two libraries with an include-line change.

## Build

```bash
./scripts/build.sh                          # build all 48 examples (minima + nanor4)
./scripts/build.sh examples/Host_cdc_msc_hid  # build one example (both boards)
./scripts/build.sh --fork -j 4              # all 48 x 2 against the fork package
./scripts/upload_r4.sh r4_HostInfo          # compile + J-Link flash
./scripts/upload_r4.sh --fork r4_HostInfo   # ... against the fork package (route A)
./scripts/hw_test.sh r4_HostFTDI 25 c "ready"  # flash + capture Serial1 + pattern check
SERIAL_PORT=/dev/ttyUSB0 ./scripts/hw_test.sh --fork r4_HostInfo 40 i "MOUNT addr=1"
./scripts/config.sh union | set <Example> | gen | install | list
```

Requires `arduino-cli` with the `arduino:renesas_uno` core installed (1.6.0).
Run the core upgrade once after installing the core: `scripts/patch_renesas_core.sh`
(Linux), `scripts/patch_renesas_core.macos.sh` (macOS), `patch_renesas_core.windows.ps1`
(+ `.windows.bat` wrapper) on Windows. `--restore` (`-Restore` on Windows) reverts.
Patch scripts must use only stock OS utilities (no Python).

`~/Arduino/libraries/UnoR4-tinyusb` is a symlink to the repo, so the build
compiles the working tree, not a copy.

## Architecture

- `src/UnoR4TinyUSB.h` — public API: `tud_arduino_init()`,
  `tud_arduino_task()`, `tuh_arduino_init()`, `tuh_arduino_poll()`,
  `arduino_tinyusb_probe()`, `arduino_tinyusb_get_mode()`,
  `UNOR4_TINYUSB_CONSOLE` (= `Serial1`)
- `src/UnoR4TinyUSB/board_auto.h` — `UNOR4_TINYUSB_BOARD_R4` from the core's
  command-line macros
- `src/UnoR4TinyUSB/bsp_r4.cpp` — role init, `_usbfs_interrupt_handler`
  dispatch, RUSB2 register probe
- `src/UnoR4TinyUSB/bsp_common.cpp` — time API, ISR-safe debug printf, weak
  class callbacks (HID/MSC/USBTMC/host CDC/HID/MSC)
- `src/tusb_config_unor4tinyusb.h` — THE active config (self-contained)
- `scripts/patch_renesas_core.{sh,macos.sh,windows.ps1}` — core upgrade
- `scripts/renesas_core.patch` — core-file edits only (`USB.cpp`)
- `scripts/tinyusb-master/` — vendored upstream tree + `SHA256SUMS` + `PIN`
- `scripts/config.sh` — active-config management + per-example config generator
- `examples/<Name>/tusb_config.h` — per-example config. The name is deliberate:
  it is what `tusb_option.h` includes, so with the fork package it is live in the
  sketch with no copying; on the stock patched core it is inert until
  `config.sh set <Name>` copies it into the variant.
- `scripts/build_fork_package.sh` — assembles the `unor4tinyusb` board package
  (upstream core + overlay + `platform.txt` sketch-dir include) and its index
- `package_unor4tinyusb_index.json` — Boards Manager index, committed at repo root
- `scripts/hw_test.sh` — flash + Serial1 capture + pattern check for HW tests
- `examples/` — 34 `TinyUSB_Arduino` ports + 16 `r4_*` R4-specific;
  `emulators/` are RP2040 fixtures

## Code style

Arduino community consensus:
- **Allman braces** (opening `{` on its own line) for functions and control structures
- **`else`/`else if`** on separate line from closing `}`
- **2-space indent**
- **camelCase** for functions/variables, **UPPER_SNAKE_CASE** for defines
- No comments unless asked

Exception, deliberately: `scripts/config.sh` and `scripts/tinyusb-master/PIN`
are heavily commented, because they encode decisions that are expensive to
re-derive (the include-isolation constraint, the per-class RAM costs, why each
local TinyUSB deviation exists).

## Port glue rules

- Do not hand-edit `~/.arduino15/.../cores/arduino/tinyusb/` — it is replaced
  whole by the upgrade script. TinyUSB changes go into
  `scripts/tinyusb-master/` (then `cd scripts/tinyusb-master && rm -f SHA256SUMS
  && find . -type f ! -name SHA256SUMS ! -name VERSION ! -name PIN -printf '%P\n'
  | sort | xargs sha256sum > SHA256SUMS`).
- Every R4-specific TinyUSB change is baked into `scripts/tinyusb-master/` and
  documented in its `PIN` file. `renesas_core.patch` is core files only.
- My library overrides `tud_hid_descriptor_report_cb` (weak) and supplies weak
  stubs for `tud_usbtmc_*`.
- Sketches may override `tud_descriptor_device_cb`,
  `tud_descriptor_configuration_cb`, `tud_descriptor_string_cb` and
  `tud_hid_descriptor_report_cb` — all weaked by the patch. Put them in a
  separate `.cpp` tab: the `.ino` prototype generator mangles `extern "C"`.
- Sketches must NEVER define `tud_cdc_line_state_cb` /
  `tud_cdc_line_coding_cb` (owned strongly by the core's `SerialUSB.cpp`) or
  `tud_dfu_runtime_reboot_to_dfu_cb` (core-owned DFU detach path).
- Host mode: call `tuh_arduino_init()` in `setup()` — the core's
  `__USBStart()` already did `tud_rhport_init(0, DEVICE)`, so this re-inits
  rhport 0 as HOST, re-latches `DCFM`/`DRPD`, re-enables `USBFS_IRQn`.
- Device mode: call `tud_arduino_init()`. It does **not** re-initialise — the
  core did it in `_init()`. It records the role and verifies `tusb_inited()`.
- TinyUSB 0.21+ requires `tusb_time_millis_api()` / `tusb_time_delay_ms_api()`
  (no RTOS) — provided by `bsp_common.cpp` as `millis()` / `delay()`.

## Configuration model — read this before touching configs

The active config is `src/tusb_config_unor4tinyusb.h`, and it is
**self-contained for a hard reason**:

> arduino-cli compiles the core's vendored TinyUSB sources as **core**
> translation units. Measured from `--only-compilation-database`: every core TU
> (`usb/USB.cpp` and each `cores/arduino/tinyusb/**/*.c`, including
> `host/usbh.c` and `class/audio/audio_host.c`) gets **exactly five** include
> directories:
>
> ```
> -I<core>/cores/arduino/tinyusb
> -I<core>/cores/arduino/api/deprecated
> -I<core>/cores/arduino/api/deprecated-avr-comp
> -I<core>/cores/arduino
> -I<core>/variants/<V>
> ```
>
> **No sketch dir, and never `-I<library>/src`.** Library sources and the sketch
> TU add exactly one entry each: `-I<library>/src`. The sketch dir is on nobody's
> include path, *not even the sketch's own TU*, which is compiled from a copy at
> `<build>/sketch/<name>.ino.cpp` — sketch-local headers resolve from there only
> because Arduino copies the sketch's other sources next to that generated file.
>
> Reproduce with `arduino-cli compile --only-compilation-database --build-path
> <dir>`: it writes `compile_commands.json` into **<dir>** and prints nothing to
> stdout, so an empty stdout is not a failure. Entries use `arguments`, not
> `command`.
>
> (An earlier revision of this file claimed core TUs get "the sketch dir". They
> do not. The conclusion drawn from it held, but the stated reason was wrong and
> points the next design attempt at a mechanism that does not exist.)

A second constraint, discovered the same way:

> The core compiles `cores/arduino/usb/USB.cpp` and `cores/arduino/usb/SerialUSB.cpp`
> **unconditionally**. `USB.cpp`'s `__SetupUSBDescriptor()` builds a
> `TUD_CDC_DESCRIPTOR` outside any `#if` and `__USBStart()` calls
> `tud_rhport_init()`; `SerialUSB.cpp`'s `_SerialUSB` object calls
> `tud_cdc_connected/peek/available/read_char/write/write_available/write_flush`.
> So `CFG_TUD_ENABLED` must be `1` and `CFG_TUD_CDC` at least `1` in **every**
> config, including host-only ones. Setting `CFG_TUD_CDC 0` fails to compile the
> core, not just the sketch.
>
> What a per-example config really saves is the host stack (~8 KB) and the
> device classes the sketch does not use.

Consequences:

- The config cannot `#include` any helper header. It is copied verbatim into
  `variants/{MINIMA,NANOR4}/tusb_config.h`, which is what the core's
  `tusb_option.h` reads.
- The board check cannot use `UnoR4TinyUSB/board_auto.h` either; it tests
  `ARDUINO_ARCH_RENESAS` from the command line and defines
  `UNOR4_TINYUSB_BOARD_R4 1` itself (an identical redefinition in library TUs
  is benign).
- Per-example configs are **generated**, not hand-written. `scripts/config.sh`
  owns the class-selection table and rewrites only the lines between the
  `BEGIN/END GENERATED SECTION` markers. Run `./scripts/config.sh gen` after
  editing the shared part, or the 50 copies drift.
- `config.sh install` refuses to overwrite a variant config it did not write
  (it looks for the `scripts/config.sh` marker), so it cannot clobber a stock
  core.
- **The union must have its own file.** `src/tusb_config_unor4tinyusb.h` is
  both the active config *and* what `set`/`build.sh`/`upload_r4.sh` overwrite
  with an example's config. So `union` copies `scripts/tusb_config.union.h`
  back before installing. Do not "simplify" this into `install_config "$ACTIVE"`
  — that is what shipped in b118f18, and it made `union` a no-op that left
  whichever example ran last in place (the commit's config was MSC-only).
  `config.sh verify` now diffs every config's shared body against the union.

### The escape hatch is a forked core, not the include path

No `-I` can make a sketch-local `tusb_config.h` visible to core TUs, so on the
stock core there is one config per machine and switching examples means copying
files. The one exception lives in a core we control: `build.extra_flags` in
`platform.txt`. The stock core leaves it empty and sets it nowhere else —
`boards.txt` contains no `extra_flags` at all, so no board or menu overrides it.
In a fork:

```
build.extra_flags=-iquote{build.source.path}
```

`-iquote` directories are searched **before** `-I` directories for
`#include "..."`, and `build.source.path` expands to the sketch directory.
Measured on the fork: the entry is on the `USB.cpp`, `usbh.c` and `audio_host.c`
command lines ahead of the `-I` list, and it changes code generation rather than
merely resolving a header. With a sketch-local `CFG_TUH_AUDIO 1`,
`class/audio/audio_host.c.o` is 257,480 bytes defining
`audioh_init/open/close/deinit`; under the union the same TU is 40,860 bytes and
defines **zero** symbols.

`scripts/build_fork_package.sh` produces that package —
`unor4tinyusb:renesas_uno:{minima,nanor4}`, index committed at
`package_unor4tinyusb_index.json`, archive a release asset and never in git. It
is upstream + the same overlay, applied by *calling* `patch_renesas_core.sh`
through its `R4TUSB_CORE_DIR` hook, so the fork and the patched-stock path cannot
drift. Minima + Nano R4 only: the UNO R4 WiFi variant is dropped (untested here,
and its USB-C port goes through a hardware multiplexer).

Do not describe the union as "every example builds against it". It is
**86/96** of the matrix (43 of the 48 examples), and five examples fail —
measured reasons, which are not interchangeable:

| example | why it fails against the union |
|---|---|
| `Device_MSC` | `region 'RAM' overflowed by 0 bytes` — genuinely does not fit |
| `Device_NET` | needs `CFG_TUD_NCM`, deliberately absent (`NCM_DATA_PROTOCOL_NETWORK_TRANSFER_BLOCK` undeclared) |
| `Host_audio_host` | needs `CFG_TUH_AUDIO`, deliberately absent (~1.9 KB on every union build) |
| `Host_Printer` | needs `CFG_TUH_PRINTER`, deliberately absent (no upstream driver) |
| `Device_usbtmc` | needs `CFG_TUD_USBTMC`, which cannot live in a default config at all — its app callbacks have no weak default and the core links `--whole-archive`, so enabling it breaks plain Blink. See Key findings |

Enabling `CFG_TUH_AUDIO` in the union trades `Host_audio_host` for
`Host_msc_file_explorer`, `r4_HostMSC` and `r4_msc_file_explorer` — 82/96 by the
same arithmetic, not re-measured after the USBTMC change.
That is why the per-sketch config is the direction the package exists to
enable, and both paths are gated: **fork package 48 × 2 = 96 builds, 0
failures** (`build.sh --fork -j 4`, every example on its own
`examples/<Name>/tusb_config.h`, no `config.sh` step anywhere in it) and
**stock core 48 × 2 = 96 builds, 0 failures**. A fork sketch with no
`tusb_config.h` falls back to the variant union — measured 41,420 B against
`Device_CDC`'s 38,864 B with its own config, so the class selection is visible
in the binary. Peak of the fork matrix: flash 73,544 B
(`Host_msc_file_explorer`), static RAM 22,200 B (`Device_MSC`, 67%).
Those peaks are *per-example* configs. The union run's own peaks are higher —
`Host_msc_file_explorer` at 87,512 B flash and `r4_HostMSC` at 22,752 B static
RAM (69%) — which
is the union tax, paid by every sketch that does not ship its own config. The
union matrix as measured: **86/96**, failing on exactly the five examples above.

**`verify` cannot see instance counts.** It maps symbol -> macro, so it proves a
class is *enabled*, never that the count matches the interfaces the sketch
declares. `Device_hid_boot_interface` and `Device_hid_multiple_interface`
declared two HID interfaces while their configs said `CFG_TUD_HID 1`: the second
interface got no driver instance, `usbd_set_config()` failed, and the host
reported `can't set config #1, error -32` with no driver bound at all. Both
compiled cleanly and both were "verified" as enumerating. Fixed by listing them
as `HID HID`. When a sketch declares N interfaces of a class, check the count by
hand — `grep CFG_TUD_<CLASS>` against `grep -c TUD_<CLASS>_DESCRIPTION`.

`scripts/build.sh` activates each example's own config before building it, and
restores the union afterwards; `--union` builds everything against the union.
`--fork` is a different mode, not a different FQBN: it builds against the
`unor4tinyusb` package, where every sketch carries its own `tusb_config.h`, so
nothing is installed into a variant. That is why `-j/--jobs` is accepted only
with `--fork`, and refused otherwise.

`build.sh` swaps the variant configs while it runs, so **never compile anything
else at the same time** (stock path; the fork path has no shared state).
A concurrent `arduino-cli compile` that installs a
different example's config makes the matrix fail with unrelated link errors —
observed: `Device_webusb_serial`/nanor4 failing to link on `tud_vendor_n_write` because
it was built against `r4_HostInfo`'s config. Before believing such a failure,
re-run that one example with nothing else running.

`scripts/config.sh verify` (also run automatically at the end of `gen`) checks
that every class API an example **calls** is enabled by that example's config,
skipping weak-callback *definitions*. The class table and the sketches are
edited separately by hand, and two real gaps got through in 0.3.0:
`r4_HubLSDebug` called `tuh_hid_mounted()` with `CFG_TUH_HID` absent, and
`Device_dynamic_configuration` implemented the MSC callbacks with `CFG_TUD_MSC`
absent. Both compiled against the union and failed against their own config.
When adding a class call to a sketch, add the class to the table in the same
commit — `gen` will tell you if you forgot.

`scripts/config.sh` has five traps, all hit and fixed:
- `local -n tbl=$1 prefix="$2" b macro` mis-parses in bash 5.3 and leaves `b`
  unbound, tripping `set -u`. Declare the nameref in its own `local`.
- Class tokens must be filtered per side. `device_capable()` drops the four
  UART-bridge drivers and HUB (host-only); `host_capable()` drops AUDIO1,
  VENDOR, NCM, USBTMC, DFU_RUNTIME (device-only — upstream removed the vendor
  host driver entirely). Emitting the wrong side's macro makes `tusb.h` try to
  include a host header that does not exist.
- Device and host instance counts are **separate tables**. A host CDC count of
  4 says nothing about the device one; a `CLASS=N` token is a per-side cap
  below that side's default in `count_for()`.
- A trailing `# comment` on a table row is tolerated **unless it contains a
  `CLASS=N` token** — that is parsed as a real cap. Writing
  `DEVICE HID HID   # ... HID=1 stalls ...` silently produced `CFG_TUD_HID 1`.
  The row parser is token-based, so it cannot tell a comment from a directive.
 - `gen` spliced the shared body out of `$ACTIVE` — whichever config was
   installed last — while `verify` compared it against `$UNION_CFG`. So editing
   the union body and running `gen` alone wrote the **old** body into all 48
   example configs and had `verify` report 48 "hand-edited?" FAILs for files the
   same command had just written. `render_config` now reads the body from
   `UNION_CFG`. Same family of hazard: `config.sh union` propagates the union
   into `$ACTIVE` *and* both variants, so whatever that file says becomes the
   default everywhere — `git checkout scripts/tusb_config.union.h` over an
   uncommitted fix silently reinstated the config that broke plain Blink.

RAM is the binding constraint: 32 KB, statically linked, every enabled class is
paid for by every sketch. See the cost table in
`src/tusb_config_unor4tinyusb.h` and `scripts/config.sh`'s `count_for`.

## Key findings

- **NEVER enable a class in a *default* (variant) config whose application
  callbacks TinyUSB has no weak default for.** `CFG_TUD_USBTMC` is that class:
  `usbtmc_device.h` declares its callbacks plain, and `usbtmc_device.c` ships
  only 3 `TU_ATTR_WEAK` helpers, none of them the 15 distinct callbacks it calls
  at 17 sites. `usbd.c`'s driver table references the class driver struct, and
  the RA core links `core.a` with `-Wl,--whole-archive`, so `usbtmc_device.c.o`
  is in **every** image for this board. Result: an unmodified `Blink.ino` failed
  to link on both routes — 17 undefined `tud_usbtmc_*` references — while the
  whole example matrix passed, because every example includes
  `UnoR4TinyUSB.h` and so always links the weak stubs in `bsp_common.cpp`.
  `audio_device.c` and `vendor_device.c` *do* ship weak defaults (13 and 6),
  which is exactly why the union's audio and vendor classes never broke it. The
  guard is now the first thing `build.sh` does: it compiles
  `scripts/vanilla/Blink` (includes nothing) for every FQBN of the route.
  Negative-tested — re-enabling `CFG_TUD_USBTMC` in the union makes it exit 1
  reproducing those 17 undefined references before any example is built.
- **NEVER use J-Link `erase`** — it wipes the bootloader at `0x00000000`.
  Always use `r; h; loadfile` (reset, halt, program).
- **NEVER define `BOARD_TUH_RHPORT`** — it makes the core call
  `tuh_rhport_init()` inside `__USBStart()`, before the sketch has chosen a
  role, and device-mode init hangs. `BOARD_TUD_RHPORT 0` IS defined, in the
  config; the core's `USB.cpp` uses it unconditionally.
- **NEVER change `CFG_TUSB_RHPORT0_MODE`** — must stay `(OPT_MODE_DEVICE)`.
  The host stack is switched on by `CFG_TUH_ENABLED 1`; the RUSB2 HCD picks its
  PHY setup from `rusb2_is_highspeed_rhport()`, not from this macro.
- **NEVER override `__USBStart()`** — the core's version handles module clocks,
  IRQ registration, ISR vector setup, VUSB LDO, and
  `tud_rhport_init(0, DEVICE)`.
- **NEVER put `extern "C"` on `_usbfs_interrupt_handler`.** The core defines it
  in a C++ TU at file scope, outside any `extern "C"` block, so it is
  C++-mangled. Verified:
  `nm <build>/core/USB.cpp.o` → `W _Z24_usbfs_interrupt_handlerv`.
  An `extern "C"` definition in `bsp_r4.cpp` compiles, links and runs, and the
  core's weak device-only handler silently keeps winning — so host mode never
  dispatches `tuh_int_handler()`. The symbol check is one command; run it after
  touching that line. The descriptor callbacks the patch weakens
  (`tud_descriptor_*_cb`, `tud_hid_descriptor_report_cb`) **are** C linkage, so
  those do use `extern "C`.
- **R4 Minima/Nano R4 VBUS**: the R4 does NOT output VBUS (5V) on its USB
  connector. Self-powered (and FS/LS) devices detect host presence and work
  direct — no hub needed. Examples: Logitech Extreme 3D Pro (LS), LS keyboard,
  DualSense, DS4, CP2102. Bus-powered devices need backpowering.
- **USBFS limits**: `TU_ASSERT(dev_addr < 6)` and 5 bulk pipes (1–5; 6–9 are
  interrupt-only). `CFG_TUH_DEVICE_MAX` is 4 (a hub costs one of the 5
  addresses). Do NOT raise it past 4–5: at 8, enumeration dies entirely
  (suspected static RAM).
- `CDC_LINE_CONDING_STOP_BITS_1`: TinyUSB keeps the misspelled enum name as a
  compat alias of `CDC_LINE_CODING_STOP_BITS_*`; use whichever compiles.
- R4 uses bootloader at `0x00000000`. App starts at `0x4000`.
- IRQ USBFS number: 0 (vector `0x20007F40` in the RAM table at `0x20007F00`).
- **RUSB2 bit positions, verified against `rusb2_type.h` rather than guessed:**
  `RHST` is `DVSTCTR0[2:0]` (0 = nothing connected or reset not complete, 1 low,
  2 full, 3 high); `LNST` is `SYSSTS0[1:0]` (0 SE0, 1 J, 2 K) and `SYSSTS0` is at
  offset `0x04`. `SYSCFG`: `USBE` 0, `DMRPU` 3, `DPRPU` 4, **`DRPD` 5**, `DCFM` 6,
  `HSE` 7, `CNEN` 8, `SCKE` 10. `DVSTCTR0`: `UACT` 4, `RESUME` 5, `USBRST` 6,
  `RWUPE` 7, `WKUP` 8, `VBUSEN` 9, `EXICEN` 10. There is **no `SPEEDS` field**,
  and `PLLSTA` reads 0 while USB is running — printing it as a health check
  misleads. The removed `tuh_arduino_init_forced_fs()` hand-rolled its bus reset
  as `1u << 1`, which is inside `RHST[2:0]` — a read-only status field — so that
  code never asserted a reset at all; `USBRST` is bit **6**. If forced-FS is ever
  wanted again, drive bit 6 and bench it.
- **`TUSB_SPEED_FULL == 0`**, which is also the zero-initialised value of an
  unused device slot. `tuh_speed_get()` therefore cannot distinguish "nothing
  attached" from "a full-speed device": use `tuh_vid_pid_get()` and
  `tuh_mounted()` to decide whether a slot is occupied.
- **`LNST` is a live line monitor, not a state.** It reflects the instantaneous
  D+/D- condition and flips with every packet, so anything that logs on an
  `LNST` change floods: the first `r4_HostInfo` capture with a live device was
  2432 `[port]` lines out of 2467, which at 115200 loads the bus being watched.
  Log on the status bits (`RHST`, `UACT`, `RESUME`, `USBRST`, `HTACT`, `SOFEA`)
  and print `LNST` without triggering on it.
- **Host HID is not auto-rearmed.** `hidh_xfer_cb()` invokes
  `tuh_hid_report_received_cb()` and returns, so the application must call
  `tuh_hid_receive_report(dev_addr, idx)` on **every** path — including any
  rate-limiting early-return. Skipping it looks exactly like a device that
  stopped reporting: you get the first report, then silence forever.
- **`PIPECFG`/`PIPEMAXP` sit behind the `PIPESEL` window**, and the HCD does
  select-then-access (`hcd_rusb2.c`: `edpt_max_packet_size()` sets `PIPESEL`
  then reads `PIPEMAXP`). Reading them from `loop()` can land an ISR's window
  access on the wrong pipe. Take the sweep as a snapshot inside
  `__disable_irq()` and print **outside** it — one 90-character line blocks a
  115200 UART for ~8 ms, long enough to cost SOFs. `PIPE_CTR` is a real
  register array (`PIPE_CTR[9]` at 0x70) and needs no selection at all.
- **Guard class-specific host callbacks with their own `CFG_TUH_*`.** A callback
  for a driver that is not compiled in can never fire, and its API does not
  exist either — calling `tuh_hid_receive_report()` with `CFG_TUH_HID 0` fails
  to compile. `tuh_mount_cb`/`tuh_umount_cb` are always present, so build
  tooling on those and print the compiled-in class counts: "enumerated but not
  mounted" is then a config fact, not a device fault.
- **`GET_DESCRIPTOR(string, index, langid)` needs a real LANGID.** 0 is not
  one. Read the supported list with
  `tuh_descriptor_get_string_langid_sync()` and use the first entry — a stall
  caused by an invented LANGID is indistinguishable from a broken device.
- `USBFS` is event 51 in the RA event list; `bsp_r4.cpp` scans `R_ICU->IELSR`
  for it to re-enable the NVIC line.
- **HCD DIR fix**: `process_pipe0_xfer` must re-set `DCPCFG_b.DIR = 1` for OUT
  data stages. `hcd_setup_send` sets DIR based on bmRequestType, but it gets
  cleared before the data stage runs (root cause unknown — possibly ISR
  re-entrancy or register write timing). Without this fix, vendor OUT control
  transfers (e.g. CP2102 SET_BAUDRATE) stall because the hardware sends an IN
  token instead of OUT during the data stage. **Still unfixed upstream.**
- **HCD PID=NAK fix**: after the SACK ISR, the hardware may send tokens before
  `process_pipe0_xfer` writes data to the FIFO, causing BRDY to fire with stale
  pipe state (remaining=0 from hcd_setup_send). Setting `PID=NAK` in the SACK
  ISR blocks tokens; `process_pipe0_xfer` sets `PID=BUF` after data is written.
  **Still unfixed upstream.**
- **HCD status-stage NAK fix**: `process_pipe0_xfer`'s ZLP branch asserted
  `PID==NAK` before the SQSET/DIR flip. PID stays BUF after an OUT data stage
  (the IN path NAKs in `pipe0_xfer_in`, nothing NAKs the OUT path), so the ACK
  stage of control OUT transfers with data was never submitted
  (`usbh_control_xfer_cb` assert, transfer hangs). Fix: set `PID=NAK`
  explicitly instead of asserting. **Still unfixed upstream.**
- **usbh control-xfer endpoint filter**: under a backlogged event queue, a stale
  DATA completion surfaces during a LATER EP0 transfer; the single shared
  control slot cannot tell them apart (observed: 8B/9B ghosts consumed as the
  data of a 62B RANGE read, killing audio mount). `usbh_control_xfer_cb` now
  drops completions whose endpoint does not match the outstanding stage.
- **CDC host line coding**: upstream `make_new_itf` zero-inits line coding. The
  PL2303 driver's sanity gate rejects a zero `data_bits` silently, so a bare
  `tuh_cdc_set_baudrate()` never opens the port. Vendored-tree fix: default to
  8N1 @9600. Samples must then use a single `tuh_cdc_set_line_coding` call
  (2-stage on FTDI/CP210x, single request on PL2303) and wait for modem-ctrl
  completion before declaring ready.
- **MSC host transfer size**: 4 sectors (2 KB) per READ10 is what works against
  a real `0bda:0306` reader; 8 sectors (4 KB) times out. `MSCH_DATA_XFER_MAX`
  is 0xFC00, so neither size is chunked -- this is an RUSB2 HCD observation, not
  a driver one.
- **FatFS cannot mount exFAT here, and enabling it needs two flags.** The
  vendored R0.15 build has `FF_FS_EXFAT 0`. Setting it to 1 alone does **not**
  compile: exFAT code in `ff.c` dereferences `fs->lfnbuf`, which `ff.h` guards
  behind `FF_USE_LFN`, and both `ffconf.h` files set `FF_USE_LFN 0`. So exFAT
  requires `FF_FS_EXFAT 1` *and* `FF_USE_LFN 1`. This matters because a factory
  64 GB microSD is exFAT: with a card in its shipping format, `f_mount()` fails
  and the explorer reports `mount failed` while the SCSI side is perfectly
  healthy. Reformatting to FAT32 is the workaround until those flags are turned
  on together (measured headroom: 28% flash used, so there is room).
- **MSC multi-LUN**: upstream `msc_host.c` probes only LUN 0 at config and
  retries TUR+sense **unbounded**. A multi-LUN reader with an empty slot (e.g.
  Realtek `0bda:0306` with media on LUN 1) then hangs mount forever. Vendored
  tree: bounded sense retries (5), per-LUN TUR+capacity walk with `max_lun`
  clamped to `CFG_TUH_MSC_MAXLUN` (=4), mount fires when all LUNs are done
  (even with zero ready, for hot-insert + rescan). App-initiated READ_CAPACITY
  also refreshes `capacity[lun]`. `tuh_msc_scsi_command()` rejects unless the
  stage is IDLE (overlapping CBWs corrupt the in-flight command). Master's
  chunked data stage (`MSCH_DATA_XFER_MAX`) must be preserved when rebasing.
- **NEVER printf from ISR context**: `CFG_TUSB_DEBUG_PRINTF` routes to
  `arduino_debug_printf`, which ring-buffers in ISR context and is drained from
  the task pump. A direct `Serial1.write` there spins forever — the SCI IRQ can
  never preempt the USB ISR. Keep `CFG_TUSB_DEBUG 0` for operation.
- **Arduino sketch quirks (Renesas GCC 7.2 + IDE preprocessor)**:
  - The `.ino` prototype generator mangles `uint8_t const *` return types
    (`uint8_tconst`). Sketches returning descriptors use
    `typedef const uint8_t *desc_ptr_t;` as the return type.
  - No nested designated initializers (`sorry, unimplemented`). Zero the
    struct, assign fields in `setup()`.
  - The RA4M1 core compiles with GCC 7.2, so upstream's
    `defined(__has_attribute) && __has_attribute(__fallthrough__)` is fine; the
    GCC 4.8 fallthrough gate `TinyUSB_Arduino` carries is not needed.
- **Pico 2 via Debug Probe**: FQBN `rp2040:rp2040:rpipico2:usbstack=tinyusb,
  uploadmethod=picoprobe_cmsis_dap`. J-Link can also read R4 regs live
  (`JLinkExe -CommanderScript`, `mem32`).

## Testing

- Full build matrix verified: 48 examples × 2 FQBNs = **96 builds, clean**. Run
  it with no other `arduino-cli` in flight — `build.sh` swaps variant configs
  while it runs, so a concurrent compile produces bogus link failures elsewhere.
- **A board definition must not break sketches that never use this library.**
  `build.sh` compiles `scripts/vanilla/Blink` for every FQBN before anything
  else (see Key findings for why the example matrix cannot see this). The wider
  check is manual: compile all 63 examples bundled with the RA4M1 core
  (`<core>/libraries/*/examples/*`) once per route, then **compare the pass/fail
  sets**, never the pass count alone. Measured baseline, package ≡ stock:
  **30 pass / 33 fail on both routes**. The 33 are upstream examples that do not
  apply to this board — 2 × `#error "CAN1 is only available on Portenta C33."`,
  2 × `ArduinoGraphics.h: No such file or directory`, 1 × `NTPClient.h`,
  1 × `r_is_api.h`, and 27 that reference a `D` symbol not in scope. Any change
  to those sets is ours, not upstream noise. Budget ~2 GB per pass: the sweep
  leaves a build tree per example in `$TMPDIR`, and here that is a 5.6 GB tmpfs
  under a user quota, where exhaustion surfaces as `fatal error: when writing
  output to /tmp/<NNN>/sketch_merged.cpp: Disk quota exceeded` — indistinguishable
  from a broken toolchain. Delete the build dirs between passes.
- Serial1 (FTDI @ 115200) for debug output (may lose setup() output due to FTDI
  timing). Open Serial1 BEFORE the enumeration trigger — the FTDI blind window
  eats the first ~2 s.
- Device config: R4 USB -> PC (nucbox).
- Host config: R4 USB -> device via C-to-A adapter.
- Verified: with no `usb_descriptors.cpp`, the core's default descriptor
  enumerates as `2341:0069` (Nano R4: `2341:0074`) with two interfaces, CDC
  Communications + CDC Data. There is **no** vendor interface — `USB.cpp` never
  emits `TUD_VENDOR_DESCRIPTOR`, and its DFU-RT/HID/MSC blocks are conditional.
  (This line previously said `2341:0058` with "CDC + Vendor"; both were wrong.)
- Verified: `/dev/ttyACM0` appears on the host in device mode.
- Verified: CP2102 (10C4:EA60) at 115200 baud, 11.2 KB/s, 1.29 MB, zero errors.
- Verified: FTDI FT232R (0403:6001) ECHO mode via `r4_HostFTDI`, 11.3 KB/s.
- Verified: PL2303 (067B:2303), 115200 baud, 11.1 KB/s, 1.26 MB. Quirk: ~1
  single-byte gap per 40 KB at line saturation (chip-side buffers, all
  realigned, flow never stalls).
- Verified: Sony DualSense PS5 (054C:0CE6) HID in/out.
- Verified: Logitech Extreme 3D Pro (046D:C215) LS joystick.
- Verified: LS keyboard direct, boot protocol.
- Verified: Korg nanoKONTROL2 host MIDI, 1809 CC messages in ~30 s, zero errors.
- Verified: Realtek `0bda:0306` reader LUN 1, 64 GB FAT32, ~890 KB/s.
- Verified (pre-0.3.0 ports, still applicable to their `Device_*`/`Host_*`
  replacements): `hid_composite`, `hid_boot_interface`, `hid_generic_inout`,
  `msc_dual_lun` (2× FAT12), `cdc_dual_ports`, `cdc_msc`, `midi_test`,
  `audio_test` (with the `SET_CUR`/`SAM_FREQ` fix), `cdc_uac2`, `usbtmc`,
  `webusb_serial`, `dfu_runtime` (`dfu-util` lists runtime; `-e` detach
  reboots into the stock DFU bootloader via the core-owned path),
  `dynamic_configuration`, Pico 2 MSCRamDisk ~950 KB/s.
- **Host_audio_host verified against a TI PCM2704 DAC** (`08bb:2704`), which closes
  the host-audio gap. UAC1, playback-only, 6 configurations, 3 interfaces (AC,
  AS with alt 0/1/2, plus a HID volume-button interface the audio driver
  correctly ignores). Selected alt 1: stereo S16_LE @ 32000 Hz, EP 2 OUT
  adaptive-sync ISO, 192 B packets.

  **The example was capture-only and is now bidirectional.** It skipped any
  stream whose direction was not `TUH_AUDIO_STREAM_CAPTURE`, so on a
  playback-only DAC it would have mounted, enumerated, printed the stream list
  and then reported `No capture streams found` — a green light proving nothing.
  It now prefers capture (so microphones are unchanged) and falls back to
  playback, driving a 440 Hz sine via `tuh_audio_write()`.

  Result: **15 MB sustained, `short=0`, `min_avail=3`, 31877 f/s against 32000
  wanted**, and the tone confirmed by ear — steady, no clicks, over 60 s.

  - **The rate check is worth the trouble.** The endpoint's `bmaAttributes` is
    `0x00`, so `ep->sam_freq_ctrl` is false, `frequency_access` is `NONE`, and
    `audio_host.c:2100` correctly skips the rate request and streams at whatever
    the device defaults to. The reported rate is then only the first one the
    device *lists*, which need not be the one in force. But delivery is bounded
    by device consumption, not by the sketch, so `achieved` is a real
    discriminator: 31877 f/s of stereo S16 is 665 packets/s of 192 B, which is
    32 kHz. A device actually running at 48 kHz would have drawn 192000 B/s and
    the sketch would have written ~50% faster.
  - **A periodic status line starves the stream into an audible click.** This
    was the only fault found, and it was self-inflicted — a repeat of the
    CP210x pattern. `audio_host.c:588-592` answers a FIFO it cannot fill with a
    packet of silence, because isochronous OUT must continue at every interval:

    ```c
    if (tu_fifo_count(&s->edpt.ff) < bytes) {
      tu_memclr(s->edpt.ep_buf, bytes);   // one packet of zeros
    }
    ```

    That is correct USB behaviour and exactly one audible click, since 192
    zero bytes spliced into a sine wave is a hard discontinuity. The cause was
    the console, not the device: steady-state slack is ~387 B (195 B in the
    stream FIFO plus the 192 B packet in flight), which at 32 kHz stereo S16 is
    **~3 ms**, while one 76-character status line blocks a 115200 UART for
    **~6.6 ms**. A 2 s print interval therefore outlasts its own buffer, and the
    symptom was one click every 2 s. Reporting is now on demand only (`r`),
    which `hw_test.sh`'s `send-chars` can drive.
  - **`min_avail=3` is the healthy plateau, not a warning.** It reads 3 both
    before and after the fix and never drops to 0 across 15 MB, so the FIFO
    sits ~195 B ahead of each transfer and the stream is genuinely just-in-time
    but never starved. Raising `CFG_TUH_AUDIO_STREAM_BUFSIZE` above its current
    512 would be speculative: nothing measured here shows the plateau would
    move, since it is already well under the 512 B capacity. Do not "fix" this
    without first observing that a larger buffer moves the plateau.
  - The DAC declares itself bus-powered at 500 mA and was run on an injected
    5 V rail, since the R4 sources no VBUS.

- Host ports needing devices not on the bench: `Host_Printer`,
  `Host_msc_file_explorer`. `CH34x` has no hardware available, so that vendor driver
  stays unexercised.
- **64 GB microSD in a USB card reader** (Realtek `0bda:0306`, 2 LUNs, media on
  LUN 1, LUN 0 empty) — the vendored multi-LUN walk verified on real hardware:
  LUN 0 probes and is reported `no medium` without hanging, LUN 1 reports
  124,735,488 x 512 B (60,906 MB), mount fires, and reads sustain **943 KB/s**
  (`r4_msc_file_explorer dd`) / **~938 KB/s** steady state (`r4_HostMSC`, whose
  per-interval delta is a constant ~975 KB/s -- its printed figure is a
  *cumulative average*, so it decays toward the true rate and must not be read
  as a slowdown). FatFS `ls`/`cat`/`cd`/`pwd` all correct on the FAT32 volume.
- **That card reader then wedged** after sustained load. Characterised, but the
  root cause is NOT established. Measured on a freshly power-cycled reader:

  | test | result |
  |---|---|
  | 3 consecutive 4 MB reads | 925 KB/s each, all OK (12 MB cumulative) |
  | 4th 4 MB read | **fails**, wedged |
  | afterwards, 1 sector (512 B) and directory reads | all fail |

  So the limit is **cumulative volume per session** (~12-17 MB; 12 MB here,
  ~17 MB in an earlier `r4_HostMSC` run), not per-request size — a single 16 MB
  read succeeds on a fresh reader, and three reads of the *same* region all
  succeed, so it is neither size- nor address-dependent. The failure is fast, not
  a timeout: commands start being rejected or returning immediately.

  The RUSB2 registers at the wedge point are **byte-identical** to those during a
  merely-slow transfer (`INTSTS0=2080`, `pipe4 ctr=0021 pid=1`, `pipe5
  ctr=8001 pid=1`) — no stall bit, no error bit. That is consistent with the
  device simply ceasing to send data rather than the HCD recording a fault, but
  identical registers are NOT proof the HCD is innocent: there is no second reader
  to A/B against, so reader firmware, the card, and the HCD all remain
  candidates.

  Recovery: a physical power cycle works; a J-Link flash (which resets the RUSB2
  port and re-enumerates the reader) also appears to clear it. **Re-flash between
  MSC runs** — and note that once wedged, every subsequent test in that session is
  meaningless, which will otherwise look like contradictory results.

  Practical ceiling to expect when testing: budget ~10 MB per session.

- **CP2104/CP210x with TX-RX shorted — no data-path fault; the "99.9% corruption"
  was our own verifier.** 115200 8N1 + DTR/RTS, **11.3 KB/s**, and a steady
  `bad=1 err=3 rs=1`. Verified over **7 consecutive 90 s sessions** (~7 MB), 4
  through a Cynthion inline and 3 with it bypassed, all identical.

  What actually happens, deterministically, once per session:

  ```
  [TUH-CP2102] ready (baud+DTR+RTS)
  [TUH-CP2102] slip exp=03 got=00 (-3) 00 01 02 03 04 05
  ```

  **One** 3-byte slip the moment DTR/RTS is asserted — 3 bytes out of 1,038,080,
  i.e. 0.0003%. Most likely the CP210x emitting a UART start-up fragment as the
  port comes up. After that event, ~864 KB flows with zero further errors.

  It was previously recorded as "intermittent corruption, cause not
  established". That was wrong, and the chain of wrong conclusions is worth
  keeping, because each step looked reasonable:

  1. The verifier compared against a running expected byte with **no resync**, so
     that single 3-byte event desynchronised it permanently — every one of the
     ~466,000 later bytes was reported as a mismatch (`err=466237` of `466752`).
  2. A **forward-only** resync was then added, which could not recover: the data
     runs *behind* the expectation (`-3`), so the search never matched and
     `rs` stayed 0.
  3. Only **re-anchoring** on the received byte (`rx_seq_expected = rxbuf[i]`,
     counting the slip the short way round) recovers in one step -> `rs=1 err=3`.

  The apparent *intermittency* was not intermittency at all: it was which
  verifier version was running (latch -> forward-only -> re-anchoring). The FTDI
  looked clean throughout because it always had re-anchoring.

  Two dead ends investigated along the way, both disproven, do not re-chase:
  - **Not the `bLength == sizeof(tusb_desc_interface_t)` check** master added to
    `ftdi_open`/`cp210x_open`/`ch34x_open`/`pl2303_open`. Descriptor dumps show
    `bLength 9` and `sizeof(tusb_desc_interface_t)` is 9, so it passes; the other
    three are in functions this device never reaches. Master's file re-tested
    as-is passes. A `>=` relaxation was tried, measured to "fix" it, then shown to
    be a no-op, and removed again.
  - **Not stale CP210x IN-buffer state** (the obvious suspect, since a report line
    from a prior session arrived *before* `cdc mounted`) — it survived a power
    cycle and never recurred once the verifier was fixed.
  - **Not short writes.** `tu_edpt_stream_write()` can in principle return less
    than requested, but the examples guard with `write_available() >= 64` and the
    TX FIFO only drains from the ISR, so `remaining` can only grow between the
    check and the write. (The guard is still `w == 0` rather than
    `w != sizeof(txbuf)` in all three host CDC examples — weak, but not
    triggerable.)

  **Method lesson:** every one of these conclusions came from an A/B whose control
  had not been re-run, or from reading a number without asking what produced it.
  Re-run the unmodified control **last**, on a known-good device, and check
  whether the "symptom" is in the device or in the measuring code.


- **`tusb_fifo.*` is exonerated.** `common/tusb_fifo.c/.h` differ from 0.21.0
  ONLY in the license header (SPDX conversion) — zero functional change. Same
  for `cdc_host.h`, `cdc.h`, `usbh.h` and `class/cdc/serial/cp210x.h`. Do not
  re-investigate the FIFO for CDC data-integrity bugs.

- **PL2303 (`067B:2303`) with TX-RX shorted** — 115200 8N1 + DTR/RTS,
  **11.1 KB/s**, and **11 single-byte slips in 410 KB** (~1 per 37 KB), every
  one re-anchored (`err == rs`, so each cost exactly one byte). That reproduces
  the long-standing note on this device — *"~1 single-byte gap per 40 KB at line
  saturation (chip-side buffers, all realigned, flow never stalls)"* — to the
  byte. So the slip is real, pre-existing and benign, not something the master
  bump introduced.

  Two example bugs found and fixed on the way, both of which had made this
  example **impossible to run** rather than merely degraded:
  1. **It had no console output at all** — LED verdict only, so it could not be
     checked by `scripts/hw_test.sh` or in CI, unlike every other host example.
     Console added (optional; the LED verdict still stands alone, and `Serial1`
     is plain UART so the writes vanish if no bridge is wired).
  2. **The config sequence was 2-stage** (`tuh_cdc_set_baudrate()` then
     `tuh_cdc_set_line_coding()`). PL2303 takes a **single** line-coding request:
     the baud request succeeded, then the second request was dropped with **no
     completion callback at all**, so the sketch never left `PL_CONFIG` and
     re-mounted every 5 s forever. Fixed to one request, DTR/RTS chained off its
     callback. FTDI and CP210x still need the 2-stage form — this is
     device-specific, not a general rule. This one is a genuine, independently
     reproduced finding.

  The re-anchoring resync (shared with `r4_HostCP2102`) is what makes the 11
  slips survivable: one slip without it would cascade into permanent desync.

  **Re-confirmed against pristine master** (3 x 75 s, the `>=` relaxation
  removed): all three mount and sustain 11.1 KB/s with `err=11/6/8`, `rs=10/6/8`
  -- the same single-byte-slip rate, so the relaxation was never required. The
  vendor-side open question is now closed for CP210x and PL2303; **CH34x remains
  the only driver never exercised on hardware.**


- **RUSB2 diff since 0.21.0: nothing relevant on the HOST side.** Checked
  `portable/renesas/rusb2/` file by file, 0.21.0 -> master `e2048238`:
  - `hcd_rusb2.c`: the only diffs are this repo's three local deviations plus the
    `rusb2_utmi_phy_powerup()` refactor, which is entirely inside
    `#ifdef RUSB2_SUPPORT_HIGHSPEED` and therefore **not compiled** for the RA4M1
    (`RUSB2_SUPPORT_HIGHSPEED` needs RA6M5/RA6M3/`BSP_CFG_MCU_PART_SERIES == 8`;
    the RA4M1 takes the `#else`, so `rusb2_is_highspeed_rhport()` is `(false)`).
    The new `PID=NAK` that accompanies that refactor is HS-only too.
  - `rusb2_type.h`, `rusb2_rx.h`, `rusb2_common.h/.c`: license header only.
  - `rusb2_ra.h`: HS-only `RUSB2_PHYSET_CLKSEL_VALUE` mapping (fixes boards whose
    XTAL is not 12/20/24 MHz -- upstream notes EK-RA8M1 at 20 MHz "never
    locks/chirps on the default"). Real fix, irrelevant here.
  - `dcd_rusb2.c` (device side) **is** substantially rewritten, and all of it
    arrived with the master bump and is covered by the 24/24 device run:
    bounded FIFO waits (`RUSB2_FIFO_READY_SPIN`, replacing **infinite**
    `while (!FRDY) {}` that could hang the device stack), a `queued` flag
    independent of `buf` (which is NULL for zero-length reads, so the old test
    was wrong) to drive clear-stall re-arm, `zlp_pending` retry-on-BRDY,
    `DTLN > 0` deferred completion for data that already arrived, a
    `!pipe->queued` guard against acting on a stall-retired transfer,
    `TU_ASSERT(mps <= 64)` for interrupt EPs, and `PIPESEL` ordering.

  Consequence: **upstream is a dead end for the two open HOST issues** (CP210x
  intermittent corruption, cumulative MSC wedge). Neither is addressed by any
  RUSB2 change. Further diagnosis needs capture of the failing traffic, not more
  code diffing.

### Post-0.3.0 hardware re-verification

Both roles re-run on the bench after the API rewrite. The full record is in the
git log (`git log --stat` on the 0.3.x history). Note the counts below are what
was measured then; the example set has since been renamed and merged, so re-run
`scripts/dev_test.sh --all` before quoting them.

- **All 24 device examples enumerate** on a Linux host (R4 USB-C -> host).
  Functional, not just enumerated: `Device_CDC` 256-byte byte-exact round trip
  through `/dev/ttyACM0`; `Device_cdc_dual_ports` second port as `/dev/ttyACM1`;
  `Device_NET` CDC-NCM produces USB-backed netdev `enx0202846a9600` matching the
  sketch's MAC; `Device_msc_dual_lun` gives two block devices at exactly 12x512 B;
  `Device_audio_test` an ALSA `pcmC1D0c`; `Device_hid_composite` four event nodes.
- **Host mode verified**, which is what closes the `_usbfs_interrupt_handler`
  linkage risk: `r4_HostFTDI` with the FT232R `0403:6001` UART TX jumpered to RX.
  `[FTDI] cdc mounted` -> `[FTDI] device mounted` -> `[FTDI] ready` all appear,
  which is only reachable via `tuh_int_handler()` dispatching the attach event
  and `tuh_task()` draining it. 63 s ECHO: `ok=2798568 bad=0 err=0 rs=0` at
  11.3 KB/s, matching the pre-0.3.0 number exactly.
- **`Host_cdc_msc_hid` verified per class, not by "it mounted"**, because one
  sketch binding three drivers needs three separate proofs. CDC: `CDCEcho`
  `239A:CAFD` mounts, the DTR/RTS request completes, and a console -> CDC ->
  console round trip returns the payload — **with the control that makes it
  evidence**: the same payload sent while `r4_HostInfo` (no bridge) is flashed
  comes back 0 times, which is what excludes a CP210x console loopback faking
  the echo. MSC: `MSCRamDisk` `239A:CAFC`, `blocks=256 size=512`, sectors read
  back with their per-LBA pattern (`pattern=01`…`pattern=05`, `bad=0`). HID:
  `MouseMove` `239A:CAFE`, 257 throttled report lines over 28 s — the 100 ms
  ceiling, still running at the end of the window. That is the test the
  re-arm-before-throttle ordering needs: a missed `tuh_hid_receive_report()`
  gives one line and then silence forever, so a single report is not evidence.
- **A byte-wide checksum of a sector is not a check.** 512 bytes of any constant
  value sum to 0 mod 256, so summing a constant-filled test disk reports the
  same figure for every LBA and passes on a buffer the HCD never wrote into.
  This is how the merged example's first MSC verification looked green while
  proving nothing — the fixture fills LBA N with `N & 0xFF`, so compare against
  the expected pattern (or use a checksum wide enough not to alias). Generalise
  it: before trusting a per-transfer check, ask what a zero-filled or
  never-written buffer would score.
- **Bench wiring**: R4 USB-C -> target device, CP210x UART -> Serial1 pins
  (console, `SERIAL_PORT=/dev/ttyUSB0`), J-Link -> SWD. In host mode the R4 is
  NOT on the Linux host's bus, so `/dev/ttyACM*` does not exist and `lsusb`
  cannot see the target: enumeration is proven from the Serial1 banner.
- **`hw_test.sh` now builds before it captures.** The ~40 s compile used to run
  inside the capture window, so the flash landed after the capture closed and a
  short window recorded the *previous* sketch's trailing UART output. `seconds`
  is now the window from the flash onward.

### Fork package (route A) verification

`build.sh --fork -j 4`: 48 examples x 2 FQBNs = **96 builds, 0 failures**, with
no `config.sh` step anywhere in it (the stock path: also 96/96). Then one
example bench-verified on hardware under `unor4tinyusb:renesas_uno:minima`:

`r4_HostInfo` facing the Pico 2 `CDCEcho` (`239a:cafd`), via
`SERIAL_PORT=/dev/ttyUSB0 SEND_AT=14 ./scripts/hw_test.sh --fork r4_HostInfo 40 i ...`.
The capture matches the stock-core baseline line for line: `[cfg] drivers
compiled in: CDC=4 HID=4 MSC=1 HUB=1` — that is the sketch's own class set, MIDI
absent, which the union's is not, so it also proves the sketch-local config was
the one in force — then `SYSCFG 0411 -> 0461`, the five-line attach sequence
(`LNST=J` -> `USBRST=1` -> `SE0` -> `RHST=FULL` -> `SOFEA=1`), `CDC mounted
idx=0`, `MOUNT addr=1 speed=FULL`, LANGID `0x0409` read back, product string
`CDC Echo`, `wTotalLength=75`, `ifc 0 class=02.02.00 nEP=1`, `ifc 1
class=0a.00.00 nEP=2`, `walked 75 bytes: 2 interfaces, 3 endpoints` — once from
the mount handler and once from the `i` command, so Serial1 RX works too.

**The cheap equivalence check — run it after touching either path:** build the
same example both ways and `cmp` the `.hex`. Swept across both matrices, all
**48 examples x 2 FQBNs = 96 pairs are byte-identical** between route A (fork
package + sketch-local config) and route B (patched stock core + variant config)
— `r4_HostInfo` alone is 202,662 B, sha256 `37412322...`, which moved when the
forced-FS API went because `tuh_arduino_poll()` is linked into every host
sketch. Same sources and same config content, so any difference means the two
paths drifted: a stale variant install, a package that was not rebuilt after an
edit, or a config that stopped being shared. Far stronger than comparing sizes
or percentages, which would miss a same-size substitution. What it does NOT
cover: the IDE upload path under the package (untested; the `upload.*` keys
are inherited unchanged from upstream `boards.txt`) — installing the package
in Arduino IDE 2.x as an anonymous user *is* now verified, see below — and
anything about the device's behaviour beyond what the identical
image implies. Sweep cost is zero compiles if both matrices' `build/` trees are
still on disk:

```bash
for board in minima nanor4; do for d in build/arduino:renesas_uno:$board/*/; do
  ex=$(basename "$d"); cmp -s "$d/$ex.ino.hex" \
    "build/unor4tinyusb:renesas_uno:$board/$ex/$ex.ino.hex" || echo "DIFFER $board/$ex"
done; done
```

(`build/` accumulates dirs of deleted examples — 88 such pairs had no route-A
counterpart: `HostForcedFS`, `hid_composite`, `midi2_*` and friends. They are
gitignored clutter, not mismatches; `rm -rf build/*` before a sweep if the noise
distracts.)

Two ways this sweep lies, both hit:

- **`build.sh --union` writes into the same `build/<fqbn>/<example>/` dirs as the
  per-example run**, so running `--union` after the stock matrix silently
  replaces route B's per-example hexes with union-built ones. The sweep then
  reports almost everything as DIFFER (measured: 104 of 144) while the two
  routes are in fact equivalent. Run it straight after the per-example stock
  matrix, or from an empty `build/` with `--fork` then stock and nothing else.
- The comparison is only meaningful if the inputs were both rebuilt: after an
  edit to the union body, `config.sh union` must run before the package build,
  or the package keeps the old variants and reproduces its old sha. That is the
  drift detector working as intended, but it reads like "my edit did nothing".

**The archive itself is reproducible** (`tar --sort=name --mtime="@N" --utc
--owner=0 --group=0 --numeric-owner` piped through `gzip -n -9`), so two builds
of the same content yield the same sha256. That makes it a *second* drift
detector, complementary to the hex sweep: rebuild and the sha unchanged means
your edit never reached the package (stale vendored tree, patch that did not
apply, config not regenerated); sha changed with no content change means
something non-deterministic got in — a timestamp, a username, or directory
order. Needs GNU tar (bsdtar has neither `--sort` nor `--mtime=@N`), and the
script refuses rather than quietly shipping a non-reproducible archive.

Two timestamp traps, both hit:

- `--mtime="UTC @$EPOCH"` is **not a valid date string**. tar substitutes
  `INT64_MIN`, every entry lands at `-9223372036854775808`, and it is still
  byte-reproducible — so the reproducibility check passed while every archive
  this script shipped until the fix (`b330cd55`, `2307fcb8`) carried underflowed
  mtimes, and the log line printed the intended `PACKAGE_DATE` anyway. A user
  running `tar -tzvf` on the release artifact got an "implausibly old time
  stamp" warning on all ~1130 entries. The spelling is `--mtime=@N` with
  `--utc`; verified here: `"UTC @1791590400"` → `INT64_MIN`,
  `"@1791590400" --utc` → `2026-10-10T00:00:00Z`. Reproducibility cannot detect
  this, so the script now greps the listing for the underflow sentinel and dies.
- Do not render `$EPOCH` with bash's `printf '%(...)T'`: it prints LOCAL time
  (this box is UTC-10, so it labelled a UTC-suffixed timestamp 10 hours out) and
  its `%!` UTC flag is not parsed — use `date -u -d "@$EPOCH"`.

**Installing the board package works for someone with no GitHub account.** On
Arduino IDE 2.x, adding the Boards Manager URL and installing
**UnoR4 TinyUSB Boards** completes with no errors: the index resolves
anonymously, the archive's SHA-256 matches, the unpack succeeds, and the
`platform.txt` tool-dependency declarations (`packager=arduino`) resolve. What
that does *not* cover is **Verify** (compile) and **Upload** inside the IDE
under the package — still untested, and the remaining gap in this section.

**Publish order matters, because a live index points at an archive it does not
host.** The index's `url` is a download URL; the archive itself lives wherever
it is published, not in this repo (`dist/` is a local, gitignored build
directory). So: publish the artifact and confirm its URL answers **200
anonymously** *before* pushing an index or tree that changes it — the reverse
order 404s an install that was working, and Boards Manager will report it as a
checksum or download failure rather than "you published out of order".
Publishing here (release creation, force-push over the public branch) is the
user's action, not the agent's.

**`set -u` does not protect a backgrounded command.** `python3 ... "$send_at" &`
with `send_at` unbound killed only the subshell: the script went on and flashed
as if all were well, and `(no capture)` was the only visible symptom, with the
traceback buried above the J-Link output. Check argument names on any `... &`
line, and treat a 0-byte capture as a harness failure — never as "the device
said nothing", which is exactly how a dead harness gets mistaken for a dead
device.

### Device-mode verification — `scripts/dev_test.sh`

`hw_test.sh` proves host-mode examples, because in host mode the R4 is not on
the Linux bus at all. In device mode it **is**, so the check that matters is
host-side: does the class come up and carry data. `dev_test.sh` installs the
example's own config, flashes over J-Link, waits for the VID:PID the sketch's
own descriptor declares, resolves the driver nodes the kernel created for that
device, and runs a class-specific check (`evdev` events, CDC byte round-trip,
MSC ramp read-back with `dd`, `arecord` at the device's declared rate, rawmidi
note bytes, USBTMC `*IDN?`, BOS platform caps, vendor bulk echo, `dfu-util`).
`--list` shows the checks; `--all` runs them and restores the union.

Bench: R4 USB-C -> this Linux machine, CP210x on Serial1, J-Link on SWD.

sysfs and driver traps, each of which produced a false FAIL on working
firmware before being fixed:

- **Never hardcode node names.** Card/event/tty/sd numbers are reassigned in
  probe order. Locate the device by VID:PID under `/sys/bus/usb/devices/*` and
  derive everything from there.
- **`select()` on `/dev/usbtmcN` never reports readable**, so a select-driven
  probe never calls `read()` — and the kernel only submits the bulk-IN URB from
  inside `read()`. The device sits with a correct reply nobody fetched, which
  is indistinguishable from a device that never answers. Call `read()` directly
  with `O_NONBLOCK` and tolerate `EAGAIN`. (This cost a full diagnosis cycle:
  the `Device_usbtmc` example was pronounced broken, and was not.)
- `/dev/usbtmcN` is flat here, not `/dev/usb/usbtmc/N`, and is `0600 root` —
  needs `sudo`. Raw block devices likewise need the `disk` group, which this
  account is not in: `blockdev`/`dd` failing with EACCES looks like "no block
  device".
- Python's recursive `glob` **hangs on sysfs** (it follows the parent symlinks
  sysfs is full of). `os.walk` does not follow them. `find` needs `-H` because
  `/sys/bus/usb/devices/<N>` is itself a symlink — with the default `-P` it
  reports the link and descends no further, so every lookup silently returns
  nothing. `event9`/`ttyACM0`/`hidraw0` are **directories**, so `-type d` is
  required or the matches are `uevent`/`power/*` files.
- This pyusb has no `.port` or `.sys_name`; use `bus` + `port_numbers`.
- `dfu-util` prints `Found Runtime:` for a runtime-DFU interface, not `DFU mode`.
- `arecord`/`aplay` produce **zero bytes** (not an error) when asked for a rate
  or channel count the device does not declare. `Device_cdc_uac2` is 48 kHz mono.
- Compare descriptor bytes against the vendored macros in
  `scripts/tinyusb-master`, never against constants typed from memory: the
  platform-capability UUID TinyUSB emits is not the byte order the spec's GUID
  literal suggests, and the capability type sits at `cap[2]`, not `cap[3]`.
- The sysfs attribute is `bos_descriptors`, not `bosdesc`.

**Method lesson, again:** every one of these was a fault in the measuring code,
and in the USBTMC case the "fix" I wrote was validated against the broken probe
rather than against a re-run of the unmodified example. Re-run the unmodified
control **last**, and confirm the symptom is in the device, not the harness.

## Agent rules

- **Do not commit without permission** — never run `git commit`, `git commit --amend`, `git push`, or `gh pr create` without explicit user approval.
