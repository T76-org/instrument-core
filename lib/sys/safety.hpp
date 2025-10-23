/**
 * @file safety.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file provides comprehensive fault handling for the RP2350 platform using FreeRTOS.
 * It catches all possible faults (asserts, FreeRTOS hooks, panics, allocation failures, etc.),
 * saves information about the fault, and triggers system reset for recovery.
 * 
 * The system follows a safe-by-default design philosophy where the system always
 * starts in a safe state upon reset, eliminating the need for active safing functions.
 * This approach is more reliable and handles all reset scenarios (including hardware
 * watchdog timeouts) uniformly.
 * 
 * This safety system has been optimized to use the absolute minimum stack space possible
 * and uses only static memory allocation. Key optimizations include:
 * 
 * - Direct operation on shared memory structures (no stack copies)
 * - Elimination of printf/snprintf from core fault handling
 * - Minimal string operations using custom safe functions
 * - Inlined critical functions to reduce call stack depth
 * - Static buffers for all string operations
 * - Reduced function parameters and local variables
 * - Eliminated all struct references and local pointers
 * - Direct global memory access (no intermediate pointer variables)
 * 
 * Multi-Core Fault Handling:
 * ==========================
 * 
 * This system is designed to handle faults from both cores of the RP2350:
 * - Core 0: Runs FreeRTOS tasks and handles FreeRTOS-specific faults
 * - Core 1: Runs bare-metal code and handles system-level faults
 * 
 * The fault information is stored in a shared memory structure that can be
 * accessed from both cores. Critical sections ensure thread-safe access.
 * 
 * Fault Types Handled:
 * ====================
 * 
 * 1. FreeRTOS Assertions (configASSERT)
 * 2. Stack Overflow (vApplicationStackOverflowHook)
 * 3. Memory Allocation Failures (vApplicationMallocFailedHook)
 * 4. C Standard Assertions (assert.h)
 * 5. Pico SDK Hard Assertions
 * 6. Hardware Faults (HardFault, MemManage, BusFault, UsageFault)
 * 7. Inter-core Communication Failures
 * 
 * Recovery Strategies:
 * ===================
 * 
 * The system uses a safe-by-default design where the system automatically
 * returns to a safe state upon any reset. This eliminates the need for
 * active safing functions and provides more reliable safety behavior.
 * 
 * The system implements two recovery strategies based on fault history:
 * 
 * - RESET: Normal fault recovery (< T76_SAFETY_MAX_REBOOTS consecutive faults)
 *   * Triggers immediate system reset via hardware watchdog
 *   * System automatically returns to safe state upon reset
 *   * Preserves fault information in persistent memory for analysis
 * 
 * - SAFETY MONITOR: Persistent fault protection (≥ T76_SAFETY_MAX_REBOOTS faults)
 *   * Enters a safe monitoring mode with continuous fault reporting
 *   * Halts normal operation to prevent infinite reboot loops
 *   * Provides detailed fault history output via USB console
 *   * Requires manual system reset to clear fault state and resume operation
 * 
 * _Note:_ This code was partially developed with the assistance of AI.
 * 
 */

#pragma once

#include <cstdint>
#include <cstring>

#include "FreeRTOS.h"
#include "task.h"

#include "safety_private.hpp"
#include "safety.hpp"


namespace T76::Sys::Safety {

    /**
     * @brief Initialize the safety system
     * 
     * This function must be called early in system initialization, before
     * any other safety functions are used. It sets up shared memory structures
     * and configures the default fault handlers.
     */
    void safetyInit();



    /**
     * @brief Initialize Core 1 watchdog protection
     * 
     * Sets up hardware watchdog for Core 1 protection using the configured timeout
     * (T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS). The application must call 
     * feedWatchdog() periodically to prevent watchdog timeout.
     * 
     * @note Should only be called on Core 1
     * @note Application must call feedWatchdog() regularly to prevent timeout
     * @note Recommended to call feedWatchdog() at least every 50% of timeout interval
     * @note Uses T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS for timeout value
     * 
     * @return true if watchdog was successfully initialized, false on error
     */
    bool initCore1Watchdog();

    /**
     * @brief Feed the watchdog to prevent timeout
     * 
     * Resets the watchdog timer. This function must be called periodically
     * by the application to prevent watchdog timeout and system reset.
     * 
     * @note Should be called at least every 50% of the configured timeout interval
     * @note Safe to call from any context (interrupt or main thread)
     * @note Only effective if initCore1Watchdog() has been called first
     */
    void feedWatchdog();

} // namespace T76::Sys::Safety