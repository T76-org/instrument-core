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

} // namespace T76::Sys::Safety