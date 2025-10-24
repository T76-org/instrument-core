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
     * Final stage of fault processing that triggers system reset for recovery.
     * Designed for maximum reliability with minimal stack and resource usage.
     * 
     * With safe-by-default design, the system automatically returns to a safe
     * state upon reset, eliminating the need for active safing functions.
     * 
     * Sequence of operations:
     * 1. Mark system as being in fault state (for persistent tracking)
     * 2. Set safety system reset flag to distinguish from watchdog timeout
     * 3. Allow brief time for pending output to complete
     * 4. Trigger immediate system reset via watchdog
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

        // Perform immediate system reset using watchdog
        // System will automatically return to safe state upon reset
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
            gSafetySpinlock = spin_lock_init(spin_lock_claim_unused(true));
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
            gSharedFaultSystem->rebootCount = 0; // No faults yet
            gSharedFaultSystem->lastBootTimestamp = to_ms_since_boot(get_absolute_time());
            gSharedFaultSystem->safetySystemReset = false;
            gSharedFaultSystem->watchdogFailureCore = 255; // No failure initially
        }

        // Only check for watchdog reboot if this is NOT the first boot
        // On first boot, we can't trust the previous state information
        if (wasWatchdogReboot && !isFirstBoot) {
            // Check if last reboot was caused by watchdog timeout (not safety system reset)
            if (!gSharedFaultSystem->safetySystemReset) {
                // This was a genuine watchdog timeout, not a safety system reset
                // Check which core caused the failure
                uint8_t failureCore = gSharedFaultSystem->watchdogFailureCore;

                switch (failureCore) {
                    case 0:
                        // Core 0 (FreeRTOS scheduler) failure
                        gSharedFaultSystem->lastFaultCore = 0;
                        break;
                    
                    case 1:
                        // Core 1 heartbeat timeout
                        gSharedFaultSystem->lastFaultCore = 1;
                        break;
                    
                    default:
                        // This probably means that a task on core 0 hung
                        gSharedFaultSystem->lastFaultCore = 1; // Default assumption
                        break;
                }
                
                // Manually add to fault history (like reportFault does but without immediate reset)
                if (gSharedFaultSystem->rebootCount < T76_SAFETY_MAX_REBOOTS) {
                    uint32_t index = gSharedFaultSystem->rebootCount;
                    gSharedFaultSystem->faultHistory[index] = gSharedFaultSystem->lastFaultInfo;
                    gSharedFaultSystem->rebootCount++;
                }
            }
        }

        // Clear the safety system reset flag and watchdog failure core for next boot
        gSharedFaultSystem->safetySystemReset = false;
        gSharedFaultSystem->watchdogFailureCore = 255;  // Reset for next boot cycle

        makeAllComponentsSafe();
        
        // Check reboot count and handle safety monitor
        if (gSharedFaultSystem->rebootCount >= T76_SAFETY_MAX_REBOOTS) {
            // Too many consecutive reboots - enter safety monitor to display fault history
            SafetyMonitor::runSafetyMonitor();
        }
        
        gSharedFaultSystem->lastBootTimestamp = to_ms_since_boot(get_absolute_time());

        gSafetyInitialized = true;

        // Try to activate all registered components
        const char* failingComponentName = nullptr;
        if (!activateAllComponents(&failingComponentName)) {
            // Build a descriptive fault message including the component name
            if (failingComponentName != nullptr) {
                // Use static buffer to avoid stack allocation during fault handling
                static char faultDescription[T76_SAFETY_MAX_FAULT_DESC_LEN];
                snprintf(faultDescription, sizeof(faultDescription), 
                        "Component activation failed: %s", failingComponentName);
                reportFault(FaultType::ACTIVATION_FAILED, faultDescription, __FILE__, __LINE__, __func__);
            } else {
                reportFault(FaultType::ACTIVATION_FAILED, "Component activation failed (unknown component)", __FILE__, __LINE__, __func__);
            }
        }
    }

    /**
     * @brief Report a fault to the safety system with minimal stack usage
     * 
     * Central fault reporting function used by system hooks and wrapper functions
     * to report various types of faults. This function captures comprehensive fault
     * information and triggers immediate system recovery through the safety mechanism.
     * 
     * The function performs the following sequence:
     * 1. Ensures shared memory is available (immediate reset if not)
     * 2. Populates detailed fault information in shared memory
     * 3. Triggers fault handling sequence (safing functions + system reset)
     * 
     * Optimized for minimal stack usage and direct operation on shared memory
     * to ensure reliability even under severe fault conditions.
     * 
     * @param type Fault type classification for categorization and analysis
     * @param description Human-readable fault description for debugging
     * @param file Source file name where fault occurred
     * @param line Line number in source file where fault occurred
     * @param function Function name where fault occurred
     * 
     * @note Never returns - always results in system reset
     * @note Thread-safe through spinlock protection in populateFaultInfo
     * @note Falls back to immediate watchdog reset if shared memory unavailable
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

    /**
     * @brief Clear fault history and reset fault tracking
     * 
     * Resets the fault information structure in shared memory to clear
     * any previously recorded fault data. This function should be called
     * after successful fault recovery or during system maintenance to
     * clear stale fault information.
     * 
     * The function:
     * - Zeros out the lastFaultInfo structure in shared memory
     * - Preserves other safety system state (reboot counter, etc.)
     * - Uses spinlock protection for thread safety
     * 
     * @note Thread-safe through spinlock protection
     * @note Does not affect reboot counter or fault history array
     * @note Safe to call even if safety system is not initialized
     */
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
