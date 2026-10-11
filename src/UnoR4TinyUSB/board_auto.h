/*
 * board_auto.h — board detection for UnoR4-tinyusb.
 *
 * Explicit -DUNOR4_TINYUSB_BOARD_* (or a sketch-level #define before any
 * include) always wins. Otherwise the board is inferred from the core's own
 * command-line macros, which are present in EVERY translation unit (sketch and
 * library alike):
 *
 *   Uno R4 Minima : ARDUINO_UNO_R4_MINIMA / ARDUINO_ARCH_RENESAS
 *   Nano R4       : ARDUINO_NANO_R4 / ARDUINO_ARCH_RENESAS
 *
 * Standalone: no Arduino.h / tusb dependency, safe from C and C++, safe to
 * include before anything else.
 */

#ifndef UNOR4_TINYUSB_BOARD_AUTO_H
#define UNOR4_TINYUSB_BOARD_AUTO_H

#if !defined(UNOR4_TINYUSB_BOARD_R4)

#if defined(ARDUINO_ARCH_RENESAS) || defined(ARDUINO_ARCH_RENESAS_UNO) || \
    defined(ARDUINO_UNO_R4_MINIMA) || defined(ARDUINO_NANO_R4)
  #define UNOR4_TINYUSB_BOARD_R4 1
#endif

#endif /* no explicit board define */

// Exactly one board must be selected after auto-detect.
#if defined(UNOR4_TINYUSB_BOARD_R4) > 1
  #error "Define exactly ONE UNOR4_TINYUSB_BOARD_*"
#endif

#endif /* UNOR4_TINYUSB_BOARD_AUTO_H */