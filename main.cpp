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

class App : public T76::Sys::App {

public:

    virtual bool activate() override {
        return true;
    }

    virtual void makeSafe() override {

    }

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

    void _printTask() {
        int count = 0;

        while (true) {
            char *ptr = new char[320];
            snprintf(ptr, 32, "C %d: %d : %u\n", get_core_num(), count++, xPortGetFreeHeapSize());
            fputs(ptr, stdout);
            delete[] ptr;

            // char *f = (char *)malloc(5000);
            // f[0] = 0;

            if (count > 30) {
                triggerMemManageFault();  // Trigger HardFault for testing
            }
            
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }

    void _tusbTask() {
        // Initialize TinyUSB
        tusb_init();

        // Main loop for TinyUSB task
        while (true) {
            tud_task(); // tinyusb device task
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    virtual void _init() override {
        // Initialize stdio and status LED
        stdio_init_all();
        status_led_init();
    }

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

    virtual void _startCore1() override {
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

};

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
