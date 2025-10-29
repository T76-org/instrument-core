/**
 * @file safety_watchdog.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of dual-core watchdog functionality for both Core 0 and Core 1 protection.
 * 
 * This file provides a dual-core watchdog system where:
 * - Core 0 (FreeRTOS) manages the hardware watchdog and monitors system health
 * - Core 1 (bare-metal) sends periodic heartbeats to Core 0 via shared memory
 * - Hardware watchdog is only fed when both cores are confirmed healthy
 * 
 * Architecture:
 * - Core 1 calls sendCore1Heartbeat() periodically to indicate it's alive
 * - Core 0 runs a FreeRTOS watchdog manager task that:
 *   * Receives heartbeats from Core 1
 *   * Monitors Core 0 FreeRTOS task health
 *   * Feeds hardware watchdog only when both cores are healthy
 * 
 * Key functionality:
 * - Dual-core protection with single hardware watchdog
 * - Inter-core communication via simple shared memory
 * - Fault isolation to identify which core failed (preserved through hardware reset)
 * - Integration with safety system for comprehensive fault logging
 * - Graduated timeout system for early fault detection
 * 
 * Usage pattern:
 * 1. Call initDualCoreWatchdog() once during Core 0 initialization
 * 2. Core 1 calls sendCore1Heartbeat() periodically (at least every 1 second)
 * 3. Core 0 watchdog manager automatically handles hardware watchdog feeding
 * 4. System reset occurs if either core fails to respond within timeout
 */

#include "safety_private.hpp"
#include "t76/safety.hpp"

namespace T76::Sys::Safety {

    // Inter-core communication constants - now using centralized configuration
    static const uint32_t CORE1_HEARTBEAT_TIMEOUT_MS = T76_SAFETY_CORE1_HEARTBEAT_TIMEOUT_MS;
    static const uint32_t WATCHDOG_TASK_PERIOD_MS = T76_SAFETY_WATCHDOG_TASK_PERIOD_MS;
    
    /*
     * Timeout Hierarchy:
     * - Core 1 should send heartbeats every ~1 second (recommended)
     * - Core 1 heartbeat timeout: 2 seconds (CORE1_HEARTBEAT_TIMEOUT_MS)
     * - Watchdog manager checks every: 500ms (WATCHDOG_TASK_PERIOD_MS) 
     * - Hardware watchdog timeout: 5 seconds (T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS)
     * 
     * This gives the system multiple opportunities to detect and handle faults:
     * 1. Core 1 misses heartbeat -> Detected within 2.5 seconds
     * 2. Core 0 can still feed watchdog for another 2.5 seconds if needed
     * 3. Hardware watchdog triggers system reset if all else fails
     */
    
    // Shared memory for inter-core communication
    // 32-bit writes are atomic on ARM Cortex-M33, so no synchronization needed
    static volatile uint32_t gCore1LastHeartbeat = 0;
    
    // Watchdog failure core is stored in persistent shared memory (gSharedFaultSystem->watchdogFailureCore)
    // This survives hardware resets and allows accurate fault reporting after reboot

    /**
     * @brief Low-priority FreeRTOS task that manages the dual-core watchdog system
     * 
     * This task runs on Core 0 with the lowest priority (1) and is responsible for:
     * - Monitoring Core 1 heartbeats via shared memory
     * - Monitoring Core 0 FreeRTOS system health
     * - Feeding the hardware watchdog only when both cores are healthy
     * - Triggering appropriate fault handling when problems are detected
     * 
     * The low priority ensures this task only runs when the system is genuinely
     * idle, which better reflects actual system health. If higher-priority tasks
     * are running continuously, that's legitimate system activity.
     * 
     * @param pvParameters Unused task parameter
     */
    static void watchdogManagerTask(void* pvParameters) {
        (void)pvParameters;
        
        TickType_t lastWakeTime = xTaskGetTickCount();
        
        while (true) {
            // Check if Core 1 heartbeat is still fresh
            uint32_t currentTime = to_ms_since_boot(get_absolute_time());
            uint32_t lastHeartbeat = gCore1LastHeartbeat;  // Read shared timestamp
            bool core1Healthy = (lastHeartbeat > 0) && 
                               (currentTime - lastHeartbeat) < CORE1_HEARTBEAT_TIMEOUT_MS;
            
            // Check Core 0 health (basic FreeRTOS scheduler health)
            bool core0Healthy = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
            
            // Only feed watchdog if both cores are healthy
            if (core0Healthy && core1Healthy && gWatchdogInitialized) {
                watchdog_update();
                gSharedFaultSystem->watchdogFailureCore = T76_SAFETY_INVALID_CORE_ID;  // Reset failure indicator when healthy
            } else {
                // Record which core failed first (for hardware watchdog handler)
                if (!core0Healthy && gSharedFaultSystem->watchdogFailureCore == T76_SAFETY_INVALID_CORE_ID) {
                    gSharedFaultSystem->watchdogFailureCore = 0;  // Core 0 failed
                } else if (!core1Healthy && gSharedFaultSystem->watchdogFailureCore == T76_SAFETY_INVALID_CORE_ID) {
                    gSharedFaultSystem->watchdogFailureCore = 1;  // Core 1 failed
                }
                // Don't feed watchdog - let hardware watchdog reset the system
            }
            
            // Wait for next check period
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(WATCHDOG_TASK_PERIOD_MS));
        }
    }

    /**
     * @brief Initialize dual-core watchdog protection system
     * 
     * Sets up the dual-core watchdog system where Core 0 manages the hardware
     * watchdog and Core 1 sends periodic heartbeats. This function should be
     * called on Core 0 during system initialization.
     * 
     * The system creates a low-priority FreeRTOS task that:
     * - Monitors Core 1 heartbeats via shared memory
     * - Checks Core 0 FreeRTOS scheduler health
     * - Feeds hardware watchdog only when both cores are confirmed healthy
     * - Only runs when no higher-priority tasks need CPU time
     * 
     * @return true if watchdog system was successfully initialized, false on error
     * 
     * @note Must be called on Core 0 (returns false if called on Core 1)
     * @note Prevents multiple initialization (returns true if already initialized)
     * @note Core 1 must call sendCore1Heartbeat() regularly after this initialization
     * @note Uses T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS for hardware watchdog timeout
     * @note Creates a low-priority FreeRTOS task that only runs when system is idle
     */
    bool watchdogInit() {
        // Only allow initialization on Core 0
        if (get_core_num() != 0) {
            return false;
        }

        // Prevent multiple initialization
        if (gWatchdogInitialized) {
            return true; // Already initialized
        }

        // Initialize hardware watchdog with configured timeout
        watchdog_enable(T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS, 1);

        // Initialize shared memory for heartbeat communication
        gCore1LastHeartbeat = 0;
        // watchdogFailureCore is now initialized in safetyInit()

        // Create the watchdog manager task with lowest priority
        // This ensures it only runs when no other tasks need CPU time,
        // which better reflects actual system health
        BaseType_t result = xTaskCreate(
            watchdogManagerTask,
            "WatchdogMgr",
            T76_SAFETY_WATCHDOG_TASK_STACK_SIZE,  // Give it enough stack
            nullptr,
            T76_SAFETY_WATCHDOG_TASK_PRIORITY,    // Lowest priority - only runs when system is idle
            nullptr
        );

        if (result != pdPASS) {
            return false;
        }

        gWatchdogInitialized = true;
        return true;
    }

    /**
     * @brief Send heartbeat from Core 1 to indicate it's alive
     * 
     * This function should be called periodically by Core 1 to indicate that
     * it's still operational. The heartbeat updates a shared memory timestamp
     * that is monitored by the watchdog manager task running on Core 0.
     * 
     * Core 1 should call this function at least every 1 second (well before
     * the 2-second timeout) to ensure the watchdog system recognizes it as
     * healthy and continues feeding the hardware watchdog.
     * 
     * @note Should be called only from Core 1
     * @note Safe to call from any context on Core 1 (interrupt or main thread)
     * @note Must be called regularly (at least every 1 second)
     * @note No-op if called from Core 0 or if watchdog system not initialized
     */
    void feedWatchdogFromCore1() {
        // Only send heartbeats from Core 1
        if (get_core_num() != 1 || !gWatchdogInitialized) {
            return;
        }

        // Update shared timestamp (32-bit write is atomic on ARM Cortex-M33)
        gCore1LastHeartbeat = to_ms_since_boot(get_absolute_time());
    }

} // namespace T76::Sys::Safety

