/*
 * CDCBulkSource — RP2040 TinyUSB CDC-only bulk TX source + RX sink for
 * throughput measurement (device -> host direction).
 *
 * Generates continuous binary blocks: each byte = sequence 0..255 wrapping.
 * Host (Giga) counts bytes and verifies sequence/CRC. Also drains any
 * host->device data and counts it.
 *
 * Fixed virtual baud 115200 (ignored by USB FS, but set for consistency).
 * VID:PID 0x239A:0xCAFE (alt PID; use CAFD for echo, CAFE for bulk source
 * to distinguish in host logs). Config descriptor CDC-only.
 */

#include "Adafruit_TinyUSB.h"

static uint32_t seq = 0;
static uint32_t tx_bytes = 0;
static uint32_t rx_bytes = 0;
static uint32_t last_report_ms = 0;

void setup() {
  Serial.end();
  TinyUSBDevice.setID(0x239A, 0xCAFE);
  TinyUSBDevice.setManufacturerDescriptor("Giga_USBH");
  TinyUSBDevice.setProductDescriptor("CDC BulkSource");
  Serial.setStringDescriptor("CDC BulkSource");
  Serial.begin(115200);
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

  while (Serial.available()) {
    uint8_t buf[64];
    int n = Serial.read(buf, sizeof(buf));
    if (n > 0) rx_bytes += n;
  }
  uint8_t txbuf[64];
  for (int i = 0; i < 64; i++) {
    txbuf[i] = (uint8_t)(seq & 0xFF);
    seq++;
  }
  if (Serial.availableForWrite() >= 64) {
    int w = Serial.write(txbuf, 64);
    if (w > 0) tx_bytes += w;
  } else {
    Serial.flush();
    delayMicroseconds(100);
  }

  // Periodic status on CDC itself is not useful (would pollute binary stream).
  // Status is via host-side counting; device just streams.
  // Optional: blink LED by TX progress
  if (millis() - last_report_ms > 1000) {
    last_report_ms = millis();
    // Toggle LED each second while streaming
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}
