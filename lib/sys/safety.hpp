/**
 * @file safety.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file provides comprehensive fault handling for the RP2350 platform using FreeRTOS.
 * It catches all possible faults (asserts, FreeRTOS hooks, panics, allocation failures, etc.),
 * saves information about the fault, and routes them to a common fault handler.
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
 * - REBOOT: Reboot into firmware recovery mode
 * - CONTINUE: Log fault and attempt to continue (for non-critical faults)
 * 
 */

#pragma once

#include <cstdint>
#include <cstring>

#include "FreeRTOS.h"
#include "task.h"

#define T76_MAX_TASK_NAME_LEN configMAX_TASK_NAME_LEN

namespace T76::Sys::Safety {

    /**
     * @brief Maximum length for fault description strings
     */
    constexpr size_t MAX_FAULT_DESC_LEN = 128;

    /**
     * @brief Maximum length for function name strings
     */
    constexpr size_t MAX_FUNCTION_NAME_LEN = 64;

    /**
     * @brief Maximum length for file name strings  
     */
    constexpr size_t MAX_FILE_NAME_LEN = 128;

    /**
     * @brief Enumeration of fault types that can be detected
     */
    enum class FaultType : uint8_t {
        UNKNOWN = 0,
        FREERTOS_ASSERT,          ///< FreeRTOS configASSERT failure
        STACK_OVERFLOW,           ///< FreeRTOS stack overflow detection
        MALLOC_FAILED,            ///< FreeRTOS malloc failure
        C_ASSERT,                 ///< Standard C assert() failure
        PICO_HARD_ASSERT,         ///< Pico SDK hard_assert failure
        HARDWARE_FAULT,           ///< Hardware exception (HardFault, etc.)
        INTERCORE_FAULT,          ///< Inter-core communication failure
        MEMORY_CORRUPTION,        ///< Detected memory corruption
        INVALID_STATE,            ///< Invalid system state detected
        RESOURCE_EXHAUSTED,       ///< System resource exhaustion
    };

    /**
     * @brief Enumeration of recovery actions
     */
    enum class RecoveryAction : uint8_t {
        HALT = 0,         ///< Halt execution, wait for external intervention
        RESET,            ///< Perform immediate system reset
    };

    /**
     * @brief Structure containing comprehensive fault information
     */
    struct FaultInfo {
        uint32_t timestamp;                                 ///< System tick when fault occurred
        uint32_t coreId;                                    ///< Core ID where fault occurred (0 or 1)
        FaultType type;                                     ///< Type of fault
        RecoveryAction recoveryAction;                      ///< Recommended recovery action
        uint32_t lineNumber;                                ///< Source code line number
        char fileName[MAX_FILE_NAME_LEN];                   ///< Source file name
        char functionName[MAX_FUNCTION_NAME_LEN];           ///< Function name where fault occurred
        char description[MAX_FAULT_DESC_LEN];               ///< Human-readable fault description
        uint32_t taskHandle;                                ///< FreeRTOS task handle (if applicable)
        char taskName[T76_MAX_TASK_NAME_LEN];              ///< FreeRTOS task name (if applicable)
        uint32_t faultSpecificData[4];                      ///< Additional fault-specific data
        uint32_t heapFreeBytes;                             ///< Available heap at time of fault
        uint32_t minHeapFreeBytes;                          ///< Minimum heap free since boot
        bool isInInterrupt;                                 ///< True if fault occurred in interrupt context
        uint32_t interruptNumber;                           ///< Interrupt number (if in interrupt)
        uint32_t faultCount;                                ///< Total number of faults since boot
    };

    /**
     * @brief Initialize the safety system
     * 
     * This function must be called early in system initialization, before
     * any other safety functions are used. It sets up shared memory structures,
     * initializes fault counters, and configures the default fault handlers.
     */
    void safetyInit();

    /**
     * @brief Get information about the last fault that occurred
     * 
     * @param faultInfo Output parameter to receive fault information
     * @return true if valid fault information was retrieved, false otherwise
     */
    bool getLastFault(FaultInfo& faultInfo);

    /**
     * @brief Get the total number of faults since system boot
     * 
     * @return Total fault count
     */
    uint32_t getFaultCount();

    /**
     * @brief Clear fault history and reset counters
     * 
     * This function should be called after successful fault recovery
     * to reset the fault tracking system.
     */
    void clearFaultHistory();

    /**
     * @brief Report a fault to the safety system
     * 
     * This function is used internally by system hooks and wrapper functions
     * to report various types of faults to the safety system.
     * 
     * @param type Type of fault that occurred
     * @param description Human-readable description of the fault
     * @param file Source file where fault occurred
     * @param line Line number where fault occurred
     * @param function Function name where fault occurred
     * @param recoveryAction Recommended recovery action
     */
    void reportFault(FaultType type, 
                    const char* description,
                    const char* file,
                    uint32_t line,
                    const char* function,
                    RecoveryAction recoveryAction);

    /**
     * @brief Check if the system is in a fault state
     * 
     * @return true if a fault is currently being processed, false otherwise
     */
    bool isInFaultState();

    /**
     * @brief Print fault information to console
     * 
     */
    void printFaultInfo();

    
} // namespace T76::Sys::Safety