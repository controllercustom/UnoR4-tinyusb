// SPDX-License-Identifier: MIT
// HostPL2303.ino — USB host self-test for Prolific PL2303 (067B:2303)
// with TX jumpered to RX (loopback). No FTDI/Serial1 required.
//
// Hardware: Minima/Nano R4 USB host port -> PL2303 (TX->RX jumper)
//
// RGB LED verdict on Minima (11=red, 12=green, 13=blue):
//   blue blink     = waiting for PL2303
//   green blink    = mounted, configuring
//   green solid    = echo test running, 0 errors
//   red solid      = byte errors detected
//   red 5 blinks   = no device for 20 s (then resumes waiting)
// Nano R4 has a single LED: the same states collapse onto LED_BUILTIN
// (blink = waiting/configuring, solid = running, off/solid = fail —
// see ledWait/ledConfig/ledPass/ledFail below).
//
// The test streams 64-byte binary sequence blocks 0..255 and verifies the
// loopback echo byte by byte, continuously.

#include <UnoR4TinyUSB.h>
#include "tusb.h"

#if defined(ARDUINO_NANO_R4)
static const uint8_t LED_GREEN = LED_BUILTIN;
#define LED_BLUE LED_BUILTIN
#else
static const uint8_t LED_GREEN = 12;
#endif
static const uint32_t NO_DEVICE_TIMEOUT_MS = 20000;

enum PlState { PL_WAIT, PL_CONFIG, PL_RUN, PL_TIMEOUT };
static PlState plState = PL_WAIT;
static bool pl_ready = false;
static uint32_t tx_seq = 0, rx_seq_expected = 0;
static uint32_t rx_errors = 0, rx_resyncs = 0;
static uint32_t tx_bytes_total = 0, rx_bytes_total = 0;
static uint32_t bench_start = 0, last_report = 0;
static uint32_t state_start_ms = 0;
static uint8_t txbuf[64], rxbuf[64];

static void fillTxBlock(uint8_t *buf, size_t len)
{
  for (size_t i = 0; i < len; i++)
    buf[i] = (uint8_t)(tx_seq++ & 0xFF);
}

static void ledWait()
{
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, HIGH);
}

static void ledConfig()
{
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, LOW);
}

static void ledPass()
{
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, LOW);
}

static void ledFail()
{
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);
}

static void ledOff()
{
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);
}

static void cfgFormatCb(tuh_xfer_t *xfer)
{
  if (xfer->result != XFER_RESULT_SUCCESS)
  {
    Serial1.print("[PL2303] set_line_coding FAILED result=");
    Serial1.println((int) xfer->result);
    return;
  }
  tuh_cdc_set_control_line_state(0,
    CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, NULL, 0);
  pl_ready = true;
  tx_seq = 0;
  rx_seq_expected = 0;
  rx_errors = 0;
  rx_resyncs = 0;
  tx_bytes_total = 0;
  rx_bytes_total = 0;
  bench_start = millis();
  last_report = bench_start;
  Serial1.println("[PL2303] ready (115200 8N1 + DTR/RTS), starting echo");
  plState = PL_RUN;
  state_start_ms = millis();
}

// The PL2303 driver takes a SINGLE line-coding request. The previous 2-stage
// sequence (tuh_cdc_set_baudrate() then tuh_cdc_set_line_coding()) left the baud
// request succeeding and the second request silently dropped -- no completion
// callback at all -- so the sketch never left PL_CONFIG and re-mounted every
// 5 s. One request carries both baud and framing; chain DTR/RTS off its
// callback. FTDI and CP210x still need the 2-stage form.
static bool pl2303StartConfig(void)
{
  cdc_line_coding_t line_coding = {0};
  line_coding.bit_rate = 115200;
  line_coding.stop_bits = 0;
  line_coding.parity = 0;
  line_coding.data_bits = 8;
  return tuh_cdc_set_line_coding(0, &line_coding, cfgFormatCb, 0);
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  // Console is optional for this example -- the LED verdict stands on its own --
  // but without it the result is only readable by eye, which makes the example
  // impossible to check from scripts/hw_test.sh or in CI. Nothing here affects
  // USB: Serial1 is plain UART on D0/D1 and the writes vanish if nothing is
  // wired to them.
  Serial1.begin(UNOR4_TINYUSB_BAUD);
  delay(200);
  Serial1.println("\n### R4 PL2303 loopback self-test ###");
  Serial1.println("green solid = 0 errors, red solid = byte errors");
  tuh_arduino_init();
  state_start_ms = millis();
}

void loop()
{
  tuh_arduino_poll();
  const uint32_t now = millis();

  if (plState == PL_WAIT)
  {
    ledWait();
    digitalWrite(LED_BLUE, (now / 250) % 2 ? HIGH : LOW);
    if (tuh_cdc_mounted(0))
    {
      plState = PL_CONFIG;
      state_start_ms = now;
      tuh_cdc_read_clear(0);
      tuh_cdc_write_clear(0);
      Serial1.println("[PL2303] cdc mounted, setting 115200 8N1 (single request)");
      if (!pl2303StartConfig())
      {
        Serial1.println("[PL2303] set_line_coding REJECTED by driver");
      }
      return;
    }
    if (now - state_start_ms > NO_DEVICE_TIMEOUT_MS)
    {
      plState = PL_TIMEOUT;
      state_start_ms = now;
    }
    return;
  }

  if (plState == PL_TIMEOUT)
  {
    for (int i = 0; i < 5; i++)
    {
      ledFail();
      delay(200);
      ledOff();
      delay(200);
    }
    plState = PL_WAIT;
    state_start_ms = millis();
    return;
  }

  if (plState == PL_CONFIG)
  {
    ledConfig();
    digitalWrite(LED_GREEN, (now / 100) % 2 ? HIGH : LOW);
    if (now - state_start_ms > 5000)
    {
      plState = PL_WAIT;
      state_start_ms = now;
    }
    return;
  }

  if (!pl_ready || !tuh_cdc_mounted(0))
    return;

  for (int i = 0; i < 8; i++)
  {
    if (tuh_cdc_write_available(0) < sizeof(txbuf))
      break;
    fillTxBlock(txbuf, sizeof(txbuf));
    uint32_t w = tuh_cdc_write(0, txbuf, sizeof(txbuf));
    if (w != sizeof(txbuf))
    {
      // fillTxBlock() advanced tx_seq by the FULL block, but only w bytes
      // reached the wire. Rewind by the unsent remainder, otherwise the generator
      // silently skips (sizeof(txbuf) - w) sequence values and every later byte
      // mismatches. Not currently reachable -- the write_available() guard plus
      // the TX FIFO only draining from the ISR means remaining can only grow
      // between the check and the write -- but a short write would desync the
      // loopback permanently.
      tx_seq -= (sizeof(txbuf) - w);
      break;
    }
    tx_bytes_total += w;
  }
  tuh_cdc_write_flush(0);

  while (tuh_cdc_read_available(0))
  {
    uint32_t avail = tuh_cdc_read_available(0);
    uint32_t toRead = avail < sizeof(rxbuf) ? avail : sizeof(rxbuf);
    uint32_t n = tuh_cdc_read(0, rxbuf, toRead);
    if (n == 0)
      break;
    rx_bytes_total += n;
    for (uint32_t i = 0; i < n; i++)
    {
      uint8_t exp = (uint8_t)(rx_seq_expected & 0xFF);
      if (rxbuf[i] != exp)
      {
        // Re-anchor on what actually arrived. The sender emits a strictly
        // incrementing stream, so any slip resolves by resetting the expectation
        // to the received byte. A forward-only search cannot handle data running
        // BEHIND the expectation (duplication, not loss) -- that is exactly how the
        // CP210x behaved, and rs stayed 0 forever. Count the slip the short way
        // round so a 3-byte duplication costs 3 errors, not 253.
        uint8_t fwd = (uint8_t)((rxbuf[i] - exp) & 0xFF);
        uint8_t bwd = (uint8_t)((exp - rxbuf[i]) & 0xFF);
        uint32_t slip = (fwd < bwd) ? fwd : bwd;
        if (!slip)
          slip = 1;
        rx_resyncs++;
        rx_errors += slip;
        if (rx_resyncs <= 8)
        {
          char dbg[96];
          snprintf(dbg, sizeof(dbg), "[PL2303] slip exp=%02x got=%02x (%+d)",
            exp, rxbuf[i], (int)(int8_t)(uint8_t)(rxbuf[i] - exp));
          Serial1.println(dbg);
        }
        rx_seq_expected = rxbuf[i];
      }
      rx_seq_expected++;
    }
  }

  // Periodic report, so the result is measurable without watching an LED.
  if (now - last_report > 1000)
  {
    last_report = now;
    uint32_t el = now - bench_start;
    if (!el)
      el = 1;
    char line[144];
    snprintf(line, sizeof(line),
      "[PL2303] elapsed=%lus tx=%lu rx=%lu (%.1f KB/s) err=%lu rs=%lu",
      (unsigned long)(el / 1000), (unsigned long)tx_bytes_total, (unsigned long)rx_bytes_total,
      (double)rx_bytes_total * 1000.0 / el / 1024.0,
      (unsigned long)rx_errors, (unsigned long)rx_resyncs);
    Serial1.println(line);
  }

  if (rx_errors > 0)
    ledFail();
  else
    ledPass();
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  (void) daddr;
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  pl_ready = false;
  if (plState == PL_RUN || plState == PL_CONFIG)
  {
    plState = PL_WAIT;
    state_start_ms = millis();
  }
}

void tuh_cdc_mount_cb(uint8_t idx)
{
  (void) idx;
}

void tuh_cdc_umount_cb(uint8_t idx)
{
  (void) idx;
  pl_ready = false;
}

void tuh_cdc_rx_cb(uint8_t idx)
{
  (void) idx;
}

void tuh_cdc_tx_complete_cb(uint8_t idx)
{
  (void) idx;
}
}
