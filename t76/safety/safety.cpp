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
#include <pico/critical_section.h>
#include <hardware/watchdog.h>

#include "t76/safety.hpp"
#include "safety_monitor.hpp"
#include "safety_private.hpp"


namespace T76::Sys::Safety {

    // === Consecutive fault reboot counter auto-reset (optional) ===
    // Implemented using a Pico SDK hardware alarm so it works even before
    // the FreeRTOS scheduler starts. When it fires, it clears the reboot
    // counter with proper critical section protection. A value of 0 seconds
    // disables the auto-reset (no alarm scheduled).

    /**
     * @brief Alarm ID for the one-shot reboot counter auto-reset.
     *
     * 0 indicates no alarm scheduled; otherwise holds the active alarm ID.
     */
    static alarm_id_t gFaultCountResetAlarmId = 0;          // 0 => no alarm scheduled

    static int64_t faultCountResetAlarmCallback(alarm_id_t /*id*/, void* /*user_data*/) {
        // Clear reboot counter after stable runtime window has elapsed
        if (gSharedFaultSystem && gSafetyCriticalSection.spin_lock) {
            critical_section_enter_blocking(&gSafetyCriticalSection);
            gSharedFaultSystem->rebootCount = 0;
            critical_section_exit(&gSafetyCriticalSection);
        }
        // Mark alarm as inactive
        gFaultCountResetAlarmId = 0;
        // Returning 0 cancels the alarm (one-shot)
        return 0;
    }

    static void scheduleFaultCountResetAlarm(uint32_t seconds) {
        // Cancel any existing alarm first
        if (gFaultCountResetAlarmId != 0) {
            cancel_alarm(gFaultCountResetAlarmId);
            gFaultCountResetAlarmId = 0;
        }

        // Schedule a new one-shot alarm if enabled
        if (seconds > 0) {
            const uint64_t delay_ms = static_cast<uint64_t>(seconds) * 1000ULL;
            gFaultCountResetAlarmId = add_alarm_in_ms(
                delay_ms,
                faultCountResetAlarmCallback,
                nullptr,
                true /* fire_if_past */
            );
        }
    }

    /**
     * @brief Core fault handling: record and trigger watchdog reset.
     *
     * Marks the system as safety-reset, saves the current fault to history,
     * and performs an immediate watchdog reset. The system returns to a safe
     * state on reboot. Never returns.
     */
    static inline void handleFault() {
        // Mark system as being in fault state
        if (gSharedFaultSystem && gSafetyCriticalSection.spin_lock) {
            critical_section_enter_blocking(&gSafetyCriticalSection);
            gSharedFaultSystem->safetySystemReset = true; // Mark as safety system reset
            
            // Store current fault in fault history
            if (gSharedFaultSystem->rebootCount < T76_SAFETY_MAX_REBOOTS) {
                uint32_t index = gSharedFaultSystem->rebootCount;
                // Copy the entire fault info structure to history
                gSharedFaultSystem->faultHistory[index] = gSharedFaultSystem->lastFaultInfo;
                gSharedFaultSystem->rebootCount++;
            }
            
            critical_section_exit(&gSafetyCriticalSection);
        }

        // Perform immediate system reset using watchdog
        // System will automatically return to safe state upon reset
        watchdog_enable(1, 1);
        while (true) {
            tight_loop_contents();
        }
    }

    // ========== Public API Implementation ==========

    void init() {
        if (gSafetyInitialized) {
            return; // Already initialized
        }

        // Initialize shared memory on first call from either core
        gSharedFaultSystem = reinterpret_cast<SharedFaultSystem*>(gSharedMemory);
        
        // Initialize Pico SDK critical section (safe to call multiple times)
        if (!critical_section_is_initialized(&gSafetyCriticalSection)) {
            critical_section_init(&gSafetyCriticalSection);
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
            gSharedFaultSystem->rebootCount = 0; // No faults yet
            gSharedFaultSystem->safetySystemReset = false;
            gSharedFaultSystem->watchdogFailureCore = T76_SAFETY_INVALID_CORE_ID; // No failure initially
        }

        // Only check for watchdog reboot if this is NOT the first boot
        // On first boot, we can't trust the previous state information
        if (wasWatchdogReboot && !isFirstBoot) {
            // Check if last reboot was caused by watchdog timeout (not safety system reset)
            if (!gSharedFaultSystem->safetySystemReset) {
                // This was a genuine watchdog timeout - populate fault info for it
                critical_section_enter_blocking(&gSafetyCriticalSection);
                
                // Create descriptive fault message including which core failed
                static char watchdogFaultDesc[T76_SAFETY_MAX_FAULT_DESC_LEN];
                uint8_t failureCore = gSharedFaultSystem->watchdogFailureCore;
                if (failureCore == 0) {
                    snprintf(watchdogFaultDesc, sizeof(watchdogFaultDesc), 
                            "Hardware watchdog timeout: Core 0 (FreeRTOS) stopped responding");
                } else if (failureCore == 0) {
                    snprintf(watchdogFaultDesc, sizeof(watchdogFaultDesc), 
                            "Hardware watchdog timeout: Core 1 (bare-metal) stopped responding");
                } else {
                    snprintf(watchdogFaultDesc, sizeof(watchdogFaultDesc), 
                            "Hardware watchdog timeout: Unknown core failure (core=%d)", failureCore);
                }
                
                populateFaultInfo(FaultType::WATCHDOG_TIMEOUT, 
                                watchdogFaultDesc,
                                __FILE__, __LINE__, __func__);
                
                // Add to fault history (like reportFault does but without immediate reset)
                if (gSharedFaultSystem->rebootCount < T76_SAFETY_MAX_REBOOTS) {
                    uint32_t index = gSharedFaultSystem->rebootCount;
                    gSharedFaultSystem->faultHistory[index] = gSharedFaultSystem->lastFaultInfo;
                    gSharedFaultSystem->rebootCount++;
                }
                
                critical_section_exit(&gSafetyCriticalSection);
            }
        }

        // Clear the safety system reset flag and watchdog failure core for next boot
        gSharedFaultSystem->safetySystemReset = false;
        gSharedFaultSystem->watchdogFailureCore = T76_SAFETY_INVALID_CORE_ID;  // Reset for next boot cycle

    makeAllComponentsSafe();

    // Configure and schedule auto-reset of reboot counter if enabled by default macro
    scheduleFaultCountResetAlarm(T76_SAFETY_FAULTCOUNT_RESET_SECONDS);
        
        // Check reboot count and handle safety monitor
        if (gSharedFaultSystem->rebootCount >= T76_SAFETY_MAX_REBOOTS) {
            // Too many consecutive reboots - enter safety monitor to display fault history
            SafetyMonitor::runSafetyMonitor();
        }
        
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
     * @note Thread-safe through critical section protection in populateFaultInfo
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
     * - Uses critical section protection for thread safety
     * 
     * @note Thread-safe through critical section protection
     * @note Does not affect reboot counter or fault history array
     * @note Safe to call even if safety system is not initialized
     */
    void clearFaultHistory() {
        if (!gSharedFaultSystem || !gSafetyCriticalSection.spin_lock) {
            return;
        }

        critical_section_enter_blocking(&gSafetyCriticalSection);
        memset(&gSharedFaultSystem->lastFaultInfo, 0, sizeof(FaultInfo));
        critical_section_exit(&gSafetyCriticalSection);
    }

    // No runtime setter; timeout is configured via T76_SAFETY_FAULTCOUNT_RESET_SECONDS.

} // namespace T76::Sys::Safety
