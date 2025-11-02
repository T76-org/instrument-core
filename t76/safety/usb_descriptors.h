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
 */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#define EPNUM_USBTMC_OUT    0x01
#define EPNUM_USBTMC_IN     0x81
#define EPNUM_USBTMC_INT    0x82

#define EPNUM_CDC_NOTIF     0x83
#define EPNUM_CDC_OUT       0x04
#define EPNUM_CDC_IN        0x84

#define EPNUM_VENDOR_OUT    0x05
#define EPNUM_VENDOR_IN     0x85

enum {
  VENDOR_REQUEST_WEBUSB = 1,
  VENDOR_REQUEST_MICROSOFT = 2
};

extern uint8_t const desc_ms_os_20[];
extern const tusb_desc_webusb_url_t desc_url;
extern uint8_t reset_interface_number;

#endif /* USB_DESCRIPTORS_H_ */
