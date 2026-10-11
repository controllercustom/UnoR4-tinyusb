// SPDX-License-Identifier: MIT
// r4_HostInfo.ino — debugger for a device attached to the R4 host port.
//
// What it is for: the failure mode worth debugging is a device that ATTACHES
// but never MOUNTS -- bad descriptor, class/instance-count mismatch, a control
// transfer that stalls. From the Linux side that is invisible, and a plain
// "device=NO" line does not tell you which stage died. This sketch shows all
// four stages:
//
//   1. port     -- line state and connect/reset status, printed on CHANGE, so
//                  an attach -> reset -> speed-chirp sequence is visible as it
//                  happens (LNST SE0/J/K, RHST LOW/FULL/HIGH, UACT)
//   2. enum     -- per-slot table: VID:PID, mounted?, speed. A slot with a
//                  speed but no VID:PID, or a VID:PID that never mounts, is a
//                  failed enumeration
//   3. describe -- device descriptor, product string, and a full walk of the
//                  configuration descriptor: every interface and endpoint with
//                  its attributes. This is where a malformed or unexpected
//                  descriptor shows up
//   4. pipes    -- RUSB2 register dump, including per-pipe CFG/MAXP/CTR+PID,
//                  which is where HCD-side transfer faults land
//
// Register bit meanings are taken from the vendored
// portable/renesas/rusb2/rusb2_type.h, not from memory: RHST is DVSTCTR0[2:0]
// (000 nothing connected or reset not complete, 001 low, 010 full, 011 high)
// and LNST is SYSSTS0[1:0] (00 SE0, 01 J, 10 K). An earlier version of this
// sketch read RHST from SYSSTS0[1:0] through offset 0x24; SYSSTS0 is at 0x04
// and RHST lives in DVSTCTR0.
//
// Hardware: R4 USB-C -> C-to-A adapter -> device. The R4 does NOT source VBUS,
// so bus-powered devices need backpowering or a powered hub; self-powered and
// low-/full-speed devices work directly.
// Console: Serial1 (FTDI/CP210x on RX1/TX1) @ 115200.
//
// Commands on Serial1:
//   i  device info + configuration walk    r  register dump
//   s  scan all device slots               p  port state now
//   d  toggle the port-transition log      h  this help

#include <UnoR4TinyUSB.h>

#define RUSB2_BASE 0x40090000UL

#define REG16(off) (*(volatile uint16_t *)(RUSB2_BASE + (off)))

// Offsets and bit positions per rusb2_type.h -- checked, not remembered.
// SYSCFG:      USBE 0 | DMRPU 3 | DPRPU 4 | DRPD 5 | DCFM 6 | HSE 7 | CNEN 8 | SCKE 10
// SYSSTS0:     LNST [1:0] | IDMON 2 | SOFEA 5 | HTACT 6 | OVCMON [15:14]
// DVSTCTR0:    RHST [2:0] | UACT 4 | RESUME 5 | USBRST 6 | RWUPE 7 | WKUP 8
//              | VBUSEN 9 | EXICEN 10 | HNPBTOA 11
// There is no SPEEDS field, and PLLSTA is not meaningful on RA4M1 (the USBFS
// clock comes from the system clock manager, and the register reads 0 while
// USB is working), so neither is printed.
#define R_SYSCFG    0x00
#define R_SYSSTS0   0x04
#define R_DVSTCTR0  0x08
#define R_INTENB0   0x30
#define R_INTENB1   0x32
#define R_INTSTS0   0x40
#define R_INTSTS1   0x42
#define R_BRDYSTS   0x46
#define R_NRDYSTS   0x48
#define R_BEMPSTS   0x4a
#define R_PIPESEL   0x64
#define R_PIPECFG   0x68
#define R_PIPEMAXP  0x6c
#define R_PIPECTR0  0x70  // pipe n at 0x70 + 2*n

#define SYSCFG_USBE   0
#define SYSCFG_DMRPU  3
#define SYSCFG_DPRPU  4
#define SYSCFG_DRPD   5
#define SYSCFG_DCFM   6
#define SYSCFG_CNEN   8
#define SYSCFG_SCKE   10

#define DVSTCTR_UACT    4
#define DVSTCTR_RESUME  5
#define DVSTCTR_USBRST  6
#define DVSTCTR_VBUSEN  9

// tusb_option.h supplies these, but be explicit so "#if CFG_TUH_x" is never a
// test of an undefined macro.
#ifndef CFG_TUH_CDC
#define CFG_TUH_CDC 0
#endif
#ifndef CFG_TUH_HID
#define CFG_TUH_HID 0
#endif
#ifndef CFG_TUH_MSC
#define CFG_TUH_MSC 0
#endif
#ifndef CFG_TUH_HUB
#define CFG_TUH_HUB 0
#endif

static char _buf[180];
static char _str[96];
static uint8_t _cfgBuf[512];

static volatile uint8_t _daddr = 0;
static volatile bool _devSeen = false;
static volatile bool _dumpWanted = false;
static bool _logTransitions = true;
static uint16_t _lastStatus = 0xFFFF;
#if CFG_TUH_HID
static uint32_t _lastReport = 0;
#endif

// ---------------------------------------------------------------------------
// decoders
// ---------------------------------------------------------------------------

static const char *lnstStr(uint16_t syssts)
{
  switch (syssts & 0x03)
  {
    case 0: return "SE0";     // no device, or held in reset
    case 1: return "J";       // idle, full-/high-speed
    case 2: return "K";       // chirp/K state
    default: return "RSVD";
  }
}

static const char *rhstStr(uint16_t dvstctr)
{
  switch (dvstctr & 0x07)    // RHST[2:0]
  {
    case 0: return "none/reset";  // nothing connected, or reset not completed
    case 1: return "LOW";
    case 2: return "FULL";
    case 3: return "HIGH";
    default: return "?";
  }
}

// tuh_speed_get() cannot report "empty": TUSB_SPEED_FULL is 0, which is the
// zero-initialised value of an unused slot. Never treat speed alone as
// evidence that something is plugged in.
static const char *speedStr(tusb_speed_t sp)
{
  switch (sp)
  {
    case TUSB_SPEED_LOW:  return "LOW";
    case TUSB_SPEED_FULL: return "FULL";
    case TUSB_SPEED_HIGH: return "HIGH";
    default:              return "n/a";
  }
}

// ---------------------------------------------------------------------------
// reports
// ---------------------------------------------------------------------------

static void portNow(bool forced)
{
  uint16_t syssts = REG16(R_SYSSTS0);
  uint16_t dvstctr = REG16(R_DVSTCTR0);

  // Trigger on the status bits only: RHST[2:0], UACT, RESUME, USBRST, plus
  // HTACT/SOFEA from SYSSTS0. LNST is a live monitor of the data lines and
  // flips with every packet -- including it turned the log into 2432 port lines
  // out of 2467 on a bus with one CDC device on it, and a 115200 UART cannot
  // carry that without perturbing the very bus being watched. LNST is still
  // printed, it is just not a reason to print.
  uint16_t status = (uint16_t) (((dvstctr & 0x0077) << 8) | (syssts & 0x0060));
  if (!forced && status == _lastStatus)
  {
    return;
  }

  snprintf(_buf, sizeof(_buf),
           "[port] LNST=%s(%d) RHST=%s(%d) UACT=%d RESUME=%d USBRST=%d HTACT=%d SOFEA=%d "
           "SYSSTS0=%04x DVSTCTR0=%04x\r\n",
           lnstStr(syssts), syssts & 0x03, rhstStr(dvstctr), dvstctr & 0x07,
           (dvstctr >> DVSTCTR_UACT) & 1, (dvstctr >> DVSTCTR_RESUME) & 1,
           (dvstctr >> DVSTCTR_USBRST) & 1, (syssts >> 6) & 1,
           (syssts >> 5) & 1, syssts, dvstctr);
  Serial1.print(_buf);

  _lastStatus = status;
}

static void regDump(void)
{
  uint16_t syscfg = REG16(R_SYSCFG);
  uint16_t syssts = REG16(R_SYSSTS0);
  uint16_t dvstctr = REG16(R_DVSTCTR0);

  Serial1.println("[regs] --- core ---");
  snprintf(_buf, sizeof(_buf),
           "[regs] SYSCFG=%04x (USBE=%d DRPD=%d DCFM=%d DPRPU=%d CNEN=%d SCKE=%d)",
           syscfg, (syscfg >> SYSCFG_USBE) & 1, (syscfg >> SYSCFG_DRPD) & 1,
           (syscfg >> SYSCFG_DCFM) & 1, (syscfg >> SYSCFG_DPRPU) & 1,
           (syscfg >> SYSCFG_CNEN) & 1, (syscfg >> SYSCFG_SCKE) & 1);
  Serial1.println(_buf);
  snprintf(_buf, sizeof(_buf), "[regs] SYSSTS0=%04x (LNST=%s SOFEA=%d HTACT=%d) DVSTCTR0=%04x (RHST=%s UACT=%d USBRST=%d VBUSEN=%d)",
           syssts, lnstStr(syssts), (syssts >> 5) & 1, (syssts >> 6) & 1,
           dvstctr, rhstStr(dvstctr), (dvstctr >> DVSTCTR_UACT) & 1,
           (dvstctr >> DVSTCTR_USBRST) & 1, (dvstctr >> DVSTCTR_VBUSEN) & 1);
  Serial1.println(_buf);
  snprintf(_buf, sizeof(_buf),
           "[regs] INTENB0=%04x INTENB1=%04x INTSTS0=%04x INTSTS1=%04x",
           REG16(R_INTENB0), REG16(R_INTENB1), REG16(R_INTSTS0), REG16(R_INTSTS1));
  Serial1.println(_buf);
  snprintf(_buf, sizeof(_buf), "[regs] BRDYSTS=%04x NRDYSTS=%04x BEMPSTS=%04x",
           REG16(R_BRDYSTS), REG16(R_NRDYSTS), REG16(R_BEMPSTS));
  Serial1.println(_buf);

  // PIPECFG and PIPEMAXP live behind the PIPESEL window, and the HCD does
  // select-then-access (hcd_rusb2.c: edpt_max_packet_size sets PIPESEL then
  // reads PIPEMAXP). Selecting pipes from main context while the USB ISR is
  // mid-sequence would land its window access on the wrong pipe, so the reads
  // are taken as a snapshot with interrupts masked. Only the reads -- one
  // 90-char line blocks a 115200 UART for ~8 ms, which is long enough to cost
  // SOFs, so the printing happens after the window is closed again.
  // PIPE_CTR is a real register array (rusb2_type.h: PIPE_CTR[9] @ 0x70), so
  // it needs no selection; it is included only to keep the three values
  // consistent with each other.
  struct { uint16_t cfg, maxp, ctr; } pipes[6];
  uint16_t savedPipesel = REG16(R_PIPESEL);

  __disable_irq();
  for (uint8_t p = 0; p <= 5; p++)
  {
    REG16(R_PIPESEL) = p;
    pipes[p].cfg = REG16(R_PIPECFG);
    pipes[p].maxp = REG16(R_PIPEMAXP);
    pipes[p].ctr = REG16(R_PIPECTR0 + 2 * p);
  }
  REG16(R_PIPESEL) = savedPipesel;
  __enable_irq();

  Serial1.println("[regs] --- pipes (PID: 0=NAK 1=BUF 2=STALL 3=ACK) ---");
  for (uint8_t p = 0; p <= 5; p++)
  {
    // PIPECFG: bit0 SCDSEL(epnum), bit1 EPDIR, [4:2] TYPE, bit8 INBFHM, [12:9] EPNUM
    uint16_t cfg = pipes[p].cfg;
    uint16_t ctr = pipes[p].ctr;
    snprintf(_buf, sizeof(_buf),
             "[pipe%u] cfg=%04x (type=%d dir=%s ep=%u) maxp=%04x ctr=%04x (PID=%d PBKREQ=%d)",
             p, cfg, (cfg >> 2) & 7, ((cfg >> 1) & 1) ? "IN" : "OUT",
             (cfg >> 9) & 0x0f, pipes[p].maxp, ctr, ctr & 3, (ctr >> 2) & 1);
    Serial1.println(_buf);
  }
}

static void slotScan(void)
{
  snprintf(_buf, sizeof(_buf), "[slots] scanning slots 1..%u\r\n",
           (unsigned) CFG_TUH_DEVICE_MAX);
  Serial1.print(_buf);

  for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX; a++)
  {
    uint16_t vid = 0, pid = 0;
    bool hasVid = tuh_vid_pid_get(a, &vid, &pid);
    bool mnt = tuh_mounted(a);

    // Only VID and mounted? are evidence of occupation. Speed is not:
    // TUSB_SPEED_FULL == 0 is also the empty-slot value, so a speed-only test
    // diagnoses every empty slot as "enumeration died".
    if (!hasVid && !mnt)
    {
      snprintf(_buf, sizeof(_buf), "[slot%u] empty\r\n", a);
      Serial1.print(_buf);
      continue;
    }

    snprintf(_buf, sizeof(_buf), "[slot%u] vid=%04x pid=%04x mounted=%d speed=%s",
             a, vid, pid, mnt, speedStr(tuh_speed_get(a)));
    Serial1.println(_buf);

    if (!mnt)
    {
      Serial1.println("       ^ enumerated but NOT mounted: no compiled-in driver "
                      "claimed it. Check the CDC/HID/MSC/HUB counts on the boot "
                      "line first -- a class at 0 means this binary, not the "
                      "device. Press `i` for the config walk and compare the "
                      "interface classes against those counts");
    }
  }
}

static void stringCb(tuh_xfer_t *xfer)
{
  if (xfer->result == XFER_RESULT_SUCCESS)
  {
    const uint8_t *s = (const uint8_t *) xfer->buffer;
    uint16_t n = xfer->actual_len > 2 ? (uint16_t) ((xfer->actual_len - 2) / 2) : 0;
    if (n >= sizeof(_str)) n = sizeof(_str) - 1;
    for (uint16_t i = 0; i < n; i++) _str[i] = (char) s[2 + i * 2];
    _str[n] = 0;
    snprintf(_buf, sizeof(_buf), "[dev] product string: %s\r\n", _str);
  }
  else
  {
    snprintf(_buf, sizeof(_buf), "[dev] product string read failed: result=%d len=%u\r\n",
             (int) xfer->result, (unsigned) xfer->actual_len);
  }
  Serial1.print(_buf);
}

static void cfgWalk(uint8_t da)
{
  uint8_t hdr[9];

  if (tuh_descriptor_get_configuration_sync(da, 0, hdr, sizeof(hdr)) != XFER_RESULT_SUCCESS)
  {
    Serial1.println("[cfg] GET_DESCRIPTOR(configuration) header FAILED -- "
                    "the device is not answering control transfers on EP0");
    return;
  }

  uint16_t total = (uint16_t) (hdr[2] | (hdr[3] << 8));
  snprintf(_buf, sizeof(_buf), "[cfg] wTotalLength=%u (ifc=%u)\r\n", total, hdr[4]);
  Serial1.print(_buf);

  if (total > sizeof(_cfgBuf))
  {
    snprintf(_buf, sizeof(_buf), "[cfg] too long to fetch (%u > %u), walking header only\r\n",
             total, (unsigned) sizeof(_cfgBuf));
    Serial1.print(_buf);
    return;
  }

  if (tuh_descriptor_get_configuration_sync(da, 0, _cfgBuf, total) != XFER_RESULT_SUCCESS)
  {
    Serial1.println("[cfg] full configuration descriptor read FAILED");
    return;
  }

  uint16_t itfCount = 0, epCount = 0;
  for (uint16_t off = 0; off + 2 <= total; )
  {
    uint8_t len = _cfgBuf[off];
    uint8_t type = _cfgBuf[off + 1];
    if (len < 2 || off + len > total)
    {
      snprintf(_buf, sizeof(_buf), "[cfg] descriptor stream broken at offset %u (len=%u) -- "
                                  "wTotalLength disagrees with the content\r\n", off, len);
      Serial1.print(_buf);
      return;
    }
    if (type == TUSB_DESC_INTERFACE)
    {
      itfCount++;
      // Interface descriptor layout: [2] bInterfaceNumber, [3] bAlternateSetting,
      // [4] bNumEndpoints, [5..7] class/subclass/protocol, [8] iInterface.
      snprintf(_buf, sizeof(_buf),
               "[ifc %2u] class=%02x.%02x.%02x nEP=%u alt=%u str=%u",
               _cfgBuf[off + 2], _cfgBuf[off + 5], _cfgBuf[off + 6],
               _cfgBuf[off + 7], _cfgBuf[off + 4], _cfgBuf[off + 3], _cfgBuf[off + 8]);
      Serial1.println(_buf);
    }
    else if (type == TUSB_DESC_ENDPOINT)
    {
      epCount++;
      uint8_t ep = _cfgBuf[off + 2];
      uint16_t mps = (uint16_t) (_cfgBuf[off + 4] | (_cfgBuf[off + 5] << 8));
      uint8_t attr = _cfgBuf[off + 3];
      const char *epType = ((attr & 0x03) == 0) ? "control" :
                           ((attr & 0x03) == 1) ? "isoc" :
                           ((attr & 0x03) == 2) ? "bulk" : "interrupt";
      snprintf(_buf, sizeof(_buf), "   [ep 0x%02x] %s %-9s mps=%u interval=%u",
               ep, ((ep & 0x80) ? "IN " : "OUT"), epType, (uint16_t) (mps & 0x07ff),
               _cfgBuf[off + 6]);
      Serial1.println(_buf);
    }
    off += len;
  }

  snprintf(_buf, sizeof(_buf), "[cfg] walked %u bytes: %u interfaces, %u endpoints\r\n",
           total, itfCount, epCount);
  Serial1.print(_buf);
}

// Which address to describe. Prefer the one that mounted; otherwise pick the
// first slot that has a device descriptor. "Enumerated but never mounted" is
// the case this tool exists to look at, and it never reaches tuh_mount_cb, so
// _daddr alone would never point at it.
static uint8_t pickAddr(void)
{
  if (_daddr)
  {
    return _daddr;
  }

  for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX; a++)
  {
    uint16_t v, p;
    if (tuh_vid_pid_get(a, &v, &p))
    {
      return a;
    }
  }
  return 0;
}

static void devDump(uint8_t da)
{
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(da, &vid, &pid);

  // The slot's own speed, not the root port's RHST: behind a hub those differ.
  snprintf(_buf, sizeof(_buf), "[dev] addr=%u vid=%04x pid=%04x speed=%s mounted=%d",
           da, vid, pid, speedStr(tuh_speed_get(da)), tuh_mounted(da));
  Serial1.println(_buf);

  tusb_desc_device_t dd;
  if (tuh_descriptor_get_device_local(da, &dd))
  {
    snprintf(_buf, sizeof(_buf),
             "[dev] USB=%04x class=%02x.%02x.%02x maxEP0=%u cfgs=%u iProd=%u",
             dd.bcdUSB, dd.bDeviceClass, dd.bDeviceSubClass, dd.bDeviceProtocol,
             dd.bMaxPacketSize0, dd.bNumConfigurations, dd.iProduct);
    Serial1.println(_buf);

    if (dd.iProduct)
    {
      // Read the device's supported LANGID list instead of assuming one. The
      // language_id this sketch used to pass, 0, is not a valid LANGID: some
      // devices answer anyway and others stall, and a stall there looks
      // exactly like a broken device.
      uint16_t langid = 0x0409;
      uint8_t lang[2 + 2 * 8];

      if (tuh_descriptor_get_string_langid_sync(da, lang, sizeof(lang)) == XFER_RESULT_SUCCESS)
      {
        uint16_t count = (uint16_t) ((lang[0] >= 2 ? lang[0] - 2 : 0) / 2);
        int n = snprintf(_buf, sizeof(_buf), "[dev] %u supported LANGID(s):", count);
        for (uint16_t i = 0; i < count && n < (int) sizeof(_buf) - 8; i++)
        {
          n += snprintf(_buf + n, sizeof(_buf) - n, " 0x%02x%02x", lang[2 + i * 2 + 1],
                        lang[2 + i * 2]);
        }
        Serial1.println(_buf);
        if (count)
        {
          langid = (uint16_t) (lang[2 + 1] << 8 | lang[2]);
        }
      }
      else
      {
        Serial1.println("[dev] LANGID list read FAILED -- falling back to 0x0409");
      }

      snprintf(_buf, sizeof(_buf), "[dev] GET_DESCRIPTOR(string, idx=%u, langid=0x%04x)",
               dd.iProduct, langid);
      Serial1.println(_buf);

      if (!tuh_descriptor_get_string(da, dd.iProduct, langid, _str, sizeof(_str), stringCb, 0))
      {
        Serial1.println("[dev] product string request could not be queued");
      }
    }
  }
  else
  {
    Serial1.println("[dev] no cached device descriptor (enumeration did not complete)");
  }

  cfgWalk(da);
}

// ---------------------------------------------------------------------------
// TinyUSB host callbacks (task context, not ISR)
//
// The class-specific ones are guarded by their own CFG_TUH_*: this example's
// config enables only the drivers it needs, and a callback for a driver that
// is not compiled in can never fire -- worse, its API does not exist, so an
// unguarded call (tuh_hid_receive_report) does not even compile. The generic
// tuh_mount_cb/tuh_umount_cb are always available and are what the tool relies
// on; the boot line prints which class drivers are actually present.
// ---------------------------------------------------------------------------

extern "C" void tuh_mount_cb(uint8_t daddr)
{
  _daddr = daddr;
  _devSeen = true;
  _dumpWanted = true;
  snprintf(_buf, sizeof(_buf), "[event] MOUNT addr=%u speed=%s\r\n", daddr,
           speedStr(tuh_speed_get(daddr)));
  Serial1.print(_buf);
}

extern "C" void tuh_umount_cb(uint8_t daddr)
{
  _devSeen = false;
  snprintf(_buf, sizeof(_buf), "[event] UMOUNT addr=%u\r\n", daddr);
  Serial1.print(_buf);
}

#if CFG_TUH_CDC
extern "C" void tuh_cdc_mount_cb(uint8_t idx)
{
  snprintf(_buf, sizeof(_buf), "[event] CDC mounted idx=%u\r\n", idx);
  Serial1.print(_buf);
}

extern "C" void tuh_cdc_umount_cb(uint8_t idx)
{
  snprintf(_buf, sizeof(_buf), "[event] CDC unmounted idx=%u\r\n", idx);
  Serial1.print(_buf);
}
#endif

#if CFG_TUH_HID
extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                                 uint8_t const *report_desc, uint16_t desc_len)
{
  (void) report_desc;
  snprintf(_buf, sizeof(_buf), "[event] HID mounted dev=%u idx=%u report_desc=%u B\r\n",
           dev_addr, idx, desc_len);
  Serial1.print(_buf);
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{
  snprintf(_buf, sizeof(_buf), "[event] HID unmounted dev=%u idx=%u\r\n", dev_addr, idx);
  Serial1.print(_buf);
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                           uint8_t const *report, uint16_t len)
{
  // The stack does not re-arm after this callback -- hidh_xfer_cb() invokes it
  // and returns -- so re-arm on EVERY path, including the rate-limited one.
  // Returning without re-arming silences the device permanently, which looks
  // exactly like a device that stopped reporting.
  if (millis() - _lastReport >= 1000)
  {
    _lastReport = millis();

    int n = snprintf(_buf, sizeof(_buf), "[hid] dev=%u idx=%u len=%u:", dev_addr, idx, len);
    for (uint16_t i = 0; i < len && n < (int) sizeof(_buf) - 3; i++)
    {
      n += snprintf(_buf + n, sizeof(_buf) - n, " %02x", report[i]);
    }
    Serial1.println(_buf);
  }

  tuh_hid_receive_report(dev_addr, idx);
}
#endif

#if CFG_TUH_MSC
extern "C" void tuh_msc_mount_cb(uint8_t dev_addr)
{
  snprintf(_buf, sizeof(_buf), "[event] MSC mounted dev=%u\r\n", dev_addr);
  Serial1.print(_buf);
}

extern "C" void tuh_msc_umount_cb(uint8_t dev_addr)
{
  snprintf(_buf, sizeof(_buf), "[event] MSC unmounted dev=%u\r\n", dev_addr);
  Serial1.print(_buf);
}
#endif

// ---------------------------------------------------------------------------

static void help(void)
{
  Serial1.println("[help] i=device+config  r=registers  s=scan slots  p=port now  d=toggle port log");
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial1.begin(115200);
  delay(1000);   // let the console bridge open before the banner

  Serial1.println("[HostInfo] R4 host-port attached-device debugger");
  help();

  // Which class drivers are actually in this binary. A device that enumerates
  // and then does not mount is expected when its class has no driver here, and
  // that is a config fact, not a device fault -- so state it up front rather
  // than leaving the reader hunting for a bug.
  snprintf(_buf, sizeof(_buf), "[cfg] drivers compiled in: CDC=%u HID=%u MSC=%u HUB=%u\r\n",
           (unsigned) CFG_TUH_CDC, (unsigned) CFG_TUH_HID, (unsigned) CFG_TUH_MSC,
           (unsigned) CFG_TUH_HUB);
  Serial1.print(_buf);
  Serial1.println("[host] init: role=host, re-initing rhport 0 as HOST");

  uint16_t preSyscfg = REG16(R_SYSCFG);

  tuh_arduino_init();

  uint16_t syscfg = REG16(R_SYSCFG);
  snprintf(_buf, sizeof(_buf),
           "[host] SYSCFG %04x -> %04x (USBE=%d DCFM=%d DRPD=%d SCKE=%d)",
           preSyscfg, syscfg, (syscfg >> SYSCFG_USBE) & 1,
           (syscfg >> SYSCFG_DCFM) & 1, (syscfg >> SYSCFG_DRPD) & 1,
           (syscfg >> SYSCFG_SCKE) & 1);
  Serial1.println(_buf);

  // Host role needs both: DCFM selects host, DRPD enables the D+/D- pull-downs
  // that make a device's attach visible.
  if (((syscfg >> SYSCFG_DCFM) & 1) == 0 || ((syscfg >> SYSCFG_DRPD) & 1) == 0)
  {
    Serial1.println("[host] WARNING: DCFM and/or DRPD clear -- the port is not "
                    "driving host connection detection, so nothing will attach");
  }

  portNow(true);
}

void loop()
{
  tuh_arduino_poll();

  while (Serial1.available())
  {
    char c = (char) Serial1.read();
    switch (c)
    {
      case 'i':
        {
          uint8_t a = pickAddr();
          if (a == 0)
          {
            Serial1.println("[dev] nothing enumerated on any slot -- 'p' shows the port, 's' lists slots");
          }
          else
          {
            if (!tuh_mounted(a))
            {
              Serial1.println("[dev] note: enumerated but NOT mounted -- the config walk below is "
                              "what the class drivers were offered, and rejected");
            }
            devDump(a);
          }
        }
        break;
      case 'r': regDump(); break;
      case 's': slotScan(); break;
      case 'p': portNow(true); break;
      case 'd':
        _logTransitions = !_logTransitions;
        Serial1.println(_logTransitions ? "[port] transition log ON" : "[port] transition log OFF");
        break;
      case 'h': help(); break;
      default: break;
    }
  }

  if (_logTransitions)
  {
    portNow(false);
  }

  if (_dumpWanted && _devSeen)
  {
    _dumpWanted = false;
    devDump(_daddr);
  }

  digitalWrite(LED_BUILTIN, _devSeen ? (uint8_t) HIGH : (uint8_t) ((millis() / 500) & 1));
}
