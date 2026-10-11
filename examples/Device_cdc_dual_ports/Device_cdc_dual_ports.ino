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
//   with `./scripts/config.sh set Device_cdc_dual_ports` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).
// Device_cdc_dual_ports.ino — dual USB CDC serial ports (device mode).
//
// Ported from TinyUSB examples/device/cdc_dual_ports. Data received on
// either port is echoed to BOTH ports: port 0 lowercases, port 1
// uppercases. The PC sees two /dev/ttyACMn devices.
//
// Works on ALL boards (UnoR4TinyUSB multi-board library).
// Debug: UNOR4_TINYUSB_CONSOLE @ UNOR4_TINYUSB_BAUD.
//
// Adaptations:
// - Descriptors in the usb_descriptors.cpp tab (the .ino prototype
//   generator mangles extern "C").
// - Upstream tud_cdc_line_state_cb() is DROPPED, so the 1200-baud
//   touch-reset to bootloader is gone; connect greetings use
//   tud_cdc_n_connected() edge polling instead.
// - The on-board-button UART-state notification is dropped (no button).
// - CORE LIMIT: with CFG_TUD_CDC=1 (unified Due/Zero/Giga config) the CDC
//   class driver only opens the first CDC function (its cdcd_open() refuses
//   a second instance). Port 0 works fully; port 1 enumerates in the
//   descriptor but stays unclaimed until the config raises CFG_TUD_CDC.
// - LED blink via LED_BUILTIN + millis().

#include <UnoR4TinyUSB.h>
#include <ctype.h>

#define USB_VID 0xCafe
#define USB_PID 0x4006

#define DUAL_CDC_PORTS 2

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500
};

uint32_t blinkIntervalMs = BLINK_NOT_MOUNTED;
static bool cdcGreeted[DUAL_CDC_PORTS] = { false, false };

static void echoSerialPort(uint8_t itf, uint8_t buf[], uint32_t count)
{
  uint8_t const caseDiff = 'a' - 'A';

  for (uint32_t i = 0; i < count; i++)
  {
    if (itf == 0)
    {
      if (isupper(buf[i]))
      {
        buf[i] += caseDiff;
      }
    }
    else
    {
      if (islower(buf[i]))
      {
        buf[i] -= caseDiff;
      }
    }

    tud_cdc_n_write_char(itf, buf[i]);
  }
  tud_cdc_n_write_flush(itf);
}

static void cdcTask(void)
{
  for (uint8_t itf = 0; itf < DUAL_CDC_PORTS; itf++)
  {
    if (tud_cdc_n_available(itf))
    {
      uint8_t buf[64];
      uint32_t count = tud_cdc_n_read(itf, buf, sizeof(buf));

      echoSerialPort(0, buf, count);
      echoSerialPort(1, buf, count);
    }
  }
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
  ledState = !ledState;
}

void setup()
{
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  pinMode(LED_BUILTIN, OUTPUT);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[CDC_Dual] UnoR4TinyUSB dual CDC ports");
  UNOR4_TINYUSB_CONSOLE.println("[CDC_Dual] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  for (uint8_t itf = 0; itf < DUAL_CDC_PORTS; itf++)
  {
    if (tud_cdc_n_connected(itf))
    {
      if (!cdcGreeted[itf])
      {
        cdcGreeted[itf] = true;
        const char *msg = (itf == 0) ? "\r\nTinyUSB dual CDC port 0 (lowercase echo)\r\n"
                                     : "\r\nTinyUSB dual CDC port 1 (UPPERCASE ECHO)\r\n";
        for (const char *p = msg; *p; p++)
        {
          tud_cdc_n_write_char(itf, *p);
        }
        tud_cdc_n_write_flush(itf);
      }
    }
    else
    {
      cdcGreeted[itf] = false;
    }
  }

  cdcTask();
  ledBlinkingTask();
}
