/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "bsp/board_api.h"
#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_descriptors.h"

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x210, // USB 2.1

    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = T76_IC_USB_VENDOR_ID,
    .idProduct          = T76_IC_USB_PRODUCT_ID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_RESET,
  ITF_NUM_VENDOR,
  ITF_NUM_USBTMC,
  ITF_NUM_TOTAL
};

#define ITF_BUFFER_SIZE     64

#define RPI_RESET_DESCRIPTOR(_itfnum, _stridx) \
  /* Interface */\
  9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, 0, 1, _stridx

#define RPI_RESET_DESCRIPTOR_LEN 9

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + RPI_RESET_DESCRIPTOR_LEN + TUD_VENDOR_DESC_LEN + TUD_USBTMC_IF_DESCRIPTOR_LEN + TUD_USBTMC_BULK_DESCRIPTORS_LEN + TUD_USBTMC_INT_DESCRIPTOR_LEN)

uint8_t const desc_fs_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 400),

    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, ITF_BUFFER_SIZE),

    // Interface number, string index, EP Out & EP In address, EP size
    RPI_RESET_DESCRIPTOR(ITF_NUM_RESET, 5),

    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 6, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, ITF_BUFFER_SIZE),

    TUD_USBTMC_IF_DESCRIPTOR(ITF_NUM_USBTMC, 3, 7, TUD_USBTMC_PROTOCOL_USB488),
    TUD_USBTMC_BULK_DESCRIPTORS(EPNUM_USBTMC_OUT, EPNUM_USBTMC_IN, ITF_BUFFER_SIZE),
    TUD_USBTMC_INT_DESCRIPTOR(EPNUM_USBTMC_INT, 64, 0x1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  return desc_fs_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

enum {
  LANGUAGE = 0,
  MANUFACTURER,
  PRODUCT,
  SERIAL_NUMBER,
  CDC_INTERFACE,
  RESET_INTERFACE,
  VENDOR_INTERFACE,
  USBTMC_INTERFACE,
  STRING_COUNT
};

// array of pointer to string descriptors
char const* string_desc_arr [] = {
  (const char[]) { 0x09, 0x04 },  // 0: is supported language is English (0x0409)
  T76_IC_USB_MANUFACTURER_STRING,        // 1: Manufacturer
  T76_IC_USB_PRODUCT_STRING,             // 2: Product
  "01234567890",                  // 3: Serial, will be replaced by unique board ID at runtime
  "Board CDC",                    // 4: CDC Interface
  "Reset",                         // 5: Reset Interface
  "Vendor",                         // 6: Vendor Interface
  "USBTMC",                        // 7: USBTMC Interface
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;

  uint8_t chr_count;

  if (index == LANGUAGE) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

    if (index >= STRING_COUNT) {
      return NULL; // no string descriptor for this index
    }

    char *str;
    chr_count = 0;

    if (index == SERIAL_NUMBER) {
      // Use the unique board ID as the serial number
      char buf[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
      memset(buf, 0, sizeof(buf)); // Clear the buffer
      pico_get_unique_board_id_string(buf, sizeof(buf));

      str = buf;
    } else {
      str = (char*) string_desc_arr[index];
    }

    // Cap at max char
    chr_count = (uint8_t) strlen(str);
    if ( chr_count > 31 ) chr_count = 31;

    // Convert ASCII string into UTF-16
    for(uint8_t i=0; i<chr_count; i++) {
      _desc_str[1+i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8 ) | (2*chr_count + 2));

  return _desc_str;
}

