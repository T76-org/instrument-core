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

void App::_setKp(const std::vector<T76::SCPI::ParameterValue> &params) {
    _buckConverter.kP(static_cast<float>(params[0].numberValue));
}

void App::_queryKp(const std::vector<T76::SCPI::ParameterValue> &params) {
    _usbInterface.sendUSBTMCBulkData(std::to_string(_buckConverter.kP()));
}

void App::_setKi(const std::vector<T76::SCPI::ParameterValue> &params) {
    _buckConverter.kI(static_cast<float>(params[0].numberValue));
}

void App::_queryKi(const std::vector<T76::SCPI::ParameterValue> &params) {
    _usbInterface.sendUSBTMCBulkData(std::to_string(_buckConverter.kI()));
}

void App::_setKd(const std::vector<T76::SCPI::ParameterValue> &params) {
    _buckConverter.kD(static_cast<float>(params[0].numberValue));
}

void App::_queryKd(const std::vector<T76::SCPI::ParameterValue> &params) {
    _usbInterface.sendUSBTMCBulkData(std::to_string(_buckConverter.kD()));
}

void App::_setTargetVoltage(const std::vector<T76::SCPI::ParameterValue> &params) {
    _buckConverter.setPoint(static_cast<float>(params[0].numberValue));
}

void App::_queryTargetVoltage(const std::vector<T76::SCPI::ParameterValue> &params) {
    _usbInterface.sendUSBTMCBulkData(std::to_string(_buckConverter.setPoint()));
}

void App::_querySensedVoltage(const std::vector<T76::SCPI::ParameterValue> &params) {
    _usbInterface.sendUSBTMCBulkData(std::to_string(_buckConverter.sensedVoltage()));
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
}

void App::_initCore0() {
    // Does nothing.
}

void App::_startCore1() {
    _buckConverter.start();

    for(;;) {
        sleep_ms(100); // Allow time for the watchdog to be fed
    }
}

