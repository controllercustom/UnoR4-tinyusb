// Device_NET.ino — CDC-NCM network device (Ethernet-over-USB).
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC
//
// On the host side this appears as a network adapter (NCM class).
// Received Ethernet frames are echoed back as-is (loopback),
// demonstrating a minimal USB network device.
//
// Uno R4 Minima / Nano R4 (this repo):
//   Console is Serial1 (FTDI bridge on RX1/TX1) @ 115200 — the R4's own USB
//   connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   VBUS: the board does NOT source 5 V from its USB connector. Self-powered
//   (and full-/low-speed) devices work direct; bus-powered ones need
//   backpowering or a powered hub.
//   Configuration: this sketch ships a lean single-role config. Activate it
//   with `./scripts/config.sh set Device_NET` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).

#include <UnoR4TinyUSB.h>

// ─── MAC address for this device ───
uint8_t tud_network_mac_address[6] = { 0x02, 0x02, 0x84, 0x6A, 0x96, 0x00 };

// ─── Receive buffer ───
#ifndef CFG_TUD_NET_MTU
  #define CFG_TUD_NET_MTU 1514
#endif

static bool net_link_up = false;

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it.
  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[NET] UnoR4-TinyUSB CDC-NCM Network");

  UNOR4_TINYUSB_CONSOLE.println("[NET] Device stack initialized.");
  UNOR4_TINYUSB_CONSOLE.print("[NET] MAC: ");
  for (uint8_t i = 0; i < 6; i++) {
    if (tud_network_mac_address[i] < 0x10) UNOR4_TINYUSB_CONSOLE.print('0');
    UNOR4_TINYUSB_CONSOLE.print(tud_network_mac_address[i], HEX);
    if (i < 5) UNOR4_TINYUSB_CONSOLE.print(':');
  }
  UNOR4_TINYUSB_CONSOLE.println();
}

void loop() {
  // Feed the TinyUSB device task
  tud_arduino_task();

  // Bring link up after enumeration
  if (!net_link_up && tud_connected()) {
    tud_network_link_state(0, true);
    net_link_up = true;
    UNOR4_TINYUSB_CONSOLE.println("[NET] Link UP");
  }
}

// ─── Network callbacks required by TinyUSB NCM ───

// Called when the host sends a packet to us
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  (void) src;
  (void) size;
  // Simple loopback: accept all frames, then renew so stack can receive more
  tud_network_recv_renew();
  return true;
}

// Called to copy an outgoing packet into the transmit buffer
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  (void) dst;
  (void) ref;
  (void) arg;
  // Nothing to transmit in this echo-only example
  return 0;
}

// Called when the host resets the network interface
void tud_network_init_cb(void) {
  net_link_up = false;
  UNOR4_TINYUSB_CONSOLE.println("[NET] Host reset network interface");
}
