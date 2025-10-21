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

#define T76_SAFETY_MAX_FAULT_DESC_LEN 128      ///< Max fault description length
#define T76_SAFETY_MAX_FUNCTION_NAME_LEN 64    ///< Max function name length
#define T76_SAFETY_MAX_FILE_NAME_LEN 128       ///< Max file name length

namespace T76::Sys::Safety {
    /**
     * @brief Stack information captured during fault
     */
    struct StackInfo {
        uint32_t stackSize;                     ///< Total stack size in bytes
        uint32_t stackUsed;                     ///< Used stack space in bytes
        uint32_t stackRemaining;                ///< Remaining stack space in bytes
        uint32_t stackHighWaterMark;            ///< Minimum stack remaining since task start
        uint8_t stackUsagePercent;              ///< Stack usage as percentage (0-100)
        bool isMainStack;                       ///< True if using main stack (MSP), false for process stack (PSP)
        bool isValidStackInfo;                  ///< True if stack information is valid
    };

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

    // Maximum number of safing functions that can be registered
    #define T76_SAFETY_MAX_SAFING_FUNCTIONS 8

    /**
     * @brief Function pointer type for safing functions
     * 
     * Safing functions are called before system halt or reset to put the system
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
     * @brief Structure containing comprehensive fault information
     */
    struct FaultInfo {
        uint32_t timestamp;                                 ///< System tick when fault occurred
        uint32_t coreId;                                    ///< Core ID where fault occurred (0 or 1)
        FaultType type;                                     ///< Type of fault
        RecoveryAction recoveryAction;                      ///< Recommended recovery action
        uint32_t lineNumber;                                ///< Source code line number
        char fileName[T76_SAFETY_MAX_FILE_NAME_LEN];                   ///< Source file name
        char functionName[T76_SAFETY_MAX_FUNCTION_NAME_LEN];           ///< Function name where fault occurred
        char description[T76_SAFETY_MAX_FAULT_DESC_LEN];               ///< Human-readable fault description
        uint32_t taskHandle;                                ///< FreeRTOS task handle (if applicable)
        char taskName[configMAX_TASK_NAME_LEN];              ///< FreeRTOS task name (if applicable)
        uint32_t faultSpecificData[4];                      ///< Additional fault-specific data
        uint32_t heapFreeBytes;                             ///< Available heap at time of fault
        uint32_t minHeapFreeBytes;                          ///< Minimum heap free since boot
        bool isInInterrupt;                                 ///< True if fault occurred in interrupt context
        uint32_t interruptNumber;                           ///< Interrupt number (if in interrupt)
        StackInfo stackInfo;                                ///< Stack information at time of fault
    };

    /**
     * @brief Shared memory structure for inter-core fault communication
     * 
     * This structure is placed in a shared memory region accessible by both cores.
     * It uses atomic operations and memory barriers to ensure thread safety.
     */
    struct SharedFaultSystem {
        volatile uint32_t magic;                    ///< Magic number for structure validation
        volatile uint32_t version;                  ///< Structure version for compatibility
        volatile bool isInFaultState;               ///< True if currently processing a fault
        volatile uint32_t lastFaultCore;            ///< Core ID of last fault
        FaultInfo lastFaultInfo;                    ///< Information about the last fault
        
        // Safing function management
        SafingFunction safingFunctions[T76_SAFETY_MAX_SAFING_FUNCTIONS]; ///< Array of registered safing functions
        volatile uint32_t safingFunctionCount;      ///< Number of registered safing functions
        
        uint32_t reserved[1];                       ///< Reserved for future use (reduced to accommodate safing functions)
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
     * @brief Get information about the last fault that occurred
     * 
     * @param faultInfo Pointer to structure to receive fault information
     * @return true if valid fault information was retrieved, false otherwise
     */
    bool getLastFault(FaultInfo* faultInfo);

    /**
     * @brief Clear fault history
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
     * NOTE: This function is intentionally minimal in the core safety system
     * to avoid including printf and increasing stack usage. The actual printing
     * implementation is in the Safety Monitor module which has printf available.
     */
    void printFaultInfo();

    /**
     * @brief Convert fault type to string representation
     * 
     * @param type Fault type to convert
     * @return Static string representation of the fault type
     */
    const char* faultTypeToString(FaultType type);

    /**
     * @brief Convert recovery action to string representation
     * 
     * @param action Recovery action to convert
     * @return Static string representation of the recovery action
     */
    const char* recoveryActionToString(RecoveryAction action);

    /**
     * @brief Register a safing function to be called before system halt/reset
     * 
     * Safing functions are executed in the order they were registered when
     * a fault occurs, before the final recovery action (halt or reset).
     * 
     * @param safingFunc Function to register
     * @return SafingResult indicating success or failure reason
     */
    SafingResult registerSafingFunction(SafingFunction safingFunc);

    /**
     * @brief Deregister a previously registered safing function
     * 
     * @param safingFunc Function to deregister
     * @return SafingResult indicating success or failure reason
     */
    SafingResult deregisterSafingFunction(SafingFunction safingFunc);

    /**
     * @brief Test function to trigger a fault with stack capture for validation
     * 
     * This function is for testing purposes only - it will cause a system reset.
     * It creates a controlled fault condition to test the stack capture functionality.
     * The captured stack information will be available through the Safety Monitor
     * on the next boot cycle.
     */
    void testStackCapture();

    
} // namespace T76::Sys::Safety