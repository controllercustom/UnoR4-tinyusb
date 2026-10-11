/*
 * TinyUSB configuration for the Uno R4 Minima / Nano R4 — THE ACTIVE CONFIG.
 *
 * One file, and it is deliberately SELF-CONTAINED. That is not a style choice:
 * arduino-cli compiles the RA4M1 core's own vendored TinyUSB sources (device/usbd.c,
 * host/usbh.c, class/*) as core translation units, and those TUs get exactly five
 * include directories — cores/arduino/tinyusb, the two api/deprecated dirs,
 * cores/arduino, and variants/<V>. Not the library's src/, and not your sketch
 * folder. So this file is copied verbatim into
 * <core>/variants/{MINIMA,NANOR4}/tusb_config.h by
 * scripts/patch_renesas_core.sh, which is what tusb_option.h actually reads.
 * A config that #included a helper header would resolve for the sketch and
 * fail with "No such file or directory" inside cdc_device.c.
 *
 * Default: the full union — device AND host stacks, every class the R4 can
 * afford inside 32 KB. It is NOT universal: 46 of the 50 bundled examples build
 * against it and four do not — Device_MSC (RAM overflow), Device_NET (wants
 * CFG_TUD_NCM), Host_audio_host (wants CFG_TUH_AUDIO), Host_Printer (wants
 * CFG_TUH_PRINTER). Those four ship their own config; the exclusion list below
 * says why their classes are left out here.
 *
 * SPECIALIZING FOR ONE SKETCH (this is how you cut Flash and RAM)
 * -------------------------------------------------------------
 * Every example ships a ready-made single-role config next to its .ino, named
 * tusb_config.h — the name tusb_option.h looks for. Whether that file is live
 * depends on which board package you use:
 *
 *   UnoR4 TinyUSB package (unor4tinyusb:renesas_uno:*)
 *       LIVE AS-IS. Its platform.txt puts the sketch folder on the include path
 *       (-iquote{build.source.path}, searched before -I), so the copy beside
 *       your .ino wins for every translation unit, the core's own included.
 *       Nothing to copy, no global state, and each sketch carries its own
 *       classes. This is the intended flow.
 *
 *   Stock Arduino core, patched in place (arduino:renesas_uno:*)
 *       INVISIBLE — the sketch folder is on nobody's include path. Activate it:
 *
 *         ./scripts/config.sh set Device_CDC
 *
 *       or by hand; `install` is the step that writes both variant configs
 *       (there is no --config-only option):
 *
 *         cp examples/Device_CDC/tusb_config.h \
 *            src/tusb_config_unor4tinyusb.h
 *         ./scripts/config.sh install
 *
 * Mixing and matching classes is just editing the CFG_TUD_* / CFG_TUH_* lines
 * in your copy: set a class to 1 to compile it in. Anything you leave out
 * defaults to 0 (tusb_option.h), so it is not compiled at all.
 *
 * TO GO BACK TO THE UNION
 *   ./scripts/config.sh union
 *
 * Stock-core path only — a sketch-local config never touches the variant copy
 * that the package falls back to. This is required on the stock core before
 * building a different example: a device-role config has CFG_TUH_ENABLED 0, so
 * host examples stop compiling (loudly — see the role guards in
 * src/UnoR4TinyUSB.h), and vice versa.
 *
 * WHY A COPY ON THE STOCK CORE?
 * The five-include-directory rule at the top: a config next to your .ino is
 * invisible there. Overcoming it is what the unor4tinyusb package buys
 * (scripts/build_fork_package.sh), and it is not merely a header-resolution
 * detail. Measured with a sketch-local CFG_TUH_AUDIO 1, class/audio/audio_host.c.o
 * is 257,480 bytes and defines audioh_init/open/close/deinit, while under the
 * union the same TU is 40,860 bytes and defines no symbols at all.
 *
 * Per-example configs are GENERATED, not hand-written: scripts/config.sh owns
 * the class-selection table and rewrites only the lines between the
 * CFG_TUD_ENABLED / CFG_TUH_ENABLED markers below. Regenerate them all with
 *   ./scripts/config.sh gen
 * after changing anything in this file, so the 50 copies cannot drift.
 *
 * ---------------------------------------------------------------------------
 * RAM BUDGET — read this before adding a class
 * ---------------------------------------------------------------------------
 * The R4 has 32 KB of SRAM, statically linked, and TinyUSB sizes its buffers
 * and slot arrays for EVERY enabled class in EVERY sketch. Measured:
 *
 *   device sketch (CDC only)           5.8 KB static (17%)
 *   device sketch (CDC + Vendor)       6.3 KB static (19%)
 *   host sketch (CDC + 4 UART          12.9 KB static (39%)
 *                 bridge drivers)
 *   host sketch + MSC + FatFS          12.3 KB static (37%)
 *   device MSC, single 8 MB RAM disk  22.2 KB static (67%)  <- worst case
 *
 *   (all 48 examples, via scripts/build.sh on both FQBNs. Peak flash is
 *    Host_msc_file_explorer at 73,544 B of 262,144 B, peak static RAM 22,200 B
 *    of 32 KB. The union itself builds 43 of the 48; see "Deliberately NOT
 *    enabled" below.)
 *
 * Enabling a class is never free:
 *
 *   class                static cost        note
 *   -------------------  -----------------  ---------------------------------
 *   CFG_TUD_CDC = 2       ~0.7 KB           2 needed for Device_cdc_dual_ports
 *   CFG_TUD_HID = 2       ~0.3 KB           2 needed for Device_hid_composite
 *   CFG_TUH_CDC = 4       ~3.3 KB           + the four UART drivers below
 *   CFG_TUH_HID = 4       ~1.6 KB           DualSense reports are large
 *   CFG_TUH_HUB = 1       ~0.6 KB           costs one of 5 device addresses
 *   CFG_TUH_MSC + 4 LUN   ~1.4 KB           +24 B/LUN for capacity[]
 *   CFG_TUD_MSC           ~2.2 KB           two 512 B EP buffers
 *   CFG_TUD_AUDIO         ~1.1 KB           392 B SW FIFO + 98 B EP
 *   CFG_TUD_USBTMC        ~0.6 KB           off, see "Deliberately NOT enabled"
 *   CFG_TUD_VENDOR        ~1.0 KB
 *   CFG_TUH_AUDIO         ~1.3 KB
 *
 * Deliberately NOT enabled here:
 *   CFG_TUD_USBTMC      The one class here whose application callbacks do not
 *                       exist anywhere unless the sketch writes them.
 *                       usbtmc_device.h declares them plain (no TU_ATTR_WEAK),
 *                       and class/usbtmc/usbtmc_device.c ships only 3 weak
 *                       helpers, none of them the callbacks it calls: 15
 *                       distinct ones at 17 call sites. The class driver struct
 *                       is referenced from usbd.c's driver table, and the RA
 *                       core links core.a with -Wl,--whole-archive, so
 *                       usbtmc_device.c.o is in EVERY image built for this
 *                       board. Measured: an unmodified Blink fails to link under
 *                       BOTH the board package and the patched stock core. Audio
 *                       and Vendor are enabled above and do not break Blink for
 *                       exactly this reason -- audio_device.c and
 *                       vendor_device.c define TU_ATTR_WEAK defaults (13 and 6).
 *                       Any default config for this board must keep USBTMC off;
 *                       Device_usbtmc ships its own config with the class on.
 *   CFG_TUH_AUDIO       measured cost ~1.9 KB on EVERY union build, which tips
 *                       Host_msc_file_explorer, r4_HostMSC and r4_msc_file_explorer
 *                       into `region 'RAM' overflowed`. Union builds go from
 *                       92/100 passing to 88/100 with it on: Host_audio_host is
 *                       gained, three MSC examples lost. Host_audio_host ships its
 *                       own config. The sizing block below stays so it is one
 *                       line to enable in a per-sketch config.
 *   CFG_TUH_PRINTER     local backport (absent upstream); enabled only by
 *                       Host_Printer's own config.
 *   CFG_TUH_MIDI2       no R4 example uses it; ~1 KB of state for nothing.
 *   CFG_TUD_NCM / ECM   Device_NET needs CFG_TUD_NCM (two ~4 KB buffers) and
 *                       will not fit alongside the host stack in 32 KB, so it
 *                       ships its own config.
 *   BTH / MTP / VIDEO   no example, no way to fit them.
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// ==== BEGIN GENERATED SECTION (scripts/config.sh) ====
// Board check. board_auto.h is NOT reachable from here: the core's TinyUSB
// translation units get no -I for the library, so derive the board from the
// core's own command-line macro, which is present in every TU. The library's
// own sources get the same value from UnoR4TinyUSB/board_auto.h, and an
// identical redefinition is benign.
#if !defined(ARDUINO_ARCH_RENESAS)
  #error "Unsupported board: this config is for the Arduino Renesas RA4M1 core (renesas_uno). No ARDUINO_ARCH_RENESAS on the command line."
#endif
#ifndef UNOR4_TINYUSB_BOARD_R4
  #define UNOR4_TINYUSB_BOARD_R4 1
#endif

// ---- Device stack ----
#define CFG_TUD_ENABLED                      1  // required by the core: USB.cpp / SerialUSB.cpp
#define CFG_TUD_CDC                          2
#define CFG_TUD_HID                          2
#define CFG_TUD_MSC                          1

// ---- Host stack ----
#define CFG_TUH_ENABLED                      1
#define CFG_TUH_CDC                          4
#define CFG_TUH_HID                          4
#define CFG_TUH_HUB                          1
#define CFG_TUH_MSC                          1
// ==== END GENERATED SECTION ====

// ---------------------------------------------------------------------------
// Everything below is IDENTICAL in every generated config. Change it here and
// run `./scripts/config.sh gen` to push it into all 50 per-example copies.
// ---------------------------------------------------------------------------

// ---- MCU / RHPort -----------------------------------------------------------
// CFG_TUSB_MCU is NOT defined here: boards.txt passes -DCFG_TUSB_MCU=OPT_MCU_RAXXX
// on the command line for every renesas_uno target, and redefining it would be
// a (harmless but noisy) duplicate definition.
//
// CFG_TUSB_RHPORT0_MODE must stay (OPT_MODE_DEVICE). Do NOT "helpfully" add
// OPT_MODE_FULL_SPEED or OPT_MODE_HOST:
//  - CFG_TUH_ENABLED above is what actually switches the host stack on, and it
//    is also what the UnoR4TinyUSB.h role guards key off.
//  - The RUSB2 HCD is full-speed only and picks its PHY setup from
//    rusb2_is_highspeed_rhport(), not from this macro.
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)

// R4 core uses a bare-metal loop, no RTOS.
#ifndef CFG_TUSB_OS
  #define CFG_TUSB_OS           OPT_OS_NONE
#endif

// rhport 0 is the only RUSB2 port. The core's USB.cpp uses BOARD_TUD_RHPORT
// unconditionally and guards its host block on BOARD_TUH_RHPORT, which must stay
// UNDEFINED: defining it would make the core call tuh_rhport_init() during
// __USBStart(), before the sketch has chosen a role, and device-mode init hangs.
#ifndef BOARD_TUD_RHPORT
  #define BOARD_TUD_RHPORT     0
#endif

// EP0. The R4's USBFS endpoint-0 max packet size is 64 in both directions.
#ifndef CFG_TUD_ENDPOINT0_SIZE
  #define CFG_TUD_ENDPOINT0_SIZE    64
#endif

// ---- Event queues -----------------------------------------------------------
// Deep, and NOT to be trimmed. Enumeration on the R4 is bursty enough to
// overflow the upstream default of 16, which silently drops SETUP /
// xfer-complete events: the device ACKs setups in hardware, software never sees
// them, and the host sees endless NAKs on DATA INs.
// 64 entries costs ~768 B of the 32 KB SRAM. Keep it.
#define CFG_TUD_TASK_QUEUE_SZ   64
#define CFG_TUH_TASK_QUEUE_SZ   64

// ---- Host limits (hardware, not preference) --------------------------------
// The RUSB2 HCD hard-asserts dev_addr < 6 and has 5 bulk pipes (1..5; 6..9 are
// interrupt-only). These caps are structural:
//  - CFG_TUH_DEVICE_MAX 5: a hub counts as one, so a hub plus N devices cannot
//    exceed 5 addresses. Measured: 2 CDC bridges through a hub both open; a
//    third enumerates but cannot open.
//  - Do NOT raise CFG_TUH_DEVICE_MAX past 4-5. At 8 enumeration dies entirely
//    (root cause unknown, suspected static RAM: the CDC and device slot arrays
//    are linked for every sketch regardless of use).
#ifndef CFG_TUH_DEVICE_MAX
  #define CFG_TUH_DEVICE_MAX        4
#endif

// Multi-LUN readers (Realtek 0bda:0306 puts the card on LUN 1 and leaves LUN 0
// empty). The msc_host multi-LUN walk costs +4 B static per extra LUN.
#ifndef CFG_TUH_MSC_MAXLUN
  #define CFG_TUH_MSC_MAXLUN        4
#endif

#if CFG_TUD_ENABLED
// Device buffer sizes
#define CFG_TUD_CDC_RX_BUFSIZE   256
#define CFG_TUD_CDC_TX_BUFSIZE   256
#define CFG_TUD_MSC_BUFSIZE      512
#define CFG_TUD_MSC_EP_BUFSIZE   512
#define CFG_TUD_MIDI_RX_BUFSIZE  256
#define CFG_TUD_MIDI_TX_BUFSIZE  256
#define CFG_TUD_VENDOR_RX_BUFSIZE 512
#define CFG_TUD_VENDOR_TX_BUFSIZE 512

// Printer device FIFOs. Macro-only: zero cost unless CFG_TUD_PRINTER > 0, but
// printer_device.c references them unconditionally inside its own guard, so
// they must exist. The HOST printer driver needs no equivalent (it streams on
// the bulk endpoints).
#define CFG_TUD_PRINTER_RX_BUFSIZE 256
#define CFG_TUD_PRINTER_TX_BUFSIZE 256
#endif

// ---- Audio device -----------------------------------------------------------
// Mono 16-bit 48 kHz IN only. Macro-only: zero cost unless CFG_TUD_AUDIO > 0.
// FS isochronous EP size = (48000/1000 + 1) * 2 B * 1 ch = 98 B; the SW FIFO is
// 4 frames.
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE            48000
#define CFG_TUD_AUDIO_ENABLE_EP_IN                  1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX  2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX          1
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX           98
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ        392
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX          98
#define CFG_TUD_AUDIO_FUNC_2_EP_IN_SZ_MAX           98
#define CFG_TUD_AUDIO_FUNC_2_EP_OUT_SZ_MAX          98
#define CFG_TUD_AUDIO_FUNC_3_EP_IN_SZ_MAX           98
#define CFG_TUD_AUDIO_FUNC_3_EP_OUT_SZ_MAX          98
#define CFG_TUD_AUDIO_EP_SZ_IN                      98

#if CFG_TUH_ENABLED
// Host buffer sizes. 512 B measured 11.2 KB/s of real UART throughput on
// CP2102 and 11.3 KB/s on FT232R (J-Link + PL2303 console), zero errors over
// 1.29 MB. Going larger does not raise that: the RUSB2 FIFO depth, not the
// software buffer, is the limit.
#define CFG_TUH_CDC_RX_BUFSIZE   512
#define CFG_TUH_CDC_TX_BUFSIZE   512
#define CFG_TUH_MIDI_RX_BUFSIZE  256
#define CFG_TUH_MIDI_TX_BUFSIZE  256
#define CFG_TUH_MSC_EP_BUFSIZE   512
#define CFG_TUH_HUB_BUFSIZE      64

// CFG_TUH_CDC_LINE_CODING_ON_ENUM is deliberately UNDEFINED. Setting it makes
// every enumerated CDC device receive a SET_LINE_CODING during host config, and
// a control OUT transfer with data is exactly the case the RUSB2 HCD needs its
// DIR / PID=NAK fixes for. The port still mounts without it; the sketch sets
// the baudrate once the device is up (and cdc_host.c now initialises line
// coding to 8N1, so a bare tuh_cdc_set_baudrate() is not silently rejected by
// the PL2303 driver's sanity gate).

// Host audio config (macro-only; ~1.3 KB static when CFG_TUH_AUDIO > 0).
#define CFG_TUH_AUDIO_PROTOCOLS  (TUH_AUDIO_PROTOCOL_UAC1 | TUH_AUDIO_PROTOCOL_UAC2)
#define CFG_TUH_AUDIO_MAX        1
#define CFG_TUH_AUDIO_MAX_SAM_FREQ 5
#define CFG_TUH_AUDIO_MAX_AS     2
#define CFG_TUH_AUDIO_EPIN_BUFSIZE   256
#define CFG_TUH_AUDIO_STREAM_BUFSIZE 512
#endif // CFG_TUH_ENABLED

// ---- Debug ------------------------------------------------------------------
// Off by default (upstream default). TU_LOG runs inside the USB ISR, and a
// ~40-char line at 115200 baud blocks for ~3.5 ms — longer than a 1 ms
// isochronous audio frame.
// #ifndef-guarded, so -DCFG_TUSB_DEBUG=2 re-enables it for bring-up without
// editing files. CFG_TUSB_DEBUG_PRINTF stays outside the guard:
// arduino_debug_printf() (src/UnoR4TinyUSB/bsp_common.cpp) must resolve even
// while logging is off, or the opt-in fails at link.
//
// When debug IS enabled, the ring-buffered printf is what keeps it safe: it is
// drained from loop() rather than written inline, because Serial1.write()
// blocks on the FSP SCI TX-done interrupt that cannot preempt the USB ISR.
#ifndef CFG_TUSB_DEBUG
  #define CFG_TUSB_DEBUG           0
#endif
#define CFG_TUSB_DEBUG_PRINTF    arduino_debug_printf

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
