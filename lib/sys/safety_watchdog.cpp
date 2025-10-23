/**
 * @file safety_watchdog.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of hardware watchdog functionality for Core 1 protection.
 * 
 * This file provides watchdog timer management specifically designed for
 * protecting Core 1 operations in the dual-core RP2350 system. Core 1
 * typically runs bare-metal code without FreeRTOS task scheduling protection,
 * making hardware watchdog essential for detecting hangs and infinite loops.
 * 
 * Key functionality:
 * - Core 1 specific watchdog initialization with configurable timeout
 * - Periodic watchdog feeding to prevent system reset
 * - Integration with the safety system for fault logging on timeout
 * - Protection against multiple initialization attempts
 * 
 * The watchdog timeout will trigger a system reset and be logged as a
 * WATCHDOG_TIMEOUT fault, allowing the safety system to track Core 1 hangs
 * and take appropriate recovery actions.
 * 
 * Usage pattern:
 * 1. Call initCore1Watchdog() once during Core 1 initialization
 * 2. Call feedWatchdog() periodically (at least every 50% of timeout)
 * 3. Watchdog will reset system if not fed within timeout period
 */

#include "safety_private.hpp"
#include "safety.hpp"

namespace T76::Sys::Safety {

    /**
     * @brief Initialize Core 1 watchdog protection
     * 
     * Sets up hardware watchdog specifically for Core 1 protection using the
     * configured timeout (T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS). The watchdog
     * monitors Core 1 for hangs or infinite loops by requiring periodic
     * feeding through feedWatchdog().
     * 
     * Core 1 typically runs bare-metal code without the protection of FreeRTOS
     * task scheduling, making watchdog protection essential for detecting
     * hangs and ensuring system reliability.
     * 
     * @return true if watchdog was successfully initialized, false on error
     * 
     * @note Should only be called on Core 1 (returns false if called on Core 0)
     * @note Prevents multiple initialization (returns true if already initialized)
     * @note Application must call feedWatchdog() regularly to prevent timeout
     * @note Recommended to call feedWatchdog() at least every 50% of timeout interval
     * @note Uses T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS for timeout value (5 seconds)
     * @note Watchdog timeout will trigger system reset and fault logging
     */
    bool initCore1Watchdog() {
        // Only allow initialization on Core 1
        if (get_core_num() != 1) {
            return false;
        }

        // Prevent multiple initialization
        if (gWatchdogInitialized) {
            return true; // Already initialized
        }

        // Initialize hardware watchdog with configured timeout
        watchdog_enable(T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS, 1);

        gWatchdogInitialized = true;
        return true;
    }

    /**
     * @brief Feed the watchdog to prevent timeout
     * 
     * Resets the watchdog timer to prevent system reset. This function must
     * be called periodically by the application running on Core 1 to indicate
     * that the core is still operational and not stuck in an infinite loop.
     * 
     * The watchdog timer is reset each time this function is called, providing
     * another full timeout period before the next required feed. If this
     * function is not called within the timeout period, the hardware watchdog
     * will trigger a system reset.
     * 
     * @note Should be called at least every 50% of the configured timeout interval
     * @note Safe to call from any context (interrupt or main thread)
     * @note Only effective if initCore1Watchdog() has been called first
     * @note No-op if watchdog was not previously initialized
     * @note Calling too frequently is safe but may mask some types of faults
     */
    void feedWatchdog() {
        if (gWatchdogInitialized) {
            watchdog_update();
        }
    }

} // namespace T76::Sys::Safety

