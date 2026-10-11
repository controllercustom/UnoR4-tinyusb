// Device_MSC.ino — RAM disk mass storage device.
//
// Presents a 16 KB RAM-backed disk to the USB host.
// The host can read/write the virtual disk; data persists until reset.
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
//   with `./scripts/config.sh set Device_MSC` (required before building any other
//   example — a device-role config sets CFG_TUH_ENABLED 0, and vice versa).

#include <UnoR4TinyUSB.h>

// 16 KB RAM disk: 32 blocks of 512 bytes each
#define BLOCK_SIZE  512
// Small-SRAM boards need a smaller disk: 32 blocks overflows static RAM
// (heap/stack overlap at link, or — worse — silent runtime stack smash).
// Boards with 32 KB of SRAM overflow by ~364 B at 32 blocks (tighter linker layout than Zero); Zero
// links at 32 blocks but with only ~216 B stack headroom (data+bss 32552/32768)
// and dies reading the config descriptor (error -110). 8 KB enumerates and
// reads/writes fine everywhere (verified on hardware).
#if defined(UNOR4_TINYUSB_BOARD_ZERO)
#define BLOCK_COUNT 16
#else
#define BLOCK_COUNT 32
#endif
#define DISK_SIZE   (BLOCK_SIZE * BLOCK_COUNT)

uint8_t disk[DISK_SIZE];

void setup() {
  UNOR4_TINYUSB_CONSOLE.begin(UNOR4_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before disk fill / console wait): on GIGA this
  // releases Mbed's CDC before the host can enumerate it.
  tud_arduino_init();

  while (!UNOR4_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  UNOR4_TINYUSB_CONSOLE.println("[MSC] UnoR4-TinyUSB RAM Disk");
  UNOR4_TINYUSB_CONSOLE.print("[MSC] Disk size: ");
  UNOR4_TINYUSB_CONSOLE.print(DISK_SIZE);
  UNOR4_TINYUSB_CONSOLE.println(" bytes");

  for (uint32_t i = 0; i < DISK_SIZE; i++) {
    disk[i] = (uint8_t)(i & 0xFF);
  }

  UNOR4_TINYUSB_CONSOLE.println("[MSC] Device stack initialized. Waiting for host...");
}

#if defined(UNOR4_TINYUSB_BOARD_DUE)
extern "C" void dcd_sam3x_stats7(int*, int*, int*, int*, int*, int*);
extern "C" void dcd_sam3x_stats8(uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);

static void decode_ep_cfg(const char *label, uint32_t cfg, uint32_t isr) {
  uint32_t alloc = (cfg >> 1) & 1;
  uint32_t epbk  = (cfg >> 2) & 3;
  uint32_t epsize = (cfg >> 4) & 7;
  uint32_t epdir = (cfg >> 8) & 1;
  uint32_t eptype = (cfg >> 11) & 3;
  uint32_t cfgok = (isr >> 28) & 1;
  UNOR4_TINYUSB_CONSOLE.print("  "); UNOR4_TINYUSB_CONSOLE.print(label);
  UNOR4_TINYUSB_CONSOLE.print(": cfg=0x"); UNOR4_TINYUSB_CONSOLE.print(cfg, HEX);
  UNOR4_TINYUSB_CONSOLE.print(" EPDIR="); UNOR4_TINYUSB_CONSOLE.print(epdir);
  UNOR4_TINYUSB_CONSOLE.print("(0=OUT,1=IN)");
  UNOR4_TINYUSB_CONSOLE.print(" EPTYPE="); UNOR4_TINYUSB_CONSOLE.print(eptype);
  UNOR4_TINYUSB_CONSOLE.print(" EPSIZE="); UNOR4_TINYUSB_CONSOLE.print(epsize);
  UNOR4_TINYUSB_CONSOLE.print(" ALLOC="); UNOR4_TINYUSB_CONSOLE.print(alloc);
  UNOR4_TINYUSB_CONSOLE.print(" CFGOK="); UNOR4_TINYUSB_CONSOLE.print(cfgok);
  UNOR4_TINYUSB_CONSOLE.println();
}

static void dump_hw_regs(const char *tag) {
  uint32_t cfg_out, isr_out, cfg_in, isr_in, devpt, devisr;
  dcd_sam3x_stats8(&cfg_out, &isr_out, &cfg_in, &isr_in, &devpt, &devisr);
  UNOR4_TINYUSB_CONSOLE.print("[MSC] HW "); UNOR4_TINYUSB_CONSOLE.print(tag); UNOR4_TINYUSB_CONSOLE.println(":");
  UNOR4_TINYUSB_CONSOLE.print("  DEVEPT=0x"); UNOR4_TINYUSB_CONSOLE.println(devpt, HEX);
  UNOR4_TINYUSB_CONSOLE.print("  DEVISR=0x"); UNOR4_TINYUSB_CONSOLE.println(devisr, HEX);
  decode_ep_cfg("phys1 OUT", cfg_out, isr_out);
  decode_ep_cfg("phys2 IN ", cfg_in, isr_in);
}
#endif

void loop() {
  tud_arduino_task();

#if defined(UNOR4_TINYUSB_BOARD_DUE)
  // First 10 seconds: dump registers every 500ms to catch EP config + CBW arrival
  static uint32_t boot_time = millis();
  static uint32_t last_hw = 0;
  if (millis() - boot_time < 10000 && millis() - last_hw > 500) {
    last_hw = millis();
    dump_hw_regs("poll");
  }

  // EP1 stats every 3 seconds
  static uint32_t last_stats = 0;
  if (millis() - last_stats > 3000) {
    last_stats = millis();
    int s7_kick, s7_rxouti, s7_complete, s7_no_active, s7_no_rxoute, s7_cfgok_fail;
    dcd_sam3x_stats7(&s7_kick, &s7_rxouti, &s7_complete, &s7_no_active, &s7_no_rxoute, &s7_cfgok_fail);
    UNOR4_TINYUSB_CONSOLE.print("[MSC] EP1: kick=");
    UNOR4_TINYUSB_CONSOLE.print(s7_kick);
    UNOR4_TINYUSB_CONSOLE.print(" rxouti=");
    UNOR4_TINYUSB_CONSOLE.print(s7_rxouti);
    UNOR4_TINYUSB_CONSOLE.print(" complete=");
    UNOR4_TINYUSB_CONSOLE.print(s7_complete);
    UNOR4_TINYUSB_CONSOLE.print(" no_active=");
    UNOR4_TINYUSB_CONSOLE.print(s7_no_active);
    UNOR4_TINYUSB_CONSOLE.print(" no_rxoute=");
    UNOR4_TINYUSB_CONSOLE.print(s7_no_rxoute);
    UNOR4_TINYUSB_CONSOLE.print(" cfgok_fail=");
    UNOR4_TINYUSB_CONSOLE.println(s7_cfgok_fail);
  }
#endif
}
