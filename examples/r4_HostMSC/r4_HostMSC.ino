// SPDX-License-Identifier: MIT
// HostMSC.ino — Uno R4 MSC host (ported from Giga-USBH-tinyusb HostTinyUSB_MSC)
#include <UnoR4TinyUSB.h>

static uint8_t msc_dev_addr = 0;
static uint8_t msc_lun = 0;      // LUN that actually holds media
static bool msc_emu_pattern = false;  // MSCRamDisk's 0x01-filled LBA 1 seen?
static bool msc_ready = false;
static uint32_t block_count = 0, block_size = 0;
static uint32_t bench_start = 0, last_report = 0;
static uint32_t bytes_xferred = 0, blocks_xferred = 0;
enum { MSC_READ_RETRIES = 3 };
static uint32_t stalls = 0, retries_left = 0, retry_lba = 0;
static CFG_TUH_MEM_ALIGN uint8_t msc_buf[4096];
static CFG_TUH_MEM_ALIGN uint8_t sync_buf[512];
static volatile bool cb_done = false, cb_success = false;
static bool msc_synced = false;

static bool msc_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data) {
  (void)dev_addr;
  cb_success = (cb_data->csw->status == 0);
  cb_done = true;
  return true;
}
static void resetBench() { bytes_xferred=0; blocks_xferred=0; stalls=0; retries_left=0; retry_lba=0; bench_start=millis(); last_report=millis(); }
static void printStatus() {
  uint32_t elapsed = millis()-bench_start; if(!elapsed) elapsed=1;
  uint32_t bps = (bytes_xferred*1000)/elapsed;
  char line[192]; snprintf(line,sizeof(line),"[MSC] elapsed=%lus blocks=%lu bytes=%lu (%.1f KB/s) stalls=%lu",elapsed/1000,(unsigned long)blocks_xferred,(unsigned long)bytes_xferred,bps/1024.0,(unsigned long)stalls);
  Serial1.println(line);
}

static void msc_dump_regs(void)
{
  volatile uint16_t *base = (volatile uint16_t *)0x40090000;
  char line[96];
  snprintf(line, sizeof(line), "[MSC] reg INTSTS0=%04x BRDY=%04x/%04x NRDY=%04x/%04x FRM=%04x",
    base[0x40/2], base[0x36/2], base[0x46/2], base[0x38/2], base[0x48/2], base[0x4C/2]);
  Serial1.println(line);
  for (int i = 0; i < 5; i++)
  {
    uint16_t ctr = base[(0x70 + i * 2) / 2];
    uint16_t tre = base[(0x90 + i * 4) / 2];
    uint16_t trn = base[(0x90 + i * 4 + 2) / 2];
    snprintf(line, sizeof(line), "[MSC] pipe%u ctr=%04x pid=%u tre=%04x trn=%04x",
      i + 1, ctr, ctr & 3, tre, trn);
    Serial1.println(line);
  }
}

// A successful READ10 (CSW status 0) is what we are actually proving. The old
// version additionally required all 512 bytes to be 0x01 -- true only of the
// MSCRamDisk emulator -- so against real media, whose LBA 1 is a filesystem
// structure, it could never sync and the bench sat at "blocks=0" forever. The
// pattern check is kept, but only as a report of which medium we are on.
static bool msc_sync(void)
{
  for (int attempt = 0; attempt < 3; attempt++)
  {
    cb_done = false; cb_success = false;
    if (!tuh_msc_read10(msc_dev_addr, msc_lun, sync_buf, 1, 1, msc_complete_cb, 0)) { delay(50); continue; }
    uint32_t wait = millis();
    while (!cb_done && millis() - wait < 2000) tuh_arduino_poll();
    if (!cb_done || !cb_success) { delay(50); continue; }
    msc_emu_pattern = true;
    for (int i = 0; i < 512; i++) if (sync_buf[i] != 0x01) { msc_emu_pattern = false; break; }
    return true;
  }
  return false;
}

void setup() {
  pinMode(LED_BUILTIN,OUTPUT);
  Serial1.begin(115200);
  delay(1000);
  Serial1.println("\n### R4 MSC bench ###");
  tuh_arduino_init();
}
void loop() {
  tuh_arduino_poll();
  static uint32_t lastTick=0;
  if(millis()-lastTick>2000){
    lastTick=millis();
    char t[96]; snprintf(t,sizeof(t),"tick msc=%d ready=%d blocks=%lu", msc_dev_addr? (tuh_msc_mounted(msc_dev_addr)?1:0):0, msc_ready?1:0,(unsigned long)block_count);
    Serial1.println(t);
    digitalWrite(LED_BUILTIN, msc_ready?HIGH:(millis()/500&1));
  }
  if(!msc_dev_addr || !tuh_msc_mounted(msc_dev_addr)) return;
  if(!msc_ready || !block_count || !block_size) return;
  if(!msc_synced) {
    if(msc_sync()) {
      msc_synced = true; resetBench();
      char sl[96]; snprintf(sl,sizeof(sl),"[MSC] synced on lun=%u (%s)", msc_lun,
        msc_emu_pattern ? "MSCRamDisk 0x01 pattern" : "real media");
      Serial1.println(sl);
    }
    else { Serial1.println("[MSC] sync failed"); msc_ready = false; }
    return;
  }
  if(millis()-last_report>1000 && bytes_xferred){ printStatus(); last_report=millis(); }
  if(blocks_xferred>=block_count) blocks_xferred=0;

  // Retry the SAME LBA instead of abandoning the bench on one hiccup. The old
  // code set msc_ready=false on the first timeout, so a single transient stall
  // ended the run and a register dump was all that survived -- which is how the
  // intermittent stalls on a real card reader looked like a hard failure.
  //
  // One counter, one decision point. An earlier version decremented the budget
  // on the command-rejected path but only tested for exhaustion on the
  // transfer-failed path, so a device that stopped accepting commands spun
  // forever with retries_left cycling 3->0->3 and no forward progress.
  uint32_t lba = retry_lba;
  uint32_t remaining=block_count-lba;
  // 4 sectors (2 KB) per READ10. 8 sectors (4 KB) times out against a Realtek
  // 0bda:0306 reader; the r4_msc_file_explorer uses the same 2 KB and sustains
  // ~940 KB/s, so 2 KB is the size known to work on the RUSB2 HCD here.
  uint32_t to_read = remaining>4?4:remaining;
  uint32_t xfer_bytes=to_read*block_size;
  if(xfer_bytes>sizeof(msc_buf)){ to_read=sizeof(msc_buf)/block_size; xfer_bytes=to_read*block_size; }
  cb_done=false; cb_success=false;
  bool ok=tuh_msc_read10(msc_dev_addr,msc_lun,msc_buf,lba,to_read,msc_complete_cb,0);
  if(ok) {
    uint32_t wait=millis();
    bool slow=false;
    while(!cb_done && millis()-wait<5000) {
      tuh_arduino_poll();
      if(!slow && millis()-wait>2500) { slow=true; Serial1.println("[MSC] slow transfer (>2.5s)..."); }
    }
    if(cb_done && cb_success) {
      retries_left=0;
      blocks_xferred+=to_read; bytes_xferred+=xfer_bytes;
      retry_lba = blocks_xferred;
      return;
    }
  }
  // Both "command rejected" and "transfer failed" land here.
  stalls++;
  if(retries_left < MSC_READ_RETRIES) retries_left++;
  if(retries_left >= MSC_READ_RETRIES) {
    // Out of retries: step over this region so the bench keeps measuring, and
    // say so rather than letting the report look like a clean run.
    Serial1.println("[MSC] giving up on this LBA, skipping ahead");
    msc_dump_regs();  // RUSB2 snapshot: the case you actually want it for
    blocks_xferred = lba + to_read;
    retry_lba = blocks_xferred;
    retries_left = 0;
  } else {
    delay(5);
  }
}

extern "C" {
void tuh_mount_cb(uint8_t daddr){ (void)daddr; Serial1.println("[MSC] device mounted"); }
void tuh_umount_cb(uint8_t daddr){ (void)daddr; Serial1.println("[MSC] device unmounted"); if(daddr==msc_dev_addr){msc_dev_addr=0;msc_ready=false;block_count=0;block_size=0;msc_lun=0;}}
void tuh_msc_mount_cb(uint8_t dev_addr){
  msc_dev_addr=dev_addr;
  // Pick the first LUN that actually has media. A multi-LUN reader (Realtek
  // 0bda:0306) leaves LUN 0 empty and puts the card on LUN 1, so the old
  // hardcoded lun 0 gave block_count == 0 and the bench never started at all.
  block_count=0; block_size=0; msc_lun=0;
  uint32_t with_media=0;
  for (uint8_t l=0; l<CFG_TUH_MSC_MAXLUN; l++) {
    uint32_t bc=tuh_msc_get_block_count(dev_addr,l);
    if (!bc) continue;
    with_media++;
    if (!block_count) { block_count=bc; block_size=tuh_msc_get_block_size(dev_addr,l); msc_lun=l; }
  }
  char line[160];
  snprintf(line,sizeof(line),"[MSC] msc mounted dev=%u lun=%u blocks=%lu bs=%lu (%lu LUN with media)",
    dev_addr, msc_lun, (unsigned long)block_count, (unsigned long)block_size, (unsigned long)with_media);
  Serial1.println(line);
  msc_ready = (block_count && block_size);
  if (!msc_ready) Serial1.println("[MSC] no LUN reported media");
  retry_lba = 0;
  msc_synced=false; resetBench();
}
void tuh_msc_umount_cb(uint8_t dev_addr){ Serial1.println("[MSC] msc unmounted"); if(dev_addr==msc_dev_addr){msc_dev_addr=0;msc_ready=false;} }
}
