// SPDX-License-Identifier: MIT
// HostHID.ino — USB Host HID keyboard on Uno R4 Minima.
//
// Hardware: Minima USB host port -> USB keyboard
// Debug: Serial1 (FTDI) @ 115200

#include <UnoR4TinyUSB.h>

static bool _hid_mounted = false;
static char _buf[128];

extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                                  uint8_t const *report_desc, uint16_t desc_len)
{
  snprintf(_buf, sizeof(_buf), "[HostHID] mounted dev=%u idx=%u desc=%u\r\n",
           dev_addr, idx, desc_len);
  Serial1.print(_buf);
  _hid_mounted = true;
  tuh_hid_receive_report(dev_addr, idx);
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{
  snprintf(_buf, sizeof(_buf), "[HostHID] unmounted dev=%u idx=%u\r\n",
           dev_addr, idx);
  Serial1.print(_buf);
  _hid_mounted = false;
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                   uint8_t const *report, uint16_t len)
{
  /* Boot keyboard: byte 0 = modifier, byte 1 = reserved, bytes 2-7 = keys */
  if (len >= 8) {
    uint8_t mod = report[0];
    if (mod & 0x01) Serial1.print("L-CTRL ");
    if (mod & 0x02) Serial1.print("L-SHIFT ");
    if (mod & 0x04) Serial1.print("L-ALT ");
    if (mod & 0x08) Serial1.print("L-GUI ");
    if (mod & 0x10) Serial1.print("R-CTRL ");
    if (mod & 0x20) Serial1.print("R-SHIFT ");
    if (mod & 0x40) Serial1.print("R-ALT ");
    if (mod & 0x80) Serial1.print("R-GUI ");
    for (int i = 2; i < 8; i++) {
      if (report[i] != 0) {
        snprintf(_buf, sizeof(_buf), "key=0x%02x ", report[i]);
        Serial1.print(_buf);
      }
    }
    Serial1.println();
  }
  /* Re-arm */
  tuh_hid_receive_report(dev_addr, idx);
}

void setup()
{
  Serial1.begin(115200);
  delay(1000);
  Serial1.println("[HostHID] Uno R4 Minima USB Host HID Keyboard");
  Serial1.println("[HostHID] Waiting for keyboard...");

  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();
}
