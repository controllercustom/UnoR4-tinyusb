// Host_MIDI.ino — Reads MIDI messages from a USB MIDI device.
//
// Plug a USB MIDI device (keyboard, controller, etc.) into the
// host port.  The sketch enumerates it and prints every MIDI
// message it receives, decoded into human-readable form.
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
//   with `./scripts/config.sh set Host_MIDI` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).

#include <UnoR4TinyUSB.h>

// ── MIDI Note Name Helper ──────────────────────────────────────────

static const char *note_name(uint8_t note) {
  static const char *names[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
  };
  static char buf[5];
  uint8_t octave = (note / 12) - 1;
  if (octave > 9) octave = 9;
  if (octave < 0) octave = -1;
  const char *name = names[note % 12];
  if (octave >= 0) {
    buf[0] = name[0];
    buf[1] = name[1] ? name[1] : ' ';
    buf[2] = '0' + octave;
    buf[3] = '\0';
  } else {
    buf[0] = name[0];
    buf[1] = name[1] ? name[1] : ' ';
    buf[2] = '-';
    buf[3] = '1';
    buf[4] = '\0';
  }
  return buf;
}

// ── TinyUSB Host MIDI callbacks ────────────────────────────────────

extern "C" void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data) {
  UNOR4_TINYUSB_CONSOLE.print(F("\n── MIDI Mounted ──\n"));
  UNOR4_TINYUSB_CONSOLE.print(F("  idx="));     UNOR4_TINYUSB_CONSOLE.print(idx);
  UNOR4_TINYUSB_CONSOLE.print(F("  addr="));    UNOR4_TINYUSB_CONSOLE.print(mount_cb_data->daddr);
  UNOR4_TINYUSB_CONSOLE.print(F("  itf="));     UNOR4_TINYUSB_CONSOLE.print(mount_cb_data->bInterfaceNumber);
  UNOR4_TINYUSB_CONSOLE.print(F("  rx_cables=")); UNOR4_TINYUSB_CONSOLE.print(mount_cb_data->rx_cable_count);
  UNOR4_TINYUSB_CONSOLE.print(F("  tx_cables=")); UNOR4_TINYUSB_CONSOLE.println(mount_cb_data->tx_cable_count);
  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for MIDI data...\n"));
}

extern "C" void tuh_midi_umount_cb(uint8_t idx) {
  UNOR4_TINYUSB_CONSOLE.print(F("\n── MIDI Removed ── idx="));
  UNOR4_TINYUSB_CONSOLE.println(idx);
  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for MIDI devices...\n"));
}

extern "C" void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes) {
  uint8_t packet[4];

  while (tuh_midi_packet_read(idx, packet)) {
    uint8_t cin = packet[0] & 0x0F;
    uint8_t cable = (packet[0] >> 4) & 0x0F;
    uint8_t status = packet[1];
    uint8_t channel = (status & 0x0F) + 1;
    uint8_t data1 = packet[2];
    uint8_t data2 = packet[3];

    UNOR4_TINYUSB_CONSOLE.print(F("[cable="));
    UNOR4_TINYUSB_CONSOLE.print(cable);
    UNOR4_TINYUSB_CONSOLE.print(F("] "));

    switch (cin) {
      case 0x8: // Note Off
        UNOR4_TINYUSB_CONSOLE.print(F("Note Off   ch="));
        UNOR4_TINYUSB_CONSOLE.print(channel);
        UNOR4_TINYUSB_CONSOLE.print(F(" note="));
        UNOR4_TINYUSB_CONSOLE.print(data1);
        UNOR4_TINYUSB_CONSOLE.print(F("("));
        UNOR4_TINYUSB_CONSOLE.print(note_name(data1));
        UNOR4_TINYUSB_CONSOLE.print(F(") vel="));
        UNOR4_TINYUSB_CONSOLE.println(data2);
        break;

      case 0x9: // Note On
        UNOR4_TINYUSB_CONSOLE.print(F("Note On    ch="));
        UNOR4_TINYUSB_CONSOLE.print(channel);
        UNOR4_TINYUSB_CONSOLE.print(F(" note="));
        UNOR4_TINYUSB_CONSOLE.print(data1);
        UNOR4_TINYUSB_CONSOLE.print(F("("));
        UNOR4_TINYUSB_CONSOLE.print(note_name(data1));
        UNOR4_TINYUSB_CONSOLE.print(F(") vel="));
        UNOR4_TINYUSB_CONSOLE.println(data2);
        break;

      case 0xA: // Poly Keypress
        UNOR4_TINYUSB_CONSOLE.print(F("Poly Key   ch="));
        UNOR4_TINYUSB_CONSOLE.print(channel);
        UNOR4_TINYUSB_CONSOLE.print(F(" note="));
        UNOR4_TINYUSB_CONSOLE.print(data1);
        UNOR4_TINYUSB_CONSOLE.print(F(" press="));
        UNOR4_TINYUSB_CONSOLE.println(data2);
        break;

      case 0xB: // Control Change
        UNOR4_TINYUSB_CONSOLE.print(F("CC         ch="));
        UNOR4_TINYUSB_CONSOLE.print(channel);
        UNOR4_TINYUSB_CONSOLE.print(F(" cc="));
        UNOR4_TINYUSB_CONSOLE.print(data1);
        UNOR4_TINYUSB_CONSOLE.print(F(" val="));
        UNOR4_TINYUSB_CONSOLE.println(data2);
        break;

      case 0xC: // Program Change
        UNOR4_TINYUSB_CONSOLE.print(F("Prog Chg   ch="));
        UNOR4_TINYUSB_CONSOLE.print(channel);
        UNOR4_TINYUSB_CONSOLE.print(F(" prog="));
        UNOR4_TINYUSB_CONSOLE.println(data1);
        break;

      case 0xD: // Channel Pressure (Aftertouch)
        UNOR4_TINYUSB_CONSOLE.print(F("Aftertouch ch="));
        UNOR4_TINYUSB_CONSOLE.print(channel);
        UNOR4_TINYUSB_CONSOLE.print(F(" press="));
        UNOR4_TINYUSB_CONSOLE.println(data1);
        break;

      case 0xE: // Pitch Bend
        {
          int16_t bend = (int16_t)((data2 << 7) | data1) - 8192;
          UNOR4_TINYUSB_CONSOLE.print(F("Pitch Bend ch="));
          UNOR4_TINYUSB_CONSOLE.print(channel);
          UNOR4_TINYUSB_CONSOLE.print(F(" val="));
          UNOR4_TINYUSB_CONSOLE.println(bend);
        }
        break;

      case 0x4: // SysEx start / continue
      case 0x5: // SysEx end with 1 byte
      case 0x6: // SysEx end with 2 bytes
      case 0x7: // SysEx end with 3 bytes
        UNOR4_TINYUSB_CONSOLE.print(F("SysEx      "));
        UNOR4_TINYUSB_CONSOLE.print(F("cin=0x"));
        UNOR4_TINYUSB_CONSOLE.print(cin, HEX);
        UNOR4_TINYUSB_CONSOLE.print(F(" data=0x"));
        if (data1 < 0x10) UNOR4_TINYUSB_CONSOLE.print('0');
        UNOR4_TINYUSB_CONSOLE.print(data1, HEX);
        UNOR4_TINYUSB_CONSOLE.print(F(" 0x"));
        if (data2 < 0x10) UNOR4_TINYUSB_CONSOLE.print('0');
        UNOR4_TINYUSB_CONSOLE.print(data2, HEX);
        if (cin >= 0x6 && packet[3] != 0xF7) {
          UNOR4_TINYUSB_CONSOLE.print(F(" 0x"));
          if (packet[3] < 0x10) UNOR4_TINYUSB_CONSOLE.print('0');
          UNOR4_TINYUSB_CONSOLE.print(packet[3], HEX);
        }
        UNOR4_TINYUSB_CONSOLE.println();
        break;

      case 0x2: // System Common: MTC Quarter Frame / Song Select
        UNOR4_TINYUSB_CONSOLE.print(F("SysCommon  "));
        if (status == 0xF1) UNOR4_TINYUSB_CONSOLE.print(F("MTC QF="));
        else if (status == 0xF3) UNOR4_TINYUSB_CONSOLE.print(F("Song Sel="));
        UNOR4_TINYUSB_CONSOLE.println(data1);
        break;

      case 0x3: // System Common: Song Position Pointer
        UNOR4_TINYUSB_CONSOLE.print(F("SysCommon  SPP="));
        UNOR4_TINYUSB_CONSOLE.println((data2 << 7) | data1);
        break;

      case 0xF: // Single byte (real-time, etc.)
        if (status == 0xF8) UNOR4_TINYUSB_CONSOLE.println(F("Timing Clock"));
        else if (status == 0xFA) UNOR4_TINYUSB_CONSOLE.println(F("Start"));
        else if (status == 0xFB) UNOR4_TINYUSB_CONSOLE.println(F("Continue"));
        else if (status == 0xFC) UNOR4_TINYUSB_CONSOLE.println(F("Stop"));
        else if (status == 0xFE) UNOR4_TINYUSB_CONSOLE.println(F("Active Sensing"));
        else if (status == 0xFF) UNOR4_TINYUSB_CONSOLE.println(F("System Reset"));
        else if (status == 0xF6) UNOR4_TINYUSB_CONSOLE.println(F("Tune Request"));
        else {
          UNOR4_TINYUSB_CONSOLE.print(F("SingleByte 0x"));
          UNOR4_TINYUSB_CONSOLE.println(status, HEX);
        }
        break;

      default:
        UNOR4_TINYUSB_CONSOLE.print(F("Unknown    cin=0x"));
        UNOR4_TINYUSB_CONSOLE.print(cin, HEX);
        UNOR4_TINYUSB_CONSOLE.print(F(" data=0x"));
        if (packet[1] < 0x10) UNOR4_TINYUSB_CONSOLE.print('0');
        UNOR4_TINYUSB_CONSOLE.print(packet[1], HEX);
        UNOR4_TINYUSB_CONSOLE.print(F(" 0x"));
        if (packet[2] < 0x10) UNOR4_TINYUSB_CONSOLE.print('0');
        UNOR4_TINYUSB_CONSOLE.print(packet[2], HEX);
        UNOR4_TINYUSB_CONSOLE.print(F(" 0x"));
        if (packet[3] < 0x10) UNOR4_TINYUSB_CONSOLE.print('0');
        UNOR4_TINYUSB_CONSOLE.println(packet[3], HEX);
        break;
    }
  }
}

// ── Arduino setup / loop ────────────────────────────────────────────

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(115200);
  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  UNOR4_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host MIDI ==="));

  // Initialise TinyUSB host stack
  tuh_arduino_init();

  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for MIDI devices...\n"));
}

void loop() {
  // Run the TinyUSB host task — drives enumeration & callbacks
  tuh_arduino_poll();
}
