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
     * @brief Get comprehensive stack information with minimal stack usage
     * @param stackInfo Structure to populate with stack information
     */
    static inline void getStackInfo(StackInfo& stackInfo) {
        // Clear the structure
        memset(&stackInfo, 0, sizeof(StackInfo));
        
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
        stackInfo.isMainStack = (currentSP == mainStackPointer);
        
        // Get stack information based on context
        if (get_core_num() == 0) {
            // Core 0 - FreeRTOS context
            uint32_t ipsr;
            __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
            bool inInterrupt = (ipsr & 0x1FF) != 0;
            
            if (!inInterrupt && !stackInfo.isMainStack) {
                // We're in a FreeRTOS task context using PSP
                TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
                if (currentTask != nullptr) {
                    // Get remaining stack space (high water mark)
                    UBaseType_t remainingWords = uxTaskGetStackHighWaterMark(currentTask);
                    stackInfo.stackRemaining = remainingWords * sizeof(StackType_t);
                    stackInfo.stackHighWaterMark = stackInfo.stackRemaining;
                    
                    // Calculate approximate stack information
                    // We can't easily get the exact stack base without additional FreeRTOS config
                    // so we'll estimate based on typical task stack sizes
                    uint32_t estimatedStackSize = 1024; // Conservative estimate
                    if (stackInfo.stackRemaining < estimatedStackSize) {
                        stackInfo.stackSize = estimatedStackSize;
                        stackInfo.stackUsed = stackInfo.stackSize - stackInfo.stackRemaining;
                    } else {
                        // If remaining > estimated, adjust our estimate
                        stackInfo.stackSize = stackInfo.stackRemaining + 256; // Add some used space estimate
                        stackInfo.stackUsed = 256; // Conservative estimate
                    }
                    
                    // Calculate usage percentage
                    if (stackInfo.stackSize > 0) {
                        stackInfo.stackUsagePercent = (stackInfo.stackUsed * 100) / stackInfo.stackSize;
                        if (stackInfo.stackUsagePercent > 100) stackInfo.stackUsagePercent = 100;
                    }
                    
                    stackInfo.isValidStackInfo = true;
                }
            } else {
                // Interrupt context or main stack - use approximate values
                stackInfo.stackSize = 0x20042000 - currentSP; // Estimated size
                stackInfo.stackUsed = stackInfo.stackSize;
                stackInfo.stackRemaining = 0; // Unknown in interrupt context
                stackInfo.stackUsagePercent = 100; // Conservative estimate
                stackInfo.isValidStackInfo = false; // Limited accuracy
            }
        } else {
            // Core 1 - Bare metal context
            // Use approximate stack information
            stackInfo.stackSize = 0x20042000 - currentSP; // Estimated size
            stackInfo.stackUsed = stackInfo.stackSize;
            stackInfo.stackRemaining = 0; // Unknown in bare metal
            stackInfo.stackUsagePercent = 100; // Conservative estimate
            stackInfo.isValidStackInfo = false; // Limited accuracy on Core 1
        }
    }

    /**
     * @brief Get heap statistics with minimal stack usage
     */
    static inline void getHeapStats(uint32_t& freeBytes, uint32_t& minFreeBytes) {
        if (get_core_num() == 0) {
            // On Core 0, we can use FreeRTOS heap functions
            freeBytes = xPortGetFreeHeapSize();
            minFreeBytes = xPortGetMinimumEverFreeHeapSize();
        } else {
            // On Core 1, set to zero to indicate unavailable
            freeBytes = 0;
            minFreeBytes = 0;
        }
    }

    /**
     * @brief Get task information with minimal stack usage
     */
    static inline void getTaskInfo(uint32_t& taskHandle, char* taskName, size_t taskNameLen) {
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
                    safeStringCopy(taskName, name, taskNameLen);
                }
            }
        }
    }

    /**
     * @brief Populate fault info directly in shared memory - no stack usage
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
        
        // Work directly with shared memory to avoid stack copies
        FaultInfo* info = &gSharedFaultSystem->lastFaultInfo;
        
        // Clear the structure
        memset(info, 0, sizeof(FaultInfo));

        // Fill in basic fault information
        info->timestamp = to_ms_since_boot(get_absolute_time());
        info->coreId = get_core_num();
        info->type = type;
        info->recoveryAction = recoveryAction;
        info->lineNumber = line;

        // Copy strings safely using our minimal function
        safeStringCopy(info->fileName, file, sizeof(info->fileName));
        safeStringCopy(info->functionName, function, sizeof(info->functionName));
        safeStringCopy(info->description, description, sizeof(info->description));

        // Check if we're in an exception/interrupt by examining the IPSR
        uint32_t ipsr;
        __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
        info->isInInterrupt = (ipsr & 0x1FF) != 0;
        info->interruptNumber = info->isInInterrupt ? (ipsr & 0x1FF) : 0;

        // Get heap statistics with minimal overhead
        getHeapStats(info->heapFreeBytes, info->minHeapFreeBytes);

        // Get task information with minimal overhead
        getTaskInfo(info->taskHandle, info->taskName, sizeof(info->taskName));

        // Capture comprehensive stack information
        getStackInfo(info->stackInfo);

        // Update fault count atomically
        if (gSharedFaultSystem && gSafetySpinlock) {
            uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
            info->faultCount = ++gSharedFaultSystem->faultCount;
            spin_unlock(gSafetySpinlock, savedIrq);
        } else {
            info->faultCount = ++gLocalFaultCount;
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
            gSharedFaultSystem->faultCount = 0;
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
