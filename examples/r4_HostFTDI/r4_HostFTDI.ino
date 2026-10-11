// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 controllercustom

/*
 * HostFTDI — R4 Minima USB-A host UART bridge for FTDI FT232R (0403:6001)
 * via the TinyUSB host stack (tuh_cdc + upstream FTDI serial driver).
 *
 * Wiring:
 *   R4 Minima --- USB-A ---> FTDI USB (FT232R 0403:6001)
 *   R4 Minima --- FTDI ---> Serial1 (D0/D1) @115200 debug console
 *
 * Two topologies:
 *   A. ECHO (default): FTDI TX jumpered to RX. Sketch sends binary seq blocks
 *      0..255 and verifies the loopback echo with seq/CRC counters.
 *   B. CROSS: FTDI TX->D0 (RX1), RX->D1 (TX1), GND->GND. Sketch bridges the
 *      TinyUSB CDC port and Serial1 so bytes round-trip through R4 UART.
 *
 * Console keys: e=ECHO c=CROSS t=TX_FLOOD, 9=9600 1=115200 R=re-configure.
 */
#include <UnoR4TinyUSB.h>
#include "tusb.h"

enum FtdiMode { FTDI_ECHO, FTDI_CROSS, FTDI_TX_FLOOD };
static FtdiMode ftdiMode = FTDI_ECHO;
static const char *ftdiModeName(int m)
{
  switch (m)
  {
    case FTDI_ECHO: return "ECHO";
    case FTDI_CROSS: return "CROSS";
    case FTDI_TX_FLOOD: return "TX_FLOOD";
  }
  return "?";
}

static uint32_t tx_seq = 0, rx_seq_expected = 0;
static uint32_t tx_bytes_total = 0, rx_bytes_total = 0, rx_errors = 0, blocks_ok = 0, blocks_bad = 0;
static uint32_t rx_resyncs = 0;
static uint32_t bench_start_ms = 0, last_report_ms = 0;
static const uint32_t REPORT_INTERVAL_MS = 1000;
static uint8_t txbuf[64], rxbuf[64];
static int ftdi_baud = 115200;
static bool ftdi_ready = false;

static void fillTxBlock(uint8_t *buf, size_t len)
{
  for (size_t i = 0; i < len; i++)
    buf[i] = (uint8_t)(tx_seq++ & 0xFF);
}

static void resetCounters()
{
  tx_seq = 0;
  rx_seq_expected = 0;
  tx_bytes_total = 0;
  rx_bytes_total = 0;
  rx_errors = 0;
  blocks_ok = 0;
  blocks_bad = 0;
  rx_resyncs = 0;
  bench_start_ms = millis();
  last_report_ms = millis();
}

static void printStatus()
{
  uint32_t now = millis();
  uint32_t elapsed = now - bench_start_ms;
  if (elapsed == 0) elapsed = 1;
  uint32_t tx_bps = (tx_bytes_total * 1000UL) / elapsed;
  uint32_t rx_bps = (rx_bytes_total * 1000UL) / elapsed;
  char line[192];
  snprintf(line, sizeof(line), "[FTDI] mode=%s baud=%d elapsed=%lus tx=%lu (%.1f KB/s) rx=%lu (%.1f KB/s) ok=%lu bad=%lu err=%lu rs=%lu",
    ftdiModeName(ftdiMode), ftdi_baud, elapsed / 1000, (unsigned long)tx_bytes_total, tx_bps / 1024.0,
    (unsigned long)rx_bytes_total, rx_bps / 1024.0, (unsigned long)blocks_ok, (unsigned long)blocks_bad, (unsigned long)rx_errors, (unsigned long)rx_resyncs);
  Serial1.println(line);
}

static void ftdi_modem_cb(tuh_xfer_t *xfer)
{
  if (xfer->result != XFER_RESULT_SUCCESS)
  {
    char line[64];
    snprintf(line, sizeof(line), "[FTDI] modem set FAILED result=%d", (int) xfer->result);
    Serial1.println(line);
    return;
  }
  ftdi_ready = true;
  delay(500);
  tuh_cdc_read_clear(0);
  tuh_cdc_write_clear(0);
  resetCounters();
  Serial1.println("[FTDI] ready (baud+8N1+DTR+RTS)");
}

static void ftdi_cfg_cb(tuh_xfer_t *xfer)
{
  if (xfer->result != XFER_RESULT_SUCCESS)
  {
    char line[64];
    snprintf(line, sizeof(line), "[FTDI] line coding set FAILED result=%d", (int) xfer->result);
    Serial1.println(line);
    return;
  }
  tuh_cdc_set_control_line_state(0,
    CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, ftdi_modem_cb, 0);
}

static void ftdi_configure(int baud)
{
  ftdi_ready = false;
  tuh_cdc_read_clear(0);
  tuh_cdc_write_clear(0);
  cdc_line_coding_t line_coding = {0};
  line_coding.bit_rate = (uint32_t) baud;
  line_coding.data_bits = 8;
  tuh_cdc_set_line_coding(0, &line_coding, ftdi_cfg_cb, 0);
}

static void setBaud(int b)
{
  if (b == ftdi_baud && ftdi_ready) return;
  ftdi_baud = b;
  char line[48];
  snprintf(line, sizeof(line), "[FTDI] baud=%d", b);
  Serial1.println(line);
  if (tuh_cdc_mounted(0)) ftdi_configure(b);
  resetCounters();
}

void setup()
{
  Serial1.begin(115200);
  uint32_t t0 = millis();
  while (!Serial1 && millis() - t0 < 1500)
    delay(10);
  delay(1000);
  Serial1.println("\n### R4 TinyUSB FTDI bridge ###");
  Serial1.println("Commands: e=ECHO(TX-RX jumper) c=CROSS(FTDI<->Serial1) t=TX_FLOOD 9=9600 1=115200 R=re-configure");
  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();
  static char cmdBuf[16];
  static uint8_t cmdLen = 0;
  while (Serial1.available())
  {
    char c = (char) Serial1.read();
    if (c == '\r' || c == '\n')
    {
      if (cmdLen == 1)
      {
        char k = cmdBuf[0];
        FtdiMode nm = ftdiMode;
        if (k == 'e' || k == 'E')
          nm = FTDI_ECHO;
        else if (k == 'c' || k == 'C')
          nm = FTDI_CROSS;
        else if (k == 't' || k == 'T')
          nm = FTDI_TX_FLOOD;
        else if (k == '9')
          setBaud(9600);
        else if (k == '1')
          setBaud(115200);
        else if (k == 'R' || k == 'r')
        {
          Serial1.println("[FTDI] re-configure...");
          if (tuh_cdc_mounted(0))
            ftdi_configure(ftdi_baud);
          else
            Serial1.println("[FTDI] not mounted");
        }
        if (nm != ftdiMode)
        {
          ftdiMode = nm;
          resetCounters();
          tuh_cdc_read_clear(0);
          tuh_cdc_write_clear(0);
          while (Serial1.available())
            Serial1.read();
          Serial1.print("[FTDI] switched to ");
          Serial1.println(ftdiModeName(ftdiMode));
        }
      }
      cmdLen = 0;
    }
    else if (cmdLen < sizeof(cmdBuf) - 1)
    {
      cmdBuf[cmdLen++] = c;
    }
    else
    {
      cmdLen = 0;
    }
  }

  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    char tbuf[96];
    snprintf(tbuf, sizeof(tbuf), "tick conn=%d rx=%lu tx=%lu",
      tuh_cdc_mounted(0) ? 1 : 0, (unsigned long)rx_bytes_total,
      (unsigned long)tx_bytes_total);
    Serial1.println(tbuf);
    lastTick = millis();
  }
  if (!tuh_cdc_mounted(0) || !ftdi_ready)
  {
    delay(1);
    return;
  }
  if (millis() - last_report_ms > REPORT_INTERVAL_MS)
  {
    if (tx_bytes_total || rx_bytes_total)
      printStatus();
    last_report_ms = millis();
  }

  switch (ftdiMode)
  {
    case FTDI_ECHO:
    {
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
        if (n == 0) break;
        for (uint32_t i = 0; i < n; i++)
        {
          uint8_t exp = (uint8_t)(rx_seq_expected & 0xFF);
          if (rxbuf[i] != exp)
          {
            uint32_t k;
            for (k = 1; k <= 16; k++)
            {
              if (rxbuf[i] == (uint8_t)((rx_seq_expected + k) & 0xFF))
              {
                break;
              }
            }
            if (k <= 16)
            {
              rx_resyncs++;
              rx_errors += k;
              rx_seq_expected += k;
            }
            else
            {
              rx_errors++;
              if (rx_errors <= 12)
              {
                char dbg[128];
                int o = 0;
                o += snprintf(dbg + o, sizeof(dbg) - o, "[FTDI] slip exp=%02x got=", exp);
                for (uint32_t m = i; m < n && m < i + 8 && o < 110; m++)
                  o += snprintf(dbg + o, sizeof(dbg) - o, "%02x ", rxbuf[m]);
                Serial1.println(dbg);
              }
            }
          }
          rx_seq_expected++;
        }
        rx_bytes_total += n;
      }
      blocks_ok++;
      break;
    }
    case FTDI_CROSS:
    {
      static uint32_t a_seq = 0, a_exp = 0, b_seq = 0, b_exp = 0;
      static uint32_t a_err = 0, b_err = 0;
      uint8_t abuf[16], bbuf[16];
      for (int i = 0; i < 16; i++)
      {
        abuf[i] = (uint8_t)(a_seq++ & 0xFF);
        bbuf[i] = (uint8_t)(b_seq++ & 0xFF);
      }
      tx_bytes_total += tuh_cdc_write(0, abuf, 16);
      tuh_cdc_write_flush(0);
      Serial1.write(bbuf, 16);
      while (tuh_cdc_read_available(0))
      {
        uint32_t avail = tuh_cdc_read_available(0);
        uint32_t toRead = avail < sizeof(rxbuf) ? avail : sizeof(rxbuf);
        uint32_t n = tuh_cdc_read(0, rxbuf, toRead);
        if (n == 0) break;
        for (uint32_t i = 0; i < n; i++)
        {
          uint8_t exp = (uint8_t)(b_exp & 0xFF);
          if (rxbuf[i] != exp)
          {
            b_err++;
            rx_errors++;
          }
          b_exp++;
        }
        rx_bytes_total += n;
      }
      while (Serial1.available())
      {
        int nav = Serial1.available();
        uint32_t toRead = (uint32_t)nav < sizeof(rxbuf) ? (uint32_t)nav : sizeof(rxbuf);
        int n = Serial1.readBytes((char*)rxbuf, toRead);
        if (n <= 0) break;
        for (int i = 0; i < n; i++)
        {
          uint8_t exp = (uint8_t)(a_exp & 0xFF);
          if (rxbuf[i] != exp)
          {
            a_err++;
            rx_errors++;
          }
          a_exp++;
        }
        rx_bytes_total += n;
      }
      blocks_ok++;
      {
        char xbuf[128];
        snprintf(xbuf, sizeof(xbuf), "[FTDI] cross A(dir USB->D0) err=%lu B(dir D1->USB) err=%lu",
          (unsigned long)a_err, (unsigned long)b_err);
        Serial1.println(xbuf);
      }
      break;
    }
    case FTDI_TX_FLOOD:
    {
      fillTxBlock(txbuf, sizeof(txbuf));
      tx_bytes_total += tuh_cdc_write(0, txbuf, sizeof(txbuf));
      tuh_cdc_write_flush(0);
      while (tuh_cdc_read_available(0))
      {
        uint32_t avail = tuh_cdc_read_available(0);
        uint32_t toRead = avail < sizeof(rxbuf) ? avail : sizeof(rxbuf);
        uint32_t n = tuh_cdc_read(0, rxbuf, toRead);
        if (n == 0) break;
        rx_bytes_total += n;
      }
      break;
    }
  }
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  (void) daddr;
  Serial1.println("[FTDI] device mounted");
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  ftdi_ready = false;
  Serial1.println("[FTDI] device unmounted");
}

void tuh_cdc_mount_cb(uint8_t idx)
{
  (void) idx;
  Serial1.println("[FTDI] cdc mounted");
  ftdi_configure(ftdi_baud);
}

void tuh_cdc_umount_cb(uint8_t idx)
{
  (void) idx;
  ftdi_ready = false;
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
