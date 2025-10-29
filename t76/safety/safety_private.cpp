/**
 * @file safety_private.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Global variable definitions for the safety system.
 * 
 * This file contains the definitions of all global variables used by the safety
 * system, including shared memory structures, synchronization primitives, and
 * static buffers for fault handling.
 * 
 * Key global variables:
 * - Shared fault system structure and raw memory buffer
 * - Inter-core synchronization spinlock
 * - Static string buffers for minimal stack usage
 * - Initialization and state tracking flags
 * 
 * All variables are placed in appropriate memory sections and properly aligned
 * for multi-core access and persistence across system resets.
 */

#include "safety_private.hpp"

namespace T76::Sys::Safety { 

    /**
     * @brief Pointer to shared fault system structure in persistent memory
     * 
     * Points to the SharedFaultSystem structure located in uninitialized memory
     * that persists across system resets. Initialized during init() to
     * point to the gSharedMemory buffer cast to the proper structure type.
     */
    SharedFaultSystem* gSharedFaultSystem = nullptr;

    /**
     * @brief Raw memory buffer for persistent fault system data
     * 
     * Pre-allocated buffer placed in .uninitialized_data section to ensure
     * fault information survives system resets. Properly aligned for the
     * SharedFaultSystem structure and sized to contain all fault tracking data.
     */
    uint8_t gSharedMemory[sizeof(SharedFaultSystem)] __attribute__((section(".uninitialized_data"))) __attribute__((aligned(4)));

    /**
     * @brief Per-core safety system initialization flag
     * 
     * Tracks whether the safety system has been initialized on this core
     * to prevent multiple initialization attempts and ensure proper setup
     * sequence. Set to true after successful init() completion.
     */
    bool gSafetyInitialized = false;

    /**
     * @brief Inter-core synchronization critical section for shared memory access
     * 
     * Provides thread-safe access to shared memory structures between both
     * cores of the RP2350. Initialized during init() using Pico SDK
     * critical section mechanism for reliable multi-core synchronization
     * with automatic interrupt handling.
     */
    critical_section_t gSafetyCriticalSection;

    /**
     * @brief Core 1 watchdog initialization status flag
     * 
     * Tracks whether the hardware watchdog has been initialized for Core 1
     * protection to prevent multiple initialization attempts. Set to true
     * after successful initCore1Watchdog() completion.
     */
    bool gWatchdogInitialized = false;

}