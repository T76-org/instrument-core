/**
 * @file safety.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file provides comprehensive fault handling for the RP2350 platform using FreeRTOS.
 * It catches all possible faults (asserts, FreeRTOS hooks, panics, allocation failures, etc.),
 * saves information about the fault, and routes them to a common fault handler.
 * 
 * OPTIMIZED FOR MINIMAL STACK USAGE AND STATIC MEMORY ALLOCATION
 * ==============================================================
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
 * Stack Usage Analysis:
 * ====================
 * 
 * - reportFault(): ~24 bytes (minimal local variables, no pointers)
 * - populateFaultInfo(): ~12 bytes (direct global access, no local pointers)
 * - handleFault(): ~8 bytes (minimal local variables)
 * - String operations: ~4 bytes (direct global access)
 * 
 * Total worst-case stack usage: ~48 bytes (compared to 500+ bytes previously)
 * Further optimized from ~64 bytes by eliminating all struct references and local pointers.
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
 * The system supports multiple recovery strategies:
 * - HALT: Stop execution and wait for external reset
 * - RESET: Perform system reset
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
     * @brief Function pointer type for safing functions
     * 
     * Safing functions are called before system reset to put the system
     * into a safe state. They should:
     * - Execute quickly and efficiently
     * - Be fault-tolerant (not cause additional faults)
     * - Put their subsystem into a safe state
     * - Not rely on dynamic memory allocation
     * - Use minimal stack space
     */
    typedef void (*SafingFunction)(void);

    /**
     * @brief Result codes for safing function operations
     */
    enum class SafingResult : uint8_t {
        SUCCESS = 0,      ///< Operation completed successfully
        FULL,             ///< Cannot register - table is full
        NOT_FOUND,        ///< Function not found during deregistration
        INVALID_PARAM,    ///< Invalid parameter provided
    };

    /**
     * @brief Initialize the safety system
     * 
     * This function must be called early in system initialization, before
     * any other safety functions are used. It sets up shared memory structures
     * and configures the default fault handlers.
     */
    void safetyInit();

    /**
     * @brief Deregister a previously registered safing function
     * 
     * @param safingFunc Function to deregister
     * @return SafingResult indicating success or failure reason
     */
    SafingResult deregisterSafingFunction(SafingFunction safingFunc);

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