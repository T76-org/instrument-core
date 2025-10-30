/**
 * @file main.cpp
 * @brief Main application entry point file
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include <stdio.h>
#include <cstdlib>

#include <FreeRTOS.h>
#include <task.h>
#include <tusb.h>

#include <pico/cyw43_arch.h>
#include <pico/status_led.h>

#include <t76/app.hpp>

/**
 * @brief Application implementation demonstrating the T76 framework
 * 
 * This class extends T76::Core::App to create a dual-core application that:
 * - Runs TinyUSB task for USB device functionality
 * - Demonstrates memory management with heap monitoring
 * - Tests fault handling system by triggering memory management faults
 * - Provides Core 1 execution with watchdog heartbeat monitoring
 * 
 * The application serves as both a functional example and a test harness
 * for the safety system's fault detection and reporting capabilities.
 */
class App : public T76::Core::App {

public:

    /**
     * @brief Activate the application component
     * 
     * Called by the safety system to activate this component. This implementation
     * always returns true to indicate successful activation.
     * 
     * @return true Always succeeds
     */
    virtual bool activate() override {
        return true;
    }

    /**
     * @brief Put the application into a safe state
     * 
     * Called by the safety system when a fault is detected. This implementation
     * currently does nothing, relying on the framework's safety mechanisms.
     * Production applications should override this to perform application-specific
     * safe shutdown procedures.
     */
    virtual void makeSafe() override {

    }

    /**
     * @brief Get the component name for safety system identification
     * 
     * @return const char* The component name "App"
     */
    virtual const char* getComponentName() const override {
        return "App";
    }

protected:
    /**
     * @brief Test function to trigger a memory management fault.
     * 
     * This attempts several different fault-triggering methods to ensure
     * a fault occurs. Different methods work on different systems depending
     * on MPU configuration and memory map.
     */
    void triggerMemManageFault() {
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

    /**
     * @brief Print task that demonstrates memory allocation and fault triggering
     * 
     * This FreeRTOS task repeatedly:
     * - Allocates memory to test the memory management system
     * - Prints heap statistics and iteration count
     * - Triggers a memory management fault after 30 iterations to test
     *   the safety system's fault detection and reporting
     * 
     * @note Runs on Core 0 as a FreeRTOS task
     * @note Intentionally triggers faults for testing purposes
     */
    void _printTask() {
        int count = 0;

        while (true) {
            char *ptr = new char[320];
            snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
            fputs(ptr, stdout);
            delete[] ptr;

            // char *f = (char *)malloc(5000);
            // f[0] = 0;

            // if (count > 30) {
            //     triggerMemManageFault();  // Trigger HardFault for testing
            // }
            
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }

    /**
     * @brief TinyUSB device task for USB functionality
     * 
     * This FreeRTOS task initializes the TinyUSB stack and continuously
     * services USB device operations by calling tud_task(). Required for
     * USB device functionality including CDC (serial over USB) support.
     * 
     * @note Runs on Core 0 as a FreeRTOS task
     * @note Runs every 10ms to ensure responsive USB handling
     */
    void _tusbTask() {
        // Initialize TinyUSB
        tusb_init();

        // Main loop for TinyUSB task
        while (true) {
            tud_task(); // tinyusb device task
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    /**
     * @brief Early initialization before core launch
     * 
     * Initializes standard I/O and the status LED. Called after safety
     * and memory system initialization but before Core 1 launch.
     * 
     * @note Called on Core 0 only
     * @note Runs before FreeRTOS scheduler starts
     */
    virtual void _init() override {
        // Initialize stdio and status LED
        stdio_init_all();
        status_led_init();
    }

    /**
     * @brief Core 0 initialization and task creation
     * 
     * Creates two FreeRTOS tasks:
     * - tusb: Handles TinyUSB device operations (priority 1, 256 words stack)
     * - print: Demonstrates memory management and triggers test faults (priority 10, 2256 words stack)
     * 
     * Called after Core 1 has been launched and the dual-core watchdog system
     * has been configured. This is where Core 0 should create its tasks and
     * initialize any Core 0-specific resources.
     * 
     * @note Called on Core 0 only, just before FreeRTOS scheduler starts
     */
    virtual void _initCore0() override {
        // Create FreeRTOS tasks
        xTaskCreate(
            [](void *param) {
                App *app = static_cast<App*>(param);
                app->_tusbTask();
            },
            "tusb",
            256,
            this,
            1,
            NULL
        );

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

    /**
     * @brief Core 1 execution loop
     * 
     * Runs continuously on Core 1, demonstrating:
     * - Watchdog heartbeat feeding to keep Core 1 monitored
     * - Memory allocation and heap monitoring on Core 1
     * - Status LED toggling for visual feedback
     * - Console output showing core number and iteration count
     * 
     * This function never returns under normal operation. The 100ms loop
     * interval ensures the watchdog heartbeat is sent well within the
     * timeout period.
     * 
     * @note Called on Core 1 only
     * @note Runs in bare-metal context (no FreeRTOS on Core 1)
     * @note Must call feedWatchdogFromCore1() regularly to prevent watchdog timeout
     */
    virtual void _startCore1() override {
        int count = 0;
        char ptr[100];

        while (true) {
            // Send heartbeat to Core 0 watchdog manager to indicate Core 1 is alive
            T76::Core::Safety::feedWatchdogFromCore1();
            
            snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
            fputs(ptr, stdout);
            status_led_set_state(!status_led_get_state());
            
            sleep_ms(100);  // Send heartbeat every 100ms (well within 2s timeout)
        }
    }

};

/**
 * @brief Global application instance
 * 
 * Creates the singleton App instance that will be run by main().
 * Construction registers this instance as the global singleton for
 * Core 1 entry point access.
 */
App app;

/**
 * @brief Main entry point for the application.
 * 
 * @return int Exit code (not used)
 */
int main() {
    app.run();
    return 0;
}
