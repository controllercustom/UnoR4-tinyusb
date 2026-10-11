/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
//
// Uno R4 Minima / Nano R4 (this repo):
//   Console is Serial1 (FTDI bridge on RX1/TX1) @ 115200 — the R4's own USB
//   connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   VBUS: the board does NOT source 5 V from its USB connector. Self-powered
//   (and full-/low-speed) devices work direct; bus-powered ones need
//   backpowering or a powered hub.
//   Configuration: this sketch ships a lean single-role config. Activate it
//   with `./scripts/config.sh set Device_hid_multiple_interface` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).
// Device_hid_multiple_interface.ino — USB keyboard + mouse on separate HID interfaces (UnoR4TinyUSB multi-board).
// Upstream: TinyUSB examples/device/hid_multiple_interface (main.c + usb_descriptors.c).
// Demo behavior: no button on the board, so a virtual button is pressed for
// 250 ms every 4 s ('a' key + mouse move down-right).
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include <UnoR4TinyUSB.h>

#define USB_VID 0xCafe
#define USB_PID 0x4013

enum
{
  ITF_KEYBOARD = 0,
  ITF_MOUSE = 1
};

extern uint32_t blinkIntervalMs;

static bool lastMounted = false;

static uint32_t demoButton(void)
{
  return ((millis() % 4000) < 250) ? 1 : 0;
}

static void hidTask(void);
static void ledBlinkingTask(void);

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[HID_Multi] UnoR4TinyUSB keyboard + mouse interfaces (cafe:4013)");
  UNOR4_TINYUSB_CONSOLE.println("[HID_Multi] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  bool mounted = tud_mounted();
  if (mounted != lastMounted)
  {
    lastMounted = mounted;
    UNOR4_TINYUSB_CONSOLE.println(mounted ? "[HID_Multi] mounted" : "[HID_Multi] unmounted");
  }
  ledBlinkingTask();
  hidTask();
}

static void hidTask(void)
{
  const uint32_t intervalMs = 10;
  static uint32_t startMs = 0;
  if (millis() - startMs < intervalMs)
  {
    return;
  }
  startMs += intervalMs;

  uint32_t const btn = demoButton();

  if (tud_suspended() && btn)
  {
    tud_remote_wakeup();
  }

  if (tud_hid_n_ready(ITF_KEYBOARD))
  {
    static bool hasKey = false;
    if (btn)
    {
      uint8_t keycode[6] = { 0 };
      keycode[0] = HID_KEY_A;
      tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, keycode);
      hasKey = true;
    }
    else
    {
      if (hasKey)
      {
        tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, NULL);
      }
      hasKey = false;
    }
  }

  // Second HID instance exists only where the config enables it
  // (CFG_TUD_HID > 1).
#if CFG_TUD_HID > 1
  if (tud_hid_n_ready(ITF_MOUSE))
  {
    if (btn)
    {
      int8_t const delta = 5;
      tud_hid_n_mouse_report(ITF_MOUSE, 0, 0x00, delta, delta, 0, 0);
    }
  }
#endif
}

static void ledBlinkingTask(void)
{
  static uint32_t startMs = 0;
  static bool ledState = false;
  if (millis() - startMs < blinkIntervalMs)
  {
    return;
  }
  startMs += blinkIntervalMs;
  digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  ledState = 1 - ledState;
}
