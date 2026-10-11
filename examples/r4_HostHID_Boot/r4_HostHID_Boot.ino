// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 controllercustom

/*
 * HostTinyUSB_HID_Boot — concurrent LS keyboard + LS mouse (boot only)
 * HID boot protocol 0 for both, prints keyboard presses and mouse moves.
 */
#include <UnoR4TinyUSB.h>
#include "tusb.h"

static uint8_t kbd_prev[6] = {0};
static uint8_t kbd_mod_prev = 0;
static uint8_t mouse_prev[4] = {0};
static bool mouse_have = false;

static char hidToChar(uint8_t usage, bool shift)
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

static void handleKbd(const uint8_t *r)
{
  uint8_t mod = r[0];
  bool shift = (mod & 0x22) != 0;
  for (int i = 0; i < 6; i++)
  {
    if (kbd_prev[i]==0) continue;
    bool still=false;
    for (int j=0;j<6;j++) if (r[2+j]==kbd_prev[i]) still=true;
    if (!still)
    {
      char c = hidToChar(kbd_prev[i], shift);
      if (c=='\n') Serial1.println("KBD Enter released");
      else if (c) { char line[16]; snprintf(line,sizeof(line),"%c released",c); Serial1.println(line); }
    }
  }
  for (int j=0;j<6;j++)
  {
    uint8_t u=r[2+j];
    if (u==0) continue;
    bool was=false;
    for (int i=0;i<6;i++) if (kbd_prev[i]==u) was=true;
    if (!was)
    {
      char c = hidToChar(u, shift);
      if (c=='\n') Serial1.println("KBD Enter pressed");
      else if (c) { char line[16]; snprintf(line,sizeof(line),"%c pressed",c); Serial1.println(line); }
    }
  }
  for (int i=0;i<6;i++) kbd_prev[i]=r[2+i];
  kbd_mod_prev = mod;
}

void setup()
{
  Serial1.begin(115200);
  uint32_t t0 = millis();
  while (!Serial1 && millis() - t0 < 1500) delay(10);
  // no VBUS control on R4 (self-powered device from 5V)
  delay(1000);
  Serial1.println("\n### HID boot KBD+MOUSE ###");
  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();
  static uint32_t lastTick=0;
  if (millis()-lastTick>2000)
  {
    lastTick=millis();
    int hid_cnt=0;
    for (uint8_t d=1; d<=5; d++) for (uint8_t i=0;i<4;i++) if (tuh_hid_mounted(d,i)) hid_cnt++;
    char tbuf[96];
    snprintf(tbuf,sizeof(tbuf),"tick hid_mounted=%d", hid_cnt);
    Serial1.println(tbuf);
  }
  delay(1);
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr){ (void)daddr; Serial1.println("[HID-BOOT] device mounted"); }
void tuh_umount_cb(uint8_t daddr){ (void)daddr; Serial1.println("[HID-BOOT] device unmounted"); memset(kbd_prev,0,sizeof(kbd_prev)); mouse_have=false; }
void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *desc, uint16_t len)
{
  (void)desc; (void)len;
  char line[64];
  snprintf(line,sizeof(line),"[HID-BOOT] hid mount daddr=%u idx=%u",daddr,idx);
  Serial1.println(line);
  if (!tuh_hid_set_protocol(daddr, idx, 0))
  {
    Serial1.println("[HID-BOOT] set_protocol failed");
  }
}
void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx){ (void)daddr; (void)idx; }
void tuh_hid_set_protocol_complete_cb(uint8_t daddr, uint8_t idx, uint8_t proto)
{
  (void)proto;
  tuh_hid_receive_report(daddr, idx);
}
void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  // Distinguish by len: keyboard 8B, mouse 4B
  if (len >= 8 && report[0]==0) {} // placeholder
  // Heuristic: mouse boot has buttons in byte0 bits 0-2, keyboard has mod in byte0
  // Check report desc via idx: idx 0 may be mouse or kbd depending on enum order
  // Use len: keyboard 8, mouse 4
  if (len == 8)
  {
    handleKbd(report);
  }
  else if (len >= 3)
  {
    uint8_t cur[4]={report[0],report[1],report[2], len>3?report[3]:0};
    if (!mouse_have || memcmp(cur, mouse_prev,4)!=0)
    {
      memcpy(mouse_prev,cur,4);
      mouse_have=true;
      uint8_t btn=cur[0];
      char line[96];
      int o=snprintf(line,sizeof(line),"[HID-BOOT] MOUSE dx=%d dy=%d", (int)(int8_t)cur[1], (int)(int8_t)cur[2]);
      if (len>3) o+=snprintf(line+o,sizeof(line)-o," wheel=%d",(int)(int8_t)cur[3]);
      o+=snprintf(line+o,sizeof(line)-o," buttons=[");
      if (btn&0x01) o+=snprintf(line+o,sizeof(line)-o,"LEFT ");
      if (btn&0x02) o+=snprintf(line+o,sizeof(line)-o,"MIDDLE ");
      if (btn&0x04) o+=snprintf(line+o,sizeof(line)-o,"RIGHT ");
      if (o>0 && line[o-1]==' ') {line[o-1]='\0'; o--;}
      snprintf(line+o,sizeof(line)-o,"]");
      Serial1.println(line);
    }
  }
  tuh_hid_receive_report(daddr, idx);
}
}
