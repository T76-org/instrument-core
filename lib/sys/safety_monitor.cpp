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
     * @brief Print fault information using printf (only available in Safety Monitor)
     */
    static void printFaultInfoToConsole(const T76::Sys::Safety::FaultInfo& faultInfo) {
        printf("\n=== SYSTEM FAULT DETECTED ===\n");
        printf("Timestamp: %lu ms\n", faultInfo.timestamp);
        printf("Core: %lu\n", faultInfo.coreId);
        printf("Type: %s\n", Safety::faultTypeToString(faultInfo.type));
        printf("Recovery: %s\n", Safety::recoveryActionToString(faultInfo.recoveryAction));
        printf("File: %s:%lu\n", faultInfo.fileName, faultInfo.lineNumber);
        printf("Function: %s\n", faultInfo.functionName);
        printf("Description: %s\n", faultInfo.description);

        if (faultInfo.taskHandle != 0) {
            printf("Task: %s (0x%08lX)\n", faultInfo.taskName, faultInfo.taskHandle);
        }

        if (faultInfo.isInInterrupt) {
            printf("Interrupt Context: %lu\n", faultInfo.interruptNumber);
        }

        if (faultInfo.heapFreeBytes > 0) {
            printf("Heap Free: %lu bytes\n", faultInfo.heapFreeBytes);
            printf("Min Heap Free: %lu bytes\n", faultInfo.minHeapFreeBytes);
        }

        // Print comprehensive stack information
        printf("\n--- Stack Information ---\n");
        if (faultInfo.stackInfo.isValidStackInfo) {
            printf("Stack Size: %lu bytes\n", faultInfo.stackInfo.stackSize);
            printf("Stack Used: %lu bytes\n", faultInfo.stackInfo.stackUsed);
            printf("Stack Remaining: %lu bytes\n", faultInfo.stackInfo.stackRemaining);
            printf("Stack High Water Mark: %lu bytes\n", faultInfo.stackInfo.stackHighWaterMark);
            printf("Stack Usage: %u%%\n", faultInfo.stackInfo.stackUsagePercent);
            printf("Stack Type: %s\n", faultInfo.stackInfo.isMainStack ? "Main (MSP)" : "Process (PSP)");
        } else {
            printf("Stack Type: %s\n", faultInfo.stackInfo.isMainStack ? "Main (MSP)" : "Process (PSP)");
            printf("Stack Usage: %u%% (estimated)\n", faultInfo.stackInfo.stackUsagePercent);
            printf("Note: Limited stack info (interrupt/Core1 context)\n");
        }

        printf("==============================\n\n");
    }

    /**
     * @brief FreeRTOS task for continuous fault reporting
     */
    static void faultReporterTask(void *param) {
        (void)param; // Parameter not needed, we'll get fault info from safety system
        
        printf("\n\n");
        printf("=====================================\n");
        printf("   SAFETY MONITOR ACTIVE\n");
        printf("   PERSISTENT FAULT DETECTED\n");
        printf("=====================================\n\n");

        // Get fault information from safety system
        T76::Sys::Safety::FaultInfo faultInfo;
        bool hasFaultInfo = Safety::getLastFault(&faultInfo);
        
        if (!hasFaultInfo) {
            printf("ERROR: No fault information available!\n");
            while (true) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }

        // Report the fault information repeatedly forever
        int reportCount = 0;
        
        while (true) {
            // Toggle status LED to indicate fault state
            status_led_set_state(!status_led_get_state());
            
            printf("--- SAFETY MONITOR REPORT #%d ---\n", reportCount + 1);
            printFaultInfoToConsole(faultInfo);
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
            256,  // Reduced stack size since we're using minimal stack design
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