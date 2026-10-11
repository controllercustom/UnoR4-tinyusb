// SPDX-License-Identifier: MIT
// r4_DeviceCDC.ino — the core's default R4 USB identity with CDC echo, on the
// TinyUSB_Arduino API.
//
// Hardware: Minima USB Type-C -> PC. Board enumerates as 2341:0069 (Nano R4:
// 2341:0074) with two interfaces, CDC Communications + CDC Data; /dev/ttyACMx
// appears on the host. Debug: Serial1 (FTDI bridge on RX1/TX1) @ 115200.
//
// Unlike the Device_* examples this one supplies NO usb_descriptors.cpp, so
// the descriptors come from the RA4M1 core: USB_VID/USB_PID from the variant's
// pins_arduino.h, and Serial (the core's _SerialUSB) owning interface 0.
//
// Note on what the core actually installs: USB.cpp has CDC plus conditionally
// DFU-RT / HID / MSC, and never a vendor interface (TUD_VENDOR_DESCRIPTOR
// does not appear in it). As built here the device is CDC only, verified from
// /sys/bus/usb/devices/*/*/bInterfaceClass (0x02 + 0x0a).
// Use Device_CDC instead when you want to own the descriptors yourself.

#include <UnoR4TinyUSB.h>

void tuh_cdc_rx_cb(uint8_t idx) { (void)idx; }

void setup()
{
  Serial1.begin(UNOR4_TINYUSB_BAUD);
  delay(1000);
  Serial1.println("[DeviceCDC] Uno R4 Minima USB Device CDC Echo");

  // The core already brought rhport 0 up as DEVICE during _init(); this only
  // records the role and starts the task pump. See src/UnoR4TinyUSB.h.
  tud_arduino_init();

  Serial1.println("[DeviceCDC] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  /* Echo CDC data from host */
  while (Serial.available())
  {
    Serial.write(Serial.read());
  }

  /* Forward Serial1 data to the CDC port */
  while (Serial1.available())
  {
    Serial.write(Serial1.read());
  }

  /* Echo vendor data from host */
  while (tud_vendor_available())
  {
    uint8_t buf[64];
    uint16_t n = tud_vendor_read(buf, sizeof(buf));
    tud_vendor_write(buf, n);
  }
}