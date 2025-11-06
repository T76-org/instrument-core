/**
 * @file app.hpp
 * @brief Buck converter application class header file
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file contains the declaration of the App class which implements
 * a buck converter control application with SCPI command interface.
 */

#pragma once

#include <stdio.h>
#include <cstdlib>
#include <string>
#include <vector>

#include <t76/app.hpp>
#include <t76/scpi_interpreter.hpp>

#include "buck.hpp"


namespace T76 {

    /**
     * @class App
     * @brief Main application class for buck converter control
     * 
     * This class extends the T76::Core::App base class to provide buck converter
     * specific functionality including PID control parameter management and
     * voltage regulation through SCPI commands.
     */
    class App : public T76::Core::App {
    public:

        /**
         * @brief SCPI command interpreter instance
         * 
         * Handles parsing and execution of SCPI (Standard Commands for 
         * Programmable Instruments) commands received via USB TMC interface.
         */
        T76::SCPI::Interpreter<T76::App> _interpreter;

        /**
         * @brief Default constructor
         * 
         * Initializes the App instance with default settings and
         * configures the SCPI command interpreter.
         */
        App();

        /**
         * @brief Callback for handling USB TMC data reception
         * @param data Received data bytes from USB TMC interface
         * @param transfer_complete Flag indicating if the transfer is complete
         * 
         * This override method processes incoming SCPI commands received
         * through the USB Test and Measurement Class interface.
         */
        void _onUSBTMCDataReceived(const std::vector<uint8_t> &data, bool transfer_complete) override;

        /**
         * @brief Query instrument identification
         * @param params SCPI command parameters (unused for IDN query)
         * 
         * Handles the *IDN? SCPI command to return instrument identification
         * information including manufacturer, model, serial number, and firmware version.
         */
        void _queryIDN(const std::vector<T76::SCPI::ParameterValue> &params);

        /**
         * @brief Reset instrument to default state
         * @param params SCPI command parameters (unused for reset command)
         * 
         * Handles the *RST SCPI command to reset the buck converter to
         * its default operational state and parameters.
         */
        void _resetInstrument(const std::vector<T76::SCPI::ParameterValue> &params);

        /**
         * @brief Set PID controller proportional gain (Kp)
         * @param params SCPI command parameters containing the new Kp value
         * 
         * Handles setting the proportional gain parameter for the PID controller
         * used in buck converter voltage regulation.
         */
        void _setKp(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Query PID controller proportional gain (Kp)
         * @param params SCPI command parameters (unused for query)
         * 
         * Returns the current proportional gain value of the PID controller.
         */
        void _queryKp(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Set PID controller integral gain (Ki)
         * @param params SCPI command parameters containing the new Ki value
         * 
         * Handles setting the integral gain parameter for the PID controller
         * used in buck converter voltage regulation.
         */
        void _setKi(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Query PID controller integral gain (Ki)
         * @param params SCPI command parameters (unused for query)
         * 
         * Returns the current integral gain value of the PID controller.
         */
        void _queryKi(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Set PID controller derivative gain (Kd)
         * @param params SCPI command parameters containing the new Kd value
         * 
         * Handles setting the derivative gain parameter for the PID controller
         * used in buck converter voltage regulation.
         */
        void _setKd(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Query PID controller derivative gain (Kd)
         * @param params SCPI command parameters (unused for query)
         * 
         * Returns the current derivative gain value of the PID controller.
         */
        void _queryKd(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Set target output voltage
         * @param params SCPI command parameters containing the new target voltage value
         * 
         * Sets the desired output voltage for the buck converter. The PID controller
         * will regulate the output to match this target voltage.
         */
        void _setTargetVoltage(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Query target output voltage
         * @param params SCPI command parameters (unused for query)
         * 
         * Returns the current target voltage setpoint for the buck converter.
         */
        void _queryTargetVoltage(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Query sensed output voltage
         * @param params SCPI command parameters (unused for query)
         * 
         * Returns the actual measured output voltage from the buck converter's
         * feedback sensing circuit.
         */
        void _querySensedVoltage(const std::vector<T76::SCPI::ParameterValue> &);

        /**
         * @brief Activate the buck converter application
         * @return true if activation was successful, false otherwise
         * 
         * Activates all components and starts the buck converter operation.
         * This includes enabling the PWM output and starting voltage regulation.
         */
        bool activate();

        /**
         * @brief Put the buck converter in a safe state
         * 
         * Disables PWM output and puts all components in a safe, non-operational
         * state. Called when errors occur or during shutdown.
         */
        void makeSafe();

        /**
         * @brief Get the component name identifier
         * @return Pointer to component name string
         * 
         * Returns a string identifier for this application component,
         * used for logging and debugging purposes.
         */
        const char* getComponentName() const;

        /**
         * @brief Initialize the application
         * 
         * Performs general application initialization including setting up
         * the SCPI command interpreter and registering command handlers.
         */
        void _init();

        /**
         * @brief Initialize Core 0 specific functionality
         * 
         * Sets up Core 0 specific tasks and components. On the RP2040/RP2350,
         * this typically handles the main application logic and USB communication.
         */
        void _initCore0();

        /**
         * @brief Start Core 1 execution
         * 
         * Launches the second CPU core with buck converter control tasks.
         * Core 1 typically handles real-time PWM generation and control loops.
         */
        void _startCore1();

    protected:
        BuckConverter _buckConverter; ///< Buck converter component instance

    }; // class App

}
