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
    _ledState = LEDState::OFF;
}

void App::_setLEDState(const std::vector<T76::SCPI::ParameterValue> &params) {
    LEDState newState = StringToLEDState(params[0].stringValue);

    if (newState == LEDState::OFF || newState == LEDState::ON || newState == LEDState::BLINK) {
        _ledState = newState;
    } else {
        _interpreter.addError(202, "Unknown LED state");
    }
}

void App::_queryLEDState(const std::vector<T76::SCPI::ParameterValue> &params) {
    std::string stateStr = std::string(LEDStateToString(_ledState));
    _usbInterface.sendUSBTMCBulkData(stateStr);
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

void App::_ledTask() {
    int count = 0;

    while (true) {
        T76::Core::Safety::feedWatchdogFromCore1();

        // Note that no synchronization is required 
        // as LED state is a uint32_t and reads/writes are atomic.

        switch (_ledState) {
            case LEDState::OFF:
                status_led_set_state(false);
                break;
            case LEDState::ON:
                status_led_set_state(true);
                break;
            case LEDState::BLINK:
                status_led_set_state(count++ % 2 == 0);
                break;
            default:
                status_led_set_state(false);
                break;
        }

        sleep_ms(100);
    }
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
    _ledTask();
}

