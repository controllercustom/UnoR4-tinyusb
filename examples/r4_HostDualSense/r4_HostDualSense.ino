// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 controllercustom

/*
 * HostDualSense — Sony DualSense (PS5) USB host via TinyUSB.
 * Verified with DualSense CFI-1208 (054C:0CE6) wired USB.
 *
 * DualSense uses the same HID report structure as DS4 but with
 * extended report ID 0x01 (64 bytes): sticks, hat, buttons, triggers,
 * touchpad, IMU, battery, mic. Report ID 0x02 for state reports.
 *
 * Wiring:
 *   R4 Minima --- USB-A ---> DualSense USB (wired)
 *   R4 Minima --- FTDI ---> Serial1 (D0/D1) @115200 debug console
 *
 * Serial commands:
 *   r/g/b = lightbar LED color, 0 = LED off
 *   v = rumble 800ms, 1 = trigger rumble
 */

#include <UnoR4TinyUSB.h>
#include "tusb.h"

static uint8_t prev_rep[64] = {0};
static bool have_rep = false;
static uint8_t cur_led[3] = {0, 0, 0};

static const char *hatDir(uint8_t h)
{
  static const char *dirs[9] = {
    "N", "NE", "E", "SE", "S", "SW", "W", "NW", "center"
  };
  return dirs[h <= 8 ? h : 8];
}

static void handleReport(const uint8_t *r)
{
  uint8_t batt_raw = r[53];
  uint8_t batt_cap = batt_raw & 0x0F;
  uint8_t charging = (batt_raw >> 4) & 0x0F;
  uint8_t batt_pct = (charging == 2) ? 100 : (batt_cap <= 10 ? batt_cap * 10 + 5 : 0);
  if (batt_pct > 100) batt_pct = 100;
  char line[192];
  int o = snprintf(line, sizeof(line),
    "[DualSense] LX=%3u LY=%3u RX=%3u RY=%3u Hat=%-6s L2=%3u R2=%3u Batt=%u%%(%02x) Btn=[",
    r[1], r[2], r[3], r[4], hatDir(r[8] & 0x0F),
    r[5], r[6], batt_pct, batt_raw);

  uint8_t b0 = r[8];
  uint8_t b1 = r[9];
  uint8_t b2 = r[10];
  const char *names[14] = {
    "SQ", "CR", "CI", "TR", "L1", "R1", "L2B", "R2B",
    "SH", "OPT", "L3", "R3", "PS", "TP"
  };
  uint8_t bits[14] = {
    (uint8_t)(b0 & 0x10 ? 1 : 0), (uint8_t)(b0 & 0x20 ? 1 : 0),
    (uint8_t)(b0 & 0x40 ? 1 : 0), (uint8_t)(b0 & 0x80 ? 1 : 0),
    (uint8_t)(b1 & 0x01 ? 1 : 0), (uint8_t)(b1 & 0x02 ? 1 : 0),
    (uint8_t)(b1 & 0x04 ? 1 : 0), (uint8_t)(b1 & 0x08 ? 1 : 0),
    (uint8_t)(b1 & 0x10 ? 1 : 0), (uint8_t)(b1 & 0x20 ? 1 : 0),
    (uint8_t)(b1 & 0x40 ? 1 : 0), (uint8_t)(b1 & 0x80 ? 1 : 0),
    (uint8_t)(b2 & 0x01 ? 1 : 0), (uint8_t)(b2 & 0x02 ? 1 : 0)
  };
  for (int i = 0; i < 14; i++)
  {
    if (bits[i]) o += snprintf(line + o, sizeof(line) - o, "%s ", names[i]);
  }
  if (o > 0 && line[o - 1] == ' ')
  {
    line[o - 1] = '\0';
    o--;
  }
  snprintf(line + o, sizeof(line) - o, "]");
  Serial1.println(line);
}

static void sendOutput(uint8_t r, uint8_t g, uint8_t b, uint8_t small, uint8_t large)
{
  static uint8_t out[31];
  memset(out, 0, sizeof(out));
  out[0] = 0xFF;
  out[1] = 0x00;
  out[2] = 0x00;
  out[3] = small;
  out[4] = large;
  out[5] = r;
  out[6] = g;
  out[7] = b;
  cur_led[0] = r;
  cur_led[1] = g;
  cur_led[2] = b;
  if (tuh_hid_mounted(1, 0))
  {
    bool ok = tuh_hid_send_report(1, 0, 0x05, out, sizeof(out));
    char sline[64];
    snprintf(sline, sizeof(sline), "[DualSense] send_report -> %d", (int) ok);
    Serial1.println(sline);
  }
  else
  {
    Serial1.println("[DualSense] send_report skipped (not mounted)");
  }
}

void setup()
{
  Serial1.begin(115200);
  uint32_t t0 = millis();
  while (!Serial1 && millis() - t0 < 1500)
  {
    delay(10);
  }
  delay(1000);
  Serial1.println("\n### R4 TinyUSB DualSense gamepad ###");
  Serial1.println("Keys: r/g/b=LED, v=rumble, 0=LED off");
  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();
  while (Serial1.available())
  {
    char c = (char) Serial1.read();
    if (c == 'r' || c == 'R')
    {
      sendOutput(255, 0, 0, 0, 0);
      Serial1.println("[DualSense] LED red");
    }
    else if (c == 'g' || c == 'G')
    {
      sendOutput(0, 255, 0, 0, 0);
      Serial1.println("[DualSense] LED green");
    }
    else if (c == 'b' || c == 'B')
    {
      sendOutput(0, 0, 255, 0, 0);
      Serial1.println("[DualSense] LED blue");
    }
    else if (c == '0')
    {
      sendOutput(0, 0, 0, 0, 0);
      Serial1.println("[DualSense] LED off");
    }
    else if (c == 'v' || c == 'V')
    {
      sendOutput(cur_led[0], cur_led[1], cur_led[2], 255, 255);
      Serial1.println("[DualSense] rumble 800ms");
      delay(800);
      sendOutput(cur_led[0], cur_led[1], cur_led[2], 0, 0);
    }
  }
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    lastTick = millis();
    char tbuf[64];
    snprintf(tbuf, sizeof(tbuf), "tick ds5=%d", tuh_hid_mounted(1, 0) ? 1 : 0);
    Serial1.println(tbuf);
  }
  delay(1);
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  uint16_t vid = 0;
  uint16_t pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  char line[64];
  snprintf(line, sizeof(line), "[DualSense] mount daddr=%u vid=%04x pid=%04x", daddr, vid, pid);
  Serial1.println(line);
  have_rep = false;
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  have_rep = false;
  Serial1.println("[DualSense] unmounted");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *report_desc, uint16_t desc_len)
{
  (void) report_desc;
  (void) desc_len;
  char line[64];
  snprintf(line, sizeof(line), "[DualSense] hid mount daddr=%u idx=%u", daddr, idx);
  Serial1.println(line);
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
  (void) daddr;
  (void) idx;
  have_rep = false;
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  if (idx == 0 && len >= 64 && report[0] == 0x01)
  {
    uint8_t a[64];
    uint8_t b[64];
    memcpy(a, report, 64);
    memcpy(b, prev_rep, 64);
    a[7] = 0;
    b[7] = 0;
    memset(&a[30], 0, 10);
    memset(&b[30], 0, 10);
    if (!have_rep || memcmp(a, b, 64) != 0)
    {
      memcpy(prev_rep, report, 64);
      have_rep = true;
      handleReport(report);
    }
  }
  tuh_hid_receive_report(daddr, idx);
}
}
