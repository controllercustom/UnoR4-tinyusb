/*
 * bsp_common.h — shared declarations for the UnoR4-tinyUSB board support.
 */

#ifndef UNOR4_TINYUSB_BSP_COMMON_H
#define UNOR4_TINYUSB_BSP_COMMON_H

#include "board_auto.h"

#include <Arduino.h>

// SAMD-style guard, harmless here but kept so a sketch that pulls in both
// this library and a SAMD core header still compiles.
#undef MSC_SUBCLASS_SCSI
#undef MSC_PROTOCOL_BULK_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CFG_TUSB_DEBUG_PRINTF target.
 *
 * NOT a plain vsnprintf-to-console wrapper: Serial1.write() blocks on the FSP
 * SCI TX-done interrupt, and CFG_TUSB_DEBUG routes TU_LOG through the USB ISR,
 * so writing from there spins forever (the SCI IRQ can never preempt the USB
 * ISR). This buffers in ISR context and the task pumps drain it.
 */
int arduino_debug_printf(char const *format, ...);

/** Flush debug output queued from the USB ISR. Called by the task pumps. */
void arduino_debug_drain(void);

#ifdef __cplusplus
}
#endif

#endif /* UNOR4_TINYUSB_BSP_COMMON_H */