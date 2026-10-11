#include "Adafruit_TinyUSB.h"
uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
Adafruit_USBD_HID usb_hid;
void setup() {
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
  if (!TinyUSBDevice.mounted()) return;
  if (!usb_hid.ready()) return;
  uint8_t keycode[6] = { HID_KEY_A, 0, 0, 0, 0, 0 };
  usb_hid.keyboardReport(0, 0, keycode);
  delay(250);
  usb_hid.keyboardRelease(0);
  delay(500);
}
