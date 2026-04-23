/*
 * TinyUSB configuration for the reusable T76 resident updater bootloader.
 */

#pragma once

#include "pico/stdlib.h"

#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 1
#define CFG_TUD_USBTMC 1
#define CFG_TUD_USBTMC_ENABLE_INT_EP 1
#define CFG_TUD_USBTMC_ENABLE_488 1
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64
#define CFG_TUD_CDC_EP_BUFSIZE 64
#define CFG_TUD_VENDOR_RX_BUFSIZE 512
#define CFG_TUD_VENDOR_TX_BUFSIZE 512
