/**
 * @file safety_print.cpp
 * @brief Fault information collection and safe string handling for the safety system.
 *
 * Provides utilities for:
 * - Safe string copying with bounds checks
 * - Stack info analysis via ARM registers and FreeRTOS APIs
 * - Heap statistics collection (Core 0)
 * - Task/interrupt context capture
 * - Populating fault data in shared memory
 */

#include <cstring>
#include <cstdio>

#include <FreeRTOS.h>
#include <task.h>

#include <pico/stdlib.h>
#include <pico/time.h>
#include <hardware/watchdog.h>

#include "safety.hpp"
#include "safety_private.hpp"


namespace T76::Sys::Safety {

    /**
     * @brief Minimal string copy function optimized for safety system
     * 
     * Safely copies strings with bounds checking and null pointer handling,
     * optimized for use in fault handling contexts where reliability is
     * critical and stack usage must be minimized.
     * 
     * Features:
     * - Null pointer safe (handles null src and dest gracefully)
     * - Always null-terminates destination buffer
     * - Respects maximum buffer length to prevent overflows
     * - Optimized for minimal stack usage and reliability
     * 
     * @param dest Destination buffer (must be valid and have space for maxLen chars)
     * @param src Source string (can be null - results in empty string)
     * @param maxLen Maximum length including null terminator
     * 
     * @note Uses character-by-character copy to avoid library dependencies
     * @note Always ensures null termination even on truncation
     * @note Safe to call with null src pointer (results in empty dest string)
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
     * Captures detailed stack usage information at the time of fault by analyzing
     * ARM Cortex-M stack pointers and FreeRTOS task information when available.
     * Operates directly on shared memory to minimize stack usage during fault handling.
     * 
     * Information captured:
     * - Stack pointer position and type (Main Stack Pointer vs Process Stack Pointer)
     * - Stack usage percentage and remaining space
     * - High water mark for stack monitoring (FreeRTOS tasks)
     * - Stack type identification (main vs task stack)
     * - Accuracy indicators based on execution context
     * 
     * Accuracy levels by context:
     * - Core 0 + FreeRTOS task: High accuracy with FreeRTOS APIs
     * - Core 0 + interrupt context: Estimated values, marked as low accuracy
     * - Core 1 + bare metal: Estimated values based on current SP
     * 
     * Stack usage calculation helps identify stack overflow conditions,
     * optimize stack allocation, and debug memory-related faults.
     * 
     * @note Uses inline assembly to read ARM Cortex-M MSP, PSP, and SP registers
     * @note Operates directly on global shared memory structure
     * @note Conservative estimates used when exact information unavailable
     * @note Handles both Main Stack (MSP) and Process Stack (PSP) contexts
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
                    uint32_t estimatedStackSize = T76_SAFETY_DEFAULT_STACK_ESTIMATE; // Conservative estimate
                    if (gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining < estimatedStackSize) {
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize = estimatedStackSize;
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed = gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize - gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining;
                    } else {
                        // If remaining > estimated, adjust our estimate
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize = gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining + T76_SAFETY_CONSERVATIVE_STACK_USED_ESTIMATE; // Add some used space estimate
                        gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed = T76_SAFETY_CONSERVATIVE_STACK_USED_ESTIMATE; // Conservative estimate
                    }
                    
                    gSharedFaultSystem->lastFaultInfo.stackInfo.isValidStackInfo = true;
                }
            } else {
                // Interrupt context or main stack - use approximate values
                gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize = T76_SAFETY_ESTIMATED_MAIN_STACK_BASE - currentSP; // Estimated size
                gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed = gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize;
                gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining = 0; // Unknown in interrupt context
                gSharedFaultSystem->lastFaultInfo.stackInfo.isValidStackInfo = false; // Limited accuracy
            }
        } else {
            // Core 1 - Bare metal context
            // Use approximate stack information
            gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize = T76_SAFETY_ESTIMATED_MAIN_STACK_BASE - currentSP; // Estimated size
            gSharedFaultSystem->lastFaultInfo.stackInfo.stackUsed = gSharedFaultSystem->lastFaultInfo.stackInfo.stackSize;
            gSharedFaultSystem->lastFaultInfo.stackInfo.stackRemaining = 0; // Unknown in bare metal
            gSharedFaultSystem->lastFaultInfo.stackInfo.isValidStackInfo = false; // Limited accuracy on Core 1
        }
    }

    /**
     * @brief Get heap statistics directly into global fault info
     * 
     * Captures current heap usage information using FreeRTOS heap management
     * APIs when available. Operates directly on shared memory to minimize
     * stack usage during fault handling.
     * 
     * Information captured:
     * - Free heap bytes available at time of fault
     * - Minimum free heap bytes since system boot (high water mark)
     * - Zero values when heap management not available
     * 
     * Availability by core:
     * - Core 0: Full heap statistics via FreeRTOS APIs
     * - Core 1: Zero values (bare-metal, no heap manager)
     * 
     * This information helps identify:
     * - Memory leaks (decreasing free heap over time)
     * - Heap exhaustion conditions leading to malloc failures
     * - Memory usage patterns and optimization opportunities
     * - Correlation between memory pressure and system faults
     * conditions that may lead to system faults.
     * 
     * @note Only functional on Core 0 where FreeRTOS heap manager is active
     * @note Operates directly on global shared memory structure
     * @note Zero values indicate heap information not available (Core 1)
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
     * Captures FreeRTOS task context and interrupt information when available,
     * using ARM Cortex-M IPSR register and FreeRTOS APIs. Operates directly
     * on shared memory to minimize stack usage during fault handling.
     * 
     * Information captured:
     * - Task handle (unique identifier for the running task)
     * - Task name for identification and debugging
     * - Interrupt context detection via IPSR register examination
     * - Interrupt number if fault occurred in interrupt handler
     * - Default values when information not available
     * 
     * Availability by context:
     * - Core 0 + FreeRTOS task: Full task information available
     * - Core 0 + interrupt: Interrupt number, no task info
     * - Core 1 + bare metal: Default values (no task system)
     * - Any core + interrupt: Interrupt detection works via IPSR
     * 
     * This information is crucial for:
     * - Identifying which task was active when fault occurred
     * - Determining if fault happened in interrupt vs task context
     * - Enabling targeted debugging and root cause analysis
     * - Understanding system execution context at fault time
     * 
     * @note Uses ARM Cortex-M IPSR register for interrupt detection
     * @note Operates directly on global shared memory structure
     * @note Safe string copying prevents buffer overflows
     * @note Works on both cores with different levels of information
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
     * @note Thread-safe through caller's critical section management
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

