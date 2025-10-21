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

namespace T76::Sys {

    namespace Safety {

        extern SharedFaultSystem* gSharedFaultSystem;

    } // namespace Safety
    
    namespace SafetyMonitor {

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
         * @brief Register a safing function to be called before system reset
         * 
         * Safing functions are executed in the order they were registered when
         * a fault occurs, before the system reset.
         * 
         * @param safingFunc Function to register
         * @return SafingResult indicating success or failure reason
         */
    const char* faultTypeToString(const T76::Sys::Safety::FaultType type) {
            switch (type) {
                case T76::Sys::Safety::FaultType::UNKNOWN: return "UNKNOWN";
                case T76::Sys::Safety::FaultType::FREERTOS_ASSERT: return "FREERTOS_ASSERT";
                case T76::Sys::Safety::FaultType::STACK_OVERFLOW: return "STACK_OVERFLOW";
                case T76::Sys::Safety::FaultType::MALLOC_FAILED: return "MALLOC_FAILED";
                case T76::Sys::Safety::FaultType::C_ASSERT: return "C_ASSERT";
                case T76::Sys::Safety::FaultType::PICO_HARD_ASSERT: return "PICO_HARD_ASSERT";
                case T76::Sys::Safety::FaultType::HARDWARE_FAULT: return "HARDWARE_FAULT";
                case T76::Sys::Safety::FaultType::INTERCORE_FAULT: return "INTERCORE_FAULT";
                case T76::Sys::Safety::FaultType::MEMORY_CORRUPTION: return "MEMORY_CORRUPTION";
                case T76::Sys::Safety::FaultType::INVALID_STATE: return "INVALID_STATE";
                case T76::Sys::Safety::FaultType::RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
                default: return "INVALID";
            }
        }

        /**
         * @brief Print fault information using printf (only available in Safety Monitor)
         */
        static void printFaultInfoToConsole(const T76::Sys::Safety::FaultInfo& faultInfo) {
            printf("\n=== SYSTEM FAULT DETECTED ===\n");
            printf("Timestamp: %lu ms\n", faultInfo.timestamp);
            printf("Core: %lu\n", faultInfo.coreId);
            printf("Type: %s\n", faultTypeToString(faultInfo.type));
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
         * @brief Print fault history when system hits reboot limit
         */
        static void printFaultHistoryToConsole() {
            printf("\n\n");
            printf("=========================================\n");
            printf("   REBOOT LIMIT EXCEEDED\n");
            printf("   MULTIPLE CONSECUTIVE FAULTS DETECTED\n");
            printf("=========================================\n\n");

            // Get direct access to shared fault system for fault history
            if (!Safety::gSharedFaultSystem) {
                printf("ERROR: Cannot access fault history!\n");
                return;
            }

            printf("Consecutive reboots: %lu (limit: %d)\n\n", 
                Safety::gSharedFaultSystem->rebootCount, T76_SAFETY_MAX_REBOOTS);

            // Print each fault in the history
            for (uint32_t i = 0; i < Safety::gSharedFaultSystem->rebootCount && i < T76_SAFETY_MAX_REBOOTS; i++) {
                printf("--- FAULT #%lu ---\n", i + 1);
                printFaultInfoToConsole(Safety::gSharedFaultSystem->faultHistory[i]);
            }

            printf("System halted to prevent infinite reboot loop.\n");
            printf("Manual intervention required.\n\n");
        }

        /**
         * @brief FreeRTOS task for continuous fault reporting
         */
        static void faultReporterTask(void *param) {
            (void)param; // Parameter not needed, we'll get fault info from safety system
            
            // Display initial fault history summary
            printFaultHistoryToConsole();
            
            while (true) {
                // Toggle status LED to indicate fault state
                status_led_set_state(!status_led_get_state());
                
                printf("REBOOT LIMIT EXCEEDED - System Halted\n");
                printf("Consecutive faults: %lu (limit: %d)\n", 
                    Safety::gSharedFaultSystem ? Safety::gSharedFaultSystem->rebootCount : 0, 
                    T76_SAFETY_MAX_REBOOTS);
                printf("Manual reset required to clear fault state.\n\n");
                
                // Output each fault report individually and continuously
                if (Safety::gSharedFaultSystem) {
                    for (uint32_t i = 0; i < Safety::gSharedFaultSystem->rebootCount && i < T76_SAFETY_MAX_REBOOTS; i++) {
                        printf("--- FAULT #%lu ---\n", i + 1);
                        printFaultInfoToConsole(Safety::gSharedFaultSystem->faultHistory[i]);
                        vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 second between fault reports
                    }
                }
                
                vTaskDelay(2000 / portTICK_PERIOD_MS); // Wait 2 seconds before repeating the cycle
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

    } // namespace SafetyMonitor
    
} // namespace T76::Sys


