// SPDX-License-Identifier: MIT
// bsp_common.cpp — pieces shared by every board support file.
//
// Only the R4 BSP exists today, so this file holds what would be shared:
// the time source, the ISR-safe debug printf, and weak default callbacks for
// the class drivers whose callbacks the library owns on the R4.

#include "bsp_common.h"

#include "../UnoR4TinyUSB.h"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif
  #include "common/tusb_types.h"
#ifdef __cplusplus
}
#endif

// ---- ISR-safe debug printf --------------------------------------------------
// CFG_TUSB_DEBUG routes TU_LOG through the USB ISR. Serial1.write() blocks on
// the FSP SCI TX-done IRQ, which cannot preempt the USB ISR that called us, so
// a direct write deadlocks. ISR context enqueues into a ring; the task pumps
// drain it. Ring overflow drops the newest bytes rather than corrupting the
// queue.

#define R4TUSB_DBG_RING_SIZE 1024
static volatile uint16_t _dbg_head = 0;
static volatile uint16_t _dbg_tail = 0;
static uint8_t _dbg_ring[R4TUSB_DBG_RING_SIZE];

static uint32_t r4tusb_in_isr(void)
{
  uint32_t ipsr;
  __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
  return ipsr;
}

void arduino_debug_drain(void)
{
  while (_dbg_tail != _dbg_head)
  {
    UNOR4_TINYUSB_CONSOLE.write(_dbg_ring[_dbg_tail]);
    _dbg_tail = (uint16_t)((_dbg_tail + 1) % R4TUSB_DBG_RING_SIZE);
  }
}

extern "C" int arduino_debug_printf(char const *format, ...)
{
  char buf[160];
  va_list args;
  va_start(args, format);
  int n = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (n <= 0)
  {
    return n;
  }
  if (n > (int)sizeof(buf))
  {
    n = (int)sizeof(buf);
  }

  if (r4tusb_in_isr())
  {
    for (int i = 0; i < n; i++)
    {
      uint16_t next = (uint16_t)((_dbg_head + 1) % R4TUSB_DBG_RING_SIZE);
      if (next == _dbg_tail)
      {
        break;
      }
      _dbg_ring[_dbg_head] = (uint8_t)buf[i];
      _dbg_head = next;
    }
  }
  else
  {
    arduino_debug_drain();
    UNOR4_TINYUSB_CONSOLE.write((const uint8_t *)buf, (size_t)n);
  }
  return n;
}

// ---- Weak device callbacks --------------------------------------------------
// The class drivers ship no weak defaults for these; a sketch that leaves one
// undefined must still link when the class is compiled in.

TU_ATTR_WEAK uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
  (void)instance;
  return NULL;
}

#if CFG_TUD_HID
TU_ATTR_WEAK uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                             hid_report_type_t report_type,
                                             uint8_t *buffer, uint16_t reqlen)
{
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
  return 0;
}

TU_ATTR_WEAK void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                         hid_report_type_t report_type,
                                         uint8_t const *buffer, uint16_t bufsize)
{
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
#endif

#if CFG_TUD_MSC
TU_ATTR_WEAK void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                                     uint8_t product_id[16], uint8_t product_rev[4])
{
  (void)lun;
  const char *vid = "Arduino";
  const char *pid = "UnoR4-tinyUSB Disk";
  const char *rev = "1.0";
  memcpy(vendor_id, vid, strlen(vid));
  memcpy(product_id, pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

TU_ATTR_WEAK bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  (void)lun;
  return false;
}

TU_ATTR_WEAK void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
  (void)lun;
  *block_count = 0;
  *block_size = 0;
}
#endif

// ---- Weak USBTMC device callbacks -------------------------------------------
// tud_usbtmc_* has no weak defaults in the driver: with CFG_TUD_USBTMC=1 every
// device sketch fails to link unless the library provides stubs. A sketch that
// implements real USBTMC overrides these.

#if CFG_TUD_USBTMC
#if CFG_TUD_USBTMC_ENABLE_488
static usbtmc_response_capabilities_488_t _r4_usbtmc_caps;
#else
static usbtmc_response_capabilities_t _r4_usbtmc_caps;
#endif

extern "C" TU_ATTR_WEAK
#if CFG_TUD_USBTMC_ENABLE_488
usbtmc_response_capabilities_488_t const *
#else
usbtmc_response_capabilities_t const *
#endif
tud_usbtmc_get_capabilities_cb(void)
{
  memset(&_r4_usbtmc_caps, 0, sizeof(_r4_usbtmc_caps));
  _r4_usbtmc_caps.USBTMC_status = USBTMC_STATUS_SUCCESS;
  _r4_usbtmc_caps.bcdUSBTMC = USBTMC_VERSION;
  return &_r4_usbtmc_caps;
}

extern "C" TU_ATTR_WEAK void tud_usbtmc_open_cb(uint8_t interface_id)
{ (void)interface_id; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_msgBulkOut_start_cb(usbtmc_msg_request_dev_dep_out const *msgHeader)
{ (void)msgHeader; return false; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete)
{ (void)data; (void)len; (void)transfer_complete; return false; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_msgBulkIn_request_cb(usbtmc_msg_request_dev_dep_in const *request)
{ (void)request; return true; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_msgBulkIn_complete_cb(void)
{ return true; }

extern "C" TU_ATTR_WEAK void tud_usbtmc_bulkOut_clearFeature_cb(void)
{ }

extern "C" TU_ATTR_WEAK void tud_usbtmc_bulkIn_clearFeature_cb(void)
{ }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_initiate_clear_cb(uint8_t *tmcResult)
{ *tmcResult = USBTMC_STATUS_SUCCESS; return true; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp)
{ rsp->USBTMC_status = USBTMC_STATUS_SUCCESS; rsp->bmClear.BulkInFifoBytes = 0u; return true; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmcResult)
{ *tmcResult = USBTMC_STATUS_SUCCESS; return true; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmcResult)
{ *tmcResult = USBTMC_STATUS_SUCCESS; return true; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp)
{ (void)rsp; return true; }

extern "C" TU_ATTR_WEAK bool tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp)
{ (void)rsp; return true; }

extern "C" TU_ATTR_WEAK uint8_t tud_usbtmc_get_stb_cb(uint8_t *tmcResult)
{ *tmcResult = USBTMC_STATUS_SUCCESS; return 0; }
#endif

// ---- Weak host callbacks ----------------------------------------------------

#if CFG_TUH_ENABLED
TU_ATTR_WEAK void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                                   uint8_t const *report_desc, uint16_t desc_len)
{ (void)dev_addr; (void)idx; (void)report_desc; (void)desc_len; }

TU_ATTR_WEAK void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{ (void)dev_addr; (void)idx; }

TU_ATTR_WEAK void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                             uint8_t const *report, uint16_t len)
{ (void)dev_addr; (void)idx; (void)report; (void)len; }
#endif

#if CFG_TUH_CDC
TU_ATTR_WEAK void tuh_cdc_mount_cb(uint8_t idx)
{ (void)idx; }

TU_ATTR_WEAK void tuh_cdc_umount_cb(uint8_t idx)
{ (void)idx; }

TU_ATTR_WEAK void tuh_cdc_rx_cb(uint8_t idx)
{ (void)idx; }
#endif

#if CFG_TUH_MSC
TU_ATTR_WEAK void tuh_msc_mount_cb(uint8_t dev_addr)
{ (void)dev_addr; }

TU_ATTR_WEAK void tuh_msc_umount_cb(uint8_t dev_addr)
{ (void)dev_addr; }
#endif