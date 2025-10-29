/**
 * @file app.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "t76/app.hpp"


using namespace T76::Sys;


App *App::_globalInstance;


App::App() {
    _globalInstance = this;
}

void App::run() {
    _init();

    // Initialize safety system first on Core 0
    T76::Sys::Safety::init();
    
    // Initialize memory management system
    T76::Sys::Memory::init();

    // Initialize Core 1
    multicore_reset_core1();
    multicore_launch_core1(_core1EntryPoint);

    // Initialize dual-core watchdog system (must be done on Core 0)
    if (!T76::Sys::Safety::watchdogInit()) {
        // Handle watchdog initialization failure
        T76::Sys::Safety::reportFault(T76::Sys::Safety::FaultType::HARDWARE_FAULT,
                                     "Failed to initialize dual-core watchdog system",
                                     __FILE__, __LINE__, __FUNCTION__);
    }

    // Initialize Core 0
    _initCore0();

    // Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here
    for(;;){}
}

