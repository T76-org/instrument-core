/**
 * @file main.cpp
 * @brief Main application entry point file
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include <stdio.h>
#include <cstdlib>
#include <string>
#include <vector>

#include <FreeRTOS.h>
#include <task.h>
#include <tusb.h>

#include <pico/cyw43_arch.h>
#include <pico/status_led.h>

#include <t76/app.hpp>
#include <t76/scpi_interpreter.hpp>

namespace T76 {

class UsbTmcOutputStream : public T76::SCPI::OutputStreamBase {
public:
    explicit UsbTmcOutputStream(T76::Core::USB::Interface &usb)
        : _usb(usb) {}

    void write(uint8_t byte, bool flush = false) override {
        _buffer.push_back(byte);
        if (flush) {
            _flushBuffer();
        }
    }

    void write(const std::string &str, bool flush = false) override {
        _buffer.insert(_buffer.end(), str.begin(), str.end());
        if (flush) {
            _flushBuffer();
        }
    }

    void flush() {
        _flushBuffer();
    }

private:
    void _flushBuffer() {
        if (_buffer.empty()) {
            return;
        }

        _usb.sendUSBTMCBulkData(_buffer);
        _buffer.clear();
    }

    T76::Core::USB::Interface &_usb;
    std::vector<uint8_t> _buffer;
};

// App class implementation

class App : public T76::Core::App, std::enable_shared_from_this<T76::Core::USB::InterfaceDelegate> {

public:

UsbTmcOutputStream _scpiOutputStream;
T76::SCPI::Interpreter<T76::App> _interpreter;

App() : _scpiOutputStream(_usbInterface),
            _interpreter(_scpiOutputStream, *this) {
}

void _onVendorDataReceived(const std::vector<uint8_t> &data) {}

bool _onVendorControlTransferIn(uint8_t port, const tusb_control_request_t *request) { 
    return true; 
}

bool _onVendorControlTransferOut(uint8_t request, uint16_t value, const std::vector<uint8_t> &data) { 
    return true; 
}

void _onUSBTMCDataReceived(const std::vector<uint8_t> &data, bool transfer_complete) {
    printf("Receiving command\n");
    for (const auto &byte : data) {
        _interpreter.processInputCharacter(byte);
    }

    if (transfer_complete) {
        _interpreter.processInputCharacter('\n'); // Finalize the command if transfer is complete
    }
}

void _queryIDN(const std::vector<T76::SCPI::ParameterValue> &params, T76::SCPI::Interpreter<App> &interpreter) {
    interpreter.outputStream.formatString("Hello there");
}

bool activate() {
    _usbInterface.delegate(shared_from_this());
    return true;
}

void makeSafe() {
    // Currently does nothing
}

const char* getComponentName() const {
    return "App";
}

void triggerMemManageFault() {
    printf("About to trigger fault...\n");
    sleep_ms(100);  // Give printf time to flush
    
    // Method 1: Execute code from an invalid address (most reliable for HardFault)
    void (*bad_function)(void) = (void(*)(void))0xFFFFFFFF;
    bad_function();  // Jump to invalid address - should definitely fault
    
    // If we somehow survive that, try other methods:
    // Method 2: Write to a high invalid address
    volatile uint32_t* bad_ptr = (volatile uint32_t*)0xFFFFFFFF;
    *bad_ptr = 0xDEADBEEF;
    
    // Method 3: NULL pointer dereference
    volatile uint32_t* null_ptr = nullptr;
    *null_ptr = 0xDEADBEEF;
}

void _printTask() {
    int count = 0;

    while (true) {
        char *ptr = new char[320];
        snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
        fputs(ptr, stdout);
        delete[] ptr;

        // char *f = (char *)malloc(5000);
        // f[0] = 0;

        // if (count > 30) {
        //     triggerMemManageFault();  // Trigger HardFault for testing
        // }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void _init() {
    // Initialize stdio and status LED
    stdio_init_all();
    status_led_init();
}

void _initCore0() {
    // Create FreeRTOS tasks
    xTaskCreate(
        [](void *param) {
            App *app = static_cast<App*>(param);
            app->_printTask();
        },
        "print",
        2256,
        this,
        10,
        NULL
    );
}

void _startCore1() {
    int count = 0;
    char ptr[100];

    while (true) {
        // Send heartbeat to Core 0 watchdog manager to indicate Core 1 is alive
        T76::Core::Safety::feedWatchdogFromCore1();
        
        snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
        fputs(ptr, stdout);
        status_led_set_state(!status_led_get_state());
        
        sleep_ms(100);  // Send heartbeat every 100ms (well within 2s timeout)
    }
}

}; // class App

} // namespace T76

/**
 * @brief Global application instance
 * 
 * Creates the singleton App instance that will be run by main().
 * Construction registers this instance as the global singleton for
 * Core 1 entry point access.
 */
T76::App app;

/**
 * @brief Main entry point for the application.
 * 
 * @return int Exit code (not used)
 */
int main() {
    app.run();
    return 0;
}
