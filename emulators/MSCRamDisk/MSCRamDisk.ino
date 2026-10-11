#include "Adafruit_TinyUSB.h"

#define DISK_BLOCK_NUM  256
#define DISK_BLOCK_SIZE 512

static uint8_t msc_disk[DISK_BLOCK_NUM][DISK_BLOCK_SIZE];

Adafruit_USBD_MSC usb_msc;

int32_t msc_read_callback(uint32_t lba, void* buffer, uint32_t bufsize) {
  if (lba >= DISK_BLOCK_NUM) return -1;
  memcpy(buffer, msc_disk[lba], bufsize);
  return bufsize;
}
int32_t msc_write_callback(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  if (lba >= DISK_BLOCK_NUM) return -1;
  memcpy(msc_disk[lba], buffer, bufsize);
  return bufsize;
}
void msc_flush_callback(void) {}

void setup() {
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  Serial.begin(115200);
  TinyUSBDevice.setID(0x239A, 0xCAFC);
  TinyUSBDevice.setManufacturerDescriptor("Giga_USBH");
  TinyUSBDevice.setProductDescriptor("MSCRamDisk");
  usb_msc.setID("Giga", "RamDisk", "1.0");
  usb_msc.setCapacity(DISK_BLOCK_NUM, DISK_BLOCK_SIZE);
  usb_msc.setReadWriteCallback(msc_read_callback, msc_write_callback, msc_flush_callback);
  usb_msc.setUnitReady(true);
  usb_msc.begin();
  for (uint32_t i=0;i<DISK_BLOCK_NUM;i++) {
    memset(msc_disk[i], (int)(i & 0xFF), DISK_BLOCK_SIZE);
  }
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach(); delay(10); TinyUSBDevice.attach();
  }
  pinMode(LED_BUILTIN, OUTPUT);
}
void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif
  digitalWrite(LED_BUILTIN, TinyUSBDevice.mounted() ? HIGH : (millis()/500 & 1));
}
