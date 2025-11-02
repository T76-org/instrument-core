/**
 * @file interface.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "t76/usb_interface.hpp"

#include <FreeRTOS.h>
#include <task.h>

#include "interface_interrupt.hpp"


using namespace T76::Core::USB;


Interface* Interface::_singleton = nullptr;


Interface::Interface(InterfaceDelegate &delegate) : 
    _delegate(delegate),
    _usbtmcBulkInDataQueue(T76_IC_USB_INTERFACE_BULK_IN_QUEUE_SIZE) {
}

void Interface::init() {
    // Initialize the USB interface
    _singleton = this;

    // Create a queue for dispatching USB events
    _dispatchQueue = xQueueCreate(10, sizeof(DispatchItem*));

    TaskHandle_t taskHandle = nullptr;

    // Create a task for runtime operations
    xTaskCreate(
        [](void* param) {
            Interface* iface = static_cast<Interface*>(param);
            iface->_runtimeTask();
        }, 
        "USBRuntime", 
        T76_IC_USB_RUNTIME_TASK_STACK_SIZE, 
        this, 
        T76_IC_USB_RUNTIME_TASK_PRIORITY,
        &taskHandle
    );

    xTaskCreate(
        [](void* param) {
            Interface* iface = static_cast<Interface*>(param);
            iface->_dispatchTask();
        }, 
        "USBDispatch", 
        T76_IC_USB_DISPATCH_TASK_STACK_SIZE, 
        this, 
        T76_IC_USB_DISPATCH_TASK_PRIORITY, 
        &taskHandle
    );
}

void Interface::sendVendorBulkData(const std::vector<uint8_t> &data) {
    DispatchItem *item = new DispatchItem;
    
    item->type = DispatchType::SendData;
    item->data = std::move(data);

    if (xQueueSend(_dispatchQueue, &item, portMAX_DELAY) != pdTRUE) {
        //TODO: Log error
        delete item; // Clean up the item if sending to the queue fails
    }
}

bool Interface::sendVendorControlTransferData(uint8_t port, const tusb_control_request_t *request, const std::vector<uint8_t> &data) {
    if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (data.size() != request->wLength) {
            //TODO: Log error
            return false;
        }

        _vendorControlDataOutBuffer = data; // Store the data to be sent

        // Handle IN control transfer
        return tud_control_xfer(port, request, static_cast<void*>(_vendorControlDataOutBuffer.data()), _vendorControlDataOutBuffer.size());
    }

    return false; // Only IN transfers are supported for control transfers
}

void Interface::sendUSBTMCBulkData(const std::vector<uint8_t> &data) {
    _usbtmcBulkInDataQueue.push(data);
}

void Interface::sendUSBTMCBulkData(std::string data, bool addNewline) {
    std::vector<uint8_t> byteData(data.begin(), data.end());

    if (addNewline) {
        byteData.push_back('\n');
    }

    _usbtmcBulkInDataQueue.push(byteData);
}

void Interface::sendUSBTMCSRQInterrupt(const uint8_t srq) {
    _usbtmcSRQInterruptData.bNotify1 = USB488_bNOTIFY1_SRQ;
    _usbtmcSRQInterruptData.StatusByte = srq;

    // A bug in TinyUSB (see https://github.com/hathach/tinyusb/issues/2735)
    // forces us to hack around the fact that the following call:
    //
    // tud_usbtmc_transmit_notification_data(&_usbtmcSRQInterruptData, sizeof(_usbtmcSRQInterruptData));
    //
    // would never work because the USB stack doesn't properly check
    // whether the interrupt endpoint is, in fact, busy. Therefore,
    // we hardcode the port and endpoint numbers here.
    //
    // TODO: Check if this works universally.

    uint8_t rhport = 0;
    uint8_t endpoint = EPNUM_USBTMC_INT;

    if (!usbd_edpt_busy(rhport, endpoint)) {
        if (!usbd_edpt_xfer(rhport, endpoint, reinterpret_cast<uint8_t*>(&_usbtmcSRQInterruptData), (uint16_t)sizeof(_usbtmcSRQInterruptData))) {
            //TODO: Log error
        }
    }
}

void Interface::_runtimeTask() {
    board_init();
    tusb_init();

    for(;;) {
        tud_task();
        
        if (!tud_task_event_ready()) {
            vTaskDelay(pdMS_TO_TICKS(T76_IC_USB_TASK_DELAY_MS));
        }
    }
}

void Interface::_dispatchTask() {
    DispatchItem *item;

    for(;;) {
        if (xQueueReceive(_dispatchQueue, &item, portMAX_DELAY) == pdTRUE) {
            switch (item->type) {
                case DispatchType::DataReceived:
                    _delegate._onVendorDataReceived(item->data);
                    break;

                case DispatchType::SendData: {
                        size_t offset = 0;
                        size_t total = item->data.size();
                        uint8_t *buffer = item->data.data();

                        while (offset < total) {
                            size_t written = tud_vendor_write(buffer + offset, total - offset);
                            tud_vendor_write_flush();
                            offset += written;
                            taskYIELD(); // Yield to allow other tasks to run
                        }
                    }
                    break;

                default:
                    // Handle unexpected dispatch type
                    //TODO: Log error
                    break;
            }

            delete item; // Clean up the item after processing
        }
    }
}

void Interface::_vendorDataReceived(uint8_t itf, uint8_t* buffer, uint16_t bufsize) {
    std::vector<uint8_t> data(buffer, buffer + bufsize);

    DispatchItem *item = new DispatchItem;
    
    item->type = DispatchType::DataReceived;
    item->data = std::move(data);

    tud_vendor_read_flush(); // Flush the vendor read buffer

    if (xQueueSend(_dispatchQueue, &item, portMAX_DELAY) != pdTRUE) {
        //TODO: Log error
        delete item;
    }
}

bool Interface::_processWebUSBRequest(uint8_t rhport, const tusb_control_request_t* request) {
    switch (request->bmRequestType_bit.type) {
        case TUSB_REQ_TYPE_VENDOR:
            switch (request->bRequest) {
                case VENDOR_REQUEST_WEBUSB:
                    // Match vendor request in BOS descriptor
                    // Get landing page url
                    return tud_control_xfer(rhport, request, (void*)(uintptr_t) &desc_url, desc_url.bLength);

                case VENDOR_REQUEST_MICROSOFT:
                    if (request->wIndex == 7) {
                        // Get Microsoft OS 2.0 compatible descriptor
                        uint16_t total_len;
                        memcpy(&total_len, desc_ms_os_20 + 8, 2);

                        return tud_control_xfer(rhport, request, (void*)(uintptr_t) desc_ms_os_20, total_len);
                    } else {
                        return false;
                    }

                default: break;
            }

            break;

            default: break;
    }

    return false; // Not a WebUSB request
}

bool Interface::_vendorControlTransfer(uint8_t rhport, uint8_t stage, const tusb_control_request_t* request) {
    switch(stage) {
        case CONTROL_STAGE_SETUP:

            if (request->wIndex == reset_interface_number) {
                return _processWebUSBRequest(rhport, request);
            }

            if (request->bmRequestType_bit.direction == TUSB_DIR_OUT) {
                // Prepare to receive data for OUT control transfer
                _vendorControlDataInBuffer.resize(request->wLength);
                return tud_control_xfer(rhport, request, _vendorControlDataInBuffer.data(), _vendorControlDataInBuffer.size());
            }
            
            return _delegate._onVendorControlTransferIn(rhport, request);


        case CONTROL_STAGE_DATA:

            if (request->bmRequestType_bit.direction == TUSB_DIR_OUT) {
                return _delegate._onVendorControlTransferOut(request->bRequest, request->wValue, std::vector<uint8_t>(_vendorControlDataInBuffer.begin(), _vendorControlDataInBuffer.begin() + request->wLength));
            }

            break;

        case CONTROL_STAGE_ACK:
            break;

        default:
            return false; // Unsupported stage
    }

    return true;
}

usbtmc_response_capabilities_488_t const * Interface::_usbtmcCapabilities() {
    return &_usbtmcStoredCapabilities;
}

void Interface::_usbtmcOpen(uint8_t interface_id) {
    // Handle USBTMC open event
    tud_usbtmc_start_bus_read(); // Start reading from the USBTMC bus
}

bool Interface::_usbtmcMsgTrigger(usbtmc_msg_generic_t* msg) {
    // Handle USBTMC message trigger
    return true;
}

bool Interface::_usbtmcMsgBulkOutStart(usbtmc_msg_request_dev_dep_out const *msgHeader) {
    // Handle USBTMC bulk OUT message start
    return true;
}

bool Interface::_usbtmcMsgData(void *data, size_t len, bool transfer_complete) {    
    if (len == 0) {
        //TODO: Log error
        return false; // Invalid data
    }

    if (len > T76_IC_USB_INTERFACE_BULK_IN_MAX_MESSAGE_SIZE) {
        //TODO: Log error
        return false; // Data too large
    }

    _delegate._onUSBTMCDataReceived(std::vector<uint8_t>(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + len), transfer_complete);
    
    tud_usbtmc_start_bus_read(); // Start reading from the USBTMC bus

    return true; // Always return true so as not to stall the USBTMC interface
}

bool Interface::_usbtmcMsgBulkInComplete() {
    // Handle USBTMC bulk IN message completion
    tud_usbtmc_start_bus_read(); // Start reading from the USBTMC bus
    return true;
}

bool Interface::_usbtmcMsgBulkInRequest(usbtmc_msg_request_dev_dep_in const *request) {
    if (_usbtmcBulkInDataPending.size() == 0) {
        // No pending data, try to get the next message from the queue

        if (_usbtmcBulkInDataQueue.size() == 0) {
            return true; // Never stall, always return true as per USBTMC spec
        }

        _usbtmcBulkInDataQueue.tryPop(_usbtmcBulkInDataPending);
        _usbtmcBulkInPendingOffset = 0;
    }

    // Send at most request->TransferSize bytes from the pending data
    // If that's less than the pending data size, keep the rest for next time

    size_t toSend = std::min(static_cast<size_t>(request->TransferSize), _usbtmcBulkInDataPending.size() - _usbtmcBulkInPendingOffset);
    bool endOfMessage = ((toSend + _usbtmcBulkInPendingOffset) == _usbtmcBulkInDataPending.size());

    if (!tud_usbtmc_transmit_dev_msg_data(_usbtmcBulkInDataPending.data() + _usbtmcBulkInPendingOffset, toSend, endOfMessage, false)) {
        _usbtmcBulkInDataPending.clear();
        _usbtmcBulkInPendingOffset = 0;

        return true; // Failed to send data
    }

    if (endOfMessage) {
        _usbtmcBulkInDataPending.clear();
        _usbtmcBulkInPendingOffset = 0;
    } else {
        _usbtmcBulkInPendingOffset += toSend;
    }

    return true; // Never stall, always return true as per USBTMC spec
}

bool Interface::_usbtmcInitiateClear(uint8_t *tmcResult) {
    // Initiate USBTMC clear operation
    *tmcResult = USBTMC_STATUS_SUCCESS;

    _usbtmcBulkInDataQueue.clear(); // Clear the bulk IN data queue
    _usbtmcBulkInDataPending.clear();
    _usbtmcBulkInPendingOffset = 0;

    tud_usbtmc_start_bus_read(); // Start reading from the USBTMC bus

    return true;
}

bool Interface::_usbtmcCheckClear(usbtmc_get_clear_status_rsp_t *rsp) {
    // Check USBTMC clear status
    
    rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
    rsp->bmClear.BulkInFifoBytes = 0;
    
    return true;
}

bool Interface::_usbtmcInitiateAbortBulkIn(uint8_t *tmcResult) {
    // Initiate USBTMC abort bulk IN operation

    *tmcResult = USBTMC_STATUS_SUCCESS;

    _usbtmcBulkInDataQueue.clear();
    _usbtmcBulkInDataPending.clear();
    _usbtmcBulkInPendingOffset = 0;

    return true;
}

bool Interface::_usbtmcCheckAbortBulkIn(usbtmc_check_abort_bulk_rsp_t *rsp) {
    // Check USBTMC abort bulk IN status

    tud_usbtmc_start_bus_read(); // Start reading from the USBTMC bus

    return true;
}

bool Interface::_usbtmcInitiateAbortBulkOut(uint8_t *tmcResult) {
    // Initiate USBTMC abort bulk OUT operation

    *tmcResult = USBTMC_STATUS_SUCCESS;

    return true;
}

bool Interface::_usbtmcCheckAbortBulkOut(usbtmc_check_abort_bulk_rsp_t *rsp) {
    // Check USBTMC abort bulk OUT status
    tud_usbtmc_start_bus_read(); // Start reading from the USBTMC bus

    return true;
}

bool Interface::_usbtmcNotificationComplete() {
    return true;
}

void Interface::_usbtmcBulkInClearFeature() {
    // Handle USBTMC bulk IN clear feature
}

void Interface::_usbtmcBulkOutClearFeature() {
    // Handle USBTMC bulk OUT clear feature
    tud_usbtmc_start_bus_read(); // Start reading from the USBTMC bus
}

uint8_t Interface::_usbtmcGetStb(uint8_t *tmcResult) {
    // Get USBTMC status byte (STB)
    *tmcResult = USBTMC_STATUS_SUCCESS; 
    return 0; // Return 0 by default. 
}

bool Interface::_usbtmcIndicatorPulse(tusb_control_request_t const * msg, uint8_t *tmcResult) {
    // Handle USBTMC indicator pulse request
    *tmcResult = USBTMC_STATUS_SUCCESS; // Set the result to 0 for now
    return true; // Return true to indicate success
}
