/**
 * @file safety.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 */

#include <cstring>
#include <cstdio>

#include <FreeRTOS.h>
#include <task.h>

#include <pico/stdlib.h>
#include <pico/time.h>
#include <hardware/sync/spin_lock.h>
#include <hardware/watchdog.h>
#include <hardware/irq.h>

#include "safety.hpp"
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
    void populateFaultInfo(FaultType type,
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

} // namespace T76::Sys::Safety

