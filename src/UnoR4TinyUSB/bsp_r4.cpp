// SPDX-License-Identifier: MIT
// bsp_r4.cpp — board support for the Arduino Renesas RA4M1 (Uno R4 Minima,
// Nano R4): runtime ISR dispatch, role switching, register probe.
//
// The core's __USBStart() handles everything that must happen before main():
//   - RUSB2 module clocks (MSTP)
//   - USBFS_IRQn registration via IRQManager and ISR vector setup
//   - VUSB LDO enable
//   - tud_rhport_init(0, DEVICE) on rhport 0
//
// We override _usbfs_interrupt_handler (made weak by scripts/renesas_core.patch)
// to dispatch to tuh_int_handler() or tud_int_handler() according to the
// current role. We must NOT override __USBStart(): re-implementing it means
// re-implementing the whole hardware bring-up above, and getting the order
// wrong hangs device-mode init. Host mode is entered afterwards by
// tuh_arduino_init(), which re-inits rhport 0 as HOST.

#include "bsp_common.h"

#include "../UnoR4TinyUSB.h"

static UnoR4TinyUSBMode _r4tusb_mode = UNOR4_TINYUSB_MODE_UNINITIALIZED;

// ---- TinyUSB time API -------------------------------------------------------
// Required by the host stack when CFG_TUSB_OS is OPT_OS_NONE.

extern "C" uint32_t tusb_time_millis_api(void)
{
  return millis();
}

extern "C" void tusb_time_delay_ms_api(uint32_t ms)
{
  delay(ms);
}

// ---- Mode tracking ----------------------------------------------------------
// The role is recorded by the *_init() entry points below and read by the ISR
// dispatcher and by arduino_tinyusb_get_mode(). Nothing outside this file ever
// needs to set it: a public setter would let a sketch flip the role behind the
// stack's back, which is how you get an ISR dispatching for a controller that
// was initialised as something else.

UnoR4TinyUSBMode arduino_tinyusb_get_mode(void)
{
  return _r4tusb_mode;
}

// ---- RUSB2 register poking ---------------------------------------------------
// The RUSB2 peripheral is memory-mapped at 0x40090000. These are the register
// blocks the host path has to fight the hardware over; see the named helpers.

#define RUSB2_BASE 0x40090000u

static volatile uint16_t *r4_reg16(uint32_t offset)
{
  return (volatile uint16_t *)(RUSB2_BASE + offset);
}

// SYSCFG (0x00) bits used here
#define RUSB2_SYSCFG_USBE  (1u << 0)  // USB enable
#define RUSB2_SYSCFG_DPRPU (1u << 4)  // D+ pull-up (device)
#define RUSB2_SYSCFG_DRPD  (1u << 5)  // D- pull-down (host)
#define RUSB2_SYSCFG_DCFM  (1u << 6)  // controller mode: 1 = host
#define RUSB2_SYSCFG_HSE   (1u << 7)  // high-speed enable

// SYSSTS0 (0x24)
#define RUSB2_SYSSTS0_RHST_MASK 0x0003u  // 0 = no device, 3 = operating

// hcd_init() leaves DCFM cleared and hcd_port_reset() disables USBFS_IRQn.
// Both have to be re-asserted, and hcd_port_reset() does it again mid-burst,
// which is why the poll re-asserts them too.
static void r4_latch_host_mode(void)
{
  volatile uint16_t *syscfg = r4_reg16(0x00);
  *syscfg = (uint16_t)(*syscfg | (RUSB2_SYSCFG_DCFM | RUSB2_SYSCFG_DRPD));
  *syscfg = (uint16_t)(*syscfg &
                       ~(uint16_t)(RUSB2_SYSCFG_DPRPU | RUSB2_SYSCFG_HSE));
}

// USBFS is event 51 in the RA event list; find and re-enable its NVIC line.
static void r4_enable_usbfs_irq(void)
{
  for (uint32_t i = 0; i < 96; i++) {
    if (R_ICU->IELSR[i] == 51) {
      NVIC_EnableIRQ((IRQn_Type)i);
      return;
    }
  }
}

// ---- Device mode ------------------------------------------------------------

#if CFG_TUD_ENABLED

void tud_arduino_init(void)
{
  _r4tusb_mode = UNOR4_TINYUSB_MODE_DEVICE;

  // The core already called tud_rhport_init(0, DEVICE) from _init(). Nothing
  // to redo — re-initialising here would tear down and rebuild a stack that is
  // already serving, and the module clocks / IRQ / VUSB LDO only get set up
  // once. Just confirm, loudly, if the core did not.
  if (!tusb_inited()) {
    UNOR4_TINYUSB_CONSOLE.println(
      "[TUD] ERROR: stack not initialised. The renesas_uno core must be "
      "patched with scripts/patch_renesas_core.sh");
  }
}

void tud_arduino_task(void)
{
  arduino_debug_drain();
  tud_task();
}

#endif // CFG_TUD_ENABLED

// ---- Host mode --------------------------------------------------------------

#if CFG_TUH_ENABLED

void tuh_arduino_init(void)
{
  _r4tusb_mode = UNOR4_TINYUSB_MODE_HOST;

  // Re-init rhport 0: the core brought it up as DEVICE in _init().
  const tusb_rhport_init_t rh_init = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_FULL
  };
  tuh_rhport_init(0, &rh_init);

  r4_latch_host_mode();
  r4_enable_usbfs_irq();
}

void tuh_arduino_poll(void)
{
  arduino_debug_drain();

  r4_enable_usbfs_irq();
  r4_latch_host_mode();

  tuh_task();
}

#endif // CFG_TUH_ENABLED

// ---- Register probe ---------------------------------------------------------

void arduino_tinyusb_probe(void)
{
  volatile uint16_t *syscfg  = r4_reg16(0x00);
  volatile uint16_t *dvstctr = r4_reg16(0x08);
  volatile uint16_t *syssts  = r4_reg16(0x24);
  volatile uint16_t *intenb  = r4_reg16(0x30);
  volatile uint16_t *intsts  = r4_reg16(0x2E);
  volatile uint16_t *dcpctr  = r4_reg16(0x70);

  char line[192];

  snprintf(line, sizeof(line),
           "[R4] mode=%u SYSCFG=%04x DVSTCTR0=%04x SYSSTS0=%04x",
           (unsigned)_r4tusb_mode, *syscfg, *dvstctr, *syssts);
  UNOR4_TINYUSB_CONSOLE.println(line);

  snprintf(line, sizeof(line),
           "[R4] USBE=%u DCFM=%u DRPD=%u HSE=%u SCKE=%u | VBUSEN=%u UACT=%u | RHST=%u",
           (unsigned)((*syscfg >> 0) & 1u), (unsigned)((*syscfg >> 6) & 1u),
           (unsigned)((*syscfg >> 5) & 1u), (unsigned)((*syscfg >> 7) & 1u),
           (unsigned)((*syscfg >> 10) & 1u),
           (unsigned)((*dvstctr >> 9) & 1u), (unsigned)((*dvstctr >> 4) & 1u),
           (unsigned)(*syssts & RUSB2_SYSSTS0_RHST_MASK));
  UNOR4_TINYUSB_CONSOLE.println(line);

  snprintf(line, sizeof(line),
           "[R4] INTENB0=%04x INTSTS0=%04x DCPCTR=%04x",
           *intenb, *intsts, *dcpctr);
  UNOR4_TINYUSB_CONSOLE.println(line);
}

// ---- ISR dispatch -----------------------------------------------------------
// Strong override of the core's weak _usbfs_interrupt_handler. RUSB2 is a
// single controller, so the role recorded by tud_arduino_init() /
// tuh_arduino_init() picks the handler.
//
// DO NOT WRAP THIS IN extern "C". The core defines it in a C++ translation
// unit at file scope, OUTSIDE any extern "C" block, so it is C++-mangled:
// verified with `nm cores/arduino/usb/USB.cpp.o` ->
//     W _Z24_usbfs_interrupt_handlerv
// An extern "C" definition here would be a DIFFERENT, unmangled symbol. It
// would compile, link and run — and the core's weak device-only handler would
// silently keep winning, so host mode would never dispatch tuh_int_handler().
// The definition must therefore inherit the same C++ linkage.
//
// (The descriptor callbacks the patch weakens — tud_descriptor_device_cb and
// friends — ARE C linkage in USB.cpp, so those DO use extern "C". Verified in
// the same nm output.)
//
// Each branch is guarded: a single-role config (one copied over
// src/tusb_config_unor4tinyusb.h) does not include the other stack's header,
// so tud_task()/tuh_task() would not even be declared.

void _usbfs_interrupt_handler(void)
{
  R_BSP_IrqStatusClear(R_FSP_CurrentIrqGet());

  if (_r4tusb_mode == UNOR4_TINYUSB_MODE_HOST)
  {
    #if CFG_TUH_ENABLED
    tuh_int_handler(0);
    #endif
  }
  else
  {
    #if CFG_TUD_ENABLED
    tud_int_handler(0);
    tud_task();
    #endif
  }
}