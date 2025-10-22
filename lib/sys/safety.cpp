/**
 * @file safety.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of comprehensive fault handling for the RP2350 platform.
 * Optimized for minimal stack usage and static-only memory allocation.
 */

// Minimal includes to reduce dependencies and stack usage
#include <cstring>
#include <cstdio>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>

// Pico SDK includes
#include <pico/stdlib.h>
#include <pico/time.h>
#include <hardware/sync/spin_lock.h>
#include <hardware/watchdog.h>
#include <hardware/irq.h>

#include "safety.hpp"
#include "safety_monitor.hpp"
#include "safety_private.hpp"


namespace T76::Sys::Safety {

    /**
     * @brief Core fault handling function - minimal stack usage
     * 
     * Final stage of fault processing that prepares the system for recovery.
     * Designed for maximum reliability with minimal stack and resource usage.
     * 
     * Sequence of operations:
     * 1. Mark system as being in fault state (for persistent tracking)
     * 2. Set safety system reset flag to distinguish from watchdog timeout
     * 3. Execute all registered safing functions to put system in safe state
     * 4. Allow brief time for pending output to complete
     * 5. Trigger immediate system reset via watchdog
     * 
     * This function never returns - it always results in system reset.
     * The fault information will persist in uninitialized memory for
     * analysis by the Safety Monitor on the next boot.
     * 
     * @note Uses busy-wait loops to avoid additional function call overhead
     * @note Watchdog reset is more reliable than software reset mechanisms
     * @note Thread-safe through spinlock protection of shared state
     */
    static inline void handleFault() {
        // Mark system as being in fault state
        if (gSharedFaultSystem && gSafetySpinlock) {
            uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
            gSharedFaultSystem->lastFaultCore = get_core_num();
            gSharedFaultSystem->safetySystemReset = true; // Mark as safety system reset
            
            // Store current fault in fault history
            if (gSharedFaultSystem->rebootCount < T76_SAFETY_MAX_REBOOTS) {
                uint32_t index = gSharedFaultSystem->rebootCount;
                // Copy the entire fault info structure to history
                gSharedFaultSystem->faultHistory[index] = gSharedFaultSystem->lastFaultInfo;
                gSharedFaultSystem->rebootCount++;
            }
            
            spin_unlock(gSafetySpinlock, savedIrq);
        }

        // Execute safing functions before recovery action
        // This puts the system into a safe state before restart/halt
        executeSafingFunctions();

        // Give a brief moment for any pending output to complete
        // Use busy wait to avoid function call overhead
        uint32_t start_time = to_ms_since_boot(get_absolute_time());
        while ((to_ms_since_boot(get_absolute_time()) - start_time) < 100) {
            tight_loop_contents();
        }

        // Perform immediate system reset using watchdog
        // This is more reliable than triggering a fault
        watchdog_enable(1, 1);
        while (true) {
            tight_loop_contents();
        }
    }

    // ========== Public API Implementation ==========

    void safetyInit() {
        if (gSafetyInitialized) {
            return; // Already initialized
        }

        // Initialize shared memory on first call from either core
        gSharedFaultSystem = reinterpret_cast<SharedFaultSystem*>(gSharedMemory);
        
        // Initialize Pico SDK spinlock (safe to call multiple times)
        if (gSafetySpinlock == nullptr) {
            gSafetySpinlock = spin_lock_init(PICO_SPINLOCK_ID_OS1);
        }

        // Store watchdog reboot status for later processing
        bool wasWatchdogReboot = watchdog_caused_reboot();
        bool isFirstBoot = false;

        // Check if already initialized by other core
        if (gSharedFaultSystem->magic != FAULT_SYSTEM_MAGIC) {
            // First initialization
            isFirstBoot = true;
            memset(gSharedFaultSystem, 0, sizeof(SharedFaultSystem));
            gSharedFaultSystem->magic = FAULT_SYSTEM_MAGIC;
            gSharedFaultSystem->version = 1;
            gSharedFaultSystem->safingFunctionCount = 0;
            gSharedFaultSystem->rebootCount = 0; // No faults yet
            gSharedFaultSystem->lastBootTimestamp = to_ms_since_boot(get_absolute_time());
            gSharedFaultSystem->safetySystemReset = false;
            
            // Initialize safing function array to null pointers
            for (uint32_t i = 0; i < T76_SAFETY_MAX_SAFING_FUNCTIONS; i++) {
                gSharedFaultSystem->safingFunctions[i] = nullptr;
            }
        }

        // Only check for watchdog reboot if this is NOT the first boot
        // On first boot, we can't trust the previous state information
        if (wasWatchdogReboot && !isFirstBoot) {
            // Check if last reboot was caused by watchdog timeout (not safety system reset)
            if (!gSharedFaultSystem->safetySystemReset) {
                // This was a genuine watchdog timeout, not a safety system reset
                // Create a watchdog timeout fault entry
                populateFaultInfo(FaultType::WATCHDOG_TIMEOUT, 
                                "Hardware watchdog timeout - Core 1 may have hung",
                                "system", 0, "watchdog");
                gSharedFaultSystem->lastFaultCore = 1; // Assume Core 1 since it's the one being protected
                
                // Manually add to fault history (like reportFault does but without immediate reset)
                if (gSharedFaultSystem->rebootCount < T76_SAFETY_MAX_REBOOTS) {
                    uint32_t index = gSharedFaultSystem->rebootCount;
                    gSharedFaultSystem->faultHistory[index] = gSharedFaultSystem->lastFaultInfo;
                    gSharedFaultSystem->rebootCount++;
                }
            }
        }

        // Clear the safety system reset flag for next boot
        gSharedFaultSystem->safetySystemReset = false;
        
        // Check reboot count and handle safety monitor
        if (gSharedFaultSystem->rebootCount >= T76_SAFETY_MAX_REBOOTS) {
            // Too many consecutive reboots - enter safety monitor to display fault history
            SafetyMonitor::runSafetyMonitor();
        }
        
        gSharedFaultSystem->lastBootTimestamp = to_ms_since_boot(get_absolute_time());

        gSafetyInitialized = true;
    }

    /**
     * @brief Internal function to report faults (used by system hooks)
     * Optimized for minimal stack usage and direct operation
     */
    void reportFault(FaultType type, 
                     const char* description,
                     const char* file,
                     uint32_t line,
                     const char* function) {
        
        // Ensure shared memory is available
        if (!gSharedFaultSystem) {
            // If no shared memory, just reset immediately
            watchdog_enable(1, 1);
            while (true) {
                tight_loop_contents();
            }
        }

        // Populate fault information directly in shared memory
        populateFaultInfo(type, description, file, line, function);
        
        // Handle fault with minimal overhead
        handleFault();
    }

    void clearFaultHistory() {
        if (!gSharedFaultSystem || !gSafetySpinlock) {
            return;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
        memset(&gSharedFaultSystem->lastFaultInfo, 0, sizeof(FaultInfo));
        spin_unlock(gSafetySpinlock, savedIrq);
    }

    /**
     * @brief Reset the consecutive reboot counter
     * 
     * This function should be called by the application after successful
     * initialization or operation to reset the consecutive reboot counter.
     * This prevents the system from entering safety monitor mode due to
     * a series of unrelated reboots.
     * 
     * @details The function:
     * - Resets rebootCount to 0
     * - Clears the fault history array
     * - Updates the last boot timestamp
     * 
     * @note Thread-safe through spinlock protection
     * @note Should be called after successful system operation/initialization
     */
    void resetRebootCounter() {
        if (!gSharedFaultSystem || !gSafetySpinlock) {
            return;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
        
        // Reset reboot count and clear fault history
        gSharedFaultSystem->rebootCount = 0;
        memset(gSharedFaultSystem->faultHistory, 0, sizeof(gSharedFaultSystem->faultHistory));
        gSharedFaultSystem->lastBootTimestamp = to_ms_since_boot(get_absolute_time());
        
        spin_unlock(gSafetySpinlock, savedIrq);
    }


} // namespace T76::Sys::Safety
