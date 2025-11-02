/**
 * @file app.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Application Base Class - Framework for dual-core FreeRTOS applications
 * 
 * The App class provides a structured framework for building dual-core applications
 * on the RP2350 platform with FreeRTOS. It handles:
 * - Safety system initialization and integration
 * - Memory management system setup
 * - Dual-core initialization and coordination
 * - Watchdog system configuration
 * - FreeRTOS scheduler startup
 * 
 * Applications should inherit from this class and implement the required pure
 * virtual methods to define core-specific initialization and execution behavior.
 * 
 * The App class implements a global singleton pattern to facilitate Core 1
 * initialization. When an App instance is constructed, it registers itself
 * as the global instance. This allows the static Core 1 entry point trampoline
 * to access the instance methods needed for Core 1 startup.
 * 
 * Only one App instance should exist in the application. Creating multiple
 * instances will cause the global pointer to reference only the most recently
 * constructed instance.
 */

#pragma once

#include <stdio.h>
#include <cstdlib>
#include <functional>

#include <FreeRTOS.h>
#include <task.h>
#include <tusb.h>

#include <pico/cyw43_arch.h>
#include <pico/multicore.h>
#include <pico/status_led.h>

#include <t76/memory.hpp>
#include <t76/safety.hpp>
#include <t76/usb_interface.hpp>


namespace T76::Core {

    /**
     * @brief Base class for dual-core FreeRTOS applications on RP2350
     * 
     * The App class provides a complete framework for initializing and running
     * dual-core applications with integrated safety and memory management systems.
     * It inherits from SafeableComponent to participate in the safety monitoring
     * system.
     * 
     * Initialization sequence:
     * 1. Safety system initialization on Core 0
     * 2. Memory management system initialization
     * 3. Application-specific initialization via _init()
     * 4. Core 1 launch and initialization via _startCore1()
     * 5. Dual-core watchdog system setup
     * 6. Core 0 initialization via _initCore0()
     * 7. FreeRTOS scheduler start
     * 
     * Derived classes must implement:
     * - _initCore0(): Core 0 specific initialization (create tasks, etc.)
     * - _startCore1(): Core 1 initialization and execution
     * 
     * Derived classes may optionally override:
     * - _init(): Early initialization before core launch
     */
    class App : public T76::Core::Safety::SafeableComponent, T76::Core::USB::InterfaceDelegate {
    public:
        /**
         * @brief Construct the App and register as the global singleton instance
         * 
         * Sets up the global App instance pointer for Core 1 entry point access.
         * This implements the singleton pattern by storing a pointer to this
         * instance in the static _globalInstance member.
         * 
         * @warning Only one App instance should exist in the application. Creating
         *          multiple instances will overwrite the global pointer, potentially
         *          causing the wrong instance to be used for Core 1 initialization.
         */
        App();

        /**
         * @brief Run the application framework initialization sequence
         * 
         * Executes the complete initialization sequence:
         * - Initializes safety and memory systems
         * - Performs application-specific initialization
         * - Launches Core 1 with watchdog protection
         * - Initializes Core 0 components
         * - Starts FreeRTOS scheduler
         * 
         * This function never returns under normal operation as the FreeRTOS
         * scheduler takes control. If the scheduler exits, enters an infinite
         * loop as a safety fallback.
         * 
         * @note Must be called from Core 0
         * @note Will report safety faults if watchdog initialization fails
         */
        virtual void run();

    protected:
        /**
         * @brief Global singleton instance pointer for Core 1 entry point access
         * 
         * Static pointer to the App instance, implementing a singleton pattern.
         * This is used by the Core 1 entry point trampoline to call the instance's
         * _startCore1() method, as the multicore launch API requires a static
         * C-style function pointer that cannot directly access instance methods.
         * 
         * @note Set automatically by the App constructor
         * @note Should only reference one App instance throughout application lifetime
         */
        static App *_globalInstance;

        T76::Core::USB::Interface _usbInterface; ///< USB interface instance for managing USB communication

        /**
         * @brief Core 1 entry point trampoline function
         * 
         * Static function that serves as the Core 1 entry point. Accesses the
         * global singleton instance and calls its _startCore1() method if a
         * valid instance exists.
         * 
         * This trampoline is necessary because multicore_launch_core1() requires
         * a static C-style function pointer, but we need to call instance methods.
         * The singleton pattern bridges this gap.
         * 
         * @note Called automatically by multicore_launch_core1()
         * @note Must be static to serve as a C-style function pointer
         * @note Relies on the global singleton being properly initialized
         */
        static void _core1EntryPoint() {
            if (_globalInstance) {
                _globalInstance->_startCore1();
            }
        }

        /**
         * @brief Early application initialization hook
         * 
         * Called after safety and memory system initialization but before
         * Core 1 launch. Use this to initialize hardware or state that must
         * be ready before multi-core execution begins.
         * 
         * Default implementation does nothing. Override if needed.
         * 
         * @note Called on Core 0 only
         * @note Executes before watchdog is initialized
         */
        virtual void _init() {};

        /**
         * @brief Core 0 initialization hook (pure virtual)
         * 
         * Called on Core 0 after Core 1 has been launched and the watchdog
         * system is initialized. This is the appropriate place to create
         * FreeRTOS tasks and initialize Core 0 peripherals and resources.
         * 
         * Must be implemented by derived classes.
         * 
         * @note Called just before FreeRTOS scheduler starts
         * @note All tasks created here will start when vTaskStartScheduler() is called
         */
        virtual void _initCore0() = 0;

        /**
         * @brief Core 1 initialization and execution hook (pure virtual)
         * 
         * Called on Core 1 immediately after it is launched. This method should
         * initialize Core 1 specific resources and either:
         * - Create FreeRTOS tasks for Core 1 and return (scheduler will be started)
         * - Enter a main loop for non-RTOS Core 1 operation
         * 
         * Must be implemented by derived classes.
         * 
         * @note Called on Core 1 only
         * @note Watchdog protection is active when this is called
         */
        virtual void _startCore1() = 0;

        /**
         * @brief Called when data is received on the vendor USB interface's
         *        bulk endpoint
         * 
         * @param data The data received
         */
        virtual void _onVendorDataReceived(const std::vector<uint8_t> &data) override {};

        /**
         * @brief Called when a control transfer IN request is received on the 
         *        vendor USB's control endpoint. This signals that the host
         *        is ready to receive data from us.
         * 
         * @param port The port of the request
         * @param request A pointer to the request structure
         * @return true If the request was successfully handled
         */
        virtual bool _onVendorControlTransferIn(uint8_t port, const tusb_control_request_t *request) override { return false; }

        /**
         * @brief Called when a control transfer OUT request is received on the 
         *        vendor USB's control endpoint. This signals that the host has
         *        sent us data.
         * 
         * @param port The port of the request
         * @param value The value associated with the request
         * @param data The data that was sent along with the request
         * @return true If the request was successfully handled
         */
        virtual bool _onVendorControlTransferOut(uint8_t request, uint16_t value, const std::vector<uint8_t> &data) override { return false; }

        /**
         * @brief Called when data is received on the USBTMC interface's bulk endpoint
         * 
         * @param data The data received
         * @param transfer_complete Whether the transfer is complete
         */
        virtual void _onUSBTMCDataReceived(const std::vector<uint8_t> &data, bool transfer_complete) override { }
    };
    
} // namespace T76::Core
