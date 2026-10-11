/*
 * UnoR4TinyUSB.h — Unified public API for UnoR4-tinyusb.
 *
 * A TinyUSB device + host stack for the Arduino Renesas RA4M1 boards
 * (Uno R4 Minima, Nano R4), with the same API shape as TinyUSB_Arduino:
 *
 *     #include <UnoR4TinyUSB.h>
 *
 *     void setup() {
 *       UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);
 *       tud_arduino_init();          // or tuh_arduino_init()
 *     }
 *     void loop() {
 *       tud_arduino_task();           // or tuh_arduino_poll();
 *     }
 *
 * Board selection is automatic via UnoR4TinyUSB/board_auto.h, or forced with
 * -DUNOR4_TINYUSB_BOARD_R4.
 *
 * The R4 has a single USB controller (RUSB2, full-speed only), so one sketch
 * is either a DEVICE or a HOST per session — never both. Unlike the other
 * TinyUSB_Arduino boards, the RA4M1 core brings the DEVICE stack up itself
 * during _init(), before setup() runs; see tud_arduino_init() below.
 */

#pragma once

// Auto-detect first: explicit -D / #define wins, otherwise core macros decide.
#include "UnoR4TinyUSB/board_auto.h"

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif
  #include "tusb.h"
#ifdef __cplusplus
}
#endif

// ---- Console ---------------------------------------------------------------
// R4 has no USB-serial chip: the board's own USB connector is the RUSB2 port,
// which TinyUSB owns in device mode. The console is therefore the Serial1 UART
// (an external FTDI/CP210x bridge on RX1/TX1). Getting this wrong is silent:
// sketch Serial.begin() opens the native-USB CDC, whose 64-byte TX buffer
// swallows the debug banner and then deadlocks setup() in Serial_::write —
// no USB, no console, stack pointer inside Print::write.
#if !defined(UNOR4_TINYUSB_CONSOLE)
  #define UNOR4_TINYUSB_CONSOLE Serial1
#endif

#if !defined(UNOR4_TINYUSB_BAUD)
  #define UNOR4_TINYUSB_BAUD 115200
#endif

// ---- Init / task API -------------------------------------------------------
// Role guards: a specialized config (one copied over
// src/tusb_config_unor4tinyusb.h) compiles in only the role it names, so the
// matching entry points do not exist. Guarding the declarations turns "built
// the wrong example against a specialized config" into a compile error on the
// call itself instead of an undefined reference at link time or, worse, a
// silently empty function body.
#ifdef __cplusplus
extern "C" {
#endif

#if CFG_TUD_ENABLED
/**
 * Enter device mode.
 *
 * On the RA4M1 core this is mostly a bookkeeping call: the core's
 * __USBStart() (run from _init(), before setup()) already did
 * tud_rhport_init(0, DEVICE), and that function is what configures the module
 * clocks, registers USBFS_IRQn, sets up the ISR vector and enables the VUSB
 * LDO. Overriding __USBStart() is therefore forbidden — see AGENTS.md.
 *
 * Must still be called from setup() so the library knows the sketch wants
 * DEVICE mode and starts pumping tud_task().
 */
void tud_arduino_init(void);

/**
 * Device task pump. Call in loop() instead of bare tud_task().
 * On this board it is a tud_task() wrapper plus a drain of any debug output
 * the USB ISR queued.
 */
void tud_arduino_task(void);
#endif // CFG_TUD_ENABLED

#if CFG_TUH_ENABLED
/**
 * Enter host mode on rhport 0.
 *
 * The core already brought rhport 0 up as DEVICE, so this re-inits it as
 * HOST, re-asserts DCFM/DRPD (something after hcd_init() clears DCFM back to
 * device mode) and re-enables USBFS_IRQn, which hcd_port_reset() disables.
 */
void tuh_arduino_init(void);

/**
 * Host task pump. Call in loop() instead of bare tuh_task().
 * Keeps RUSB2 latched in host mode and re-enables the USB IRQ, then runs
 * tuh_task().
 */
void tuh_arduino_poll(void);
#endif // CFG_TUH_ENABLED

/**
 * Print an RUSB2 register snapshot to the console. Role-agnostic: always
 * available, whichever side the config compiles in.
 */
void arduino_tinyusb_probe(void);

#ifdef __cplusplus
}
#endif

// ---- Mode enum -------------------------------------------------------------
typedef enum {
  UNOR4_TINYUSB_MODE_UNINITIALIZED = 0,
  UNOR4_TINYUSB_MODE_DEVICE,
  UNOR4_TINYUSB_MODE_HOST
} UnoR4TinyUSBMode;

/** Which role the library is currently running as. */
UnoR4TinyUSBMode arduino_tinyusb_get_mode(void);