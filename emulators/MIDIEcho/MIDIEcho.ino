/*
 * MIDIEcho — RP2040 TinyUSB USB-MIDI echo device for Giga USB-A host testing.
 *
 * Single-cable MIDIStreaming device: echoes every 4-byte USB-MIDI event
 * packet verbatim (host verifies order/content). No note semantics —
 * transport verification only (like CDCEcho for CDC).
 *
 * FQBN: rp2040:rp2040:rpipico:usbstack=tinyusb,uploadmethod=picoprobe_cmsis_dap
 * VID:PID 0x239A:0xCAFB (CAFD=CDC echo, CAFE=CDC bulk, CAFB=MIDI echo)
 */

#include "Adafruit_TinyUSB.h"

Adafruit_USBD_MIDI usb_midi;

void setup() {
  Serial.end(); // disable default CDC: MIDI-only device
  TinyUSBDevice.setID(0x239A, 0xCAFB);
  TinyUSBDevice.setManufacturerDescriptor("Giga_USBH");
  TinyUSBDevice.setProductDescriptor("MIDI Echo");
  usb_midi.setStringDescriptor("MIDI Echo");
  usb_midi.begin();
  // Force re-enumeration with new PID/product
  TinyUSBDevice.detach();
  delay(10);
  TinyUSBDevice.attach();
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

  // Echo every 4-byte USB-MIDI event packet verbatim
  uint8_t pkt[4];
  while (usb_midi.readPacket(pkt)) {
    usb_midi.writePacket(pkt);
  }
}
