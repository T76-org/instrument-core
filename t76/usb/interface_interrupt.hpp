/**
 * @file interface_interrupt.hpp
 * @brief TinyUSB interrupt communications patch
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * These structures are required to fix an issue that prevents interrupt data requests
 * from being sent out. See https://github.com/hathach/tinyusb/issues/2735
 */
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "common/tusb_verify.h"
#include "class/usbtmc/usbtmc_device.h"
#include "usb_descriptors.h"

typedef enum {
  STATE_CLOSED,  // Endpoints have not yet been opened since USB reset
  STATE_NAK,     // Bulk-out endpoint is in NAK state.
  STATE_IDLE,    // Bulk-out endpoint is waiting for CMD.
  STATE_RCV,     // Bulk-out is receiving DEV_DEP message
  STATE_TX_REQUESTED,
  STATE_TX_INITIATED,
  STATE_TX_SHORTED,
  STATE_CLEARING,
  STATE_ABORTING_BULK_IN,
  STATE_ABORTING_BULK_IN_SHORTED, // aborting, and short packet has been queued for transmission
  STATE_ABORTING_BULK_IN_ABORTED, // aborting, and short packet has been transmitted
  STATE_ABORTING_BULK_OUT,
  STATE_NUM_STATES
} usbtmcd_state_enum;

typedef usbtmc_response_capabilities_488_t usbtmc_capabilities_specific_t;

typedef struct {
  volatile usbtmcd_state_enum state;

  uint8_t itf_id;
  uint8_t rhport;
  uint8_t ep_bulk_in;
  uint8_t ep_bulk_out;
  uint8_t ep_int_in;
  uint32_t ep_bulk_in_wMaxPacketSize;
  uint32_t ep_bulk_out_wMaxPacketSize;
  uint32_t transfer_size_remaining; // also used for requested length for bulk IN.
  uint32_t transfer_size_sent;      // To keep track of data bytes that have been queued in FIFO (not header bytes)

  uint8_t lastBulkOutTag; // used for aborts (mostly)
  uint8_t lastBulkInTag; // used for aborts (mostly)

  uint8_t const * devInBuffer; // pointer to application-layer used for transmissions

  usbtmc_capabilities_specific_t const * capabilities;
} usbtmc_interface_state_t;

#ifndef CFG_TUD_USBTMC_INT_EP_SIZE
#define CFG_TUD_USBTMC_INT_EP_SIZE 2
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern usbtmc_interface_state_t usbtmc_state;

#ifdef __cplusplus
}
#endif
