/**
 * @file app.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of the Application Base Class framework for dual-core
 * FreeRTOS applications on RP2350.
 */

#include "t76/app.hpp"


using namespace T76::Core;

// Global instance pointer for Core 1 entry point access
App *App::_globalInstance;

/**
 * @brief Construct the App and register as the global instance
 * 
 * Initializes the global instance pointer to allow the Core 1 entry point
 * trampoline to access the instance methods. Only one App instance should
 * be created in the application lifetime.
 */
App::App() {
    _globalInstance = this;
}

/**
 * @brief Run the complete application initialization sequence
 * 
 * Executes the framework initialization in the following order:
 * 
 * 1. Safety System Initialization
 *    - Sets up fault detection and reporting infrastructure
 *    - Configures shared memory for cross-core fault information
 *    - Initializes safety wrappers for FreeRTOS hooks
 * 
 * 2. Memory Management Initialization
 *    - Configures heap and memory allocation system
 *    - Sets up inter-core memory allocation service (if enabled)
 * 
 * 3. Application Early Initialization
 *    - Calls _init() hook for derived class setup
 *    - Prepares application state before multi-core execution
 * 
 * 4. Core 1 Launch
 *    - Resets Core 1 to clean state
 *    - Launches Core 1 with _core1EntryPoint trampoline
 *    - Core 1 begins executing _startCore1() hook
 * 
 * 5. Watchdog Initialization
 *    - Configures dual-core watchdog protection system
 *    - Reports hardware fault if initialization fails
 *    - Critical for system reliability and fault detection
 * 
 * 6. Core 0 Initialization
 *    - Calls _initCore0() hook for Core 0 specific setup
 *    - Typically creates FreeRTOS tasks for Core 0
 * 
 * 7. Scheduler Start
 *    - Starts FreeRTOS scheduler on Core 0
 *    - Begins task execution and system operation
 *    - Never returns under normal operation
 * 
 * If the scheduler exits (abnormal condition), the function enters an
 * infinite loop as a safety fallback to prevent undefined behavior.
 * 
 * @note Must be called from Core 0 only
 * @note This function does not return under normal operation
 * @note Safety faults will be reported if critical initialization fails
 */
void App::run() {
    // Initialize safety system first on Core 0
    T76::Core::Safety::init();
    
    // Initialize memory management system
    T76::Core::Memory::init();

    // Initialize USB interface
    _usbInterface.init();

    // Perform application-specific initialization
    _init();

    // Initialize Core 1
    multicore_reset_core1();
    multicore_launch_core1(_core1EntryPoint);

    // Initialize dual-core watchdog system (must be done on Core 0)
    if (!T76::Core::Safety::watchdogInit()) {
        // Handle watchdog initialization failure
        T76::Core::Safety::reportFault(T76::Core::Safety::FaultType::HARDWARE_FAULT,
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

