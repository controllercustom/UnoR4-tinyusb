// SPDX-License-Identifier: MIT
// Device_Vendor.ino — USB Vendor class echo (device mode).
//
// VID:PID come from tud_arduino_set_vidpid(); the usb_descriptors.cpp tab
// serves the CDC + Vendor layout.
//
// Works on ALL boards (UnoR4TinyUSB multi-board library).
// Debug: UNOR4_TINYUSB_CONSOLE @ UNOR4_TINYUSB_BAUD.
//
// NOTE: the source sketch's tuh_cdc_rx_cb() (a host-stack callback) is
// dropped — it can never fire in device mode.
//
// Uno R4 Minima / Nano R4 (this repo):
//   Console is Serial1 (FTDI bridge on RX1/TX1) @ 115200 — the R4's own USB
//   connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   VBUS: the board does NOT source 5 V from its USB connector. Self-powered
//   (and full-/low-speed) devices work direct; bus-powered ones need
//   backpowering or a powered hub.
//   Configuration: this sketch ships a lean single-role config. Activate it
//   with `./scripts/config.sh set Device_Vendor` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).

#include <UnoR4TinyUSB.h>

#define USB_VID 0x2341
#define USB_PID 0x0058

void setup()
{
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[Vendor] UnoR4TinyUSB Vendor Echo");
  UNOR4_TINYUSB_CONSOLE.println("[Vendor] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  /* Echo vendor data from host (standard TinyUSB vendor API, all boards) */
  while (tud_vendor_available()) {
    uint8_t buf[64];
    uint32_t n = tud_vendor_read(buf, sizeof(buf));
    if (n) {
      tud_vendor_write(buf, n);
      tud_vendor_write_flush();
    }
  }
}
