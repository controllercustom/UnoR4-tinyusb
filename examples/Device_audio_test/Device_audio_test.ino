// Device_audio_test.ino — UAC2 microphone that sends a sine wave tone.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC
//
// Uno R4 Minima / Nano R4 (this repo):
//   Console is Serial1 (FTDI bridge on RX1/TX1) @ 115200 — the R4's own USB
//   connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   VBUS: the board does NOT source 5 V from its USB connector. Self-powered
//   (and full-/low-speed) devices work direct; bus-powered ones need
//   backpowering or a powered hub.
//   Configuration: this sketch ships a lean single-role config. Activate it
//   with `./scripts/config.sh set Device_audio_test` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).

#include <UnoR4TinyUSB.h>

// Audio parameters — match tusb_config.h values
#define SAMPLE_RATE   48000
#define N_BYTES       2     // 16-bit samples
#define N_CHANNELS    1

// EP size: one USB full-speed frame (1 ms) of 16-bit mono 48 kHz audio
#define EP_SZ_IN      ((SAMPLE_RATE / 1000 + 1) * N_BYTES * N_CHANNELS)  // 96 bytes

// Sine-wave tone generator (~440 Hz)
static uint32_t phase = 0;
static const uint32_t PHASE_INC = (440 * 65536) / SAMPLE_RATE;  // 16.16 fixed-point

static int16_t sine_next(void) {
  phase += PHASE_INC;
  float angle = (float)(phase & 0xFFFF) / 65536.0f * 2.0f * PI;
  return (int16_t)(sinf(angle) * 16000);
}

// Sample buffer — enough for one USB frame
static int16_t buf[EP_SZ_IN / N_BYTES];

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it.
  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[Audio] UnoR4-TinyUSB UAC2 Microphone");
  UNOR4_TINYUSB_CONSOLE.print("[Audio] Sample rate: ");
  UNOR4_TINYUSB_CONSOLE.print(SAMPLE_RATE);
  UNOR4_TINYUSB_CONSOLE.println(" Hz");

  UNOR4_TINYUSB_CONSOLE.println("[Audio] Device stack initialized.");
}

void loop() {
  // Feed the TinyUSB device task
  tud_arduino_task();

  // Only send audio when the host has opened the streaming interface
  if (!tud_audio_mounted()) return;

  // Fill buffer with one USB frame (1 ms) of sine-wave samples
  uint16_t n_samples = EP_SZ_IN / N_BYTES;
  for (uint16_t i = 0; i < n_samples; i++) {
    buf[i] = sine_next();
  }

  // Write audio data into the EP IN FIFO
  tud_audio_write(buf, sizeof(buf));
}
