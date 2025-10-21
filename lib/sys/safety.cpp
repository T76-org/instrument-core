/**
 * @file safety.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of comprehensive fault handling for the RP2350 platform.
 * Optimized for minimal stack usage and static-only memory allocation.
 */

#include "safety.hpp"
#include "safety_monitor.hpp"

// Minimal includes to reduce dependencies and stack usage
#include <cstring>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>

// Pico SDK includes
#include <pico/stdlib.h>
#include <pico/time.h>
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
        volatile bool isInFaultState;               ///< True if currently processing a fault
        volatile uint32_t lastFaultCore;            ///< Core ID of last fault
        FaultInfo lastFaultInfo;                    ///< Information about the last fault
        uint32_t reserved[11];                      ///< Reserved for future use (increased from 9)
    };

    // Place shared fault system in uninitialized RAM for persistence across resets
    static SharedFaultSystem* gSharedFaultSystem = nullptr;
    static uint8_t gSharedMemory[sizeof(SharedFaultSystem)] __attribute__((section(".uninitialized_data"))) __attribute__((aligned(4)));

    // Local fault state for each core
    static bool gSafetyInitialized = false;

    // Pico SDK spinlock instance for inter-core synchronization
    static spin_lock_t* gSafetySpinlock = nullptr;

    // Static buffers for string operations - no stack usage
    static char gStaticFileName[T76_SAFETY_MAX_FILE_NAME_LEN];
    static char gStaticFunctionName[T76_SAFETY_MAX_FUNCTION_NAME_LEN];
    static char gStaticDescription[T76_SAFETY_MAX_FAULT_DESC_LEN];

    /**
     * @brief Minimal string copy function optimized for safety system
     * @param dest Destination buffer
     * @param src Source string (can be null)
     * @param maxLen Maximum length including null terminator
     */
    static inline void safeStringCopy(char* dest, const char* src, size_t maxLen) {
        if (!dest || maxLen == 0) return;
        
        if (!src) {
            dest[0] = '\0';
            return;
        }
        
        size_t i = 0;
        while (i < (maxLen - 1) && src[i] != '\0') {
            dest[i] = src[i];
            i++;
        }
        dest[i] = '\0';
    }

    /**
     * @brief Get comprehensive stack information directly into global fault info
     */
    static inline void getStackInfo() {
        if (!gSharedFaultSystem) return;
        
        // Clear the structure directly in global memory
        memset(&gSharedFaultSystem->lastFaultInfo.stackInfo, 0, sizeof(StackInfo));
        
        uint32_t currentSP;
        uint32_t mainStackPointer;
        uint32_t processStackPointer;
        
        // Get current stack pointer and both MSP/PSP using inline assembly
        __asm volatile (
            "MRS %0, MSP\n\t"           // Get Main Stack Pointer
            "MRS %1, PSP\n\t"           // Get Process Stack Pointer  
            "MOV %2, SP"                // Get current Stack Pointer
            : "=r" (mainStackPointer), "=r" (processStackPointer), "=r" (currentSP)
            :
            : "memory"
        );
        
        // Determine which stack we're using
        gSharedFaultSystem->lastFaultInfo.stackInfo.isMainStack = (currentSP == mainStackPointer);
        
        // Get stack information based on context
        if (get_core_num() == 0) {
            // Core 0 - FreeRTOS context
            uint32_t ipsr;
            __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
            bool inInterrupt = (ipsr & 0x1FF) != 0;
            
            if (!inInterrupt && !gSharedFaultSystem->lastFaultInfo.stackInfo.isMainStack) {
                // We're in a FreeRTOS task context using PSP
                TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
                if (currentTask != nullptr) {
                    // Get remaining stack space (high water mark)
                    UBaseType_t remainingWords = uxTaskGetStackHighWaterMark(currentTask);
                    gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining = remainingWords * sizeof(StackType_t);
                    gSharedFaultSystem->lastFaultInfo.stackInfo.stackHighWaterMark = gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining;
                    
                    // Calculate approximate stack information
                    uint32_t estimatedStackSize = 1024; // Conservative estimate
                    if (gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining < estimatedStackSize) {
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize = estimatedStackSize;
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed = gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize - gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining;
                    } else {
                        // If remaining > estimated, adjust our estimate
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize = gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining + 256; // Add some used space estimate
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed = 256; // Conservative estimate
                    }
                    
                    // Calculate usage percentage
                    if (gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize > 0) {
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsagePercent = (gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed * 100) / gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize;
                        if (gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsagePercent > 100) gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsagePercent = 100;
                    }
                    
                    gSharedFaultSystem->lastFaultInfo.stackInfo.isValidStackInfo = true;
                }
            } else {
                // Interrupt context or main stack - use approximate values
                gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize = 0x20042000 - currentSP; // Estimated size
                gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed = gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize;
                gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining = 0; // Unknown in interrupt context
                gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsagePercent = 100; // Conservative estimate
                gSharedFaultSystem->lastFaultInfo.stackInfo.isValidStackInfo = false; // Limited accuracy
            }
        } else {
            // Core 1 - Bare metal context
            // Use approximate stack information
            gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize = 0x20042000 - currentSP; // Estimated size
            gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed = gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize;
            gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining = 0; // Unknown in bare metal
            gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsagePercent = 100; // Conservative estimate
            gSharedFaultSystem->lastFaultInfo.stackInfo.isValidStackInfo = false; // Limited accuracy on Core 1
        }
    }

    /**
     * @brief Get heap statistics directly into global fault info
     */
    static inline void getHeapStats() {
        if (!gSharedFaultSystem) return;
        
        if (get_core_num() == 0) {
            // On Core 0, we can use FreeRTOS heap functions
            gSharedFaultSystem->lastFaultInfo.heapFreeBytes = xPortGetFreeHeapSize();
            gSharedFaultSystem->lastFaultInfo.minHeapFreeBytes = xPortGetMinimumEverFreeHeapSize();
        } else {
            // On Core 1, set to zero to indicate unavailable
            gSharedFaultSystem->lastFaultInfo.heapFreeBytes = 0;
            gSharedFaultSystem->lastFaultInfo.minHeapFreeBytes = 0;
        }
    }

    /**
     * @brief Get task information directly into global fault info
     */
    static inline void getTaskInfo() {
        if (!gSharedFaultSystem) return;
        
        gSharedFaultSystem->lastFaultInfo.taskHandle = 0;
        gSharedFaultSystem->lastFaultInfo.taskName[0] = '\0';

        // Check if we're in an exception/interrupt by examining the IPSR
        uint32_t ipsr;
        __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
        bool inInterrupt = (ipsr & 0x1FF) != 0;

        if (get_core_num() == 0 && !inInterrupt) {
            // Only available on Core 0 in task context
            TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
            if (currentTask != nullptr) {
                gSharedFaultSystem->lastFaultInfo.taskHandle = reinterpret_cast<uint32_t>(currentTask);
                const char* name = pcTaskGetName(currentTask);
                if (name != nullptr) {
                    safeStringCopy(gSharedFaultSystem->lastFaultInfo.taskName, name, sizeof(gSharedFaultSystem->lastFaultInfo.taskName));
                }
            }
        }
    }

    /**
     * @brief Populate fault info directly in shared memory - minimal stack usage
     * @param type Fault type
     * @param description Fault description
     * @param file Source file name
     * @param line Line number
     * @param function Function name
     * @param recoveryAction Recovery action to take
     */
    static inline void populateFaultInfo(FaultType type,
                                        const char* description,
                                        const char* file,
                                        uint32_t line,
                                        const char* function,
                                        RecoveryAction recoveryAction) {
        
        if (!gSharedFaultSystem) return;
        
        // Clear the structure directly in global memory
        memset(&gSharedFaultSystem->lastFaultInfo, 0, sizeof(FaultInfo));

        // Fill in basic fault information directly
        gSharedFaultSystem->lastFaultInfo.timestamp = to_ms_since_boot(get_absolute_time());
        gSharedFaultSystem->lastFaultInfo.coreId = get_core_num();
        gSharedFaultSystem->lastFaultInfo.type = type;
        gSharedFaultSystem->lastFaultInfo.recoveryAction = recoveryAction;
        gSharedFaultSystem->lastFaultInfo.lineNumber = line;

        // Copy strings safely using our minimal function
        safeStringCopy(gSharedFaultSystem->lastFaultInfo.fileName, file, sizeof(gSharedFaultSystem->lastFaultInfo.fileName));
        safeStringCopy(gSharedFaultSystem->lastFaultInfo.functionName, function, sizeof(gSharedFaultSystem->lastFaultInfo.functionName));
        safeStringCopy(gSharedFaultSystem->lastFaultInfo.description, description, sizeof(gSharedFaultSystem->lastFaultInfo.description));

        // Check if we're in an exception/interrupt by examining the IPSR
        uint32_t ipsr;
        __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
        gSharedFaultSystem->lastFaultInfo.isInInterrupt = (ipsr & 0x1FF) != 0;
        gSharedFaultSystem->lastFaultInfo.interruptNumber = gSharedFaultSystem->lastFaultInfo.isInInterrupt ? (ipsr & 0x1FF) : 0;

        // Get heap statistics directly into global structure
        getHeapStats();

        // Get task information directly into global structure  
        getTaskInfo();

        // Capture comprehensive stack information directly into global structure
        getStackInfo();
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
     * @brief Core fault handling function - minimal stack usage
     */
    static inline void handleFault() {
        // Mark system as being in fault state
        if (gSharedFaultSystem && gSafetySpinlock) {
            uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
            gSharedFaultSystem->isInFaultState = true;
            gSharedFaultSystem->lastFaultCore = get_core_num();
            spin_unlock(gSafetySpinlock, savedIrq);
        }

        // Give a brief moment for any pending output to complete
        // Use busy wait to avoid function call overhead
        uint32_t start_time = to_ms_since_boot(get_absolute_time());
        while ((to_ms_since_boot(get_absolute_time()) - start_time) < 100) {
            tight_loop_contents();
        }

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
            gSharedFaultSystem->isInFaultState = false;
        }

        gSafetyInitialized = true;
    }

    /**
     * @brief Internal function to report faults (used by system hooks)
     * Optimized for minimal stack usage and direct operation
     */
    void reportFault(FaultType type, 
                     const char* description,
                     const char* file,
                     uint32_t line,
                     const char* function,
                     RecoveryAction recoveryAction) {
        
        // Ensure shared memory is available
        if (!gSharedFaultSystem) {
            // If no shared memory, just reset immediately
            watchdog_enable(1, 1);
            while (true) {
                tight_loop_contents();
            }
        }

        // Populate fault information directly in shared memory
        populateFaultInfo(type, description, file, line, function, recoveryAction);
        
        // Handle fault with minimal overhead
        handleFault();
    }

    bool getLastFault(FaultInfo* faultInfo) {
        if (!faultInfo || !gSharedFaultSystem || !gSafetySpinlock) {
            return false;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
        
        // Check if we're in a fault state (indicates fault info is available)
        if (!gSharedFaultSystem->isInFaultState) {
            spin_unlock(gSafetySpinlock, savedIrq);
            return false;
        }

        *faultInfo = gSharedFaultSystem->lastFaultInfo;
        spin_unlock(gSafetySpinlock, savedIrq);
        
        return true;
    }

    void clearFaultHistory() {
        if (!gSharedFaultSystem || !gSafetySpinlock) {
            return;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
        gSharedFaultSystem->isInFaultState = false;
        memset(&gSharedFaultSystem->lastFaultInfo, 0, sizeof(FaultInfo));
        spin_unlock(gSafetySpinlock, savedIrq);
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
     * @brief Print fault information to console (for Safety Monitor use only)
     * This function is only called from the Safety Monitor which has printf available
     */
    void printFaultInfo() {
        // This function is intentionally left for the Safety Monitor to implement
        // The Safety Monitor will include <cstdio> and implement its own printing
        // We cannot include printf here as it increases stack usage significantly
        
        // The Safety Monitor will access gSharedFaultSystem->lastFaultInfo directly
        // and use its own printf implementation
    }

    /**
     * @brief Test function to trigger a controlled fault with stack capture
     */
    void testStackCapture() {
        // This function creates a deliberate stack overflow to test stack capture
        // It's designed to be called from application code for testing purposes
        
        // Create some local variables to put data on the stack
        volatile uint32_t testArray[64]; // 256 bytes on stack
        for (int i = 0; i < 64; i++) {
            testArray[i] = 0xDEADBEEF + i;
        }
        
        // Report a test fault
        reportFault(
            FaultType::INVALID_STATE,
            "Test fault for stack capture validation",
            __FILE__,
            __LINE__,
            __func__,
            RecoveryAction::RESET
        );
        
        // This point should never be reached as reportFault causes a reset
        while (true) {
            tight_loop_contents();
        }
    }

} // namespace T76::Sys::Safety
