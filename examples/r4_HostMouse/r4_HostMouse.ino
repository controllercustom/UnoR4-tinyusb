// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom

/*
 * HostMouse — Uno R4 Minima USB HID mouse/trackball host via TinyUSB.
 * Decodes 3-byte relative mouse reports: buttons + signed dx/dy.
 * Also handles boot mice (4-byte reports with wheel).
 *
 * Wiring:
 *   Uno R4 --- USB Type-C ---> mouse/trackball (self-powered, direct)
 *   FTDI   --- TX/RX --------> Serial1 (debug output @115200)
 *   J-Link --- SWD ----------> SWDIO/SWCLK (upload + debug)
 */
#include <UnoR4TinyUSB.h>
#include "tusb.h"

static int16_t accum_x = 0;
static int16_t accum_y = 0;
static uint8_t prev_buttons = 0;
static volatile bool dev_mounted = false;

static void decodeMouse(const uint8_t *r, uint16_t len)
{
  uint8_t buttons = r[0];
  int8_t dx = (int8_t)r[1];
  int8_t dy = (int8_t)r[2];
  int8_t wheel = (len > 3) ? (int8_t)r[3] : 0;

  accum_x += dx;
  accum_y += dy;

  uint8_t changed = buttons ^ prev_buttons;
  if (changed || dx || dy || wheel)
  {
    char line[120];
    int o = snprintf(line, sizeof(line),
      "[MOUSE] dx=%+4d dy=%+4d acc=(%+5d,%+5d)",
      dx, dy, accum_x, accum_y);

    if (buttons || changed)
    {
      o += snprintf(line + o, sizeof(line) - o, " btn=[");
      if (buttons & 0x01) o += snprintf(line + o, sizeof(line) - o, "L");
      if (buttons & 0x02) o += snprintf(line + o, sizeof(line) - o, "R");
      if (buttons & 0x04) o += snprintf(line + o, sizeof(line) - o, "M");
      if (changed & 0x01) o += snprintf(line + o, sizeof(line) - o, "L%s", (buttons & 0x01) ? "+" : "-");
      if (changed & 0x02) o += snprintf(line + o, sizeof(line) - o, "R%s", (buttons & 0x02) ? "+" : "-");
      if (changed & 0x04) o += snprintf(line + o, sizeof(line) - o, "M%s", (buttons & 0x04) ? "+" : "-");
      o += snprintf(line + o, sizeof(line) - o, "]");
    }

    if (wheel)
    {
      o += snprintf(line + o, sizeof(line) - o, " wheel=%+d", wheel);
    }

    Serial1.println(line);
    prev_buttons = buttons;
  }
}

void setup()
{
  Serial1.begin(115200);
  uint32_t t0 = millis();
  while (!Serial1 && millis() - t0 < 1500) delay(10);
  delay(1000);
  Serial1.println("\n### Uno R4 TinyUSB mouse ###");
  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();

  static bool was_mounted = false;
  if (was_mounted && !dev_mounted)
  {
    Serial1.println("[MOUSE] lost, re-init...");
    delay(200);
    tuh_arduino_init();
    accum_x = 0;
    accum_y = 0;
    prev_buttons = 0;
  }
  was_mounted = dev_mounted;

  delay(1);
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  uint16_t v, p;
  tuh_vid_pid_get(daddr, &v, &p);
  char line[64];
  snprintf(line, sizeof(line), "[MOUSE] mount daddr=%u vid=%04x pid=%04x", daddr, v, p);
  Serial1.println(line);
  dev_mounted = true;
}

void tuh_umount_cb(uint8_t daddr)
{
  (void)daddr;
  dev_mounted = false;
  Serial1.println("[MOUSE] unmounted");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *report_desc, uint16_t desc_len)
{
  (void)report_desc;
  (void)desc_len;
  char line[64];
  snprintf(line, sizeof(line), "[MOUSE] hid mount daddr=%u idx=%u", daddr, idx);
  Serial1.println(line);
  for (uint8_t i = 0; i < CFG_TUH_HID; i++)
  {
    tuh_hid_receive_report(daddr, i);
  }
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
  (void)daddr;
  (void)idx;
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  if (len >= 3)
  {
    decodeMouse(report, len);
  }
  tuh_hid_receive_report(daddr, idx);
}
}
