/*
 * CDCEcho — RP2040 TinyUSB CDC-only echo device for Giga USB-A host testing.
 *
 * CDC-only (no HID) to keep config descriptor ~62-75 B within Giga FS 64 B
 * handling. Uses Adafruit_USBD_CDC (TinyUSB stack, usbstack=tinyusb).
 * Fixed virtual baud 115200. Binary echo with sequence/CRC verification
 * done on host side; device just mirrors bytes and can also generate
 * deterministic streams when not echoing.
 *
 * FQBN: rp2040:rp2040:rpipico:usbstack=tinyusb,uploadmethod=picoprobe_cmsis_dap
 * VID:PID 0x239A:0xCAFD (new PID to bust Giga host cache)
 */

#include "Adafruit_TinyUSB.h"

// Use Serial (SerialTinyUSB) as the single CDC instance — do not create second Adafruit_USBD_CDC
// to keep descriptor single CDC (60B) not composite 2xCDC (115B)

static uint32_t boot_ms = 0;

void setup() {
  Serial.end(); // disable default CDC to allow clean PID re-enumeration
  TinyUSBDevice.setID(0x239A, 0xCAFD);
  TinyUSBDevice.setManufacturerDescriptor("Giga_USBH");
  TinyUSBDevice.setProductDescriptor("CDC Echo");
  Serial.setStringDescriptor("CDC Echo");
  Serial.begin(115200);
  // Force re-enumeration with new PID/product
  TinyUSBDevice.detach();
  delay(10);
  TinyUSBDevice.attach();
  boot_ms = millis();
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif

  if (!TinyUSBDevice.mounted()) {
    digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
    return;
  }
  digitalWrite(LED_BUILTIN, HIGH);

  // Echo any data back verbatim
  if (Serial.available()) {
    uint8_t buf[64];
    int n = Serial.read(buf, sizeof(buf));
    if (n > 0) {
      Serial.write(buf, n);
      Serial.flush();
    }
  }
  // NOTE: no unsolicited heartbeat stream. HostCDCSerial ECHO mode drives its
  // own seq blocks and verifies the echo — self-synchronizing (both sides
  // start at 0 on connect; a free-running source could never align with the
  // host's independent counter). For host-driven RX throughput use TX_FLOOD
  // (host sends, device sinks) or CDCBulkSource.
}
