#include "Adafruit_TinyUSB.h"
uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_MOUSE() };
Adafruit_USBD_HID usb_hid;
void setup() {
#if defined(ARDUINO_ARCH_RP2040)
  Serial.end();
#else
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  TinyUSBDevice.clearConfiguration();
#endif
  TinyUSBDevice.setID(0x239A, 0xCAFE);
  TinyUSBDevice.setManufacturerDescriptor("Adafruit");
  TinyUSBDevice.setProductDescriptor("TinyUSB Mouse");
  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_MOUSE);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("TinyUSB Mouse");
  usb_hid.begin();
  if (TinyUSBDevice.mounted()) { TinyUSBDevice.detach(); delay(10); TinyUSBDevice.attach(); }
}
void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif
  if (!TinyUSBDevice.mounted()) return;
  if (!usb_hid.ready()) return;
  static int step = 0;
  const int r = 10;
  float a = step * 0.3f;
  float pa = (step-1) * 0.3f;
  int dx = (int)(r * cosf(a) - r * cosf(pa));
  int dy = (int)(r * sinf(a) - r * sinf(pa));
  if (dx > 127) dx=127; if (dx<-127) dx=-127;
  if (dy > 127) dy=127; if (dy<-127) dy=-127;
  uint8_t buttons = 0;
  if (step % 20 == 0) buttons = MOUSE_BUTTON_LEFT;
  usb_hid.mouseReport(0, buttons, dx, dy, 0, 0);
  delay(20);
  if (buttons) { usb_hid.mouseReport(0, 0, 0, 0, 0, 0); delay(50); }
  step++;
  if (step>=100) step=0;
}
