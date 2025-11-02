/**
 * @file main.cpp
 * @brief Main application entry point file
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "app.hpp"

#include <FreeRTOS.h>
#include <task.h>
#include <tusb.h>

#include <pico/cyw43_arch.h>
#include <pico/status_led.h>


using namespace T76;

App::App() : _interpreter(*this) {
}

void App::_onUSBTMCDataReceived(const std::vector<uint8_t> &data, bool transfer_complete) {
    for (const auto &byte : data) {
        _interpreter.processInputCharacter(byte);
    }

    if (transfer_complete) {
        _interpreter.processInputCharacter('\n'); // Finalize the command if transfer is complete
    }
}

void App::_queryIDN(const std::vector<T76::SCPI::ParameterValue> &params) {
    // printf("Sending response\n");
    // interpreter.outputStream.write("MTA,T76-Device,123456,1.0\n", true);
        std::string response = "Hello there\n";
        std::vector<uint8_t> data(response.begin(), response.end());
        _usbInterface.sendUSBTMCBulkData(data);

}

bool App::activate() {
    return true;
}

void App::makeSafe() {
    // Currently does nothing
}

const char* App::getComponentName() const {
    return "App";
}

void App::triggerMemManageFault() {
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

void App::_printTask() {
    int count = 0;

    while (true) {
        char *ptr = new char[320];
        // snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
        // fputs(ptr, stdout);
        delete[] ptr;

        // char *f = (char *)malloc(5000);
        // f[0] = 0;

        // if (count > 30) {
        //     triggerMemManageFault();  // Trigger HardFault for testing
        // }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void App::_init() {
    // Initialize stdio and status LED
    stdio_init_all();
    status_led_init();
}

void App::_initCore0() {
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

void App::_startCore1() {
    int count = 0;
    char ptr[100];

    while (true) {
        // Send heartbeat to Core 0 watchdog manager to indicate Core 1 is alive
        T76::Core::Safety::feedWatchdogFromCore1();
        
        // snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
        // fputs(ptr, stdout);
        // status_led_set_state(!status_led_get_state());
        
        sleep_ms(100);  // Send heartbeat every 100ms (well within 2s timeout)
    }
}

