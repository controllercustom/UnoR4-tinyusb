// Host_HID_Boot.ino — Reads HID input reports using boot protocol only.
//
// Plug a USB keyboard or mouse into the host port.  The sketch forces
// boot protocol mode and parses fixed-format boot reports directly,
// without needing to parse report descriptors.
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
//   with `./scripts/config.sh set Host_HID_Boot` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).

#include <UnoR4TinyUSB.h>

// ── Boot Report Structs ────────────────────────────────────────────
// These match the USB HID Boot Protocol fixed layouts.

typedef struct TU_ATTR_PACKED {
  uint8_t buttons;
  int8_t  x;
  int8_t  y;
  int8_t  wheel;
} BootMouseReport;

typedef struct TU_ATTR_PACKED {
  uint8_t modifier;
  uint8_t reserved;
  uint8_t keycode[6];
} BootKeyboardReport;

// ── Helpers ────────────────────────────────────────────────────────

static const char *mod_string(uint8_t mod) {
  static char buf[5];
  buf[0] = (mod & 0x01) ? 'C' : '-';
  buf[1] = (mod & 0x02) ? 'S' : '-';
  buf[2] = (mod & 0x04) ? 'A' : '-';
  buf[3] = (mod & 0x08) ? 'G' : '-';
  buf[4] = '\0';
  return buf;
}

static void print_keycode(uint8_t kc) {
  if (kc == 0) return;
  if (kc >= 0x04 && kc <= 0x1D) {
    UNOR4_TINYUSB_CONSOLE.write('a' + kc - 0x04);
  } else if (kc >= 0x1E && kc <= 0x27) {
    UNOR4_TINYUSB_CONSOLE.write('1' + kc - 0x1E);
  } else {
    UNOR4_TINYUSB_CONSOLE.print(F("0x"));
    UNOR4_TINYUSB_CONSOLE.print(kc, HEX);
  }
}

// ── TinyUSB Host Callbacks ─────────────────────────────────────────

extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                                  const uint8_t *report_desc,
                                  uint16_t desc_len) {
  uint8_t proto = tuh_hid_interface_protocol(dev_addr, idx);

  UNOR4_TINYUSB_CONSOLE.print(F("\n── HID Mounted ──\n"));
  UNOR4_TINYUSB_CONSOLE.print(F("  addr="));
  UNOR4_TINYUSB_CONSOLE.print(dev_addr);
  UNOR4_TINYUSB_CONSOLE.print(F("  itf="));
  UNOR4_TINYUSB_CONSOLE.print(idx);
  UNOR4_TINYUSB_CONSOLE.print(F("  proto="));

  if (proto == HID_ITF_PROTOCOL_KEYBOARD) {
    UNOR4_TINYUSB_CONSOLE.println(F("Keyboard (Boot)"));
  } else if (proto == HID_ITF_PROTOCOL_MOUSE) {
    UNOR4_TINYUSB_CONSOLE.println(F("Mouse (Boot)"));
  } else {
    UNOR4_TINYUSB_CONSOLE.println(F("Other — forcing Boot protocol"));
  }

  // Force boot protocol mode (protocol = 0)
  // Boot protocol gives us the fixed 8-byte keyboard / 4-byte mouse layout
  // without needing to parse the report descriptor.
  tuh_hid_set_protocol(dev_addr, idx, HID_PROTOCOL_BOOT);

  // Start receiving reports
  tuh_hid_receive_report(dev_addr, idx);
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx) {
  UNOR4_TINYUSB_CONSOLE.print(F("\n── HID Removed ── addr="));
  UNOR4_TINYUSB_CONSOLE.print(dev_addr);
  UNOR4_TINYUSB_CONSOLE.print(F("  itf="));
  UNOR4_TINYUSB_CONSOLE.println(idx);
}

extern "C" void tuh_hid_set_protocol_complete_cb(uint8_t dev_addr,
                                                  uint8_t idx,
                                                  uint8_t protocol) {
  UNOR4_TINYUSB_CONSOLE.print(F("  set_protocol → "));
  UNOR4_TINYUSB_CONSOLE.println(protocol == 0 ? F("Boot") : F("Report"));
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                            const uint8_t *report,
                                            uint16_t len) {
  uint8_t proto = tuh_hid_interface_protocol(dev_addr, idx);

  // ── Boot Keyboard Report (8 bytes) ──────────────────────────────
  if (proto == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
    const BootKeyboardReport *k = (const BootKeyboardReport *)report;

    UNOR4_TINYUSB_CONSOLE.print(F("Key mod="));
    UNOR4_TINYUSB_CONSOLE.print(mod_string(k->modifier));
    UNOR4_TINYUSB_CONSOLE.print(F(" ["));
    for (uint8_t i = 0; i < 6; i++) {
      if (k->keycode[i] != 0) {
        if (i > 0) UNOR4_TINYUSB_CONSOLE.write(' ');
        print_keycode(k->keycode[i]);
      }
    }
    UNOR4_TINYUSB_CONSOLE.println(F("]"));

  // ── Boot Mouse Report (4 bytes) ─────────────────────────────────
  } else if (proto == HID_ITF_PROTOCOL_MOUSE && len >= 4) {
    const BootMouseReport *m = (const BootMouseReport *)report;

    UNOR4_TINYUSB_CONSOLE.print(F("Mouse b="));
    UNOR4_TINYUSB_CONSOLE.print(m->buttons, HEX);
    UNOR4_TINYUSB_CONSOLE.print(F(" x="));
    UNOR4_TINYUSB_CONSOLE.print(m->x);
    UNOR4_TINYUSB_CONSOLE.print(F(" y="));
    UNOR4_TINYUSB_CONSOLE.print(m->y);
    UNOR4_TINYUSB_CONSOLE.print(F(" wh="));
    UNOR4_TINYUSB_CONSOLE.println(m->wheel);

  } else {
    // Unexpected size or unknown protocol — hex dump
    UNOR4_TINYUSB_CONSOLE.print(F("Boot report itf="));
    UNOR4_TINYUSB_CONSOLE.print(idx);
    UNOR4_TINYUSB_CONSOLE.print(F(" len="));
    UNOR4_TINYUSB_CONSOLE.print(len);
    UNOR4_TINYUSB_CONSOLE.print(F(" data="));
    for (uint16_t i = 0; i < len && i < 32; i++) {
      if (report[i] < 0x10) UNOR4_TINYUSB_CONSOLE.write('0');
      UNOR4_TINYUSB_CONSOLE.print(report[i], HEX);
    }
    UNOR4_TINYUSB_CONSOLE.println();
  }

  // Re-arm for the next report
  tuh_hid_receive_report(dev_addr, idx);
}

// ── Arduino setup / loop ───────────────────────────────────────────

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(115200);
  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  UNOR4_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host HID Boot ==="));

  // Initialise TinyUSB host stack
  tuh_arduino_init();

  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for HID devices (boot protocol)...\n"));
}

void loop() {
  // Run the TinyUSB host task — drives enumeration & callbacks
  tuh_arduino_poll();
}
