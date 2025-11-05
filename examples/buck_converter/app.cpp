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
    _usbInterface.sendUSBTMCBulkData("MTA Inc.,T76-Dev,0001,1.0");
}

void App::_resetInstrument(const std::vector<T76::SCPI::ParameterValue> &params) {
    _interpreter.reset();
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

void App::_init() {
    // Initialize stdio and status LED
    stdio_init_all();
    status_led_init();
}

void App::_initCore0() {
    // Does nothing.
}

void App::_startCore1() {
    for(;;) {
        T76::Core::Safety::feedWatchdogFromCore1();
        status_led_set_state(!status_led_get_state()); // Toggle status LED to indicate Core 1 is running
        sleep_ms(100); // Allow time for the watchdog to be fed
    }
}

