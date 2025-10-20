/**
 * @file safety_monitor.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of the Safety Monitor for persistent fault reporting.
 */

#include "safety_monitor.hpp"

#include <cstdio>
#include <cstring>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>

// Pico SDK includes
#include <pico/stdlib.h>
#include <pico/time.h>
#include <pico/cyw43_arch.h>
#include <pico/status_led.h>
#include <tusb.h>

#include "safety.hpp"

namespace T76::Sys::SafetyMonitor {

    // Forward declaration of shared fault system structure
    namespace Safety = T76::Sys::Safety;

    /**
     * @brief FreeRTOS task for TinyUSB device processing
     */
    static void tinyUSBTask(void *param) {
        (void)param;

        // Initialize TinyUSB
        tusb_init();

        // Main loop for TinyUSB task
        while (true) {
            tud_task(); // tinyusb device task
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    /**
     * @brief FreeRTOS task for continuous fault reporting
     */
    static void faultReporterTask(void *param) {
        T76::Sys::Safety::FaultInfo faultInfo = *(T76::Sys::Safety::FaultInfo*)param;
        
        printf("\n\n");
        printf("=====================================\n");
        printf("   SAFETY MONITOR ACTIVE\n");
        printf("   PERSISTENT FAULT DETECTED\n");
        printf("=====================================\n\n");

        // Report the fault information repeatedly forever
        int reportCount = 0;
        
        while (true) {
            // Toggle status LED to indicate fault state
            status_led_set_state(!status_led_get_state());
            
            printf("--- SAFETY MONITOR REPORT #%d ---\n", reportCount + 1);
            Safety::printFaultInfo();
            printf("Safety Monitor will continue reporting indefinitely...\n");
            printf("Manual reset required to clear fault state.\n\n");
            
            vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 second
            reportCount++;
        }
    }

    void runSafetyMonitor() {
        // Initialize stdio for output
        stdio_init_all();
        
        // Initialize status LED for visual indication
        status_led_init();

        // Create FreeRTOS tasks for Safety Monitor operation
        xTaskCreate(
            tinyUSBTask,
            "SafetyMonitor_USB",
            256,
            nullptr,
            1,
            nullptr
        );

        xTaskCreate(
            faultReporterTask,
            "SafetyMonitor_Reporter",
            512,  // Increased stack size for fault reporting
            nullptr,
            2,    // Higher priority for fault reporting
            nullptr
        );

        // Start the FreeRTOS scheduler - this function does not return
        vTaskStartScheduler();

        // Should never reach here, but provide infinite loop as fallback
        while (true) {
            tight_loop_contents();
        }
    }

} // namespace T76::Sys::SafetyMonitor