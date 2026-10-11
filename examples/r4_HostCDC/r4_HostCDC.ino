// SPDX-License-Identifier: MIT
// HostCDC.ino — USB Host CDC serial bridge on Uno R4 Minima.
//
// Hardware: Minima USB host port -> USB CDC device (e.g. RP2350, FTDI)
// Debug: Serial1 (FTDI) @ 115200
//
// Bridges data between the host USB CDC device and Serial1.

#include <UnoR4TinyUSB.h>

static uint8_t _cdc_idx = 0xFF;
static bool _cdc_mounted = false;

static void hostcdc_line_state_cb(tuh_xfer_t *xfer)
{
  if (xfer->result == XFER_RESULT_SUCCESS)
    Serial1.println("[HostCDC] DTR+RTS set");
  else {
    Serial1.print("[HostCDC] line state set FAILED result=");
    Serial1.println((int) xfer->result);
  }
}

extern "C" void tuh_cdc_mount_cb(uint8_t idx)
{
  Serial1.print("[HostCDC] CDC mounted idx=");
  Serial1.println(idx);
  _cdc_idx = idx;
  _cdc_mounted = true;
  /* Assert DTR+RTS: many CDC devices (e.g. Adafruit_TinyUSB, which gates
     tud_cdc_n_connected() on DTR) stay silent until the host sets it. */
  tuh_cdc_set_control_line_state(idx,
    CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS,
    hostcdc_line_state_cb, 0);
}

extern "C" void tuh_cdc_umount_cb(uint8_t idx)
{
  Serial1.print("[HostCDC] CDC unmounted idx=");
  Serial1.println(idx);
  if (idx == _cdc_idx) {
    _cdc_mounted = false;
    _cdc_idx = 0xFF;
  }
}

extern "C" void tuh_cdc_rx_cb(uint8_t idx)
{
  /* Data available — read in loop() */
  (void)idx;
}

void setup()
{
  Serial1.begin(115200);
  delay(1000);
  Serial1.println("[HostCDC] Uno R4 Minima USB Host CDC Bridge");
  Serial1.println("[HostCDC] Waiting for CDC device...");

  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();

  /* CDC device -> Serial1 */
  if (_cdc_mounted && tuh_cdc_read_available(_cdc_idx)) {
    uint8_t buf[64];
    uint32_t n = tuh_cdc_read(_cdc_idx, buf, sizeof(buf));
    Serial1.write(buf, n);
  }

  /* Serial1 -> CDC device */
  if (_cdc_mounted) {
    while (Serial1.available()) {
      uint8_t buf[64];
      uint32_t n = Serial1.readBytes(buf, sizeof(buf));
      tuh_cdc_write(_cdc_idx, buf, n);
      tuh_cdc_write_flush(_cdc_idx);
    }
  }
}
