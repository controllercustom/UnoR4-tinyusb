// Device_HID.ino — Boot HID mouse that moves cursor in a figure-8 pattern.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC
//
// Uno R4 Minima / Nano R4 (this repo):
//   Console is Serial1 (FTDI bridge on RX1/TX1) @ 115200 — the R4's own USB
//   connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   VBUS: the board does NOT source 5 V from its USB connector. Self-powered
//   (and full-/low-speed) devices work direct; bus-powered ones need
//   backpowering or a powered hub.
//   Configuration: this sketch ships a lean single-role config. Activate it
//   with `./scripts/config.sh set Device_HID` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).

#include <UnoR4TinyUSB.h>

static uint32_t prev_ms = 0;
static uint32_t t = 0;

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[HID] UnoR4-TinyUSB HID Mouse");

  UNOR4_TINYUSB_CONSOLE.println("[HID] Device stack initialized.");
}

void loop() {
  tud_arduino_task();

  uint32_t now = millis();
  if (now - prev_ms < 8) return;
  prev_ms = now;

  if (!tud_hid_ready()) return;

  t += 2;
  int8_t dx = (int8_t)((t * 3) & 0xFF);
  int8_t dy = (int8_t)((t * 7) & 0xFF);

  tud_hid_mouse_report(0x00, 0, dx, dy, 0, 0);
}
