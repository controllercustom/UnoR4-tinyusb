#include "Adafruit_TinyUSB.h"
uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
Adafruit_USBD_HID usb_hid;
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.end();
  TinyUSBDevice.setID(0x239A, 0xCAFE);
  TinyUSBDevice.setManufacturerDescriptor("Adafruit");
  TinyUSBDevice.setProductDescriptor("TinyUSB Keyboard A");
  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_KEYBOARD);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("TinyUSB Keyboard A");
  usb_hid.begin();
  TinyUSBDevice.detach();
  delay(10);
  TinyUSBDevice.attach();
}
void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif
  digitalWrite(LED_BUILTIN, (millis() / 200) % 2);
  if (!TinyUSBDevice.mounted()) return;
  // Send raw 8-byte boot keyboard report without checking ready()
  uint8_t report[8] = {0, 0, HID_KEY_A, 0, 0, 0, 0, 0};
  usb_hid.sendReport(0, report, sizeof(report));
  delay(250);
  uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  usb_hid.sendReport(0, release, sizeof(release));
  delay(500);
}
