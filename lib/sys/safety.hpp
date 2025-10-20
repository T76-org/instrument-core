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

// Check if FreeRTOS is available and include proper headers
#ifdef __has_include
    #if __has_include("FreeRTOS.h")
        #include "FreeRTOS.h"
        #include "task.h"
        #define T76_FREERTOS_AVAILABLE 1
    #else
        #define T76_FREERTOS_AVAILABLE 0
        // Forward declaration when FreeRTOS is not available
        typedef void* TaskHandle_t;
    #endif
#else
    #define T76_FREERTOS_AVAILABLE 0
    // Forward declaration when FreeRTOS is not available
    typedef void* TaskHandle_t;
#endif

// Define max task name length based on FreeRTOS availability
#if T76_FREERTOS_AVAILABLE
    #define T76_MAX_TASK_NAME_LEN configMAX_TASK_NAME_LEN
#else
    #define T76_MAX_TASK_NAME_LEN 16
#endif

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
     * @brief Enumeration of fault severity levels
     */
    enum class FaultSeverity : uint8_t {
        INFO = 0,         ///< Informational, system can continue
        WARNING,          ///< Warning, system can continue with degraded performance
        ERROR,            ///< Error, system should attempt recovery
        CRITICAL,         ///< Critical error, immediate action required
        FATAL,            ///< Fatal error, system must halt/reset
    };

    /**
     * @brief Enumeration of recovery actions
     */
    enum class RecoveryAction : uint8_t {
        CONTINUE = 0,     ///< Log fault and continue execution
        HALT,             ///< Halt execution, wait for external intervention
        RESET,            ///< Perform immediate system reset
        REBOOT,           ///< Reboot into recovery mode
        RESTART_TASK,     ///< Restart the affected task (FreeRTOS only)
        RESTART_CORE,     ///< Restart the affected core
    };

    /**
     * @brief Structure containing comprehensive fault information
     */
    struct FaultInfo {
        uint32_t timestamp;                                 ///< System tick when fault occurred
        uint32_t core_id;                                   ///< Core ID where fault occurred (0 or 1)
        FaultType type;                                     ///< Type of fault
        FaultSeverity severity;                             ///< Severity level
        RecoveryAction recovery_action;                     ///< Recommended recovery action
        uint32_t line_number;                               ///< Source code line number
        char file_name[MAX_FILE_NAME_LEN];                  ///< Source file name
        char function_name[MAX_FUNCTION_NAME_LEN];          ///< Function name where fault occurred
        char description[MAX_FAULT_DESC_LEN];               ///< Human-readable fault description
        uint32_t task_handle;                               ///< FreeRTOS task handle (if applicable)
        char task_name[T76_MAX_TASK_NAME_LEN];             ///< FreeRTOS task name (if applicable)
        uint32_t stack_pointer;                             ///< Stack pointer at time of fault
        uint32_t program_counter;                           ///< Program counter at time of fault
        uint32_t link_register;                             ///< Link register at time of fault
        uint32_t fault_specific_data[4];                    ///< Additional fault-specific data
        uint32_t heap_free_bytes;                           ///< Available heap at time of fault
        uint32_t min_heap_free_bytes;                       ///< Minimum heap free since boot
        bool is_in_interrupt;                               ///< True if fault occurred in interrupt context
        uint32_t interrupt_number;                          ///< Interrupt number (if in interrupt)
        uint32_t fault_count;                               ///< Total number of faults since boot
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
     * @param fault_info Output parameter to receive fault information
     * @return true if valid fault information was retrieved, false otherwise
     */
    bool getLastFault(FaultInfo& fault_info);

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
     * @brief Check if the system is in a fault state
     * 
     * @return true if a fault is currently being processed, false otherwise
     */
    bool isInFaultState();

    /**
     * @brief Convert fault type to string
     * 
     * @param type Fault type
     * @return String representation of fault type
     */
    const char* faultTypeToString(FaultType type);

    /**
     * @brief Convert fault severity to string
     * 
     * @param severity Fault severity
     * @return String representation of fault severity
     */
    const char* faultSeverityToString(FaultSeverity severity);

    /**
     * @brief Convert recovery action to string
     * 
     * @param action Recovery action
     * @return String representation of recovery action
     */
    const char* recoveryActionToString(RecoveryAction action);

} // namespace T76::Sys::Safety

// C-style wrapper functions for use with FreeRTOS hooks and other C code
extern "C" {
    /**
     * @brief FreeRTOS assert hook function (called by configASSERT)
     */
    void my_assert_func(const char* file, int line, const char* func, const char* expr);

    /**
     * @brief FreeRTOS malloc failed hook (called when heap allocation fails)
     */
    void vApplicationMallocFailedHook(void);

    /**
     * @brief FreeRTOS stack overflow hook (called when stack overflow detected)
     */
    void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName);

    /**
     * @brief Hardware fault handlers
     */
    void HardFault_Handler(void);
    void MemManage_Handler(void);
    void BusFault_Handler(void);
    void UsageFault_Handler(void);
}