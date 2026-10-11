// Device_dfu_runtime.ino — DFU Runtime detach example (Uno R4 Minima / Nano R4).
//
// Exposes a DFU Runtime interface so the host can request a detach (reboot into
// the DFU bootloader) with `dfu-util -e`, or with a serial command.
//
// Uno R4 Minima / Nano R4 (this repo):
//   Console is Serial1 (FTDI bridge on RX1/TX1) @ 115200 — the R4's own USB
//   connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   Configuration: this sketch ships a lean config. Activate it with
//   `./scripts/config.sh set Device_dfu_runtime` before building another example.
//
// IMPORTANT — tud_dfu_runtime_reboot_to_dfu_cb is CORE-OWNED on this board.
//
// The RA4M1 core already defines it strongly in cores/arduino/usb/SerialUSB.cpp
// and it does the right thing for this part: detach, then reboot into the stock
// Arduino DFU bootloader at 0x00000000, which `dfu-util` then enumerates as
// 2341:0369. Defining your own here is a LINK error ("multiple definition"),
// not a silent override. Verified on hardware: `dfu-util` lists the runtime
// interface and `-e` detaches into the bootloader.
//
// Same rule for tud_cdc_line_state_cb / tud_cdc_line_coding_cb: core-owned by
// SerialUSB.cpp. Only tud_descriptor_device_cb / _configuration_cb /
// _string_cb and tud_hid_descriptor_report_cb are yours to override (the core
// patch makes them weak).

#include <UnoR4TinyUSB.h>

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  // The core brought the device stack up during _init(); this records the role
  // and starts the task pump. See src/UnoR4TinyUSB.h.
  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[DFU_RT] UnoR4-tinyUSB DFU Runtime");
  UNOR4_TINYUSB_CONSOLE.println("[DFU_RT] Send 'd' to reboot into the DFU bootloader,");
  UNOR4_TINYUSB_CONSOLE.println("[DFU_RT] or run: dfu-util -e  (device 2341:0369 after detach).");
}

void loop() {
  tud_arduino_task();

  // The USB-side detach path is handled entirely by the core's callback: the
  // board reboots without the sketch getting a say, so there is nothing to poll
  // here for that case. Only the console command is ours.
  if (!UNOR4_TINYUSB_CONSOLE.available()) {
    return;
  }

  char c = UNOR4_TINYUSB_CONSOLE.read();
  if (c == 'd' || c == 'D') {
    UNOR4_TINYUSB_CONSOLE.println("[DFU_RT] rebooting into DFU bootloader...");
    delay(100);
    // A plain system reset lands in the stock Arduino DFU bootloader, which is
    // what `dfu-util` expects. (The core's own detach path does the same thing
    // after servicing the USB control request.)
    NVIC_SystemReset();
  }
}