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
#include <cstdio>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>

// Pico SDK includes
#include <pico/stdlib.h>
#include <pico/time.h>
#include <hardware/sync/spin_lock.h>
#include <hardware/watchdog.h>
#include <hardware/irq.h>

#include "safety_private.hpp"

namespace T76::Sys::Safety {

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
     * 
     * Captures detailed stack usage information at the time of fault, including:
     * - Stack pointer position and type (Main/Process stack)
     * - Stack usage percentage and remaining space
     * - High water mark for stack monitoring
     * 
     * Works on both cores but has different accuracy levels:
     * - Core 0: Full accuracy when in task context with FreeRTOS
     * - Core 1: Estimated values based on current stack pointer
     * - Interrupt context: Limited accuracy with estimation
     * 
     * Stack usage is calculated to help identify stack overflow conditions
     * and optimize stack allocation in the system.
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
     * 
     * Captures current heap usage information including:
     * - Free heap bytes available at time of fault
     * - Minimum free heap bytes since system boot (high water mark)
     * 
     * Only available on Core 0 where FreeRTOS heap management is active.
     * Core 1 running bare-metal code will show zero values as it doesn't
     * use the FreeRTOS heap manager.
     * 
     * This information helps identify memory leaks and heap exhaustion
     * conditions that may lead to system faults.
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
     * 
     * Captures FreeRTOS task context information when available:
     * - Task handle (unique identifier for the running task)
     * - Task name for identification and debugging
     * - Interrupt context detection via IPSR register
     * - Interrupt number if fault occurred in interrupt handler
     * 
     * Only available on Core 0 in task context. Core 1 (bare-metal) and
     * interrupt contexts will show default values. Interrupt detection
     * works on both cores by examining the ARM Cortex-M IPSR register.
     * 
     * This information is crucial for identifying which task or interrupt
     * was active when the fault occurred, enabling targeted debugging.
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
    static inline void populateFaultInfo(FaultType type,
                                        const char* description,
                                        const char* file,
                                        uint32_t line,
                                        const char* function) {
        
        if (!gSharedFaultSystem) return;
        
        // Clear the structure directly in global memory
        memset(&gSharedFaultSystem->lastFaultInfo, 0, sizeof(FaultInfo));

        // Fill in basic fault information directly
        gSharedFaultSystem->lastFaultInfo.timestamp = to_ms_since_boot(get_absolute_time());
        gSharedFaultSystem->lastFaultInfo.coreId = get_core_num();
        gSharedFaultSystem->lastFaultInfo.type = type;
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
    uint32_t executeSafingFunctions() {
        if (!gSharedFaultSystem || !gSafetySpinlock) {
            return 0;
        }

        uint32_t executedCount = 0;
        uint32_t safingCount;
        SafingFunction functions[T76_SAFETY_MAX_SAFING_FUNCTIONS];

        // Copy function pointers to local array to minimize spinlock time
        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
        safingCount = gSharedFaultSystem->safingFunctionCount;
        for (uint32_t i = 0; i < safingCount; i++) {
            functions[i] = gSharedFaultSystem->safingFunctions[i];
        }
        spin_unlock(gSafetySpinlock, savedIrq);

        // Execute each safing function
        for (uint32_t i = 0; i < safingCount; i++) {
            if (functions[i] != nullptr) {
                // Call the safing function
                functions[i]();
                executedCount++;
            }
        }

        return executedCount;
    }

    /**
     * @brief Core fault handling function - minimal stack usage
     * 
     * Final stage of fault processing that prepares the system for recovery.
     * Designed for maximum reliability with minimal stack and resource usage.
     * 
     * Sequence of operations:
     * 1. Mark system as being in fault state (for persistent tracking)
     * 2. Set safety system reset flag to distinguish from watchdog timeout
     * 3. Execute all registered safing functions to put system in safe state
     * 4. Allow brief time for pending output to complete
     * 5. Trigger immediate system reset via watchdog
     * 
     * This function never returns - it always results in system reset.
     * The fault information will persist in uninitialized memory for
     * analysis by the Safety Monitor on the next boot.
     * 
     * @note Uses busy-wait loops to avoid additional function call overhead
     * @note Watchdog reset is more reliable than software reset mechanisms
     * @note Thread-safe through spinlock protection of shared state
     */
    static inline void handleFault() {
        // Mark system as being in fault state
        if (gSharedFaultSystem && gSafetySpinlock) {
            uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
            gSharedFaultSystem->isInFaultState = true;
            gSharedFaultSystem->lastFaultCore = get_core_num();
            gSharedFaultSystem->safetySystemReset = true; // Mark as safety system reset
            
            // Store current fault in fault history
            if (gSharedFaultSystem->rebootCount < T76_SAFETY_MAX_REBOOTS) {
                uint32_t index = gSharedFaultSystem->rebootCount;
                // Copy the entire fault info structure to history
                gSharedFaultSystem->faultHistory[index] = gSharedFaultSystem->lastFaultInfo;
                gSharedFaultSystem->rebootCount++;
            }
            
            spin_unlock(gSafetySpinlock, savedIrq);
        }

        // Execute safing functions before recovery action
        // This puts the system into a safe state before restart/halt
        executeSafingFunctions();

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

        // Store watchdog reboot status for later processing
        bool wasWatchdogReboot = watchdog_caused_reboot();
        bool isFirstBoot = false;

        // Check if already initialized by other core
        if (gSharedFaultSystem->magic != FAULT_SYSTEM_MAGIC) {
            // First initialization
            isFirstBoot = true;
            memset(gSharedFaultSystem, 0, sizeof(SharedFaultSystem));
            gSharedFaultSystem->magic = FAULT_SYSTEM_MAGIC;
            gSharedFaultSystem->version = 1;
            gSharedFaultSystem->isInFaultState = false;
            gSharedFaultSystem->safingFunctionCount = 0;
            gSharedFaultSystem->rebootCount = 0; // No faults yet
            gSharedFaultSystem->lastBootTimestamp = to_ms_since_boot(get_absolute_time());
            gSharedFaultSystem->safetySystemReset = false;
            
            // Initialize safing function array to null pointers
            for (uint32_t i = 0; i < T76_SAFETY_MAX_SAFING_FUNCTIONS; i++) {
                gSharedFaultSystem->safingFunctions[i] = nullptr;
            }
        }

        // Only check for watchdog reboot if this is NOT the first boot
        // On first boot, we can't trust the previous state information
        if (wasWatchdogReboot && !isFirstBoot) {
            // Check if last reboot was caused by watchdog timeout (not safety system reset)
            if (!gSharedFaultSystem->safetySystemReset) {
                // This was a genuine watchdog timeout, not a safety system reset
                // Create a watchdog timeout fault entry
                populateFaultInfo(FaultType::WATCHDOG_TIMEOUT, 
                                "Hardware watchdog timeout - Core 1 may have hung",
                                "system", 0, "watchdog");
                gSharedFaultSystem->isInFaultState = true;
                gSharedFaultSystem->lastFaultCore = 1; // Assume Core 1 since it's the one being protected
                
                // Manually add to fault history (like reportFault does but without immediate reset)
                if (gSharedFaultSystem->rebootCount < T76_SAFETY_MAX_REBOOTS) {
                    uint32_t index = gSharedFaultSystem->rebootCount;
                    gSharedFaultSystem->faultHistory[index] = gSharedFaultSystem->lastFaultInfo;
                    gSharedFaultSystem->rebootCount++;
                }
            }
        }

        // Clear the safety system reset flag for next boot
        gSharedFaultSystem->safetySystemReset = false;
        
        // Check reboot count and handle safety monitor
        if (gSharedFaultSystem->rebootCount >= T76_SAFETY_MAX_REBOOTS) {
            // Too many consecutive reboots - enter safety monitor to display fault history
            SafetyMonitor::runSafetyMonitor();
        }
        
        gSharedFaultSystem->lastBootTimestamp = to_ms_since_boot(get_absolute_time());

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
                     const char* function) {
        
        // Ensure shared memory is available
        if (!gSharedFaultSystem) {
            // If no shared memory, just reset immediately
            watchdog_enable(1, 1);
            while (true) {
                tight_loop_contents();
            }
        }

        // Populate fault information directly in shared memory
        populateFaultInfo(type, description, file, line, function);
        
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

    /**
     * @brief Reset the consecutive reboot counter
     * 
     * This function should be called by the application after successful
     * initialization or operation to reset the consecutive reboot counter.
     * This prevents the system from entering safety monitor mode due to
     * a series of unrelated reboots.
     * 
     * @details The function:
     * - Resets rebootCount to 0
     * - Clears the fault history array
     * - Updates the last boot timestamp
     * 
     * @note Thread-safe through spinlock protection
     * @note Should be called after successful system operation/initialization
     */
    void resetRebootCounter() {
        if (!gSharedFaultSystem || !gSafetySpinlock) {
            return;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);
        
        // Reset reboot count and clear fault history
        gSharedFaultSystem->rebootCount = 0;
        memset(gSharedFaultSystem->faultHistory, 0, sizeof(gSharedFaultSystem->faultHistory));
        gSharedFaultSystem->lastBootTimestamp = to_ms_since_boot(get_absolute_time());
        
        spin_unlock(gSafetySpinlock, savedIrq);
    }

    bool isInFaultState() {
        if (!gSharedFaultSystem) {
            return false;
        }

        // Reading a single boolean is atomic, no spinlock needed
        return gSharedFaultSystem->isInFaultState;
    }

    SafingResult registerSafingFunction(SafingFunction safingFunc) {
        if (!safingFunc || !gSharedFaultSystem || !gSafetySpinlock) {
            return SafingResult::INVALID_PARAM;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);

        // Check if table is full
        if (gSharedFaultSystem->safingFunctionCount >= T76_SAFETY_MAX_SAFING_FUNCTIONS) {
            spin_unlock(gSafetySpinlock, savedIrq);
            return SafingResult::FULL;
        }

        // Check if function is already registered
        for (uint32_t i = 0; i < gSharedFaultSystem->safingFunctionCount; i++) {
            if (gSharedFaultSystem->safingFunctions[i] == safingFunc) {
                spin_unlock(gSafetySpinlock, savedIrq);
                return SafingResult::SUCCESS; // Already registered, treat as success
            }
        }

        // Add the function to the table
        gSharedFaultSystem->safingFunctions[gSharedFaultSystem->safingFunctionCount] = safingFunc;
        gSharedFaultSystem->safingFunctionCount++;

        spin_unlock(gSafetySpinlock, savedIrq);
        return SafingResult::SUCCESS;
    }

    SafingResult deregisterSafingFunction(SafingFunction safingFunc) {
        if (!safingFunc || !gSharedFaultSystem || !gSafetySpinlock) {
            return SafingResult::INVALID_PARAM;
        }

        uint32_t savedIrq = spin_lock_blocking(gSafetySpinlock);

        // Find the function in the table
        uint32_t foundIndex = T76_SAFETY_MAX_SAFING_FUNCTIONS; // Invalid index
        for (uint32_t i = 0; i < gSharedFaultSystem->safingFunctionCount; i++) {
            if (gSharedFaultSystem->safingFunctions[i] == safingFunc) {
                foundIndex = i;
                break;
            }
        }

        if (foundIndex >= T76_SAFETY_MAX_SAFING_FUNCTIONS) {
            spin_unlock(gSafetySpinlock, savedIrq);
            return SafingResult::NOT_FOUND;
        }

        // Shift remaining functions down to fill the gap
        for (uint32_t i = foundIndex; i < gSharedFaultSystem->safingFunctionCount - 1; i++) {
            gSharedFaultSystem->safingFunctions[i] = gSharedFaultSystem->safingFunctions[i + 1];
        }

        // Clear the last entry and decrement count
        gSharedFaultSystem->safingFunctions[gSharedFaultSystem->safingFunctionCount - 1] = nullptr;
        gSharedFaultSystem->safingFunctionCount--;

        spin_unlock(gSafetySpinlock, savedIrq);
        return SafingResult::SUCCESS;
    }

    bool initCore1Watchdog() {
        // Only allow initialization on Core 1
        if (get_core_num() != 1) {
            return false;
        }

        // Prevent multiple initialization
        if (gWatchdogInitialized) {
            return true; // Already initialized
        }

        // Initialize hardware watchdog with configured timeout
        watchdog_enable(T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS, 1);

        gWatchdogInitialized = true;
        return true;
    }

    void feedWatchdog() {
        if (gWatchdogInitialized) {
            watchdog_update();
        }
    }

} // namespace T76::Sys::Safety
