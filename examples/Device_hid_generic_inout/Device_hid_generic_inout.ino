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
//   with `./scripts/config.sh set Device_hid_generic_inout` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).
// Device_hid_generic_inout.ino — USB generic HID in/out echo device (UnoR4TinyUSB multi-board).
// Upstream: TinyUSB examples/device/hid_generic_inout (main.c + usb_descriptors.c).
// Test with node-hid (hid_test.js) or python hid package (hid_test.py);
// the device echoes back anything the host sends.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include <UnoR4TinyUSB.h>

#define USB_VID 0xCafe
#define USB_PID 0x4012

extern uint32_t blinkIntervalMs;
extern volatile uint32_t echoCount;

static bool lastMounted = false;
static uint32_t lastEchoCount = 0;

static void ledBlinkingTask(void);

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[HID_Gen] UnoR4TinyUSB generic HID in/out echo (cafe:4012)");
  UNOR4_TINYUSB_CONSOLE.println("[HID_Gen] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  bool mounted = tud_mounted();
  if (mounted != lastMounted)
  {
    lastMounted = mounted;
    UNOR4_TINYUSB_CONSOLE.println(mounted ? "[HID_Gen] mounted" : "[HID_Gen] unmounted");
  }
  if (echoCount != lastEchoCount)
  {
    lastEchoCount = echoCount;
    UNOR4_TINYUSB_CONSOLE.print("[HID_Gen] echoed reports: ");
    UNOR4_TINYUSB_CONSOLE.println(lastEchoCount);
  }
  ledBlinkingTask();
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
