/**
 * @file stage3_bootloader.c
 * @copyright Copyright (c) 2026 MTA, Inc.
 *
 * Resident instrument-core updater bootloader. It either jumps to the
 * application image or enumerates as a WinUSB updater and accepts
 * application-region flash writes from the browser.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"
#include "device/usbd.h"
#include "device/usbd_pvt.h"

#include <t76/updater/boot_request.h>

#ifndef T76_IC_USB_VENDOR_ID
#define T76_IC_USB_VENDOR_ID 0x2E8A
#endif
#ifndef T76_IC_USB_PRODUCT_ID
#define T76_IC_USB_PRODUCT_ID 0x000A
#endif
#ifndef T76_IC_USB_MANUFACTURER_STRING
#define T76_IC_USB_MANUFACTURER_STRING "MTA Inc."
#endif
#ifndef T76_IC_USB_PRODUCT_STRING
#define T76_IC_USB_PRODUCT_STRING "Dr. PD"
#endif
#ifndef T76_UPDATER_FLASH_SIZE_BYTES
#error "T76_UPDATER_FLASH_SIZE_BYTES must be defined by the project"
#endif
#ifndef T76_UPDATER_PROTECTED_TAIL_BYTES
#error "T76_UPDATER_PROTECTED_TAIL_BYTES must be defined by the project"
#endif

#ifndef T76_UPDATER_DRY_RUN_FLASH
#define T76_UPDATER_DRY_RUN_FLASH 0u
#endif
#ifndef T76_UPDATER_WINUSB_GUID
#define T76_UPDATER_WINUSB_GUID "{e6a8e15c-d6be-4a1d-8c25-2a8973d8cb5f}"
#endif

#define T76_STAGE3_UPDATER_MAX_FRAME_SIZE 4608u
#define T76_STAGE3_UPDATER_CDC_ITF 0u
#define T76_STAGE3_UPDATER_CDC_DATA_ITF 1u
#define T76_STAGE3_UPDATER_RESET_ITF 2u
#define T76_STAGE3_UPDATER_VENDOR_ITF 3u
#define T76_STAGE3_UPDATER_USBTMC_ITF 4u
#define T76_STAGE3_UPDATER_WINUSB_ITF 5u
#define T76_STAGE3_UPDATER_CDC_EP_NOTIF 0x83u
#define T76_STAGE3_UPDATER_CDC_EP_OUT 0x04u
#define T76_STAGE3_UPDATER_CDC_EP_IN 0x84u
#define T76_STAGE3_UPDATER_VENDOR_EP_OUT 0x05u
#define T76_STAGE3_UPDATER_VENDOR_EP_IN 0x85u
#define T76_STAGE3_UPDATER_USBTMC_EP_OUT 0x01u
#define T76_STAGE3_UPDATER_USBTMC_EP_IN 0x81u
#define T76_STAGE3_UPDATER_USBTMC_EP_INT 0x82u
#define T76_STAGE3_UPDATER_WINUSB_EP_OUT 0x06u
#define T76_STAGE3_UPDATER_WINUSB_EP_IN 0x86u
#define T76_STAGE3_UPDATER_WINUSB_EP_SIZE 64u
#define T76_STAGE3_UPDATER_RESET_SUBCLASS 0x00u
#define T76_STAGE3_UPDATER_RESET_PROTOCOL 0x01u
#define T76_STAGE3_UPDATER_WINUSB_SUBCLASS 0x01u
#define T76_STAGE3_UPDATER_WINUSB_PROTOCOL 0x02u
#define T76_STAGE3_UPDATER_WINUSB_INSTANCE 0u
#define T76_STAGE3_UPDATER_ITF_BUFFER_SIZE 64u
#define T76_STAGE3_UPDATER_WINUSB_DESC_LEN 23u
#define T76_STAGE3_UPDATER_RESET_DESC_LEN 9u
#define T76_STAGE3_UPDATER_MIN_WRITE_PAYLOAD 4u
#define T76_STAGE3_UPDATER_STATE_IDLE 0u
#define T76_STAGE3_UPDATER_STATE_ACTIVE 1u
#define T76_STAGE3_BOOTLOADER_SRAM_START 0x20000000u
#define T76_STAGE3_BOOTLOADER_SRAM_END 0x20082000u
#define T76_STAGE3_BOOTLOADER_SCB_VTOR (*(volatile uint32_t *)0xe000ed08u)
#define T76_STAGE3_UPDATER_BOS_WEBUSB_VENDOR_REQUEST 1u
#define T76_STAGE3_UPDATER_VENDOR_REQUEST_MICROSOFT 2u
#define T76_STAGE3_UPDATER_MS_OS_20_DESC_INDEX 7u
#define T76_STAGE3_UPDATER_BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define T76_STAGE3_UPDATER_MS_OS_20_DESC_LEN 478u
#define T76_STAGE3_UPDATER_MS_OS_20_FUNCTION_DESC_LEN 156u
#define T76_STAGE3_UPDATER_MS_OS_20_VENDOR_PROPERTY_LEN 128u
#define T76_STAGE3_UPDATER_RESET_GUID "{bc7398c1-73cd-4cb7-98b8-913a8fca7bf6}"
#define T76_STAGE3_UPDATER_VENDOR_GUID "{06b63d79-4f6b-4d9c-9918-32b9c1d6f7b2}"
#define T76_STAGE3_UPDATER_WEBUSB_URL "t76.org/drpd"

#define T76_STAGE3_UPDATER_MS_OS_20_FUNCTION_SUBSET(_itfnum, _guid_literal) \
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), _itfnum, 0, U16_TO_U8S_LE(T76_STAGE3_UPDATER_MS_OS_20_FUNCTION_DESC_LEN), \
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    U16_TO_U8S_LE(T76_STAGE3_UPDATER_MS_OS_20_VENDOR_PROPERTY_LEN), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY), \
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

static uint8_t rx_frame[T76_STAGE3_UPDATER_MAX_FRAME_SIZE];
static uint32_t rx_frame_len;
static uint8_t pending_frame[T76_STAGE3_UPDATER_MAX_FRAME_SIZE];
static uint32_t pending_frame_len;
static bool pending_frame_ready;
static uint8_t response_frame[256];
static uint32_t update_base_offset;
static uint32_t update_total_length;
static uint32_t update_crc32;
static uint32_t update_bytes_written;
static uint32_t update_running_crc32;
static uint32_t update_next_offset;
static uint32_t update_state;
static uint8_t const desc_ms_os_20[T76_STAGE3_UPDATER_MS_OS_20_DESC_LEN];
static uint8_t reset_interface_number;
static uint8_t winusb_interface_number;
static uint8_t winusb_ep_out_address;
static uint8_t winusb_ep_in_address;
static CFG_TUD_MEM_ALIGN uint8_t winusb_ep_out_buffer[CFG_TUD_VENDOR_RX_BUFSIZE];
static CFG_TUD_MEM_ALIGN uint8_t winusb_ep_in_buffer[CFG_TUD_VENDOR_TX_BUFSIZE];

static const tusb_desc_webusb_url_t desc_url = {
    .bLength = 3 + sizeof(T76_STAGE3_UPDATER_WEBUSB_URL) - 1,
    .bDescriptorType = 3,
    .bScheme = 1,
    .url = T76_STAGE3_UPDATER_WEBUSB_URL,
};

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) | ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static void write_u32_le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8u) & 0xffu);
    data[2] = (uint8_t)((value >> 16u) & 0xffu);
    data[3] = (uint8_t)((value >> 24u) & 0xffu);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t size) {
    for (uint32_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? 0xedb88320u : 0u);
        }
    }
    return crc;
}

static bool updater_requested(void) {
    return watchdog_hw->scratch[T76_UPDATER_BOOT_SCRATCH_MAGIC] == T76_UPDATER_BOOT_MAGIC &&
           watchdog_hw->scratch[T76_UPDATER_BOOT_SCRATCH_ARM] == T76_UPDATER_BOOT_ARM_VALUE;
}

static void clear_updater_request(void) {
    watchdog_hw->scratch[T76_UPDATER_BOOT_SCRATCH_MAGIC] = 0u;
    watchdog_hw->scratch[T76_UPDATER_BOOT_SCRATCH_ARM] = 0u;
}

static bool app_vector_valid(void) {
    const uint32_t *vectors = (const uint32_t *)T76_UPDATER_APPLICATION_XIP_BASE;
    const uint32_t stack = vectors[0];
    const uint32_t reset = vectors[1];
    const bool stack_in_sram = stack >= T76_STAGE3_BOOTLOADER_SRAM_START &&
                               stack <= T76_STAGE3_BOOTLOADER_SRAM_END &&
                               (stack & 0x7u) == 0u;
    const bool reset_in_app = reset >= T76_UPDATER_APPLICATION_XIP_BASE &&
                              reset < (XIP_BASE + T76_UPDATER_FLASH_SIZE_BYTES - T76_UPDATER_PROTECTED_TAIL_BYTES);
    return stack_in_sram && reset_in_app && (reset & 1u) != 0u;
}

static void __attribute__((noreturn)) jump_to_app(void) {
    const uint32_t *vectors = (const uint32_t *)T76_UPDATER_APPLICATION_XIP_BASE;
    const uint32_t stack = vectors[0];
    const uint32_t reset = vectors[1];
    void (*const app_entry)(void) = (void (*)(void))reset;

    __asm volatile("cpsid i");
    T76_STAGE3_BOOTLOADER_SCB_VTOR = T76_UPDATER_APPLICATION_XIP_BASE;
    __asm volatile("dsb");
    __asm volatile("isb");
    __asm volatile("msr msp, %0" : : "r"(stack) : );
    __asm volatile("isb");
    app_entry();
    __builtin_unreachable();
}

static bool range_allowed(uint32_t flash_offset, uint32_t length) {
    const uint32_t app_start = T76_UPDATER_APPLICATION_FLASH_OFFSET_BYTES;
    const uint32_t app_end = T76_UPDATER_FLASH_SIZE_BYTES - T76_UPDATER_PROTECTED_TAIL_BYTES;
    if (length == 0u || flash_offset < app_start || flash_offset > app_end) {
        return false;
    }
    return length <= (app_end - flash_offset);
}

static void send_frame(uint8_t type, uint8_t tag, const uint8_t *payload, uint32_t payload_len) {
    if (payload_len + T76_WINUSB_FRAME_HEADER_SIZE > sizeof(response_frame)) {
        payload_len = 0u;
        type = T76_WINUSB_FRAME_ERROR_RESPONSE;
        payload = (const uint8_t *)"Response too large";
        payload_len = 18u;
    }
    response_frame[0] = T76_WINUSB_FRAME_MAGIC0;
    response_frame[1] = T76_WINUSB_FRAME_MAGIC1;
    response_frame[2] = T76_WINUSB_FRAME_VERSION;
    response_frame[3] = type;
    response_frame[4] = tag;
    response_frame[5] = 0u;
    response_frame[6] = 0u;
    response_frame[7] = 0u;
    write_u32_le(&response_frame[8], payload_len);
    if (payload_len > 0u) {
        memcpy(&response_frame[T76_WINUSB_FRAME_HEADER_SIZE], payload, payload_len);
    }
    if (winusb_ep_in_address == 0u) {
        return;
    }
    while (usbd_edpt_busy(0, winusb_ep_in_address)) {
        tud_task();
    }
    memcpy(winusb_ep_in_buffer, response_frame, T76_WINUSB_FRAME_HEADER_SIZE + payload_len);
    usbd_edpt_xfer(0, winusb_ep_in_address, winusb_ep_in_buffer, T76_WINUSB_FRAME_HEADER_SIZE + payload_len);
}

static void send_error(uint8_t tag, const char *message) {
    send_frame(T76_WINUSB_FRAME_ERROR_RESPONSE, tag, (const uint8_t *)message, (uint32_t)strlen(message));
}

static void send_ack(uint8_t tag) {
    send_frame(T76_WINUSB_FRAME_UPDATE_ACK, tag, NULL, 0u);
}

static void send_status(uint8_t tag) {
    uint8_t payload[16];
    write_u32_le(&payload[0], update_state);
    write_u32_le(&payload[4], update_base_offset);
    write_u32_le(&payload[8], update_total_length);
    write_u32_le(&payload[12], update_bytes_written);
    send_frame(T76_WINUSB_FRAME_UPDATE_STATUS_RESPONSE, tag, payload, sizeof(payload));
}

static void erase_application_region(uint32_t base_offset, uint32_t total_length) {
    const uint32_t erase_start = base_offset & ~(FLASH_SECTOR_SIZE - 1u);
    const uint32_t erase_end = (base_offset + total_length + FLASH_SECTOR_SIZE - 1u) & ~(FLASH_SECTOR_SIZE - 1u);
    flash_range_erase(erase_start, erase_end - erase_start);
}

static void __not_in_flash_func(program_flash)(uint32_t flash_offset, const uint8_t *payload, uint32_t payload_len) {
    flash_range_program(flash_offset, payload, payload_len);
}

static void handle_update_begin(uint8_t tag, const uint8_t *payload, uint32_t payload_len) {
    if (payload_len != 12u) {
        send_error(tag, "Invalid update begin payload");
        return;
    }
    const uint32_t base_offset = read_u32_le(&payload[0]);
    const uint32_t total_length = read_u32_le(&payload[4]);
    if (!range_allowed(base_offset, total_length)) {
        send_error(tag, "Update range is outside application flash");
        return;
    }
    update_base_offset = base_offset;
    update_total_length = total_length;
    update_crc32 = read_u32_le(&payload[8]);
    update_bytes_written = 0u;
    update_running_crc32 = 0xffffffffu;
    update_next_offset = update_base_offset;
    update_state = T76_STAGE3_UPDATER_STATE_ACTIVE;
#if T76_UPDATER_DRY_RUN_FLASH == 0
    uint32_t ints = save_and_disable_interrupts();
    erase_application_region(update_base_offset, update_total_length);
    restore_interrupts(ints);
#endif
    send_ack(tag);
}

static void handle_update_write(uint8_t tag, const uint8_t *payload, uint32_t payload_len) {
    if (update_state != T76_STAGE3_UPDATER_STATE_ACTIVE) {
        send_error(tag, "Update has not begun");
        return;
    }
    if (payload_len < T76_STAGE3_UPDATER_MIN_WRITE_PAYLOAD) {
        send_error(tag, "Invalid update write payload");
        return;
    }
    const uint32_t flash_offset = read_u32_le(payload);
    const uint32_t data_len = payload_len - 4u;
    if ((flash_offset & (FLASH_PAGE_SIZE - 1u)) != 0u ||
        data_len != FLASH_PAGE_SIZE) {
        send_error(tag, "Update writes must be flash-page aligned");
        return;
    }
    if (!range_allowed(flash_offset, data_len) ||
        flash_offset < update_base_offset ||
        flash_offset + data_len > update_base_offset + update_total_length) {
        send_error(tag, "Update write is outside the active range");
        return;
    }
#if T76_UPDATER_DRY_RUN_FLASH != 0
    while (update_next_offset < flash_offset) {
        const uint8_t erased = 0xffu;
        update_running_crc32 = crc32_update(update_running_crc32, &erased, sizeof(erased));
        update_next_offset += 1u;
        update_bytes_written += 1u;
    }
    update_running_crc32 = crc32_update(update_running_crc32, payload + 4u, data_len);
    update_next_offset = flash_offset + data_len;
#else
    uint32_t ints = save_and_disable_interrupts();
    program_flash(flash_offset, payload + 4u, data_len);
    restore_interrupts(ints);
#endif
    update_bytes_written += data_len;
    send_ack(tag);
}

static void handle_update_finish(uint8_t tag) {
    if (update_state != T76_STAGE3_UPDATER_STATE_ACTIVE) {
        send_error(tag, "Update has not begun");
        return;
    }
#if T76_UPDATER_DRY_RUN_FLASH != 0
    while (update_bytes_written < update_total_length) {
        const uint8_t erased = 0xffu;
        update_running_crc32 = crc32_update(update_running_crc32, &erased, sizeof(erased));
        update_bytes_written += 1u;
    }
    const uint32_t computed_crc = ~update_running_crc32;
#else
    const uint8_t *image = (const uint8_t *)(XIP_BASE + update_base_offset);
    const uint32_t computed_crc = ~crc32_update(0xffffffffu, image, update_total_length);
#endif
    if (computed_crc != update_crc32) {
        send_error(tag, "Updated firmware CRC mismatch");
        return;
    }
    clear_updater_request();
    send_ack(tag);
    sleep_ms(50);
    watchdog_reboot(0, 0, 10);
}

static void handle_frame(const uint8_t *frame, uint32_t frame_len) {
    if (frame_len < T76_WINUSB_FRAME_HEADER_SIZE ||
        frame[0] != T76_WINUSB_FRAME_MAGIC0 ||
        frame[1] != T76_WINUSB_FRAME_MAGIC1 ||
        frame[2] != T76_WINUSB_FRAME_VERSION) {
        return;
    }
    const uint8_t type = frame[3];
    const uint8_t tag = frame[4];
    const uint32_t payload_len = read_u32_le(&frame[8]);
    const uint8_t *payload = frame + T76_WINUSB_FRAME_HEADER_SIZE;
    switch (type) {
        case T76_WINUSB_FRAME_SESSION_RESET_REQUEST:
            send_frame(T76_WINUSB_FRAME_SESSION_RESET_ACK, tag, NULL, 0u);
            break;
        case T76_WINUSB_FRAME_UPDATE_BEGIN:
            handle_update_begin(tag, payload, payload_len);
            break;
        case T76_WINUSB_FRAME_UPDATE_WRITE:
            handle_update_write(tag, payload, payload_len);
            break;
        case T76_WINUSB_FRAME_UPDATE_FINISH:
            handle_update_finish(tag);
            break;
        case T76_WINUSB_FRAME_UPDATE_ABORT:
            update_state = T76_STAGE3_UPDATER_STATE_IDLE;
            send_ack(tag);
            break;
        case T76_WINUSB_FRAME_UPDATE_STATUS:
            send_status(tag);
            break;
        default:
            send_error(tag, "Unsupported updater frame");
            break;
    }
}

static void stage3_winusb_rx(const uint8_t *buffer, uint16_t bufsize) {
    if (rx_frame_len + bufsize > sizeof(rx_frame)) {
        rx_frame_len = 0u;
        return;
    }
    memcpy(rx_frame + rx_frame_len, buffer, bufsize);
    rx_frame_len += bufsize;
    if (rx_frame_len < T76_WINUSB_FRAME_HEADER_SIZE) {
        return;
    }
    const uint32_t payload_len = read_u32_le(&rx_frame[8]);
    const uint32_t frame_len = T76_WINUSB_FRAME_HEADER_SIZE + payload_len;
    if (frame_len > sizeof(rx_frame)) {
        rx_frame_len = 0u;
        return;
    }
    if (rx_frame_len >= frame_len) {
        if (!pending_frame_ready) {
            memcpy(pending_frame, rx_frame, frame_len);
            pending_frame_len = frame_len;
            pending_frame_ready = true;
        }
        rx_frame_len = 0u;
    }
}

void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize) {
    (void)itf;
    (void)buffer;
    (void)bufsize;
    tud_vendor_n_read_flush(itf);
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) {
        return false;
    }
    if (request->bRequest == T76_STAGE3_UPDATER_BOS_WEBUSB_VENDOR_REQUEST) {
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)&desc_url, desc_url.bLength);
    }
    if (request->bRequest == T76_STAGE3_UPDATER_VENDOR_REQUEST_MICROSOFT &&
        request->wIndex == T76_STAGE3_UPDATER_MS_OS_20_DESC_INDEX) {
        uint16_t total_len;
        memcpy(&total_len, desc_ms_os_20 + 8, sizeof(total_len));
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, total_len);
    }
    return false;
}

static void resetd_init(void) {
}

static void resetd_reset(uint8_t rhport) {
    (void)rhport;
    reset_interface_number = 0u;
    winusb_interface_number = 0u;
    winusb_ep_out_address = 0u;
    winusb_ep_in_address = 0u;
}

static uint16_t resetd_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass, 0);

    if (itf_desc->bInterfaceSubClass == T76_STAGE3_UPDATER_RESET_SUBCLASS &&
        itf_desc->bInterfaceProtocol == T76_STAGE3_UPDATER_RESET_PROTOCOL) {
        uint16_t const drv_len = sizeof(tusb_desc_interface_t);
        TU_VERIFY(max_len >= drv_len, 0);
        reset_interface_number = itf_desc->bInterfaceNumber;
        return drv_len;
    }

    if (itf_desc->bInterfaceSubClass == T76_STAGE3_UPDATER_WINUSB_SUBCLASS &&
        itf_desc->bInterfaceProtocol == T76_STAGE3_UPDATER_WINUSB_PROTOCOL) {
        TU_VERIFY(itf_desc->bNumEndpoints == 2, 0);

        const uint8_t *p_desc = tu_desc_next(itf_desc);
        const uint8_t *desc_end = ((uint8_t const *)itf_desc) + max_len;
        uint8_t found_ep = 0u;

        winusb_interface_number = itf_desc->bInterfaceNumber;

        while ((found_ep < itf_desc->bNumEndpoints) && (p_desc < desc_end)) {
            while ((p_desc < desc_end) && (tu_desc_type(p_desc) != TUSB_DESC_ENDPOINT)) {
                p_desc = tu_desc_next(p_desc);
            }

            TU_VERIFY(p_desc < desc_end, 0);

            tusb_desc_endpoint_t const *desc_ep = (tusb_desc_endpoint_t const *)p_desc;
            TU_ASSERT(usbd_edpt_open(rhport, desc_ep), 0);

            if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_OUT) {
                winusb_ep_out_address = desc_ep->bEndpointAddress;
                TU_ASSERT(usbd_edpt_xfer(rhport, winusb_ep_out_address, winusb_ep_out_buffer, sizeof(winusb_ep_out_buffer)), 0);
            } else {
                winusb_ep_in_address = desc_ep->bEndpointAddress;
            }

            found_ep++;
            p_desc = tu_desc_next(p_desc);
        }

        TU_VERIFY(found_ep == itf_desc->bNumEndpoints, 0);
        return (uint16_t)((uintptr_t)p_desc - (uintptr_t)itf_desc);
    }

    return 0;
}

static bool resetd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    (void)rhport;
    if (request->wIndex == reset_interface_number) {
        if (stage != CONTROL_STAGE_SETUP) {
            return true;
        }
        return false;
    }

    if (request->wIndex == winusb_interface_number) {
        if (stage != CONTROL_STAGE_SETUP) {
            return true;
        }
        if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
            request->bRequest == T76_STAGE3_UPDATER_VENDOR_REQUEST_MICROSOFT &&
            request->wIndex == T76_STAGE3_UPDATER_MS_OS_20_DESC_INDEX) {
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20 + 8, sizeof(total_len));
            return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, total_len);
        }
        return false;
    }

    return false;
}

static bool resetd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)result;

    if (ep_addr == winusb_ep_out_address) {
        if (xferred_bytes > 0u) {
            stage3_winusb_rx(winusb_ep_out_buffer, (uint16_t)xferred_bytes);
        }
        return usbd_edpt_xfer(rhport, winusb_ep_out_address, winusb_ep_out_buffer, sizeof(winusb_ep_out_buffer));
    }

    if (ep_addr == winusb_ep_in_address) {
        return true;
    }

    return true;
}

static usbd_class_driver_t const reset_driver = {
    .init = resetd_init,
    .reset = resetd_reset,
    .open = resetd_open,
    .control_xfer_cb = resetd_control_xfer_cb,
    .xfer_cb = resetd_xfer_cb,
    .sof = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &reset_driver;
}

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = T76_IC_USB_VENDOR_ID,
    .idProduct = T76_IC_USB_PRODUCT_ID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

static uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(T76_STAGE3_UPDATER_BOS_TOTAL_LEN, 2),
    TUD_BOS_WEBUSB_DESCRIPTOR(T76_STAGE3_UPDATER_BOS_WEBUSB_VENDOR_REQUEST, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(T76_STAGE3_UPDATER_MS_OS_20_DESC_LEN, T76_STAGE3_UPDATER_VENDOR_REQUEST_MICROSOFT),
};

TU_VERIFY_STATIC(sizeof(desc_bos) == T76_STAGE3_UPDATER_BOS_TOTAL_LEN, "Incorrect updater BOS size");

uint8_t const *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

static uint8_t const desc_ms_os_20[T76_STAGE3_UPDATER_MS_OS_20_DESC_LEN] = {
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(T76_STAGE3_UPDATER_MS_OS_20_DESC_LEN),
    T76_STAGE3_UPDATER_MS_OS_20_FUNCTION_SUBSET(T76_STAGE3_UPDATER_RESET_ITF, T76_STAGE3_UPDATER_RESET_GUID),
    T76_STAGE3_UPDATER_MS_OS_20_FUNCTION_SUBSET(T76_STAGE3_UPDATER_VENDOR_ITF, T76_STAGE3_UPDATER_VENDOR_GUID),
    T76_STAGE3_UPDATER_MS_OS_20_FUNCTION_SUBSET(T76_STAGE3_UPDATER_WINUSB_ITF, T76_UPDATER_WINUSB_GUID),
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == T76_STAGE3_UPDATER_MS_OS_20_DESC_LEN, "Incorrect updater MS OS 2.0 size");

#define T76_STAGE3_UPDATER_RESET_DESCRIPTOR(_itfnum, _stridx) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, T76_STAGE3_UPDATER_RESET_SUBCLASS, T76_STAGE3_UPDATER_RESET_PROTOCOL, _stridx

#define T76_STAGE3_UPDATER_WINUSB_DESCRIPTOR(_itfnum, _stridx, _epout, _epin, _epsize) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 2, TUSB_CLASS_VENDOR_SPECIFIC, T76_STAGE3_UPDATER_WINUSB_SUBCLASS, T76_STAGE3_UPDATER_WINUSB_PROTOCOL, _stridx, \
    7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_BULK, TU_U16_LOW(_epsize), TU_U16_HIGH(_epsize), 0, \
    7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_BULK, TU_U16_LOW(_epsize), TU_U16_HIGH(_epsize), 0

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + T76_STAGE3_UPDATER_RESET_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_USBTMC_IF_DESCRIPTOR_LEN + TUD_USBTMC_BULK_DESCRIPTORS_LEN + TUD_USBTMC_INT_DESCRIPTOR_LEN + T76_STAGE3_UPDATER_WINUSB_DESC_LEN)
static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 6, 0, CONFIG_TOTAL_LEN, 0x00, 400),
    TUD_CDC_DESCRIPTOR(T76_STAGE3_UPDATER_CDC_ITF, 4, T76_STAGE3_UPDATER_CDC_EP_NOTIF, 8, T76_STAGE3_UPDATER_CDC_EP_OUT, T76_STAGE3_UPDATER_CDC_EP_IN, T76_STAGE3_UPDATER_ITF_BUFFER_SIZE),
    T76_STAGE3_UPDATER_RESET_DESCRIPTOR(T76_STAGE3_UPDATER_RESET_ITF, 5),
    TUD_VENDOR_DESCRIPTOR(T76_STAGE3_UPDATER_VENDOR_ITF, 6, T76_STAGE3_UPDATER_VENDOR_EP_OUT, T76_STAGE3_UPDATER_VENDOR_EP_IN, T76_STAGE3_UPDATER_ITF_BUFFER_SIZE),
    TUD_USBTMC_IF_DESCRIPTOR(T76_STAGE3_UPDATER_USBTMC_ITF, 3, 7, TUD_USBTMC_PROTOCOL_USB488),
    TUD_USBTMC_BULK_DESCRIPTORS(T76_STAGE3_UPDATER_USBTMC_EP_OUT, T76_STAGE3_UPDATER_USBTMC_EP_IN, T76_STAGE3_UPDATER_ITF_BUFFER_SIZE),
    TUD_USBTMC_INT_DESCRIPTOR(T76_STAGE3_UPDATER_USBTMC_EP_INT, 64, 0x1),
    T76_STAGE3_UPDATER_WINUSB_DESCRIPTOR(
        T76_STAGE3_UPDATER_WINUSB_ITF,
        8,
        T76_STAGE3_UPDATER_WINUSB_EP_OUT,
        T76_STAGE3_UPDATER_WINUSB_EP_IN,
        T76_STAGE3_UPDATER_WINUSB_EP_SIZE),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

enum {
    STRING_LANGUAGE = 0,
    STRING_MANUFACTURER = 1,
    STRING_PRODUCT = 2,
    STRING_SERIAL = 3,
    STRING_CDC = 4,
    STRING_RESET = 5,
    STRING_VENDOR = 6,
    STRING_USBTMC = 7,
    STRING_WINUSB = 8,
};

static uint16_t desc_string[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *value = NULL;
    uint8_t count = 0u;
    if (index == STRING_LANGUAGE) {
        desc_string[1] = 0x0409;
        count = 1u;
    } else {
        switch (index) {
            case STRING_MANUFACTURER:
                value = T76_IC_USB_MANUFACTURER_STRING;
                break;
            case STRING_PRODUCT:
                value = T76_IC_USB_PRODUCT_STRING;
                break;
            case STRING_SERIAL:
                pico_get_unique_board_id_string(serial, sizeof(serial));
                value = serial;
                break;
            case STRING_CDC:
                value = "Board CDC";
                break;
            case STRING_RESET:
                value = "Reset";
                break;
            case STRING_VENDOR:
                value = "Vendor";
                break;
            case STRING_USBTMC:
                value = "USBTMC";
                break;
            case STRING_WINUSB:
                value = "WinUSB";
                break;
            default:
                return NULL;
        }
        count = (uint8_t)strlen(value);
        if (count > 31u) {
            count = 31u;
        }
        for (uint8_t i = 0u; i < count; ++i) {
            desc_string[1u + i] = value[i];
        }
    }
    desc_string[0] = (uint16_t)((TUSB_DESC_STRING << 8u) | (2u * count + 2u));
    return desc_string;
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    (void)dtr;
    (void)rts;
}

void tud_cdc_rx_cb(uint8_t itf) {
    uint8_t buffer[64];
    while (tud_cdc_n_available(itf) != 0u) {
        uint32_t count = tud_cdc_n_read(itf, buffer, sizeof(buffer));
        if (count == 0u) {
            break;
        }
    }
}

usbtmc_response_capabilities_488_t const *tud_usbtmc_get_capabilities_cb(void) {
    static const usbtmc_response_capabilities_488_t capabilities = {
        .USBTMC_status = USBTMC_STATUS_SUCCESS,
        .bcdUSBTMC = 0x0100,
        .bcdUSB488 = 0x0100,
    };
    return &capabilities;
}

void tud_usbtmc_open_cb(uint8_t interface_id) {
    (void)interface_id;
}

bool tud_usbtmc_msg_trigger_cb(usbtmc_msg_generic_t *msg) {
    (void)msg;
    return false;
}

bool tud_usbtmc_msgBulkOut_start_cb(usbtmc_msg_request_dev_dep_out const *msgHeader) {
    (void)msgHeader;
    return true;
}

bool tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete) {
    (void)data;
    (void)len;
    return transfer_complete;
}

bool tud_usbtmc_msgBulkIn_complete_cb(void) {
    return true;
}

bool tud_usbtmc_msgBulkIn_request_cb(usbtmc_msg_request_dev_dep_in const *request) {
    (void)request;
    return tud_usbtmc_transmit_dev_msg_data(NULL, 0u, true, false);
}

bool tud_usbtmc_initiate_clear_cb(uint8_t *tmcResult) {
    *tmcResult = USBTMC_STATUS_SUCCESS;
    return true;
}

bool tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp) {
    rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
    rsp->bmClear.BulkInFifoBytes = 0u;
    return true;
}

bool tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmcResult) {
    *tmcResult = USBTMC_STATUS_SUCCESS;
    return true;
}

bool tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp) {
    rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
    rsp->NBYTES_RXD_TXD = 0u;
    return true;
}

bool tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmcResult) {
    *tmcResult = USBTMC_STATUS_SUCCESS;
    return true;
}

bool tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp) {
    rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
    rsp->NBYTES_RXD_TXD = 0u;
    return true;
}

bool tud_usbtmc_notification_complete_cb(void) {
    return true;
}

void tud_usbtmc_bulkIn_clearFeature_cb(void) {
}

void tud_usbtmc_bulkOut_clearFeature_cb(void) {
}

uint8_t tud_usbtmc_get_stb_cb(uint8_t *tmcResult) {
    *tmcResult = USBTMC_STATUS_SUCCESS;
    return 0u;
}

bool tud_usbtmc_indicator_pulse_cb(tusb_control_request_t const *msg, uint8_t *tmcResult) {
    (void)msg;
    *tmcResult = USBTMC_STATUS_SUCCESS;
    return true;
}

int main(void) {
    if (!updater_requested() && app_vector_valid()) {
        jump_to_app();
    }

    board_init();
    tusb_init();
    while (true) {
        tud_task();
        if (pending_frame_ready) {
            handle_frame(pending_frame, pending_frame_len);
            pending_frame_ready = false;
            pending_frame_len = 0u;
        }
    }
}
