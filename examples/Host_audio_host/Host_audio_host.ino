// Host_audio_host.ino — Streams audio to or from a USB UAC device.
//
// Plug a USB audio device (UAC1 or UAC2 microphone, or DAC) into the host port.
// The sketch enumerates it, selects the first supported configuration of the
// first usable stream, starts streaming, and reports what it is doing:
//
//   Capture  — reads frames and prints an RMS level every ~100 ms.
//   Playback — writes a 440 Hz sine so you can hear the stream running.
//
// Capture streams are preferred; a playback-only DAC (no capture stream) is
// started instead. Both directions use the same async stream API.
//
// Uno R4 Minima / Nano R4 (this repo):
//   Console is Serial1 (FTDI bridge on RX1/TX1) @ 115200 — the R4's own USB
//   connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   VBUS: the board does NOT source 5 V from its USB connector. Self-powered
//   (and full-/low-speed) devices work direct; bus-powered ones need
//   backpowering or a powered hub. Verified with a TI PCM2704 DAC, which
//   declares itself bus-powered at 500 mA, on an injected 5 V rail.
//   Configuration: this sketch ships a lean single-role config. Activate it
//   with `./scripts/config.sh set Host_audio_host` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).
//
// Playback note: a UAC1 device that does not advertise sample-rate control on
// the endpoint (endpoint bmaAttributes bit 0 clear, as on the PCM2704) cannot
// be retuned — the driver correctly skips the request and streams at whatever
// rate the device defaults to. The reported rate is then the first rate the
// device lists, which need not be the rate in force, so `achieved` in the
// statistics is the check that matters: delivery is bounded by device
// consumption, not by this sketch, so a running total matching the reported
// rate proves the device is running at it. On the PCM2704, 31877 f/s against a
// reported 32000 confirms 32 kHz is really in force.

#include <UnoR4TinyUSB.h>

#define TONE_HZ          440
#define SINE_TABLE_SZ    256
#define PLAYBACK_CHUNK   256
#define SINE_AMPLITUDE   10000

// ── Audio state ─────────────────────────────────────────────────────

static bool     audio_active = false;
static bool     is_playback  = false;
static uint8_t  audio_dev    = 0;
static uint8_t  audio_stream = 0;
static uint32_t audio_sample_rate     = 0;
static uint8_t  audio_channels        = 0;
static uint8_t  audio_bytes_per_sample = 0;

// ── Playback generator ─────────────────────────────────────────────

static int16_t  sine_table[SINE_TABLE_SZ];
static uint32_t sine_phase;
static uint32_t sine_step;

static uint32_t fillSine(int16_t *buf, uint32_t frames, uint8_t channels)
{
  for (uint32_t i = 0; i < frames; i++)
  {
    int16_t sample = sine_table[sine_phase >> 24];
    for (uint8_t c = 0; c < channels; c++)
    {
      buf[i * channels + c] = sample;
    }
    sine_phase += sine_step;
  }
  return frames;
}

// ── Playback statistics ────────────────────────────────────────────

static uint32_t frames_written;
static uint32_t bytes_written;
static uint32_t short_writes;
static uint32_t min_avail_seen = 0xFFFFFFFF;

// ── Format name helper ─────────────────────────────────────────────

static const char *format_name(tuh_audio_format_t fmt)
{
  switch (fmt)
  {
    case TUH_AUDIO_FORMAT_S8:      return "S8";
    case TUH_AUDIO_FORMAT_S16_LE:  return "S16_LE";
    case TUH_AUDIO_FORMAT_S24_3LE: return "S24_3LE";
    case TUH_AUDIO_FORMAT_S24_LE:  return "S24_LE";
    case TUH_AUDIO_FORMAT_S32_LE:  return "S32_LE";
    default:                       return "Unknown";
  }
}

// ── Start the first usable stream ──────────────────────────────────
//
// Capture streams are tried first so a microphone keeps working unchanged; a
// playback-only DAC is started when no capture stream exists.

static bool startOne(uint8_t idx, tuh_audio_direction_t want)
{
  uint8_t stream_count = tuh_audio_stream_count(idx);

  for (uint8_t s = 0; s < stream_count; s++)
  {
    if (!tuh_audio_stream_exists(idx, s)) continue;
    if (tuh_audio_stream_direction(idx, s) != want) continue;

    uint8_t config_count = tuh_audio_config_count(idx, s);
    if (config_count == 0)
    {
      UNOR4_TINYUSB_CONSOLE.print(F("  Stream "));
      UNOR4_TINYUSB_CONSOLE.print(s);
      UNOR4_TINYUSB_CONSOLE.println(F(": no configurations available."));
      continue;
    }

    for (uint8_t cfg_idx = 0; cfg_idx < config_count; cfg_idx++)
    {
      tuh_audio_stream_config_t cfg;
      if (!tuh_audio_config_get(idx, s, cfg_idx, &cfg)) continue;

      UNOR4_TINYUSB_CONSOLE.print(F("  Trying config["));
      UNOR4_TINYUSB_CONSOLE.print(cfg_idx);
      UNOR4_TINYUSB_CONSOLE.print(F("]: "));
      UNOR4_TINYUSB_CONSOLE.print(cfg.channels);
      UNOR4_TINYUSB_CONSOLE.print(F("ch "));
      UNOR4_TINYUSB_CONSOLE.print(format_name(cfg.format));
      UNOR4_TINYUSB_CONSOLE.print(F(" @ "));
      UNOR4_TINYUSB_CONSOLE.print(cfg.sample_rate);
      UNOR4_TINYUSB_CONSOLE.println(F(" Hz"));

      if (cfg.format != TUH_AUDIO_FORMAT_S16_LE)
      {
        UNOR4_TINYUSB_CONSOLE.println(F("    Skipped: playback tone generator is S16_LE only."));
        continue;
      }

      if (!tuh_audio_configure(idx, s, cfg_idx))
      {
        UNOR4_TINYUSB_CONSOLE.println(F("    Failed to configure stream."));
        continue;
      }

      if (!tuh_audio_start(idx, s))
      {
        UNOR4_TINYUSB_CONSOLE.println(F("    Failed to start stream."));
        continue;
      }

      audio_dev              = idx;
      audio_stream           = s;
      audio_sample_rate      = cfg.sample_rate;
      audio_channels         = cfg.channels;
      audio_bytes_per_sample = tuh_audio_format_bytes(cfg.format);
      is_playback            = (want == TUH_AUDIO_STREAM_PLAYBACK);

      if (is_playback)
      {
        sine_phase = 0;
        sine_step  = (audio_sample_rate > 0)
                   ? (uint32_t)(((uint64_t)TONE_HZ << 32) / audio_sample_rate)
                   : 0;
      }

      UNOR4_TINYUSB_CONSOLE.print(F("  Streaming started: "));
      UNOR4_TINYUSB_CONSOLE.print(is_playback ? F("playback ") : F("capture "));
      UNOR4_TINYUSB_CONSOLE.print(F("— "));
      UNOR4_TINYUSB_CONSOLE.print(TONE_HZ);
      UNOR4_TINYUSB_CONSOLE.print(F(" Hz sine at "));
      UNOR4_TINYUSB_CONSOLE.print(cfg.sample_rate);
      UNOR4_TINYUSB_CONSOLE.println(F(" Hz."));
      return true;
    }
  }

  return false;
}

static void start_stream(uint8_t idx)
{
  if (startOne(idx, TUH_AUDIO_STREAM_CAPTURE))  return;
  if (startOne(idx, TUH_AUDIO_STREAM_PLAYBACK)) return;

  UNOR4_TINYUSB_CONSOLE.println(F("  No usable capture or playback stream found."));
}

// ── TinyUSB Host Audio callbacks ───────────────────────────────────

extern "C" void tuh_audio_mount_cb(uint8_t idx)
{
  UNOR4_TINYUSB_CONSOLE.print(F("\n── Audio Mounted ── idx="));
  UNOR4_TINYUSB_CONSOLE.println(idx);

  uint8_t stream_count = tuh_audio_stream_count(idx);
  UNOR4_TINYUSB_CONSOLE.print(F("  Streams: "));
  UNOR4_TINYUSB_CONSOLE.println(stream_count);

  for (uint8_t s = 0; s < stream_count; s++)
  {
    if (!tuh_audio_stream_exists(idx, s)) continue;
    tuh_audio_direction_t dir = tuh_audio_stream_direction(idx, s);
    uint8_t n = tuh_audio_config_count(idx, s);

    UNOR4_TINYUSB_CONSOLE.print(F("  Stream "));
    UNOR4_TINYUSB_CONSOLE.print(s);
    UNOR4_TINYUSB_CONSOLE.print(F(": "));
    UNOR4_TINYUSB_CONSOLE.print(dir == TUH_AUDIO_STREAM_CAPTURE ? F("Capture") : F("Playback"));
    UNOR4_TINYUSB_CONSOLE.print(F(", "));
    UNOR4_TINYUSB_CONSOLE.print(n);
    UNOR4_TINYUSB_CONSOLE.println(F(" config(s)"));
  }

  start_stream(idx);
}

extern "C" void tuh_audio_umount_cb(uint8_t idx)
{
  UNOR4_TINYUSB_CONSOLE.print(F("\n── Audio Removed ── idx="));
  UNOR4_TINYUSB_CONSOLE.println(idx);

  if (audio_dev == idx)
  {
    audio_active = false;
    is_playback  = false;
    audio_dev    = 0;
  }

  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for audio devices...\n"));
}

extern "C" void tuh_audio_playback_cb(uint8_t idx, uint8_t stream_idx, uint16_t xferred_bytes)
{
  (void)idx;
  (void)stream_idx;
  (void)xferred_bytes;

  if (!is_playback) return;

  uint32_t avail = tuh_audio_write_available(idx, stream_idx);
  if (avail < min_avail_seen)
  {
    min_avail_seen = avail;
  }
}

extern "C" void tuh_audio_capture_cb(uint8_t idx, uint8_t stream_idx, uint16_t xferred_bytes)
{
  (void)idx;
  (void)stream_idx;

  if (xferred_bytes == 0) return;

  uint32_t available = tuh_audio_read_available(idx, stream_idx);
  if (available == 0) return;

  uint32_t frame_size = (uint32_t)audio_bytes_per_sample * audio_channels;
  if (frame_size == 0) return;

  uint32_t max_frames = 480;
  uint32_t to_read = available < max_frames ? available : max_frames;

  uint8_t buf[480 * 4];
  uint32_t bytes_needed = to_read * frame_size;
  if (bytes_needed > sizeof(buf))
  {
    to_read = sizeof(buf) / frame_size;
    bytes_needed = to_read * frame_size;
  }

  uint32_t frames_read = tuh_audio_read(idx, stream_idx, buf, to_read);
  if (frames_read == 0) return;

  int64_t sum_squares = 0;
  uint32_t total_samples = frames_read * audio_channels;

  for (uint32_t i = 0; i < total_samples; i++)
  {
    int32_t sample = 0;
    switch (audio_bytes_per_sample)
    {
      case 1: sample = (int8_t)buf[i]; break;
      case 2: sample = (int16_t)(buf[i*2] | (buf[i*2+1] << 8)); break;
      case 3: {
        uint32_t v = buf[i*3] | (buf[i*3+1] << 8) | (buf[i*3+2] << 16);
        if (v & 0x800000) v |= 0xFF000000;
        sample = (int32_t)v;
        break;
      }
      case 4: sample = (int32_t)(buf[i*4] | (buf[i*4+1] << 8) |
                                  (buf[i*4+2] << 16) | (buf[i*4+3] << 24));
              break;
    }
    sum_squares += (int64_t)sample * sample;
  }

  static uint32_t last_print = 0;
  uint32_t now = millis();
  if (now - last_print >= 100)
  {
    last_print = now;

    float rms = 0.0f;
    if (total_samples > 0)
    {
      float mean_sq = (float)sum_squares / total_samples;
      rms = sqrtf(mean_sq);
    }

    float full_scale = (1 << (audio_bytes_per_sample * 8 - 1)) - 1;
    float level_pct = (rms / full_scale) * 100.0f;

    UNOR4_TINYUSB_CONSOLE.print(F("Audio Level: "));
    UNOR4_TINYUSB_CONSOLE.print(level_pct, 1);
    UNOR4_TINYUSB_CONSOLE.print(F("%  frames="));
    UNOR4_TINYUSB_CONSOLE.println(frames_read);
  }
}

extern "C" void tuh_audio_event_cb(uint8_t idx, uint8_t stream_idx,
                                   tuh_audio_event_t event, tusb_xfer_result_t result)
{
  switch (event)
  {
    case TUH_AUDIO_EVENT_START_COMPLETE:
      if (result == XFER_RESULT_SUCCESS)
      {
        audio_active = true;
        UNOR4_TINYUSB_CONSOLE.println(F("  Start complete."));
      } else
      {
        UNOR4_TINYUSB_CONSOLE.print(F("  Start failed, result="));
        UNOR4_TINYUSB_CONSOLE.println(result);
      }
      break;

    case TUH_AUDIO_EVENT_STOP_COMPLETE:
      audio_active = false;
      UNOR4_TINYUSB_CONSOLE.println(F("  Stream stopped."));
      break;

    case TUH_AUDIO_EVENT_XFER_FAILED:
      UNOR4_TINYUSB_CONSOLE.print(F("  Transfer error on stream "));
      UNOR4_TINYUSB_CONSOLE.print(stream_idx);
      UNOR4_TINYUSB_CONSOLE.print(F(", result="));
      UNOR4_TINYUSB_CONSOLE.println(result);
      break;
  }
}

// ── Playback pump and status ───────────────────────────────────────

static int16_t  txbuf[PLAYBACK_CHUNK * 2];
static uint32_t stat_last_ms;
static uint32_t stat_last_frames;

static void pumpPlayback(void)
{
  if (!audio_active || !is_playback) return;

  uint32_t frame_size = (uint32_t)audio_bytes_per_sample * audio_channels;
  if (frame_size == 0) return;

  uint32_t avail = tuh_audio_write_available(audio_dev, audio_stream);
  if (avail < frame_size) return;

  uint32_t want_frames = avail / frame_size;
  if (want_frames > PLAYBACK_CHUNK) want_frames = PLAYBACK_CHUNK;

  uint32_t prepared = fillSine(txbuf, want_frames, audio_channels);
  uint32_t w = tuh_audio_write(audio_dev, audio_stream, txbuf, prepared);
  if (w > 0)
  {
    frames_written += w;
    bytes_written  += w * frame_size;
  } else if (want_frames > 0)
  {
    short_writes++;
  }
}

static void printStatus(void)
{
  uint32_t now = millis();
  uint32_t span_ms = now - stat_last_ms;
  uint32_t frames_delta = frames_written - stat_last_frames;
  uint32_t achieved = (span_ms > 0) ? (frames_delta * 1000UL / span_ms) : 0;

  stat_last_ms = now;
  stat_last_frames = frames_written;

  UNOR4_TINYUSB_CONSOLE.print(F("playback: achieved="));
  UNOR4_TINYUSB_CONSOLE.print(achieved);
  UNOR4_TINYUSB_CONSOLE.print(F(" f/s (want "));
  UNOR4_TINYUSB_CONSOLE.print(audio_sample_rate);
  UNOR4_TINYUSB_CONSOLE.print(F("), total="));
  UNOR4_TINYUSB_CONSOLE.print(bytes_written / 1024);
  UNOR4_TINYUSB_CONSOLE.print(F(" KiB, short="));
  UNOR4_TINYUSB_CONSOLE.print(short_writes);
  UNOR4_TINYUSB_CONSOLE.print(F(", min_avail="));
  UNOR4_TINYUSB_CONSOLE.println(min_avail_seen == 0xFFFFFFFF ? 0 : min_avail_seen);
}

// Status is on demand only ('r'), never periodic.
//
// A status line is ~76 characters, which blocks a 115200 UART for ~6.6 ms.
// Steady-state slack is only ~387 B (195 B in the stream FIFO plus the 192 B
// packet in flight), which at 32 kHz stereo S16 is ~3 ms. A periodic print
// therefore outlasts the buffer it depends on, and audio_host.c answers a FIFO
// it cannot fill with a packet of silence -- a 192-byte run of zeros spliced
// into a sine wave is exactly one audible click. It was one click per report
// interval: self-inflicted, not the device.

static void serviceStatus(void)
{
  if (!is_playback || !audio_active) return;

  while (UNOR4_TINYUSB_CONSOLE.available())
  {
    char c = (char)UNOR4_TINYUSB_CONSOLE.read();
    if (c == 'r' || c == 'R')
    {
      printStatus();
    }
  }
}

// ── Arduino setup / loop ────────────────────────────────────────────

void setup(void)
{
  UNOR4_TINYUSB_CONSOLE.begin(115200);
  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  for (uint16_t i = 0; i < SINE_TABLE_SZ; i++)
  {
    float x = (float)i * 2.0f * 3.14159265f / (float)SINE_TABLE_SZ;
    sine_table[i] = (int16_t)(sinf(x) * SINE_AMPLITUDE);
  }

  UNOR4_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host Audio ==="));
  UNOR4_TINYUSB_CONSOLE.print(F("Playback tone generator: "));
  UNOR4_TINYUSB_CONSOLE.print(TONE_HZ);
  UNOR4_TINYUSB_CONSOLE.println(F(" Hz, S16_LE only."));
  UNOR4_TINYUSB_CONSOLE.println(F("Send 'r' for stream statistics."));

  tuh_arduino_init();

  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for audio devices...\n"));
}

void loop(void)
{
  tuh_arduino_poll();
  pumpPlayback();
  serviceStatus();
}