#include <stdlib.h>
#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"

#ifdef __cplusplus
extern "C" {
#endif

// Vendor callbacks

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize);
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request);

/**
 * @brief Route a control transfer to the WinUSB interface handler.
 *
 * @param rhport The USB root port handling the request.
 * @param stage The current control transfer stage.
 * @param request The control request descriptor from the host.
 * @return true if the request was handled, false otherwise.
 */
bool t76_winusb_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request);

/**
 * @brief Notify the runtime that a WinUSB bulk OUT packet was received.
 *
 * @param buffer Pointer to the received packet bytes.
 * @param bufsize Number of bytes in the received packet.
 */
void t76_winusb_bulk_out_received_cb(uint8_t const* buffer, uint16_t bufsize);

/**
 * @brief Notify the runtime that a WinUSB bulk IN transfer completed.
 *
 * @param xferred_bytes Number of bytes transferred to the host.
 */
void t76_winusb_bulk_in_complete_cb(uint32_t xferred_bytes);

/**
 * @brief Start a WinUSB bulk IN transfer.
 *
 * @param buffer Pointer to the payload to send.
 * @param bufsize Number of bytes to transfer.
 * @return true if the transfer was started, false otherwise.
 */
bool t76_winusb_bulk_in_xfer(uint8_t const* buffer, uint16_t bufsize);

/**
 * @brief Start a zero-length WinUSB bulk IN transfer.
 *
 * @return true if the zero-length packet transfer was started, false otherwise.
 */
bool t76_winusb_bulk_in_zlp(void);

// USBTMC callbacks

usbtmc_response_capabilities_488_t const * tud_usbtmc_get_capabilities_cb(void);
void tud_usbtmc_open_cb(uint8_t interface_id);
bool tud_usbtmc_msg_trigger_cb(usbtmc_msg_generic_t* msg);
bool tud_usbtmc_msgBulkOut_start_cb(usbtmc_msg_request_dev_dep_out const * msgHeader);
bool tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete);
bool tud_usbtmc_msgBulkIn_complete_cb();
bool tud_usbtmc_msgBulkIn_request_cb(usbtmc_msg_request_dev_dep_in const * request);
bool tud_usbtmc_initiate_clear_cb(uint8_t *tmcResult);
bool tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp);
bool tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmcResult);
bool tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp);
bool tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmcResult);
bool tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp);
bool tud_usbtmc_notification_complete_cb(void);
void tud_usbtmc_bulkIn_clearFeature_cb(void);
void tud_usbtmc_bulkOut_clearFeature_cb(void);
uint8_t tud_usbtmc_get_stb_cb(uint8_t *tmcResult);
bool tud_usbtmc_indicator_pulse_cb(tusb_control_request_t const * msg, uint8_t *tmcResult);

#ifdef __cplusplus
}
#endif
