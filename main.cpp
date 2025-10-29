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
#include <pico/multicore.h>
#include <pico/status_led.h>

#include <t76/memory.hpp>
#include <t76/safety.hpp>

class A : public T76::Sys::Safety::SafeableComponent {
public:
    A() {
        T76::Sys::Safety::registerComponent(this);
        gpio_set_function(3, GPIO_FUNC_SIO);
        gpio_init(3);
        gpio_set_dir(3, GPIO_OUT);
    }

    bool activate() {
        gpio_put(3, true);
        return true;
    }

    void makeSafe() {
        gpio_put(3, false);
    }

    const char *getComponentName() const {
        return "Component A";
    }
};

A aa;

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
 * @brief Task to handle TinyUSB events. This will run on core 0 because it is
 *        executed in a FreeRTOS task.
 * 
 * @param params Unused parameter for task function signature.
 */
void tinyUSBTask(void *params) {
    (void) params;

    // Initialize TinyUSB
    tusb_init();

    // Main loop for TinyUSB task
    while (true) {
        tud_task(); // tinyusb device task
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Task to print messages to the console. This will run on core 0 and
 *        confirms that FreeRTOS is functioning correctly.
 * 
 * @param params 
 */
void printTask(void *params) {
    int count = 0;

    while (true) {
        char *ptr = new char[320];
        snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
        fputs(ptr, stdout);
        delete[] ptr;

        // char *f = (char *)malloc(5000);
        // f[0] = 0;

        if (count > 30) {
            abort();  // Trigger HardFault for testing
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief This task runs on core 1 and prints messages to the console. It
 *        does not use FreeRTOS and demonstrates that both cores are active.
 *        Now sends periodic heartbeats to Core 0 for dual-core watchdog protection.
 * 
 */
void core1Task() {
    int count = 0;

    while (true) {
        // Send heartbeat to Core 0 watchdog manager to indicate Core 1 is alive
        T76::Sys::Safety::feedWatchdogFromCore1();
        
        // Your application code here
        char *ptr = static_cast<char*>(malloc(320));
        snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
        fputs(ptr, stdout);
        free(ptr);
        status_led_set_state(!status_led_get_state());
        
        sleep_ms(100);  // Send heartbeat every 100ms (well within 2s timeout)
    }
}

/**
 * @brief Main entry point for the application.
 * 
 * @return int Exit code (not used)
 */
int main() {
    // Initialize safety system first on Core 0
    T76::Sys::Safety::init();
    
    // Initialize memory management system
    T76::Sys::Memory::init();

    // Initialize stdio and status LED
    stdio_init_all();
    status_led_init();

    // Initialize dual-core watchdog system (must be done on Core 0)
    if (!T76::Sys::Safety::watchdogInit()) {
        // Handle watchdog initialization failure
        T76::Sys::Safety::reportFault(T76::Sys::Safety::FaultType::HARDWARE_FAULT,
                                     "Failed to initialize dual-core watchdog system",
                                     __FILE__, __LINE__, __FUNCTION__);
    }
    
    // Reset core 1 and start a new task in it.
    // This must be done before starting the FreeRTOS scheduler.
    multicore_reset_core1();
    multicore_launch_core1(core1Task);

    // Create FreeRTOS tasks
    xTaskCreate(
        tinyUSBTask,
        "tusb",
        256,
        NULL,
        1,
        NULL
    );

    xTaskCreate(
        printTask,
        "print",
        2256,
        NULL,
        10,
        NULL
    );

    // Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here
    for(;;){}
}
