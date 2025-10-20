/**
 * @file safety.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of comprehensive fault handling for the RP2350 platform.
 */

#include "safety.hpp"
#include "safety_monitor.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

// Pico SDK includes
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <pico/platform.h>
#include <pico/time.h>
#include <hardware/exception.h>
#include <hardware/sync.h>
#include <hardware/sync/spin_lock.h>
#include <hardware/watchdog.h>

namespace T76::Sys::Safety {
    // Constants for shared memory magic numbers
    constexpr uint32_t FAULT_SYSTEM_MAGIC = 0x54F3570;    // "SYSTEM" in hex

    /**
     * @brief Shared memory structure for inter-core fault communication
     * 
     * This structure is placed in a shared memory region accessible by both cores.
     * It uses atomic operations and memory barriers to ensure thread safety.
     */
    struct SharedFaultSystem {
        volatile uint32_t magic;                    ///< Magic number for structure validation
        volatile uint32_t version;                  ///< Structure version for compatibility
        volatile uint32_t faultCount;               ///< Total number of faults since boot
        volatile bool isInFaultState;               ///< True if currently processing a fault
        volatile uint32_t lastFaultCore;            ///< Core ID of last fault
        FaultInfo lastFaultInfo;                    ///< Information about the last fault
        uint32_t reserved[9];                       ///< Reserved for future use
    };

    // Place shared fault system in uninitialized RAM for persistence across resets
    static SharedFaultSystem* gSharedFaultSystem = nullptr;
    static uint8_t gSharedMemory[sizeof(SharedFaultSystem)] __attribute__((section(".uninitialized_data"))) __attribute__((aligned(4)));

    // Local fault state for each core
    static bool gSafetyInitialized = false;
    static uint32_t gLocalFaultCount = 0;

    // Pico SDK spinlock instance for inter-core synchronization
    static spin_lock_t* gSafetySpinlock = nullptr;

    /**
     * @brief Get heap statistics
     */
    static void getHeapStats(uint32_t& freeBytes, uint32_t& minFreeBytes) {
        if (get_core_num() == 0) {
            // On Core 0, we can use FreeRTOS heap functions
            freeBytes = xPortGetFreeHeapSize();
            minFreeBytes = xPortGetMinimumEverFreeHeapSize();
        } else {
            // On Core 1, we need to proxy through Core 0 or use approximations
            // For now, set to zero to indicate unavailable
            freeBytes = 0;
            minFreeBytes = 0;
        }
    }

    /**
     * @brief Get task information (if running under FreeRTOS)
     */
    static void getTaskInfo(uint32_t& taskHandle, char* taskName, size_t taskNameLen) {
        taskHandle = 0;
        taskName[0] = '\0';

        // Check if we're in an exception/interrupt by examining the IPSR
        uint32_t ipsr;
        __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
        bool inInterrupt = (ipsr & 0x1FF) != 0;

        if (get_core_num() == 0 && !inInterrupt) {
            // Only available on Core 0 in task context
            TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
            if (currentTask != nullptr) {
                taskHandle = reinterpret_cast<uint32_t>(currentTask);
                const char* name = pcTaskGetName(currentTask);
                if (name != nullptr) {
                    strncpy(taskName, name, taskNameLen - 1);
                    taskName[taskNameLen - 1] = '\0';
                }
            }
        }
    }

    /**
     * @brief Create and populate fault info structure
     */
    static void createFaultInfo(FaultInfo& info, 
                               FaultType type,
                               const char* description,
                               const char* file,
                               uint32_t line,
                               const char* function,
                               RecoveryAction recoveryAction) {
        // Clear the structure
        memset(&info, 0, sizeof(FaultInfo));

        // Fill in basic fault information
        info.timestamp = to_ms_since_boot(get_absolute_time());
        info.coreId = get_core_num();
        info.type = type;
        info.recoveryAction = recoveryAction;
        info.lineNumber = line;

        // Copy strings safely using snprintf (automatically null-terminates and handles bounds)
        snprintf(info.fileName, sizeof(info.fileName), "%s", file ? file : "unknown");
        snprintf(info.functionName, sizeof(info.functionName), "%s", function ? function : "unknown");
        snprintf(info.description, sizeof(info.description), "%s", description ? description : "No description");

        // Check if we're in an exception/interrupt by examining the IPSR
        uint32_t ipsr;
        __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
        info.isInInterrupt = (ipsr & 0x1FF) != 0;
        info.interruptNumber = info.isInInterrupt ? (ipsr & 0x1FF) : 0;

        // Get heap statistics
        getHeapStats(info.heapFreeBytes, info.minHeapFreeBytes);

        // Get task information
        getTaskInfo(info.taskHandle, info.taskName, sizeof(info.taskName));

        // Update fault count
        if (gSharedFaultSystem && gSafetySpinlock) {
            uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
            info.faultCount = ++gSharedFaultSystem->faultCount;
            spin_unlock(gSafetySpinlock, savedIrq);
        } else {
            info.faultCount = ++gLocalFaultCount;
        }
    }

    /**
     * @brief Check for persistent fault information from previous boot
     * 
     * @param fault_info Output parameter to receive fault information if found
     * @return true if a persistent fault was detected, false otherwise
     */
    static bool checkForPersistentFault() {
        if (!gSharedFaultSystem) {
            return false;
        }

        // Check if the shared system is valid and contains fault information
        if (gSharedFaultSystem->magic != FAULT_SYSTEM_MAGIC) {
            return false;
        }

        // Check if we're in a fault state from a previous boot
        if (!gSharedFaultSystem->isInFaultState) {
            return false;
        }

        // Copy the fault information
        return true;
    }

    /**
     * @brief Core fault handling function
     */
    static void handleFault(const FaultInfo& info) {
        // Store fault information in persistent memory before reset
        if (gSharedFaultSystem && gSafetySpinlock) {
            uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
            gSharedFaultSystem->isInFaultState = true;
            gSharedFaultSystem->lastFaultCore = info.coreId;
            gSharedFaultSystem->lastFaultInfo = info;
            spin_unlock(gSafetySpinlock, savedIrq);
        }

        // Give a brief moment for any pending output to complete
        sleep_ms(100);

        // Perform immediate system reset using watchdog
        // This is more reliable than triggering a fault
        watchdog_enable(1, 1);
        while (true) {
            tight_loop_contents();
        }
    }

    // ========== Public API Implementation ==========

    void safetyInit() {
        if (gSafetyInitialized) {
            return; // Already initialized
        }

        // Initialize shared memory on first call from either core
        gSharedFaultSystem = reinterpret_cast<SharedFaultSystem*>(gSharedMemory);
        
        // Initialize Pico SDK spinlock (safe to call multiple times)
        if (gSafetySpinlock == nullptr) {
            gSafetySpinlock = spin_lock_init(PICO_SPINLOCK_ID_OS1);
        }

        // Check if we are responding to a fault condition
        if (checkForPersistentFault()) {
            // Initialize the Safety Monitor - this will check for persistent faults
            // and enter reporting mode if needed (function will not return in that case)
            SafetyMonitor::runSafetyMonitor();
        }

        // Check if already initialized by other core
        if (gSharedFaultSystem->magic != FAULT_SYSTEM_MAGIC) {
            // First initialization
            memset(gSharedFaultSystem, 0, sizeof(SharedFaultSystem));
            gSharedFaultSystem->magic = FAULT_SYSTEM_MAGIC;
            gSharedFaultSystem->version = 1;
            gSharedFaultSystem->faultCount = 0;
            gSharedFaultSystem->isInFaultState = false;
        }

        gSafetyInitialized = true;
    }

    /**
     * @brief Internal function to report faults (used by system hooks)
     */
    void reportFault(FaultType type, 
                     const char* description,
                     const char* file,
                     uint32_t line,
                     const char* function,
                     RecoveryAction recoveryAction) {
        
        FaultInfo info;
        createFaultInfo(info, type, description, file, line, function, recoveryAction);
        handleFault(info);
    }

    bool getLastFault(FaultInfo& faultInfo) {
        if (!gSharedFaultSystem || !gSafetySpinlock) {
            return false;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
        
        if (gSharedFaultSystem->faultCount == 0) {
            spin_unlock(gSafetySpinlock, savedIrq);
            return false;
        }

        faultInfo = gSharedFaultSystem->lastFaultInfo;
        spin_unlock(gSafetySpinlock, savedIrq);
        
        return true;
    }

    uint32_t getFaultCount() {
        if (!gSharedFaultSystem) {
            return gLocalFaultCount;
        }

        // Reading a single 32-bit value is atomic on ARM, no spinlock needed
        return gSharedFaultSystem->faultCount;
    }

    void clearFaultHistory() {
        if (!gSharedFaultSystem || !gSafetySpinlock) {
            gLocalFaultCount = 0;
            return;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
        gSharedFaultSystem->faultCount = 0;
        gSharedFaultSystem->isInFaultState = false;
        memset(&gSharedFaultSystem->lastFaultInfo, 0, sizeof(FaultInfo));
        spin_unlock(gSafetySpinlock, savedIrq);
        gLocalFaultCount = 0;
    }

    bool isInFaultState() {
        if (!gSharedFaultSystem) {
            return false;
        }

        // Reading a single boolean is atomic, no spinlock needed
        return gSharedFaultSystem->isInFaultState;
    }

    const char* faultTypeToString(FaultType type) {
        switch (type) {
            case FaultType::UNKNOWN: return "UNKNOWN";
            case FaultType::FREERTOS_ASSERT: return "FREERTOS_ASSERT";
            case FaultType::STACK_OVERFLOW: return "STACK_OVERFLOW";
            case FaultType::MALLOC_FAILED: return "MALLOC_FAILED";
            case FaultType::C_ASSERT: return "C_ASSERT";
            case FaultType::PICO_HARD_ASSERT: return "PICO_HARD_ASSERT";
            case FaultType::HARDWARE_FAULT: return "HARDWARE_FAULT";
            case FaultType::INTERCORE_FAULT: return "INTERCORE_FAULT";
            case FaultType::MEMORY_CORRUPTION: return "MEMORY_CORRUPTION";
            case FaultType::INVALID_STATE: return "INVALID_STATE";
            case FaultType::RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
            default: return "INVALID";
        }
    }

    const char* recoveryActionToString(RecoveryAction action) {
        switch (action) {
            case RecoveryAction::HALT: return "HALT";
            case RecoveryAction::RESET: return "RESET";
            default: return "INVALID";
        }
    }

    // ========== Safety Monitor Access Functions ==========

    /**
     * @brief Get access to the shared fault system for Safety Monitor
     */
    SharedFaultSystem* getSharedFaultSystem() {
        return gSharedFaultSystem;
    }

    /**
     * @brief Get the fault system magic number for Safety Monitor
     */
    uint32_t getFaultSystemMagic() {
        return FAULT_SYSTEM_MAGIC;
    }

    /**
     * @brief Print fault information to console (public version for Safety Monitor)
     */
    void printFaultInfo() {
        printf("\n" "=== SYSTEM FAULT DETECTED ===\n");
        printf("Timestamp: %lu ms\n", gSharedFaultSystem->lastFaultInfo.timestamp);
        printf("Core: %lu\n", gSharedFaultSystem->lastFaultInfo.coreId);
        printf("Type: %s\n", faultTypeToString(gSharedFaultSystem->lastFaultInfo.type));
        printf("Recovery: %s\n", recoveryActionToString(gSharedFaultSystem->lastFaultInfo.recoveryAction));
        printf("File: %s:%lu\n", gSharedFaultSystem->lastFaultInfo.fileName, gSharedFaultSystem->lastFaultInfo.lineNumber);
        printf("Function: %s\n", gSharedFaultSystem->lastFaultInfo.functionName);
        printf("Description: %s\n", gSharedFaultSystem->lastFaultInfo.description);

        if (gSharedFaultSystem->lastFaultInfo.taskHandle != 0) {
            printf("Task: %s (0x%08lX)\n", gSharedFaultSystem->lastFaultInfo.taskName, gSharedFaultSystem->lastFaultInfo.taskHandle);
        }

        if (gSharedFaultSystem->lastFaultInfo.isInInterrupt) {
            printf("Interrupt Context: %lu\n", gSharedFaultSystem->lastFaultInfo.interruptNumber);
        }

        if (gSharedFaultSystem->lastFaultInfo.heapFreeBytes > 0) {
            printf("Heap Free: %lu bytes\n", gSharedFaultSystem->lastFaultInfo.heapFreeBytes);
            printf("Min Heap Free: %lu bytes\n", gSharedFaultSystem->lastFaultInfo.minHeapFreeBytes);
        }

        printf("Fault Count: %lu\n", gSharedFaultSystem->faultCount);
        printf("==============================\n\n");
    }

} // namespace T76::Sys::Safety
