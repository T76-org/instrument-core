/**
 * @file interface.hpp
 * @brief USB interface runtime and delegate definitions.
 * 
 * This file defines the abstract `InterfaceDelegate` callback contract and the
 * concrete `Interface` runtime responsible for initializing the USB interface,
 * sending data, and handling control transfers.
 * 
 * The runtime exposes four interfaces:
 * 
 * - A CDC interface for serial communication. This is co-opted by stdio so
 *   that printf and other stdio functions can be dumped directly to USB.
 * - An interface that's compatible with picotool's reset mechanism.
 *   This allows you to reset the device and enter bootloader mode,
 *   _provided_ that you set the USB properties correctly at compile time.
 * - A vendor interface that supports WebUSB and Microsoft OS 2.0 descriptors.
 *   This allows you to provide a custom landing page for your device
 *   when working with a compatible browser, as well as providing additional
 *   functionality, such as USBTMC compatibility.
 * - A USBTMC interface that provides a standard interface for test and 
 *   measurement devices.
 * 
 * The runtime is multithreaded and fully reentrant, allowing you to
 * send and receive data from multiple threads without blocking. It uses
 * a FreeRTOS queue to manage receiving and sending bulk data over the
 * vendor interface, and provides an internal task that manages 
 * TinyUSB events.
 * 
 * The runtime is designed to be used as a singleton, and you should
 * call the `init()` method to initialize it before using it.
 *
 * Implement an `InterfaceDelegate` subclass to handle application-specific callbacks.
 * All delegate callbacks are declared pure virtual, so each must be implemented
 * by your concrete delegate:
 * 
 * - `_onVendorControlTransferIn`: called when a control transfer that expects data
 *   to be sent back to the host (`IN` direction from the host's perspective)
 *   is received. You must reply with the data to be sent back by calling
 *   `sendVendorControlTransferData()` before returning. Note that the data
 *   sent back will be truncated to ITF_BUFFER_SIZE bytes, so you must
 *   ensure that the data you send back is no larger than that. If you 
 *   need to send more data, you should use bulk transfers instead.
 * - `_onVendorControlTransferOut`: called when a control transfer that expects data
 *   to be sent to the host (`OUT` direction from the host's perspective) is
 *   received. The data is passed as a vector of bytes, and you can process it
 *   as needed.
 * - `_onVendorDataReceived`: called when bulk data is received from the vendor 
 *   interface.
 * - `_onUSBTMCDataReceived`: called when bulk data is received from the USBTMC 
 *   interface. Remember that USBTMC transfers can be fragmented, so you may need to
 *   accumulate data until the transfer is complete. The speed USBTMC replies
 *   can be processed asynchronously, but you must be cognizant of the fact that
 *   USBTMC generally requires a specific response format and timing. Also,
 *   the runtime doesn't provide any mechanism for ensuring that a request is
 *   completely processed before the next request is handled, so you must
 *   ensure that your implementation can handle this.
 *
 * If you choose to subclass `Interface` itself, you can change the USBTMC
 * capabilities by initializing the `_usbtmcStoredCapabilities` member in your
 * subclass constructor.
 *
 * Control transfer methods must return `true` if the transfer was
 * successfully handled, or `false` if it was not. If you return `false`, the
 * TinyUSB stack will handle the transfer as an error and stall the endpoint.
 * 
 * USB vendor and product information can be set using the following
 * macros in your `CMakeLists.txt` file:
 * 
 * - T76_IC_USB_VENDOR_ID: The vendor ID for the USB device. This should be a unique identifier
 *   assigned to your organization by the USB Implementers Forum (USB-IF).
 * - T76_IC_USB_PRODUCT_ID: The product ID for the USB device. This should be a unique identifier
 *   assigned to your product by your organization.
 * - T76_IC_USB_MANUFACTURER_STRING: The manufacturer string for the USB device.
 * - T76_IC_USB_PRODUCT_STRING: The product string for the USB device.
 * - T76_IC_USB_URL: The URL for the USB device. This is typically a link to your product's
 *   documentation or support page.
 * 
 * You can further customize the USB interface by setting the following
 * macros in your `CMakeLists.txt` file:
 * 
 * - `T76_IC_USB_RUNTIME_TASK_PRIORITY`: The priority for the USB runtime task. This should be a
 *   value between 0 and configMAX_PRIORITIES - 1, where 0 is the highest priority.
 * - `T76_IC_USB_RUNTIME_TASK_STACK_SIZE`: The stack size for the USB runtime task. This should be
 *   a value in bytes, and should be large enough to handle the USB runtime task's
 *   requirements.
 * - `T76_IC_USB_DISPATCH_TASK_PRIORITY`: The priority for the USB dispatch task. Generally, this
 *   should be lower than the runtime task priority, but still high enough to handle
 *   dispatching USB events in a timely manner.
 * - `T76_IC_USB_DISPATCH_TASK_STACK_SIZE`: The stack size for the USB dispatch task. Like the
 *   runtime task stack size, this should be large enough to handle the dispatch task's
 *   requirements.
 * - `T76_IC_USB_INTERFACE_BULK_IN_QUEUE_SIZE`: The size of the USB interface bulk IN queue. This
 *   should be large enough to handle the maximum expected number of bulk IN transfers.
 * - `T76_IC_USB_INTERFACE_BULK_IN_MAX_MESSAGE_SIZE`: The maximum message size for USB interface
 *   bulk IN transfers. This should be large enough to handle the maximum expected
 *   size of bulk IN messages.
 */

#pragma once


#include <memory>
#include <queue>
#include <stdint.h>
#include <vector>

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

#include <t76/fixed_queue.hpp>
#include "callbacks.hpp"

namespace T76::Core::USB {

    class Interface;

    /**
     * @brief Abstract delegate interface for handling USB callbacks.
     * 
     * Implement this abstract base class to receive data and control transfer
     * events emitted by the concrete `Interface` runtime.
     */
    class InterfaceDelegate {
    friend class Interface;

    protected:        
        /**
         * @brief Bulk data received callback.
         * 
         * @param data The data received from the USB host.
         * 
         * This method is called when bulk data is received from the USB host.
         * You can implement this method in a subclass to handle the received data.
         * 
         * Note that this method is called asynchronously from the USB dispatch task.
         * If you wish to process the data asynchronously, you can take ownership
         * of it and process it in your own task or callback.
         */
        virtual void _onVendorDataReceived(const std::vector<uint8_t> &data) = 0;

        /**
         * @brief Vendor control transfer IN callback.
         * 
         * @param port The USB port number.
         * @param request The control request that was received.
         * 
         * This method is called when a control transfer that expects data to be sent back
         * to the host is received. You can implement this method in a subclass to handle
         * the control transfer and send the appropriate data back using `sendVendorControlTransferData()`.
         * 
         * @return true if the control transfer was successfully handled, false otherwise.
         */
        virtual bool _onVendorControlTransferIn(uint8_t port, const tusb_control_request_t *request) = 0;

        /**
         * @brief Vendor control transfer OUT callback.
         * 
         * @param request The control request that was received.
         * @param value The value associated with the control request.
         * @param data The data received in the control transfer.
         * 
         * This method is called when a control transfer that expects data to be sent to the host
         * is received. You can implement this method in a subclass to handle the control transfer
         * and process the received data as needed.
         * 
         * @return true if the control transfer was successfully handled, false otherwise.
         */
        virtual bool _onVendorControlTransferOut(uint8_t request, uint16_t value, const std::vector<uint8_t> &data) = 0;

        /**
         * @brief USBTMC data received callback.
         * 
         * @param data The data received from the USB host.
         * @param transfer_complete Indicates whether the transfer is complete.
         * 
         * This method is called when bulk data is received from the USB host on the USBTMC interface.
         * You can implement this method in a subclass to handle the received data.
         * 
         */
        virtual void _onUSBTMCDataReceived(const std::vector<uint8_t> &data, bool transfer_complete) = 0;
    };

    /**
     * @brief Concrete USB interface runtime.
     * 
     * This class provides the operational implementation for USB communication.
     * It sets up a stdio-compatible CDC interface, a reset interface compatible
     * with picotool, and a vendor interface that supports WebUSB and Microsoft
     * OS 2.0 descriptors in addition to custom functionality such as USBTMC.
     * 
     * Register an `InterfaceDelegate` via `delegate()` to attach application
     * specific behavior. The delegate is expected to implement:
     * 
     * - `_onVendorControlTransferIn`: invoked when a control transfer that expects
     *   data to be sent back to the host (`IN` direction from the host's perspective)
     *   is received. You must reply with the data to be sent back by calling
     *   `sendVendorControlTransferData()` before returning. Note that the data
     *   sent back will be truncated to ITF_BUFFER_SIZE bytes, so you must ensure
     *   that the data you send back is no larger than that. If you need to send
     *   more data, you should use bulk transfers instead.
     * - `_onVendorControlTransferOut`: called when a control transfer that expects
     *   data to be sent to the host (`OUT` direction from the host's perspective)
     *   is received. The data is passed as a vector of bytes, and you can process it
     *   as needed.
     * - `_onVendorDataReceived`: called when bulk data is received from the vendor 
     *   interface.
     * - `_onUSBTMCDataReceived`: called when bulk data is received from the USBTMC 
     *   interface. Remember that USBTMC transfers can be fragmented, so you may need to
     *   accumulate data until the transfer is complete. The speed USBTMC replies
     *   can be processed asynchronously, but you must be cognizant of the fact that
     *   USBTMC generally requires a specific response format and timing. Also,
     *   the runtime doesn't provide any mechanism for ensuring that a request is
     *   completely processed before the next request is handled, so you must
     *   ensure that your implementation can handle this.
     * 
     * Control transfer methods must return `true` if the transfer was
     * successfully handled, or `false` if it was not. If you return `false`, the
     * TinyUSB stack will handle the transfer as an error and stall the endpoint.
     * 
     */
    class Interface {
    public:
        /**
         * @brief Constructor for the USB interface.
         */
        Interface(InterfaceDelegate &delegate);
        
        /**
         * @brief Virtual destructor to allow safe subclass cleanup.
         */
        virtual ~Interface() = default;

        /**
         * @brief Initialize the USB interface. 
         * 
         * This method should be called before using the class.
         * 
         * You can oveerride this method to perform additional initialization, but 
         * you must call the base class implementation to ensure that the USB 
         * interface is properly initialized.
         */
        virtual void init();

        /**
         * @brief Send bulk data to the USB host over the vendor interface.
         * 
         * @param data The data to be sent. The function takes ownership of the data
         *            and will copy it to the USB buffer asynchronously.
         * 
         * This method is thread-safe and can be called from any thread. The data
         * transfer operation is queued and processed in the USB dispatch task.
         */
        void sendVendorBulkData(const std::vector<uint8_t> &data);

        /**
         * @brief Send data for a control transfer.
         * 
         * @param port The USB port number.
         * @param request The control request to be sent.
         * @param data The data to be sent in the control transfer.
         * 
         * This method is used to send data in response to a control transfer request.
         * It handles the IN direction transfers where the host expects data to be sent back.
         * 
         * @return true if the data was successfully sent, false otherwise.
         */
        bool sendVendorControlTransferData(uint8_t port, const tusb_control_request_t *request, const std::vector<uint8_t> &data);

        /**
         * @brief Send USBTMC bulk data to the USB host.
         * @param data The data to be sent. The function takes ownership of the data
         *            and will copy it to the USBTMC buffer asynchronously.
         * 
         * This method is thread-safe and can be called from any thread. The data
         * transfer operation is queued and processed in the USB dispatch task.
         */
        void sendUSBTMCBulkData(const std::vector<uint8_t> &data);

        /**
         * @brief Send a USBTMC SRQ interrupt to the USB host.
         * 
         * @param srq The SRQ value to be sent.
         */
        void sendUSBTMCSRQInterrupt(const uint8_t srq);

    protected:

        /**
         * @brief Registered delegate that receives interface callbacks.
         */
        InterfaceDelegate &_delegate;

        /**
         * @brief The type of an item sent to the dispatch queue.
         * 
         * The dispatch queue handles both data received from the USB host
         * and data to be sent to the USB host. This enum is used to differentiate
         * between the two types of items in the dispatch queue.
         * 
         */
        enum class DispatchType {
            DataReceived,
            SendData,
        };

        /**
         * @brief A single item in the dispatch queue.
         * 
         * This struct represents a single item in the dispatch queue.
         * It contains the type of the item and the data associated with it.
         * 
         * Note that ownership of the data property is transferred to the
         * dispatch queue when the item is sent. The queue will manage the
         * lifetime of the data, and you should not access it after sending
         * the item to the queue.
         * 
         */
        typedef struct {
            DispatchType type;
            std::vector<uint8_t> data;
        } DispatchItem;

        /**
         * @brief The singleton instance of the USB interface.
         * 
         * This static member holds the singleton instance of the USB interface.
         * 
         * This instance is used by the TinyUSB callbacks to dispatch events
         * to the USB interface class.
         * 
         */
        static Interface *_singleton;

        /**
         * @brief Queue used to dispatch USB events to the worker task.
         */
        QueueHandle_t _dispatchQueue;

        /**
         * @brief Buffer that stores IN-direction vendor control transfer data.
         */
        std::vector<uint8_t> _vendorControlDataOutBuffer;

        /**
         * @brief Buffer that stores OUT-direction vendor control transfer data.
         */
        std::vector<uint8_t> _vendorControlDataInBuffer;

        /**
         * @brief Fixed-size queue backing USBTMC bulk IN transfers.
         */
        T76::Core::Utils::FixedSizeQueue<std::vector<uint8_t>> _usbtmcBulkInDataQueue;

        /**
         * @brief Data currently in-flight for a USBTMC bulk IN transfer.
         */
        std::vector<uint8_t> _usbtmcBulkInDataPending;

        /**
         * @brief Offset into the pending USBTMC bulk IN payload.
         */
        size_t _usbtmcBulkInPendingOffset = 0;

        /**
         * @brief Default USBTMC capability descriptor returned to the host.
         */
        usbtmc_response_capabilities_488_t _usbtmcStoredCapabilities = {
            .USBTMC_status = USBTMC_STATUS_SUCCESS,
            .bcdUSBTMC = USBTMC_VERSION,
            .bmIntfcCapabilities = {
                .listenOnly = 0,
                .talkOnly = 0,
                .supportsIndicatorPulse = 1
            },
            .bmDevCapabilities = {
                .canEndBulkInOnTermChar = 0
            },
            .bcdUSB488 = USBTMC_488_VERSION,
            .bmIntfcCapabilities488 = {
                .supportsTrigger = 1,
                .supportsREN_GTL_LLO = 0,
                .is488_2 = 1
            },
            .bmDevCapabilities488 = {
                .DT1 =0,
                .RL1 = 0,
                .SR1 = 1,
                .SCPI = 1,
            }
        };

        /**
         * @brief Cached SRQ interrupt payload for USBTMC notifications.
         */
        usbtmc_srq_interrupt_488_t _usbtmcSRQInterruptData;

        /**
         * @brief The runtime task.
         * 
         * This task simply calls `tud_task()` in a loop, allowing the TinyUSB stack
         * to process USB events. It is created during the initialization of the USB
         * interface and runs indefinitely.
         * 
         */
        void _runtimeTask();

        /**
         * @brief The dispatch task.
         * 
         * This task processes items from the dispatch queue. It handles both data
         * received from the USB host and data to be sent to the host. It is created
         * during the initialization of the USB interface and runs indefinitely.
         * 
         */
        void _dispatchTask();

        /**
         * @brief Process a WebUSB request.
         * 
         * This method processes WebUSB requests, including the landing page URL
         * and Microsoft OS 2.0 compatible descriptor requests. It is called during
         * the control transfer stage when a WebUSB request is received.
         * 
         * @param rhport The USB port number.
         * @param request The control request to be processed.
         * 
         * @return true if the request was successfully processed, false otherwise.
         */
        bool _processWebUSBRequest(uint8_t rhport, const tusb_control_request_t* request);

        /**
         * @brief Handle vendor data received.
         * 
         * This method is called when bulk data is received from the USB host on the
         * vendor interface.
         * 
         * It queues the received data for processing in the dispatch task.
         * 
         * @param itf The interface number.
         * @param buffer The buffer containing the received data.
         * @param bufsize The size of the received data buffer.
         */
        void _vendorDataReceived(uint8_t itf, uint8_t* buffer, uint16_t bufsize);

        /**
         * @brief Handle vendor control transfer.
         * 
         * This method is called when a control transfer is received on the vendor interface.
         * If the transfer is in the setup stage, it first determines whether the host is
         * sending a WebUSB request or a Microsoft OS 2.0 request, in which case it delegates
         * the handling to `_processWebUSBRequest()`.
         * 
         * Otherwise, if the transfer is of type IN (meaning that the host expects data back),
         * it delegates the handling to `_onVendorControlTransferIn()`.
         * 
         * During the data stage, if the request is of type OUT (meaning that the host is
         * sending data to the device), it passes the data to `_onVendorControlTransferOut()`.
         * 
         * In all other cases, it returns true to indicate that the control transfer
         * was successfully handled.
         * 
         * @param rhport The USB port number.
         * @param stage The stage of the control transfer (setup, data, or status).
         * @param request The control request to be processed.
         * 
         * @return true if the control transfer was successfully handled, false otherwise.
         */
        bool _vendorControlTransfer(uint8_t rhport, uint8_t stage, const tusb_control_request_t* request);

        /**
         * @brief Get the USBTMC capabilities.
         * 
         * This method returns the USBTMC capabilities of the device.
         * It is called when the host requests the USBTMC capabilities.
         * 
         * @return A pointer to the USBTMC capabilities structure.
         */
        usbtmc_response_capabilities_488_t const *_usbtmcCapabilities();

        /**
         * @brief Handle USBTMC open event.
         * 
         * This method is called when the USBTMC interface is opened.
         * It initializes the USBTMC interface and prepares it for communication.
         * 
         * @param interface_id The ID of the USBTMC interface being opened.
         */ 
        void _usbtmcOpen(uint8_t interface_id);

        /**
         * @brief Handle USBTMC message trigger.
         * @param msg The USBTMC message to be processed.
         * 
         * This method is called when a USBTMC message is received.
         * It processes the message and returns true if the message was successfully handled,
         * or false if it was not.
         * 
         * @return true if the message was successfully handled, false otherwise.
         */
        bool _usbtmcMsgTrigger(usbtmc_msg_generic_t* msg);

        /**
         * @brief Handle USBTMC bulk OUT message start.
         * @param msgHeader The header of the USBTMC bulk OUT message.
         * 
         * This method is called when a USBTMC bulk OUT message is received.
         * It processes the message header and returns true if the message was successfully handled,
         * or false if it was not.
         * 
         * @return true if the message was successfully handled, false otherwise.
         */
        bool _usbtmcMsgBulkOutStart(usbtmc_msg_request_dev_dep_out const * msgHeader);

        /**
         * @brief Handle USBTMC message data.
         * @param data The data received in the USBTMC message.
         * @param len The length of the data.
         * @param transfer_complete Indicates whether the transfer is complete.
         * 
         * This method is called when data is received in a USBTMC message.
         * It processes the data and returns true if the data was successfully handled,
         * or false if it was not.
         * 
         * @return true if the data was successfully handled, false otherwise.
         */
        bool _usbtmcMsgData(void *data, size_t len, bool transfer_complete);

        /**
         * @brief Handle USBTMC bulk IN message completion.
         * @return true if the bulk IN message was successfully completed, false otherwise.
         * 
         * This method is called when a USBTMC bulk IN message is completed.
         * It processes the completion and returns true if the message was successfully handled,
         * or false if it was not.
         */
        bool _usbtmcMsgBulkInComplete();

        /**
         * @brief Handle USBTMC bulk IN message request.
         * @param request The request for the USBTMC bulk IN message.
         * 
         * This method is called when a USBTMC bulk IN message request is received.
         * It processes the request and returns true if the request was successfully handled,
         * or false if it was not.
         * 
         * @return true if the request was successfully handled, false otherwise.
         */
        bool _usbtmcMsgBulkInRequest(usbtmc_msg_request_dev_dep_in const * request);

        /**
         * @brief Initiate a USBTMC clear status operation.
         * @param tmcResult Pointer to a variable where the result of the operation will be stored.
         * 
         * This method initiates a USBTMC clear status operation and returns true if the operation
         * was successfully initiated, or false if it was not.
         * 
         * @return true if the operation was successfully initiated, false otherwise.
         */
        bool _usbtmcInitiateClear(uint8_t *tmcResult);

        /**
         * @brief Check the status of a USBTMC clear status operation.
         * @param rsp Pointer to the response structure.
         * 
         * This method checks the status of a USBTMC clear status operation and returns true if the operation
         * was successfully completed, or false if it was not.
         * 
         * @return true if the operation was successfully completed, false otherwise.
         */
        bool _usbtmcCheckClear(usbtmc_get_clear_status_rsp_t *rsp);

        /**
         * @brief Initiate a USBTMC abort bulk IN operation.
         * @param tmcResult Pointer to a variable where the result of the operation will be stored.
         * 
         * This method initiates a USBTMC abort bulk IN operation and returns true if the operation
         * was successfully initiated, or false if it was not.
         * 
         * @return true if the operation was successfully initiated, false otherwise.
         */
        bool _usbtmcInitiateAbortBulkIn(uint8_t *tmcResult);

        /**
         * @brief Check the status of a USBTMC abort bulk IN operation.
         * @param rsp Pointer to the response structure.
         * 
         * This method checks the status of a USBTMC abort bulk IN operation and returns true if the operation
         * was successfully completed, or false if it was not.
         * 
         * @return true if the operation was successfully completed, false otherwise.
         */
        bool _usbtmcInitiateAbortBulkOut(uint8_t *tmcResult);

        /**
         * @brief Check the status of a USBTMC abort bulk OUT operation.
         * @param rsp Pointer to the response structure.
         * 
         * This method checks the status of a USBTMC abort bulk OUT operation and returns true if the operation
         * was successfully completed, or false if it was not.
         * 
         * @return true if the operation was successfully completed, false otherwise.
         */
        bool _usbtmcCheckAbortBulkIn(usbtmc_check_abort_bulk_rsp_t *rsp);

        /**
         * @brief Check the status of a USBTMC abort bulk OUT operation.
         * @param rsp Pointer to the response structure.
         * 
         * This method checks the status of a USBTMC abort bulk OUT operation and returns true if the operation
         * was successfully completed, or false if it was not.
         * 
         * @return true if the operation was successfully completed, false otherwise.
         */
        bool _usbtmcCheckAbortBulkOut(usbtmc_check_abort_bulk_rsp_t *rsp);

        /**
         * @brief Called when a notification packet can be sent.
         * 
         * @return true if the notification packet was successfully sent, false otherwise.
         */
        bool _usbtmcNotificationComplete();

        /**
         * @brief Clear the feature for the USBTMC bulk IN endpoint.
         * This method is called when the host clears the feature for the bulk IN endpoint.
         * 
         * It is used to reset the state of the bulk IN endpoint and prepare it for new data transfers.
         */
        void _usbtmcBulkInClearFeature();

        /**
         * @brief Clear the feature for the USBTMC bulk OUT endpoint.
         * This method is called when the host clears the feature for the bulk OUT endpoint.
         * 
         * It is used to reset the state of the bulk OUT endpoint and prepare it for new data transfers.
         */
        void _usbtmcBulkOutClearFeature();

        /**
         * @brief Get the status byte (STB) for USBTMC.
         * @param tmcResult Pointer to a variable where the result of the operation will be stored.
         * 
         * This method retrieves the status byte for USBTMC and returns it. The status byte
         * indicates the current status of the USBTMC interface.
         * 
         * Override this method in your subclass to provide the actual status byte.
         * 
         * @return The status byte, or an error code if the operation failed.
         */
        virtual uint8_t _usbtmcGetStb(uint8_t *tmcResult);

        /**
         * @brief Handle USBTMC indicator pulse request.
         * @param msg The control request message received.
         * @param tmcResult Pointer to a variable where the result of the operation will be stored.
         * 
         * This method handles the USBTMC indicator pulse request. It processes the request
         * and returns true if the request was successfully handled, or false if it was not.
         * 
         * Override this method in your subclass to provide the actual handling of the indicator pulse request.
         * 
         * @return true if the request was successfully handled, false otherwise.
         */
        virtual bool _usbtmcIndicatorPulse(tusb_control_request_t const * msg, uint8_t *tmcResult);

        // TinyUSB callbacks, defined in callbacks.cpp and callbacks.hpp

        // Vendor interface callbacks

        friend void ::tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize);
        friend bool ::tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request);

        // USBTMC interface callbacks

        friend usbtmc_response_capabilities_488_t const * ::tud_usbtmc_get_capabilities_cb();
        friend void ::tud_usbtmc_open_cb(uint8_t interface_id);
        friend bool ::tud_usbtmc_msg_trigger_cb(usbtmc_msg_generic_t* msg);
        friend bool ::tud_usbtmc_msgBulkOut_start_cb(usbtmc_msg_request_dev_dep_out const * msgHeader);
        friend bool ::tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete);
        friend bool ::tud_usbtmc_msgBulkIn_complete_cb();
        friend bool ::tud_usbtmc_msgBulkIn_request_cb(usbtmc_msg_request_dev_dep_in const * request);
        friend bool ::tud_usbtmc_initiate_clear_cb(uint8_t *tmcResult);
        friend bool ::tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp);
        friend bool ::tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmcResult);
        friend bool ::tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp);
        friend bool ::tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmcResult);
        friend bool ::tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp);
        friend bool ::tud_usbtmc_notification_complete_cb();
        friend void ::tud_usbtmc_bulkIn_clearFeature_cb(void);
        friend void ::tud_usbtmc_bulkOut_clearFeature_cb(void);
        friend uint8_t ::tud_usbtmc_get_stb_cb(uint8_t *tmcResult);
        friend bool ::tud_usbtmc_indicator_pulse_cb(tusb_control_request_t const * msg, uint8_t *tmcResult);

    };
    
} // namespace T76::Core::USB
