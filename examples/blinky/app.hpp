/**
 * @file main.cpp
 * @brief Main application entry point file
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#pragma once

#include <stdio.h>
#include <cstdlib>
#include <string>
#include <vector>

#include <t76/app.hpp>
#include <t76/scpi_interpreter.hpp>

namespace T76 {

    enum class LEDState : uint32_t {
        OFF = 0,
        ON = 1,
        BLINK = 2,
        UNKNOWN = 255
    };

    constexpr std::array<std::pair<std::string_view, LEDState>, 4> LEDStateStrings = {{
        {"OFF", LEDState::OFF},
        {"ON", LEDState::ON},
        {"BLINK", LEDState::BLINK},
        {"UNKNOWN", LEDState::UNKNOWN}
    }};

    constexpr std::string_view LEDStateToString(LEDState state) {
        for (const auto& pair : LEDStateStrings) {
            if (pair.second == state) {
                return pair.first;
            }
        }
        return "UNKNOWN";
    }

    constexpr LEDState StringToLEDState(std::string_view str) {
        for (const auto& pair : LEDStateStrings) {
            if (pair.first == str) {
                return pair.second;
            }
        }
        return LEDState::OFF; // Default fallback
    }

    // App class implementation

    class App : public T76::Core::App {
    public:

        T76::SCPI::Interpreter<T76::App> _interpreter;

        App();

        void _onUSBTMCDataReceived(const std::vector<uint8_t> &data, bool transfer_complete) override;

        void _queryIDN(const std::vector<T76::SCPI::ParameterValue> &params);
        void _resetInstrument(const std::vector<T76::SCPI::ParameterValue> &params);
        void _setLEDState(const std::vector<T76::SCPI::ParameterValue> &params);
        void _queryLEDState(const std::vector<T76::SCPI::ParameterValue> &params);

        bool activate();
        void makeSafe();
        const char* getComponentName() const;

        void triggerMemManageFault();
        void _ledTask();
        void _init();
        void _initCore0();
        void _startCore1();

    protected:
        LEDState _ledState = LEDState::OFF;

    }; // class App

}
