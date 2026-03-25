/**
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * Modified for T76.org
 * 
 */

#include "tusb.h"

#include "pico/bootrom.h"

#include "pico/stdio_usb/reset_interface.h"
#include "hardware/watchdog.h"
#include "device/usbd_pvt.h"
#include "device/usbd.h"

#include "usb_descriptors.h"


uint8_t reset_interface_number;

// Support for Microsoft OS 2.0 descriptor
#define BOS_TOTAL_LEN      (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

#define MS_OS_20_DESC_LEN            322
#define MS_OS_20_FUNCTION_DESC_LEN   156
#define MS_OS_20_VENDOR_PROPERTY_LEN 128

#define RESET_INTERFACE_GUID "{bc7398c1-73cd-4cb7-98b8-913a8fca7bf6}"
#define VENDOR_INTERFACE_GUID "{06b63d79-4f6b-4d9c-9918-32b9c1d6f7b2}"

#define MS_OS_20_FUNCTION_SUBSET(_itfnum, _guid_literal) \
    /* Function subset header */ \
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), _itfnum, 0, U16_TO_U8S_LE(MS_OS_20_FUNCTION_DESC_LEN), \
    /* Compatible ID descriptor */ \
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    /* Registry property descriptor for DeviceInterfaceGUID */ \
    U16_TO_U8S_LE(MS_OS_20_VENDOR_PROPERTY_LEN), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY), \
    U16_TO_U8S_LE(0x0001), U16_TO_U8S_LE(0x0028), \
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, \
    'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 0x00, 0x00, \
    U16_TO_U8S_LE(sizeof(_guid_literal) * 2), \
    _guid_literal[0], 0x00, _guid_literal[1], 0x00, _guid_literal[2], 0x00, _guid_literal[3], 0x00, \
    _guid_literal[4], 0x00, _guid_literal[5], 0x00, _guid_literal[6], 0x00, _guid_literal[7], 0x00, \
    _guid_literal[8], 0x00, _guid_literal[9], 0x00, _guid_literal[10], 0x00, _guid_literal[11], 0x00, \
    _guid_literal[12], 0x00, _guid_literal[13], 0x00, _guid_literal[14], 0x00, _guid_literal[15], 0x00, \
    _guid_literal[16], 0x00, _guid_literal[17], 0x00, _guid_literal[18], 0x00, _guid_literal[19], 0x00, \
    _guid_literal[20], 0x00, _guid_literal[21], 0x00, _guid_literal[22], 0x00, _guid_literal[23], 0x00, \
    _guid_literal[24], 0x00, _guid_literal[25], 0x00, _guid_literal[26], 0x00, _guid_literal[27], 0x00, \
    _guid_literal[28], 0x00, _guid_literal[29], 0x00, _guid_literal[30], 0x00, _guid_literal[31], 0x00, \
    _guid_literal[32], 0x00, _guid_literal[33], 0x00, _guid_literal[34], 0x00, _guid_literal[35], 0x00, \
    _guid_literal[36], 0x00, _guid_literal[37], 0x00, _guid_literal[38], 0x00


uint8_t const desc_bos[] = {
    // total length, number of device caps
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),

    // Vendor Code, iLandingPage
    TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),

    // Microsoft OS 2.0 descriptor
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)
};

TU_VERIFY_STATIC(sizeof(desc_bos) == BOS_TOTAL_LEN, "Incorrect size");

uint8_t const * tud_descriptor_bos_cb(void) {
    return desc_bos;
}

const uint8_t desc_ms_os_20[] = {
    // Set header: length, type, windows version, total length
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    MS_OS_20_FUNCTION_SUBSET(ITF_NUM_RESET, RESET_INTERFACE_GUID),
    MS_OS_20_FUNCTION_SUBSET(ITF_NUM_VENDOR, VENDOR_INTERFACE_GUID)
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

const tusb_desc_webusb_url_t desc_url = {
	.bLength         = 3 + sizeof(T76_IC_USB_URL) - 1,
	.bDescriptorType = 3,
	.bScheme         = 1,
	.url             = T76_IC_USB_URL
};

static void resetd_init(void) {
}

static void resetd_reset(uint8_t __unused rhport) {
    reset_interface_number = 0;
}

static uint16_t resetd_open(uint8_t __unused rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
              RESET_INTERFACE_SUBCLASS == itf_desc->bInterfaceSubClass &&
              RESET_INTERFACE_PROTOCOL == itf_desc->bInterfaceProtocol, 0);

    uint16_t const drv_len = sizeof(tusb_desc_interface_t);
    TU_VERIFY(max_len >= drv_len, 0);

    reset_interface_number = itf_desc->bInterfaceNumber;
    return drv_len;
}

// Support for parameterized reset via vendor interface control request
static bool resetd_control_xfer_cb(uint8_t __unused rhport, uint8_t stage, tusb_control_request_t const * request) {
    // nothing to do with DATA & ACK stage
    if (stage != CONTROL_STAGE_SETUP) return true;

    if (request->wIndex == reset_interface_number) {

        if (request->bRequest == RESET_REQUEST_BOOTSEL) {
            int gpio = -1;
            bool active_low = false;
            if (request->wValue & 0x100) {
                gpio = request->wValue >> 9u;
            }
            active_low = request->wValue & 0x200;
            rom_reset_usb_boot_extra(gpio, (request->wValue & 0x7f) | PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK, active_low);
            // does not return, otherwise we'd return true
        }

        if (request->bRequest == RESET_REQUEST_FLASH) {
            watchdog_reboot(0, 0, PICO_STDIO_USB_RESET_RESET_TO_FLASH_DELAY_MS);
            return true;
        }
    }
    return false;
}

static bool resetd_xfer_cb(uint8_t __unused rhport, uint8_t __unused ep_addr, xfer_result_t __unused result, uint32_t __unused xferred_bytes) {
    return true;
}

static usbd_class_driver_t const _resetd_driver = {
    .init             = resetd_init,
    .reset            = resetd_reset,
    .open             = resetd_open,
    .control_xfer_cb  = resetd_control_xfer_cb,
    .xfer_cb          = resetd_xfer_cb,
    .sof              = NULL
};

// Implement callback to add our custom driver
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &_resetd_driver;
}
