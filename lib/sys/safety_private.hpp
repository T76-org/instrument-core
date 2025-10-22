/**
 * @brief Magic number for fault system structure validation
 * 
 * This constant is used to validate that the shared memory structure
 * has been properly initialized and is not corrupted.
 */

#pragma once

#include "safety.hpp"


#define FAULT_SYSTEM_MAGIC 0x54F3570


namespace T76::Sys::Safety {

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
     * @brief Inter-core synchronization spinlock
     * 
     * Provides thread-safe access to shared memory between both cores.
     * Uses Pico SDK spinlock mechanism for reliable multi-core synchronization.
     */
    extern spin_lock_t* gSafetySpinlock;

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
     * @note Thread-safe through caller's spinlock management
     */
    void populateFaultInfo(FaultType type,
                                  const char* description,
                                  const char* file,
                                  uint32_t line,
                                  const char* function);

    /**
     * @brief Execute all registered safing functions before system reset
     * 
     * Safely executes all registered safing functions to put the system
     * into a safe state before reset. Uses a local copy approach to
     * minimize spinlock hold time while ensuring thread safety.
     * 
     * Process:
     * 1. Quickly copy function pointers from shared memory to local array
     * 2. Release spinlock to minimize interference with other operations
     * 3. Execute each function sequentially in registration order
     * 4. Count successful executions for potential debugging
     * 
     * @return Number of safing functions that were successfully executed
     * 
     * @note Does not handle exceptions - relies on safing functions being fault-tolerant
     * @note Executes all functions even if one fails (no early termination)
     * @note Uses minimal stack by avoiding dynamic allocations
     */
    uint32_t executeSafingFunctions();

} // namespace T76::Sys::Safety