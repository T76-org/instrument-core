#include <stdlib.h>
#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "t76/usb_interface.hpp"

using namespace T76::Core::USB;


extern "C" void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    Interface::_singleton->_vendorDataReceived(itf, (uint8_t*)buffer, bufsize);
}

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request) {
    return Interface::_singleton->_vendorControlTransfer(rhport, stage, request);
}

extern "C" usbtmc_response_capabilities_488_t const * tud_usbtmc_get_capabilities_cb(void) {
    return Interface::_singleton->_usbtmcCapabilities();
}

extern "C" void tud_usbtmc_open_cb(uint8_t interface_id) {
  Interface::_singleton->_usbtmcOpen(interface_id);
}

bool tud_usbtmc_msg_trigger_cb(usbtmc_msg_generic_t* msg) {
  return Interface::_singleton->_usbtmcMsgTrigger(msg);
}

bool tud_usbtmc_msgBulkOut_start_cb(usbtmc_msg_request_dev_dep_out const * msgHeader) {
  return Interface::_singleton->_usbtmcMsgBulkOutStart(msgHeader);
}

bool tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete) {
  return Interface::_singleton->_usbtmcMsgData(data, len, transfer_complete);
}

bool tud_usbtmc_msgBulkIn_complete_cb() {
  return Interface::_singleton->_usbtmcMsgBulkInComplete();
}

bool tud_usbtmc_msgBulkIn_request_cb(usbtmc_msg_request_dev_dep_in const * request) {
    return Interface::_singleton->_usbtmcMsgBulkInRequest(request);
}

bool tud_usbtmc_initiate_clear_cb(uint8_t *tmcResult) {
  return Interface::_singleton->_usbtmcInitiateClear(tmcResult);
}

bool tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp) {
    return Interface::_singleton->_usbtmcCheckClear(rsp);
}

bool tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmcResult) {
    return Interface::_singleton->_usbtmcInitiateAbortBulkIn(tmcResult);
}

bool tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp) {
    return Interface::_singleton->_usbtmcCheckAbortBulkIn(rsp);
}

bool tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmcResult) {
    return Interface::_singleton->_usbtmcInitiateAbortBulkOut(tmcResult);
}

bool tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp) {
    return Interface::_singleton->_usbtmcCheckAbortBulkOut(rsp);
}

bool tud_usbtmc_notification_complete_cb(void) {
    return Interface::_singleton->_usbtmcNotificationComplete();
}

void tud_usbtmc_bulkIn_clearFeature_cb(void) {
    Interface::_singleton->_usbtmcBulkInClearFeature();
}

void tud_usbtmc_bulkOut_clearFeature_cb(void) {
    Interface::_singleton->_usbtmcBulkOutClearFeature();
}

uint8_t tud_usbtmc_get_stb_cb(uint8_t *tmcResult) {
    return Interface::_singleton->_usbtmcGetStb(tmcResult);
}

bool tud_usbtmc_indicator_pulse_cb(tusb_control_request_t const * msg, uint8_t *tmcResult) {
    return Interface::_singleton->_usbtmcIndicatorPulse(msg, tmcResult);
}


