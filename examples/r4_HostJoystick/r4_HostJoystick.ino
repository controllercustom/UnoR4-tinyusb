// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 controllercustom

/*
 * HostJoystick — Uno R4 Minima USB HID joystick host via TinyUSB.
 * Decodes per-device layouts (detected by VID:PID at mount):
 *   046D:C215 Logitech Extreme 3D Pro: 7-byte reports (10-bit packed X/Y,
 *     4-bit hat, Rz twist, 12 buttons, throttle) at 10 ms.
 *   04D9:011B Holtek game controller + 044F:B10A Thrustmaster stick:
 *     9-byte reports (16 buttons, 4-bit hat, 14-bit X/Y, 8-bit Rz +
 *     slider) at 10 ms (identical DragonRise-style layout).
 *   06A3:075C Saitek X52 stick: 14-byte reports (11-bit X/Y, 10-bit Rz,
 *     Z/Rx/Ry/slider bytes, 34 buttons, 1-8 hat, ministick nibbles).
 * Anything else: raw hex dump on change.
 *
 * Wiring:
 *   Uno R4 --- USB Type-C ---> joystick (HID, VBUS powered)
 *   FTDI   --- TX/RX --------> Serial1 (debug output @115200)
 *   J-Link --- SWD ----------> SWDIO/SWCLK (upload + debug)
 *
 * Prints on report CHANGE only (10 ms polling would flood otherwise).
 */
#include <UnoR4TinyUSB.h>
#include "tusb.h"

static uint8_t prev_rep[16] = {0};
static bool have_rep = false;
static uint16_t joy_vid = 0;
static uint16_t joy_pid = 0;
static volatile bool joy_mounted = false;

static const char *hatNameX52(uint8_t h)
{
  static const char *dirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  if (h >= 1 && h <= 8)
  {
    return dirs[h - 1];
  }
  return "center";
}

static const char *hatName(uint8_t h)
{
  switch (h & 0x0F)
  {
    case 0: return "N";
    case 1: return "NE";
    case 2: return "E";
    case 3: return "SE";
    case 4: return "S";
    case 5: return "SW";
    case 6: return "W";
    case 7: return "NW";
    default: return "center";
  }
}

static void handleJoyReport(const uint8_t *r)
{
  uint16_t x = (uint16_t)r[0] | ((uint16_t)(r[1] & 0x03) << 8);
  uint16_t y = ((uint16_t)(r[1] >> 2) & 0x3F) | ((uint16_t)(r[2] & 0x0F) << 6);
  uint8_t hat = (r[2] >> 4) & 0x0F;
  uint8_t rz = r[3];
  uint8_t b18 = r[4];
  uint8_t slider = r[5];
  uint8_t b912 = r[6] & 0x0F;
  char line[160];
  int o = snprintf(line, sizeof(line),
    "[TUH-JOY] X=%4u Y=%4u Hat=%-6s Rz=%3u Thr=%3u Btn=[", x, y, hatName(hat), rz, slider);
  for (int b = 0; b < 8; b++)
  {
    if (b18 & (1u << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 1);
  }
  for (int b = 0; b < 4; b++)
  {
    if (b912 & (1u << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 9);
  }
  if (o > 0 && line[o - 1] == ' ')
  {
    line[o - 1] = '\0';
    o--;
  }
  snprintf(line + o, sizeof(line) - o, "]");
  Serial1.println(line);
}

static void handleHoltekReport(const uint8_t *r)
{
  uint16_t x = (uint16_t)r[3] | ((uint16_t)(r[4] & 0x3F) << 8);
  uint16_t y = (uint16_t)r[5] | ((uint16_t)(r[6] & 0x3F) << 8);
  uint8_t hat = r[2] & 0x0F;
  char line[160];
  int o = snprintf(line, sizeof(line),
    "[TUH-JOY] X=%5u Y=%5u Hat=%-6s Rz=%3u Thr=%3u Btn=[",
    x, y, hatName(hat), r[7], r[8]);
  for (int b = 0; b < 8; b++)
  {
    if (r[0] & (1u << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 1);
  }
  for (int b = 0; b < 8; b++)
  {
    if (r[1] & (1u << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 9);
  }
  if (o > 0 && line[o - 1] == ' ')
  {
    line[o - 1] = '\0';
    o--;
  }
  snprintf(line + o, sizeof(line) - o, "]");
  Serial1.println(line);
}

static void handleX52Report(const uint8_t *r)
{
  uint16_t x = (uint16_t)r[0] | ((uint16_t)(r[1] & 0x07) << 8);
  uint16_t y = ((uint16_t)(r[1] >> 3) & 0x1F) | ((uint16_t)(r[2] & 0x3F) << 5);
  uint16_t rz = ((uint16_t)(r[2] >> 6) & 0x03) | ((uint16_t)r[3] << 2);
  uint8_t hat = (r[12] >> 4) & 0x0F;
  uint64_t btn = (uint64_t)r[8] | ((uint64_t)r[9] << 8) |
                 ((uint64_t)r[10] << 16) | ((uint64_t)r[11] << 24);
  uint8_t b3334 = r[12] & 0x03;
  if (b3334 & 0x01) btn |= (1ULL << 32);
  if (b3334 & 0x02) btn |= (1ULL << 33);
  char line[192];
  int o = snprintf(line, sizeof(line),
    "[TUH-JOY] X=%4u Y=%4u Hat=%-6s Rz=%4u Z=%3u Rx=%3u Ry=%3u Sl=%3u Mini=%X%X Btn=[",
    x, y, hatNameX52(hat), rz, r[4], r[5], r[6], r[7],
    r[13] & 0x0F, (r[13] >> 4) & 0x0F);
  for (int b = 0; b < 34; b++)
  {
    if (btn & (1ULL << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 1);
  }
  if (o > 0 && line[o - 1] == ' ')
  {
    line[o - 1] = '\0';
    o--;
  }
  snprintf(line + o, sizeof(line) - o, "]");
  Serial1.println(line);
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
  Serial1.println("\n### Uno R4 TinyUSB joystick ###");
  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();

  static bool was_mounted = false;
  if (was_mounted && !joy_mounted)
  {
    Serial1.println("[TUH-JOY] device lost, re-init host...");
    delay(200);
    tuh_arduino_init();
    have_rep = false;
    joy_vid = 0;
    joy_pid = 0;
  }
  was_mounted = joy_mounted;

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
  tuh_vid_pid_get(daddr, &joy_vid, &joy_pid);
  char line[64];
  snprintf(line, sizeof(line), "[TUH-JOY] mount daddr=%u vid=%04x pid=%04x", daddr, joy_vid, joy_pid);
  Serial1.println(line);
  have_rep = false;
  joy_mounted = true;
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  have_rep = false;
  joy_mounted = false;
  Serial1.println("[TUH-JOY] device unmounted");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *report_desc, uint16_t desc_len)
{
  (void) report_desc;
  (void) desc_len;
  char line[64];
  snprintf(line, sizeof(line), "[TUH-JOY] hid mount daddr=%u idx=%u", daddr, idx);
  Serial1.println(line);
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
  char line[64];
  snprintf(line, sizeof(line), "[TUH-JOY] hid unmount daddr=%u idx=%u", daddr, idx);
  Serial1.println(line);
  have_rep = false;
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  if (joy_vid == 0)
  {
    tuh_vid_pid_get(daddr, &joy_vid, &joy_pid);
  }
  if (joy_vid == 0x046D && joy_pid == 0xC215 && len >= 7)
  {
    if (!have_rep || memcmp(report, prev_rep, 7) != 0)
    {
      memcpy(prev_rep, report, 7);
      have_rep = true;
      handleJoyReport(report);
    }
  }
  else if (((joy_vid == 0x04D9 && joy_pid == 0x011B) ||
               (joy_vid == 0x044F && joy_pid == 0xB10A)) && len >= 9)
  {
    if (!have_rep || memcmp(report, prev_rep, 9) != 0)
    {
      memcpy(prev_rep, report, 9);
      have_rep = true;
      handleHoltekReport(report);
    }
  }
  else if (joy_vid == 0x06A3 && joy_pid == 0x075C && len >= 14)
  {
    if (!have_rep || memcmp(report, prev_rep, 14) != 0)
    {
      memcpy(prev_rep, report, 14);
      have_rep = true;
      handleX52Report(report);
    }
  }
  else if (len > 0 && len <= 16)
  {
    if (!have_rep || memcmp(report, prev_rep, len) != 0)
    {
      memcpy(prev_rep, report, len);
      have_rep = true;
      char line[96];
      int o = snprintf(line, sizeof(line), "[TUH-JOY] raw(%u) vid=%04x pid=%04x idx=%u: ", len, joy_vid, joy_pid, idx);
      for (int i = 0; i < len && o < 90; i++)
      {
        o += snprintf(line + o, sizeof(line) - o, "%02x ", report[i]);
      }
      Serial1.println(line);
    }
  }
  tuh_hid_receive_report(daddr, idx);
}
}
