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

#include "t76/safety.hpp"

namespace T76::Core {

    namespace Safety {

        extern SharedFaultSystem* gSharedFaultSystem;

    } // namespace Safety
    
    namespace SafetyMonitor {

        // Forward declaration of shared fault system structure
        namespace Safety = T76::Core::Safety;

        /**
         * @brief FreeRTOS task for TinyUSB device processing
         * 
         * Handles TinyUSB device task processing to maintain USB communication
         * during Safety Monitor operation. This enables console output over USB
         * for fault reporting and system status information.
         * 
         * The task runs continuously with a 10ms delay between TinyUSB task
         * executions to balance responsiveness with system resource usage.
         * 
         * @param param Unused task parameter (required by FreeRTOS task signature)
         * 
         * @note Runs with priority 1 (lower than fault reporter task)
         * @note Never returns - runs indefinitely until system reset
         * @note Essential for USB console output functionality
         */
        static void tinyUSBTask(void *param) {
            (void)param;

            // Initialize TinyUSB
            tusb_init();

            // Main loop for TinyUSB task
            while (true) {
                tud_task(); // tinyusb device task
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }

    /**
     * @brief Convert fault type enumeration to a printable string.
     * @param type Fault type.
     * @return Constant string name.
     */
        const char* faultTypeToString(const T76::Core::Safety::FaultType type) {
            switch (type) {
                case T76::Core::Safety::FaultType::UNKNOWN: return "UNKNOWN";
                case T76::Core::Safety::FaultType::FREERTOS_ASSERT: return "FREERTOS_ASSERT";
                case T76::Core::Safety::FaultType::STACK_OVERFLOW: return "STACK_OVERFLOW";
                case T76::Core::Safety::FaultType::MALLOC_FAILED: return "MALLOC_FAILED";
                case T76::Core::Safety::FaultType::C_ASSERT: return "C_ASSERT";
                case T76::Core::Safety::FaultType::PICO_HARD_ASSERT: return "PICO_HARD_ASSERT";
                case T76::Core::Safety::FaultType::HARDWARE_FAULT: return "HARDWARE_FAULT";
                case T76::Core::Safety::FaultType::INTERCORE_FAULT: return "INTERCORE_FAULT";
                case T76::Core::Safety::FaultType::MEMORY_CORRUPTION: return "MEMORY_CORRUPTION";
                case T76::Core::Safety::FaultType::INVALID_STATE: return "INVALID_STATE";
                case T76::Core::Safety::FaultType::RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
                case T76::Core::Safety::FaultType::WATCHDOG_TIMEOUT: return "WATCHDOG_TIMEOUT";
                case T76::Core::Safety::FaultType::ACTIVATION_FAILED: return "ACTIVATION_FAILED";
                default: return "INVALID";
            }
        }

    /**
     * @brief Print comprehensive fault information to console.
     * Shows timestamp, core, type, source location, task (if any), heap stats,
     * and stack details when available.
     * @param faultInfo Fault information to display.
     */
        static void printFaultInfoToConsole(const T76::Core::Safety::FaultInfo& faultInfo) {
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
                printf("Stack Type: %s\n", faultInfo.stackInfo.isMainStack ? "Main (MSP)" : "Process (PSP)");
            } else {
                printf("Stack Type: %s\n", faultInfo.stackInfo.isMainStack ? "Main (MSP)" : "Process (PSP)");
                printf("Note: Limited stack info (interrupt/Core1 context)\n");
            }

            printf("==============================\n\n");
        }

    /**
     * @brief Print fault history and reboot limit status.
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

            printf("Consecutive faults: %lu (limit: %d)\n\n", 
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
     * @brief FreeRTOS task for continuous fault reporting and status indication.
     * @param param Unused.
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
                        vTaskDelay(pdMS_TO_TICKS(T76_SAFETY_MONITOR_REPORT_INTERVAL_MS)); // Wait between fault reports
                    }
                }
                
                vTaskDelay(pdMS_TO_TICKS(T76_SAFETY_MONITOR_CYCLE_DELAY_MS)); // Wait before repeating the cycle
            }
        }

        /**
         * @brief Initialize and run the Safety Monitor fault reporting system
         * 
         * Entry point for Safety Monitor mode when the system has experienced
         * too many consecutive faults. Sets up minimal system infrastructure
         * and creates FreeRTOS tasks for continuous fault reporting.
         * 
         * This function:
         * 1. Initializes stdio for console output over USB
         * 2. Initializes status LED for visual fault indication
         * 3. Creates TinyUSB task for USB communication
         * 4. Creates fault reporter task for continuous fault output
         * 5. Starts FreeRTOS scheduler (never returns)
         * 
         * The Safety Monitor provides a safe, minimal environment for fault
         * analysis without risking additional faults that could mask the
         * root cause of system issues.
         * 
         * @note This function never returns - system remains in Safety Monitor mode
         * @note Requires manual reset to exit Safety Monitor mode
         * @note Uses minimal system resources to ensure reliability
         * @note Provides infinite loop fallback if scheduler fails to start
         */
        void runSafetyMonitor() {
            // Initialize stdio for output
            stdio_init_all();
            
            // Initialize status LED for visual indication
            status_led_init();

            // Create FreeRTOS tasks for Safety Monitor operation
            xTaskCreate(
                tinyUSBTask,
                "SafetyMonitor_USB",
                T76_SAFETY_MONITOR_USB_TASK_STACK_SIZE,
                nullptr,
                T76_SAFETY_MONITOR_USB_TASK_PRIORITY,
                nullptr
            );

            xTaskCreate(
                faultReporterTask,
                "SafetyMonitor_Reporter",
                T76_SAFETY_MONITOR_REPORTER_STACK_SIZE,  // Reduced stack size since we're using minimal stack design
                nullptr,
                T76_SAFETY_MONITOR_REPORTER_PRIORITY,    // Higher priority for fault reporting
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

} // namespace T76::Core


