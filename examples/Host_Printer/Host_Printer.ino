// Host_Printer.ino — Sends data to a USB printer.
//
// Plug a USB printer into the host port.  The sketch enumerates it,
// reads the device ID and port status, then sends a short text job.
//
// Requires the printer host driver (class/printer/printer_host.c).
//
// Uno R4 Minima / Nano R4 (this repo):
//   Console is Serial1 (FTDI bridge on RX1/TX1) @ 115200 — the R4's own USB
//   connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   VBUS: the board does NOT source 5 V from its USB connector. Self-powered
//   (and full-/low-speed) devices work direct; bus-powered ones need
//   backpowering or a powered hub.
//   Configuration: this sketch ships a lean single-role config. Activate it
//   with `./scripts/config.sh set Host_Printer` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).

#include <UnoR4TinyUSB.h>

// ── Printer State ─────────────────────────────────────────────────

static bool printerMounted = false;
static uint8_t printerDevAddr = 0;

// ── Helpers ───────────────────────────────────────────────────────

static void print_port_status(uint8_t dev_addr) {
  uint8_t status = 0;
  if (tuh_printer_get_status(dev_addr, &status)) {
    tusb_printer_port_status_t *ps = (tusb_printer_port_status_t *)&status;
    UNOR4_TINYUSB_CONSOLE.print(F("  Status: 0x"));
    UNOR4_TINYUSB_CONSOLE.print(status, HEX);
    UNOR4_TINYUSB_CONSOLE.print(F("  ["));
    if (ps->status_bm.selected)    UNOR4_TINYUSB_CONSOLE.print(F("Selected "));
    if (ps->status_bm.not_error)   UNOR4_TINYUSB_CONSOLE.print(F("OK "));
    if (ps->status_bm.paper_empty) UNOR4_TINYUSB_CONSOLE.print(F("PaperEmpty "));
    UNOR4_TINYUSB_CONSOLE.println(F("]"));
  } else {
    UNOR4_TINYUSB_CONSOLE.println(F("  Status: unavailable"));
  }
}

static void print_device_id(uint8_t dev_addr) {
  uint8_t id_buf[128];
  uint16_t id_len = tuh_printer_get_device_id(dev_addr, id_buf, sizeof(id_buf));
  if (id_len > 0) {
    // Device ID is a length-prefixed string (2-byte big-endian length, then UTF-8 text)
    if (id_len > 2) {
      UNOR4_TINYUSB_CONSOLE.print(F("  Device ID: "));
      for (uint16_t i = 2; i < id_len; i++) {
        if (id_buf[i] >= 0x20 && id_buf[i] < 0x7F) {
          UNOR4_TINYUSB_CONSOLE.write(id_buf[i]);
        }
      }
      UNOR4_TINYUSB_CONSOLE.println();
    }
  } else {
    UNOR4_TINYUSB_CONSOLE.println(F("  Device ID: unavailable"));
  }
}

static void send_test_page(uint8_t dev_addr) {
  static const char test_page[] =
    "\x1B@ ..."          // @ = Initialize printer (ESC @)
    "Hello from TinyUSB Host!\r\n"
    "Printer test page.\r\n"
    "\x1Bd\x03"         // ESC d 3 = Print and feed 3 lines
    "\x1B@";             // Initialize again

  UNOR4_TINYUSB_CONSOLE.println(F("  Sending test page..."));

  uint32_t written = tuh_printer_write(dev_addr, test_page, sizeof(test_page) - 1, NULL, 0);
  UNOR4_TINYUSB_CONSOLE.print(F("  Wrote "));
  UNOR4_TINYUSB_CONSOLE.print(written);
  UNOR4_TINYUSB_CONSOLE.println(F(" bytes"));
}

// ── TinyUSB Host Printer Callbacks ────────────────────────────────

extern "C" {

// Called when a printer is mounted (ready to use)
void tuh_printer_mount_cb(uint8_t dev_addr) {
  UNOR4_TINYUSB_CONSOLE.print(F("\n── Printer Mounted ── addr="));
  UNOR4_TINYUSB_CONSOLE.println(dev_addr);

  printerMounted = true;
  printerDevAddr = dev_addr;

  print_device_id(dev_addr);
  print_port_status(dev_addr);

  // Send a test page
  send_test_page(dev_addr);

  UNOR4_TINYUSB_CONSOLE.println(F("Printer ready.\n"));
}

// Called when a printer is unmounted
void tuh_printer_umount_cb(uint8_t dev_addr) {
  UNOR4_TINYUSB_CONSOLE.print(F("\n── Printer Removed ── addr="));
  UNOR4_TINYUSB_CONSOLE.println(dev_addr);

  if (printerMounted && printerDevAddr == dev_addr) {
    printerMounted = false;
  }

  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for printer...\n"));
}

// Called when data is received from the printer (bidirectional printers only)
void tuh_printer_rx_cb(uint8_t dev_addr, const uint8_t *data, uint16_t len) {
  UNOR4_TINYUSB_CONSOLE.print(F("[Printer addr="));
  UNOR4_TINYUSB_CONSOLE.print(dev_addr);
  UNOR4_TINYUSB_CONSOLE.print(F("] len="));
  UNOR4_TINYUSB_CONSOLE.print(len);
  UNOR4_TINYUSB_CONSOLE.print(F(" data="));

  for (uint16_t i = 0; i < len && i < 64; i++) {
    if (data[i] < 0x10) UNOR4_TINYUSB_CONSOLE.write('0');
    UNOR4_TINYUSB_CONSOLE.print(data[i], HEX);
  }
  UNOR4_TINYUSB_CONSOLE.println();
}

} // extern "C"

// ── Arduino setup / loop ──────────────────────────────────────────

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(115200);
  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  UNOR4_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host Printer ==="));

  // Initialise TinyUSB host stack
  tuh_arduino_init();

  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for printer...\n"));
}

void loop() {
  // Run the TinyUSB host task — drives enumeration & callbacks
  tuh_arduino_poll();
}
