// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 controllercustom

/*
 * HostKeyboard — Uno R4 Minima USB HID boot-keyboard host via TinyUSB.
 * Sets HID boot protocol for clean 8-byte reports.
 * Supports both Full-Speed and Low-Speed keyboards (direct, no hub needed).
 *
 * Wiring:
 *   Uno R4 --- USB Type-C ---> keyboard (self-powered, direct)
 *   FTDI   --- TX/RX --------> Serial1 (debug output @115200)
 *   J-Link --- SWD ----------> SWDIO/SWCLK (upload + debug)
 */
#include <UnoR4TinyUSB.h>
#include "tusb.h"

static char hidUsageToChar(uint8_t usage, bool shift)
{
  if (usage >= 0x04 && usage <= 0x1D)
  {
    return (char)('a' + (usage - 0x04) + (shift ? ('A' - 'a') : 0));
  }
  if (usage >= 0x1E && usage <= 0x27)
  {
    static const char plain[] = "1234567890";
    static const char shifted[] = "!@#$%^&*()";
    return shift ? shifted[usage - 0x1E] : plain[usage - 0x1E];
  }
  switch (usage)
  {
    case 0x28: return '\n';
    case 0x2B: return '\t';
    case 0x2C: return ' ';
    case 0x2A: return '\b';
    default: return 0;
  }
}

static uint8_t prev_keys[6] = {0};
static uint8_t prev_mod = 0;
static uint16_t vid = 0;
static uint16_t pid = 0;

static void handleBootReport(const uint8_t *r)
{
  uint8_t mod = r[0];
  (void) prev_mod;
  prev_mod = mod;
  bool shift = (mod & 0x22) != 0;
  for (int i = 0; i < 6; i++)
  {
    if (prev_keys[i] == 0) continue;
    bool still = false;
    for (int j = 0; j < 6; j++)
    {
      if (r[2 + j] == prev_keys[i]) { still = true; break; }
    }
    if (!still)
    {
      char c = hidUsageToChar(prev_keys[i], shift);
      if (c == '\n') Serial1.println("Enter released");
      else if (c == '\t') Serial1.println("Tab released");
      else if (c == ' ') Serial1.println("Space released");
      else if (c == '\b') Serial1.println("Backspace released");
      else if (c) { char line[16]; snprintf(line, sizeof(line), "%c released", c); Serial1.println(line); }
    }
  }
  for (int j = 0; j < 6; j++)
  {
    uint8_t u = r[2 + j];
    if (u == 0) continue;
    bool was = false;
    for (int i = 0; i < 6; i++) { if (prev_keys[i] == u) { was = true; break; } }
    if (!was)
    {
      char c = hidUsageToChar(u, shift);
      if (c == '\n') Serial1.println("Enter pressed");
      else if (c == '\t') Serial1.println("Tab pressed");
      else if (c == ' ') Serial1.println("Space pressed");
      else if (c == '\b') Serial1.println("Backspace pressed");
      else if (c) { char line[16]; snprintf(line, sizeof(line), "%c pressed", c); Serial1.println(line); }
    }
  }
  for (int i = 0; i < 6; i++) prev_keys[i] = r[2 + i];
}

void setup()
{
  Serial1.begin(115200);
  uint32_t t0 = millis();
  while (!Serial1 && millis() - t0 < 1500) delay(10);
  delay(1000);
  Serial1.println("\n### Uno R4 TinyUSB keyboard ###");
  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    lastTick = millis();
    Serial1.println("tick");
  }
  delay(1);
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  tuh_vid_pid_get(daddr, &vid, &pid);
  char line[64];
  snprintf(line, sizeof(line), "[KBD] mount daddr=%u vid=%04x pid=%04x", daddr, vid, pid);
  Serial1.println(line);
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  memset(prev_keys, 0, sizeof(prev_keys));
  prev_mod = 0;
  Serial1.println("[KBD] device unmounted");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *report_desc, uint16_t desc_len)
{
  (void) report_desc;
  (void) desc_len;
  char line[64];
  snprintf(line, sizeof(line), "[KBD] hid mount daddr=%u idx=%u", daddr, idx);
  Serial1.println(line);
  if (idx == 0)
  {
    if (!tuh_hid_set_protocol(daddr, idx, 0))
    {
      Serial1.println("[KBD] set_protocol failed, trying receive anyway");
      tuh_hid_receive_report(daddr, idx);
    }
  }
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
  (void) daddr;
  (void) idx;
  memset(prev_keys, 0, sizeof(prev_keys));
  prev_mod = 0;
}

void tuh_hid_set_protocol_complete_cb(uint8_t daddr, uint8_t idx, uint8_t protocol)
{
  (void) protocol;
  char line[64];
  snprintf(line, sizeof(line), "[KBD] proto set daddr=%u idx=%u", daddr, idx);
  Serial1.println(line);
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  if (idx == 0 && len >= 8)
  {
    handleBootReport(report);
  }
  tuh_hid_receive_report(daddr, idx);
}
}
