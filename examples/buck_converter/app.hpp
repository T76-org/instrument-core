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

    // App class implementation

    class App : public T76::Core::App {
    public:

        T76::SCPI::Interpreter<T76::App> _interpreter;

        App();

        void _onUSBTMCDataReceived(const std::vector<uint8_t> &data, bool transfer_complete) override;

        void _queryIDN(const std::vector<T76::SCPI::ParameterValue> &params);
        void _resetInstrument(const std::vector<T76::SCPI::ParameterValue> &params);

        bool activate();
        void makeSafe();
        const char* getComponentName() const;

        void _init();
        void _initCore0();
        void _startCore1();

    protected:

    }; // class App

}
