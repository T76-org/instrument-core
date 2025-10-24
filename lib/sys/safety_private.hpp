/**
 * @file safety_private.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Private internal definitions and structures for the safety system.
 * 
 * This file contains internal data structures, constants, and function declarations
 * used by the safety system implementation. It includes:
 * 
 * - Shared memory structures for inter-core fault communication
 * - Fault information data structures and enumerations  
 * - Internal function declarations for fault handling
 * - Configuration constants and limits
 * - Global variable declarations for shared state
 * 
 * This file should only be included by safety system implementation files
 * and is not part of the public API.
 */

#pragma once

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
#include <hardware/irq.h>


// ========== T76 SAFETY SYSTEM CONFIGURATION PARAMETERS ==========
// All configurable parameters for the safety system are defined here.
// Modify these values to customize the safety system behavior for your application.
//
// IMPORTANT: After modifying any of these parameters, you must:
// 1. Recompile the entire project (not just incremental build)
// 2. Test thoroughly as these affect safety-critical behavior
// 3. Update your documentation to reflect the new configuration
//
// These parameters control various aspects of the safety system including:
// - Memory limits and buffer sizes
// - Timing parameters for watchdog and monitoring
// - Task priorities and stack sizes
// - Recovery behavior thresholds

// === Fault Information Limits ===
#define T76_SAFETY_MAX_FAULT_DESC_LEN 128           ///< Maximum fault description string length (bytes)
#define T76_SAFETY_MAX_FUNCTION_NAME_LEN 64         ///< Maximum function name string length (bytes)
#define T76_SAFETY_MAX_FILE_NAME_LEN 128            ///< Maximum file name string length (bytes)

// === Safety Recovery Configuration ===
#define T76_SAFETY_MAX_REBOOTS 3                    ///< Maximum consecutive reboots before entering safety monitor mode

// === Dual-Core Watchdog System Configuration ===
#define T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS 5000 ///< Hardware watchdog timeout in milliseconds (5 seconds)
#define T76_SAFETY_CORE1_HEARTBEAT_TIMEOUT_MS 2000  ///< Core 1 heartbeat timeout in milliseconds (2 seconds)
#define T76_SAFETY_WATCHDOG_TASK_PERIOD_MS 500      ///< Watchdog manager task check period in milliseconds (500ms)
#define T76_SAFETY_WATCHDOG_TASK_PRIORITY 1         ///< FreeRTOS priority for watchdog manager task (lowest priority)
#define T76_SAFETY_WATCHDOG_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 2) ///< Stack size for watchdog task

// === Component Registry Configuration ===
#define T76_SAFETY_MAX_REGISTERED_COMPONENTS 32     ///< Maximum number of SafeableComponent objects that can be registered

// === Safety Monitor Configuration ===
#define T76_SAFETY_MONITOR_USB_TASK_STACK_SIZE 256  ///< Stack size for Safety Monitor USB task (words)
#define T76_SAFETY_MONITOR_USB_TASK_PRIORITY 1      ///< FreeRTOS priority for Safety Monitor USB task
#define T76_SAFETY_MONITOR_REPORTER_STACK_SIZE 256  ///< Stack size for Safety Monitor fault reporter task (words)
#define T76_SAFETY_MONITOR_REPORTER_PRIORITY 2      ///< FreeRTOS priority for Safety Monitor fault reporter task
#define T76_SAFETY_MONITOR_REPORT_INTERVAL_MS 1000  ///< Interval between fault reports in milliseconds (1 second)
#define T76_SAFETY_MONITOR_CYCLE_DELAY_MS 2000      ///< Delay between fault reporting cycles in milliseconds (2 seconds)

// === Stack Analysis Configuration ===
#define T76_SAFETY_ESTIMATED_MAIN_STACK_BASE 0x20042000 ///< Estimated base address for main stack (RP2350 specific)
#define T76_SAFETY_DEFAULT_STACK_ESTIMATE 1024      ///< Conservative stack size estimate when exact size unknown (bytes)
#define T76_SAFETY_CONSERVATIVE_STACK_USED_ESTIMATE 256 ///< Conservative estimate for stack usage in bytes

// === Core Identification Constants ===
#define T76_SAFETY_INVALID_CORE_ID 255              ///< Value indicating unknown/invalid core ID
#define T76_SAFETY_CORE0_ID 0                       ///< Core 0 identifier (FreeRTOS core)
#define T76_SAFETY_CORE1_ID 1                       ///< Core 1 identifier (bare-metal core)

/**
 * @brief Magic number for fault system structure validation
 * 
 * This constant is used to validate that the shared memory structure
 * has been properly initialized and is not corrupted.
 */
#define FAULT_SYSTEM_MAGIC 0x54F3570

/**
 * @brief Magic number for component registry validation
 * 
 * This constant is used to validate that the component registry structure
 * has been properly initialized and is not corrupted.
 */
#define COMPONENT_REGISTRY_MAGIC 0x53414645


namespace T76::Sys::Safety {

    /**
     * @brief Stack information captured during fault
     */
    struct StackInfo {
        uint32_t stackSize;                     ///< Total stack size in bytes
        uint32_t stackUsed;                     ///< Used stack space in bytes
        uint32_t stackRemaining;                ///< Remaining stack space in bytes
        uint32_t stackHighWaterMark;            ///< Minimum stack remaining since task start
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
        WATCHDOG_TIMEOUT,         ///< Hardware watchdog timeout (Core 1 hang)
        ACTIVATION_FAILED,        ///< Activation failed at startup
    };

    /**
     * @brief Structure containing comprehensive fault information
     */
    typedef struct {
        uint32_t timestamp;                                     ///< System tick when fault occurred
        uint32_t coreId;                                        ///< Core ID where fault occurred (0 or 1)
        FaultType type;                                         ///< Type of fault
        uint32_t lineNumber;                                    ///< Source code line number
        char fileName[T76_SAFETY_MAX_FILE_NAME_LEN];            ///< Source file name
        char functionName[T76_SAFETY_MAX_FUNCTION_NAME_LEN];    ///< Function name where fault occurred
        char description[T76_SAFETY_MAX_FAULT_DESC_LEN];        ///< Human-readable fault description
        uint32_t taskHandle;                                    ///< FreeRTOS task handle (if applicable)
        char taskName[configMAX_TASK_NAME_LEN];                 ///< FreeRTOS task name (if applicable)
        uint32_t heapFreeBytes;                                 ///< Available heap at time of fault
        uint32_t minHeapFreeBytes;                              ///< Minimum heap free since boot
        bool isInInterrupt;                                     ///< True if fault occurred in interrupt context
        uint32_t interruptNumber;                               ///< Interrupt number (if in interrupt)
        StackInfo stackInfo;                                    ///< Stack information at time of fault
    } FaultInfo;

    /**
     * @brief Shared memory structure for inter-core fault communication
     * 
     * This structure is placed in a shared memory region accessible by both cores.
     * It uses atomic operations and memory barriers to ensure thread safety.
     */
    struct SharedFaultSystem {
        volatile uint32_t magic;                    ///< Magic number for structure validation
        volatile uint32_t version;                  ///< Structure version for compatibility
        FaultInfo lastFaultInfo;                    ///< Information about the last fault
        
        // Reboot limiting and fault history
        volatile uint32_t rebootCount;              ///< Number of consecutive fault-related reboots
        FaultInfo faultHistory[T76_SAFETY_MAX_REBOOTS]; ///< History of faults leading to reboots
        
        // Watchdog management
        volatile bool safetySystemReset;            ///< True if last reset was triggered by safety system
        volatile uint8_t watchdogFailureCore;       ///< Core that caused watchdog timeout (0, 1, or T76_SAFETY_INVALID_CORE_ID=unknown)
    };

    /**
     * @brief Pointer to shared fault system in uninitialized memory
     * 
     * This points to the SharedFaultSystem structure placed in uninitialized
     * memory that persists across system resets, allowing fault information
     * to be preserved for analysis after reboot.
     */
    extern SharedFaultSystem* gSharedFaultSystem;

    /**
     * @brief Raw memory buffer for shared fault system
     * 
     * This buffer is placed in the .uninitialized_data section to ensure
     * it persists across system resets. It's properly aligned for the
     * SharedFaultSystem structure.
     */
    extern uint8_t gSharedMemory[];

    /**
     * @brief Per-core initialization flag
     * 
     * Tracks whether the safety system has been initialized on this core.
     * Prevents multiple initialization and ensures proper setup sequence.
     */
    extern bool gSafetyInitialized;

    /**
     * @brief Inter-core synchronization critical section
     * 
     * Provides thread-safe access to shared memory between both cores.
     * Uses Pico SDK critical section mechanism for reliable multi-core synchronization
     * with automatic interrupt handling.
     */
    extern critical_section_t gSafetyCriticalSection;

    /**
     * @brief Static buffer for file names to avoid stack allocation
     * 
     * Pre-allocated buffer used for file name string operations during
     * fault handling to minimize stack usage in critical error paths.
     */
    extern char gStaticFileName[];

    /**
     * @brief Static buffer for function names to avoid stack allocation
     * 
     * Pre-allocated buffer used for function name string operations during
     * fault handling to minimize stack usage in critical error paths.
     */
    extern char gStaticFunctionName[];

    /**
     * @brief Static buffer for fault descriptions to avoid stack allocation
     * 
     * Pre-allocated buffer used for fault description string operations during
     * fault handling to minimize stack usage in critical error paths.
     */
    extern char gStaticDescription[];

    /**
     * @brief Watchdog initialization state
     * 
     * Tracks whether the Core 1 watchdog has been initialized to prevent
     * multiple initialization attempts.
     */
    extern bool gWatchdogInitialized;

    /**
     * @brief Populate fault info directly in shared memory - minimal stack usage
     * 
     * Central function for capturing comprehensive fault information directly
     * into the shared memory structure. Designed for minimal stack usage by
     * operating directly on global memory without intermediate copies.
     * 
     * Captures all available fault context including:
     * - Basic fault metadata (type, timestamp, core ID, location)
     * - Source code location (file, function, line number)
     * - System state (stack, heap, task information)
     * - Hardware context (interrupt status, core identification)
     * 
     * @param type Fault type classification for categorization
     * @param description Human-readable fault description for debugging
     * @param file Source file name where fault occurred
     * @param line Line number in source file where fault occurred
     * @param function Function name where fault occurred
     * 
     * @note Uses safe string copying to prevent buffer overflows
     * @note Calls helper functions to gather system state information
     * @note Thread-safe through caller's critical section management
     */
    void populateFaultInfo(FaultType type,
                                  const char* description,
                                  const char* file,
                                  uint32_t line,
                                  const char* function);

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
     */
    void reportFault(FaultType type, 
                    const char* description,
                    const char* file,
                    uint32_t line,
                    const char* function);

    /**
     * @brief Clear fault history
     * 
     * This function should be called after successful fault recovery
     * to reset the fault tracking system.
     */
    void clearFaultHistory();



} // namespace T76::Sys::Safety