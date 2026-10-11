// Host_cdc_msc_hid.ino — CDC, MSC and HID in one sketch.
//
// Mirrors upstream TinyUSB's examples/host/cdc_msc_hid: one host sketch that
// binds three class drivers and shows what each one's mount/read path looks
// like. Any subset of devices can be attached at once; each class prints on
// its own mount, and nothing here assumes which one showed up first.
//
// The three single-class examples this replaces were thinner versions of the
// same code. Keeping them apart meant three places to update whenever a class
// path changed, and it hid the thing worth seeing: the drivers coexist, and a
// composite device (hub, phone, dock) mounts several at the same time.
//
// Uno R4 Minima / Nano R4:
//   Console is Serial1 (FTDI/CP210x bridge on RX1/TX1) @ 115200 — the R4's own
//   USB connector is the RUSB2 port TinyUSB owns, so there is no USB console.
//   VBUS: the board does NOT source 5 V. Self-powered and full-/low-speed
//   devices work direct; bus-powered ones need backpowering or a powered hub.
//   Configuration: this sketch ships its own tusb_config.h with CDC, HID, MSC
//   and HUB enabled. With the unor4tinyusb package that file is live as-is. On
//   the patched stock core, activate it first: ./scripts/config.sh set
//   Host_cdc_msc_hid
//
// Two console rules are baked in below, because breaking either one looks like
// a device fault rather than a firmware one (AGENTS.md):
//   - Nothing prints on a timer in loop(). One 76-character line blocks a
//     115200 console for ~6.6 ms, long enough to cost SOF frames on a bus that
//     is also carrying isochronous or just-in-time traffic.
//   - tuh_hid_receive_report() re-arms the interrupt IN endpoint, and TinyUSB
//     does not do it for you. Every return path re-arms, including the
//     rate-limit early return. Skip it once and the device looks dead forever.

#include <UnoR4TinyUSB.h>

// ── Compiled-in class set ────────────────────────────────────────────
// Printed at boot so "enumerated but not mounted" reads as a config fact
// rather than a broken device.

static void print_driver_config(void) {
  UNOR4_TINYUSB_CONSOLE.print(F("[cfg] drivers compiled in: CDC="));
  UNOR4_TINYUSB_CONSOLE.print(CFG_TUH_CDC);
  UNOR4_TINYUSB_CONSOLE.print(F(" HID="));
  UNOR4_TINYUSB_CONSOLE.print(CFG_TUH_HID);
  UNOR4_TINYUSB_CONSOLE.print(F(" MSC="));
  UNOR4_TINYUSB_CONSOLE.print(CFG_TUH_MSC);
  UNOR4_TINYUSB_CONSOLE.print(F(" HUB="));
  UNOR4_TINYUSB_CONSOLE.println(CFG_TUH_HUB);
}

// ── Device-level attach ──────────────────────────────────────────────
// tuh_mount_cb fires once the whole configuration has been walked and at least
// one driver bound. Speed comes from tuh_speed_get(), which cannot be trusted
// alone: TUSB_SPEED_FULL is 0, the same value an unused slot holds, so the
// VID:PID is what says whether this slot is real.

extern "C" void tuh_mount_cb(uint8_t dev_addr) {
  uint16_t vid = 0;
  uint16_t pid = 0;
  bool known = tuh_vid_pid_get(dev_addr, &vid, &pid);

  UNOR4_TINYUSB_CONSOLE.print(F("[dev] MOUNT addr="));
  UNOR4_TINYUSB_CONSOLE.print(dev_addr);
  UNOR4_TINYUSB_CONSOLE.print(F(" speed="));
  UNOR4_TINYUSB_CONSOLE.print(tuh_speed_get(dev_addr) == TUSB_SPEED_HIGH ? "HIGH" : "FULL");
  UNOR4_TINYUSB_CONSOLE.print(F(" vidpid="));
  if (!known) {
    UNOR4_TINYUSB_CONSOLE.println(F("(unread)"));
    return;
  }
  if (vid < 0x1000) UNOR4_TINYUSB_CONSOLE.write('0');
  UNOR4_TINYUSB_CONSOLE.print(vid, HEX);
  UNOR4_TINYUSB_CONSOLE.write(':');
  if (pid < 0x1000) UNOR4_TINYUSB_CONSOLE.write('0');
  UNOR4_TINYUSB_CONSOLE.println(pid, HEX);
}

extern "C" void tuh_umount_cb(uint8_t dev_addr) {
  UNOR4_TINYUSB_CONSOLE.print(F("[dev] umount addr="));
  UNOR4_TINYUSB_CONSOLE.println(dev_addr);
}

// ── CDC ──────────────────────────────────────────────────────────────

static uint8_t cdc_idx = 0;
static bool cdc_open = false;

// DTR+RTS on mount. Many CDC devices gate their "connected" flag on DTR, so
// without this the device mounts, sits there, and never sends anything.
static void cdc_line_state_cb(tuh_xfer_t *xfer) {
  if (xfer->result == XFER_RESULT_SUCCESS) {
    UNOR4_TINYUSB_CONSOLE.println(F("[cdc] DTR+RTS set, port open"));
  } else {
    UNOR4_TINYUSB_CONSOLE.print(F("[cdc] line state FAILED result="));
    UNOR4_TINYUSB_CONSOLE.println((int) xfer->result);
  }
}

extern "C" void tuh_cdc_mount_cb(uint8_t idx) {
  cdc_idx = idx;
  cdc_open = true;
  UNOR4_TINYUSB_CONSOLE.print(F("[cdc] mounted idx="));
  UNOR4_TINYUSB_CONSOLE.println(idx);
  tuh_cdc_set_control_line_state(idx,
    CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, cdc_line_state_cb, 0);
}

extern "C" void tuh_cdc_umount_cb(uint8_t idx) {
  if (idx == cdc_idx) cdc_open = false;
  UNOR4_TINYUSB_CONSOLE.print(F("[cdc] umount idx="));
  UNOR4_TINYUSB_CONSOLE.println(idx);
}

static void cdc_poll(void) {
  if (!cdc_open) return;

  uint8_t buf[64];
  uint32_t n = tuh_cdc_read(cdc_idx, buf, sizeof(buf));
  for (uint32_t i = 0; i < n; i++) {
    UNOR4_TINYUSB_CONSOLE.write(buf[i]);
  }

  // Bridge the console to the device, but only as much as the TX FIFO will
  // take in one go: write_available() is the space the ISR has already drained.
  int avail = UNOR4_TINYUSB_CONSOLE.available();
  if (avail > 0 && (uint32_t) tuh_cdc_write_available(cdc_idx) >= 8) {
    uint8_t tx[32];
    uint32_t len = 0;
    while (UNOR4_TINYUSB_CONSOLE.available() && len < sizeof(tx)) {
      tx[len++] = UNOR4_TINYUSB_CONSOLE.read();
    }
    tuh_cdc_write(cdc_idx, tx, len);
    tuh_cdc_write_flush(cdc_idx);
  }
}

// ── MSC ──────────────────────────────────────────────────────────────

static bool msc_ready = false;
static uint8_t msc_addr = 0;
static uint32_t msc_blocks = 0;
static uint32_t msc_lba = 1;
// One SCSI command in flight, always. Overlapping CBWs corrupt the one the
// stack is already tracking; the guard here matches the TU_VERIFY(IDLE) in
// msc_host.c, which rejects rather than queueing.
static bool msc_busy = false;
static uint32_t msc_reads_ok = 0;
static uint32_t msc_reads_bad = 0;

CFG_TUH_MEM_ALIGN static uint8_t sector[512];

// A full 512-byte hex dump is ~1.5 KB of console, which is ~130 ms spent
// blocking the UART. Dump only at mount, where nothing else is in flight;
// periodic reads report one line.
static void dump_prefix(const uint8_t *data, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    if (data[i] < 0x10) UNOR4_TINYUSB_CONSOLE.write('0');
    UNOR4_TINYUSB_CONSOLE.print(data[i], HEX);
    UNOR4_TINYUSB_CONSOLE.write((i & 0x0F) == 0x0F ? '\n' : ' ');
  }
}

// The RP2040 MSCRamDisk fixture fills sector N with the byte (N & 0xFF), so a
// real transfer is distinguishable from a buffer nobody wrote into. Do NOT
// reduce this to a byte-wide sum: 512 bytes of any constant value sum to 0
// modulo 256, so every sector reports the same figure and the check passes on
// an untouched buffer. Compare against the expected pattern instead.
static bool pattern_ok(const uint8_t *data, uint32_t len, uint8_t expected) {
  for (uint32_t i = 0; i < len; i++) {
    if (data[i] != expected) return false;
  }
  return true;
}

static void print_hex8(uint8_t v) {
  if (v < 0x10) UNOR4_TINYUSB_CONSOLE.write('0');
  UNOR4_TINYUSB_CONSOLE.print(v, HEX);
}

static bool read_complete_cb(uint8_t dev_addr,
                             const tuh_msc_complete_data_t *cb_data) {
  (void) dev_addr;

  if (cb_data->csw->status != 0) {
    msc_reads_bad++;
    UNOR4_TINYUSB_CONSOLE.println(F("[msc] read failed, CSW error"));
    msc_busy = false;
    return true;
  }

  const uint8_t want = (uint8_t) (msc_lba & 0xFF);
  const bool good = pattern_ok(sector, 512, want);
  if (good) msc_reads_ok++; else msc_reads_bad++;

  UNOR4_TINYUSB_CONSOLE.print(F("[msc] sector "));
  UNOR4_TINYUSB_CONSOLE.print((unsigned long) msc_lba);
  UNOR4_TINYUSB_CONSOLE.print(good ? F(" pattern=") : F(" MISMATCH want="));
  print_hex8(want);
  if (!good) {
    UNOR4_TINYUSB_CONSOLE.print(F(" got="));
    print_hex8(sector[0]);
  }
  UNOR4_TINYUSB_CONSOLE.print(F("  reads ok="));
  UNOR4_TINYUSB_CONSOLE.print((unsigned long) msc_reads_ok);
  UNOR4_TINYUSB_CONSOLE.print(F(" bad="));
  UNOR4_TINYUSB_CONSOLE.println((unsigned long) msc_reads_bad);

  // Dump the first sector whose pattern is non-zero, so the console shows real
  // transferred bytes rather than the all-zeros that lba 0 legitimately is.
  if (good && msc_lba == 1) dump_prefix(sector, 64);

  msc_busy = false;
  return true;
}

extern "C" void tuh_msc_mount_cb(uint8_t dev_addr) {
  msc_addr = dev_addr;
  msc_blocks = tuh_msc_get_block_count(dev_addr, 0);
  msc_lba = 0;
  UNOR4_TINYUSB_CONSOLE.print(F("[msc] mounted addr="));
  UNOR4_TINYUSB_CONSOLE.print(dev_addr);
  UNOR4_TINYUSB_CONSOLE.print(F(" blocks="));
  UNOR4_TINYUSB_CONSOLE.print((unsigned long) msc_blocks);
  UNOR4_TINYUSB_CONSOLE.print(F(" size="));
  UNOR4_TINYUSB_CONSOLE.print((unsigned long) tuh_msc_get_block_size(dev_addr, 0));
  UNOR4_TINYUSB_CONSOLE.println(F(" bytes"));

  msc_reads_ok = 0;
  msc_reads_bad = 0;
  msc_ready = true;
  if (tuh_msc_read10(dev_addr, 0, sector, 0, 1, read_complete_cb, 0)) {
    msc_busy = true;
  }
}

extern "C" void tuh_msc_umount_cb(uint8_t dev_addr) {
  UNOR4_TINYUSB_CONSOLE.print(F("[msc] umount addr="));
  UNOR4_TINYUSB_CONSOLE.print(dev_addr);
  UNOR4_TINYUSB_CONSOLE.print(F("  reads ok="));
  UNOR4_TINYUSB_CONSOLE.print((unsigned long) msc_reads_ok);
  UNOR4_TINYUSB_CONSOLE.print(F(" bad="));
  UNOR4_TINYUSB_CONSOLE.println((unsigned long) msc_reads_bad);
  msc_ready = false;
  msc_busy = false;
  msc_blocks = 0;
}

static void msc_poll(void) {
  static uint32_t last = 0;
  if (!msc_ready || msc_busy || millis() - last < 5000) return;
  if (msc_lba >= msc_blocks) return;

  last = millis();
  msc_lba++;
  if (tuh_msc_read10(msc_addr, 0, sector, msc_lba, 1, read_complete_cb, 0)) {
    msc_busy = true;
  }
}

// ── HID ──────────────────────────────────────────────────────────────

extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                                 const uint8_t *report_desc, uint16_t desc_len) {
  (void) report_desc;
  (void) desc_len;
  uint8_t proto = tuh_hid_interface_protocol(dev_addr, idx);

  UNOR4_TINYUSB_CONSOLE.print(F("[hid] mounted addr="));
  UNOR4_TINYUSB_CONSOLE.print(dev_addr);
  UNOR4_TINYUSB_CONSOLE.print(F(" itf="));
  UNOR4_TINYUSB_CONSOLE.print(idx);
  UNOR4_TINYUSB_CONSOLE.print(F(" proto="));
  if (proto == HID_ITF_PROTOCOL_KEYBOARD)      UNOR4_TINYUSB_CONSOLE.println(F("keyboard"));
  else if (proto == HID_ITF_PROTOCOL_MOUSE)    UNOR4_TINYUSB_CONSOLE.println(F("mouse"));
  else                                         UNOR4_TINYUSB_CONSOLE.println(F("other"));

  tuh_hid_receive_report(dev_addr, idx);
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx) {
  UNOR4_TINYUSB_CONSOLE.print(F("[hid] umount addr="));
  UNOR4_TINYUSB_CONSOLE.print(dev_addr);
  UNOR4_TINYUSB_CONSOLE.print(F(" itf="));
  UNOR4_TINYUSB_CONSOLE.println(idx);
}

// A mouse at 125 Hz is 125 report lines a second, and at ~40 characters each
// that outruns the console. Throttle per interface — and note that re-arm
// happens before the throttle check, not after it.
extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                           const uint8_t *report, uint16_t len) {
  static uint32_t last_print[CFG_TUH_HID + 1] = { 0 };
  const bool fresh = (millis() - last_print[idx] >= 100);
  if (fresh) last_print[idx] = millis();

  if (!fresh) {
    tuh_hid_receive_report(dev_addr, idx);
    return;
  }

  uint8_t proto = tuh_hid_interface_protocol(dev_addr, idx);

  if (proto == HID_ITF_PROTOCOL_MOUSE && len >= 4) {
    const hid_mouse_report_t *m = (const hid_mouse_report_t *) report;
    UNOR4_TINYUSB_CONSOLE.print(F("[hid] mouse b="));
    UNOR4_TINYUSB_CONSOLE.print(m->buttons, HEX);
    UNOR4_TINYUSB_CONSOLE.print(F(" x="));
    UNOR4_TINYUSB_CONSOLE.print(m->x);
    UNOR4_TINYUSB_CONSOLE.print(F(" y="));
    UNOR4_TINYUSB_CONSOLE.print(m->y);
    UNOR4_TINYUSB_CONSOLE.print(F(" wheel="));
    UNOR4_TINYUSB_CONSOLE.println(m->wheel);
  } else if (proto == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
    const hid_keyboard_report_t *k = (const hid_keyboard_report_t *) report;
    UNOR4_TINYUSB_CONSOLE.print(F("[hid] kbd mod="));
    UNOR4_TINYUSB_CONSOLE.print(k->modifier, HEX);
    UNOR4_TINYUSB_CONSOLE.print(F(" keys="));
    for (uint8_t i = 0; i < 6; i++) {
      if (k->keycode[i] == 0) continue;
      if (k->keycode[i] < 0x10) UNOR4_TINYUSB_CONSOLE.write('0');
      UNOR4_TINYUSB_CONSOLE.print(k->keycode[i], HEX);
      UNOR4_TINYUSB_CONSOLE.write(' ');
    }
    UNOR4_TINYUSB_CONSOLE.println();
  } else {
    UNOR4_TINYUSB_CONSOLE.print(F("[hid] itf="));
    UNOR4_TINYUSB_CONSOLE.print(idx);
    UNOR4_TINYUSB_CONSOLE.print(F(" len="));
    UNOR4_TINYUSB_CONSOLE.print(len);
    UNOR4_TINYUSB_CONSOLE.print(F(" data="));
    for (uint16_t i = 0; i < len && i < 16; i++) {
      if (report[i] < 0x10) UNOR4_TINYUSB_CONSOLE.write('0');
      UNOR4_TINYUSB_CONSOLE.print(report[i], HEX);
    }
    UNOR4_TINYUSB_CONSOLE.println();
  }

  tuh_hid_receive_report(dev_addr, idx);
}

// ── Arduino entry points ─────────────────────────────────────────────

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);
  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println(F("\n=== TinyUSB host: CDC + MSC + HID ==="));
  print_driver_config();
  UNOR4_TINYUSB_CONSOLE.println(F("Waiting for devices (console input bridges to CDC)..."));

  tuh_arduino_init();
}

void loop() {
  tuh_arduino_poll();
  cdc_poll();
  msc_poll();
}
