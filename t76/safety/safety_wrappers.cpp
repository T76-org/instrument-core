/**
 * @file safety_wrappers.cpp
 * @brief C wrapper functions for the safety system
 * 
 * This file contains all the C-style wrapper functions that integrate
 * the safety system with FreeRTOS hooks, hardware fault handlers,
 * and standard C library functions.
 * 
 * Optimized for minimal stack usage and static memory allocation.
 */

// Include system headers first to avoid conflicts with abort() macro
#include <cstring>
#include <cstdlib>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

// Include safety.hpp after system headers so abort() macro works correctly
#include "t76/safety.hpp"

/**
 * @brief Static buffer for fault descriptions in wrapper functions
 * 
 * Pre-allocated buffer used by wrapper functions to construct fault
 * descriptions without using stack space. Shared among all wrapper
 * functions since they execute in fault conditions where one leads
 * to system reset.
 */
static char gWrapperDescription[T76_SAFETY_MAX_FAULT_DESC_LEN];

// ========== C-style wrapper functions ==========

extern "C" {

    /**
     * @brief FreeRTOS assertion failure handler
     * 
     * Wrapper function for FreeRTOS configASSERT failures. Constructs
     * a descriptive error message including the failed expression and
     * routes it through the safety system for proper fault handling.
     * 
     * This function is called when FreeRTOS encounters an assertion
     * failure in debug builds. It captures the assertion context and
     * triggers a fault that will result in system reset.
     * 
     * @param file Source file where assertion failed
     * @param line Line number where assertion failed  
     * @param func Function name where assertion failed
     * @param expr The assertion expression that failed
     * 
     * @note Never returns - triggers system reset through safety system
     * @note Uses static buffer to avoid stack allocation in fault path
     */
    void my_assert_func(const char* file, int line, const char* func, const char* expr) {
        // Use static buffer to avoid stack allocation
        const char* desc_start = "FreeRTOS assertion failed: ";
        size_t desc_len = strlen(desc_start);
        size_t remaining = sizeof(gWrapperDescription) - desc_len - 1;
        
        // Manual string concatenation to avoid snprintf stack usage
        strcpy(gWrapperDescription, desc_start);
        if (expr && remaining > 0) {
            strncpy(gWrapperDescription + desc_len, expr, remaining);
            gWrapperDescription[sizeof(gWrapperDescription) - 1] = '\0';
        }
        
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::FREERTOS_ASSERT,
            gWrapperDescription, file, static_cast<uint32_t>(line), func
        );
    }

    /**
     * @brief FreeRTOS heap allocation failure handler
     * 
     * Called by FreeRTOS when heap allocation fails (malloc returns NULL).
     * Indicates the system has run out of heap memory, which is a critical
     * condition requiring immediate attention.
     * 
     * This typically occurs due to:
     * - Memory leaks in application code
     * - Insufficient heap size configuration
     * - Fragmentation of the heap space
     * - Excessive memory allocation by tasks
     * 
     * @note Never returns - triggers system reset through safety system
     * @note Only called on Core 0 where FreeRTOS heap management is active
     */
    void vApplicationMallocFailedHook(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::MALLOC_FAILED,
            "FreeRTOS malloc failed - insufficient heap memory",
            __FILE__, __LINE__, __func__
        );
    }

    /**
     * @brief FreeRTOS stack overflow detection handler
     * 
     * Called by FreeRTOS when stack overflow is detected in a task.
     * This is a critical safety condition that must result in immediate
     * system reset to prevent memory corruption and unpredictable behavior.
     * 
     * Stack overflow can occur due to:
     * - Insufficient stack size allocation for the task
     * - Deep recursion or nested function calls
     * - Large local variables or arrays on the stack
     * - Stack corruption from buffer overflows
     * 
     * @param xTask Handle of the task that overflowed its stack
     * @param pcTaskName Name of the task for identification
     * 
     * @note Never returns - triggers system reset through safety system
     * @note Uses manual string concatenation to avoid additional stack usage
     * @note Ironically, this function must be very careful about its own stack usage
     */
    void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
        // Use static buffer to avoid stack allocation
        const char* desc_start = "Stack overflow detected in task: ";
        size_t desc_len = strlen(desc_start);
        size_t remaining = sizeof(gWrapperDescription) - desc_len - 1;
        
        // Manual string concatenation to avoid snprintf stack usage
        strcpy(gWrapperDescription, desc_start);
        if (pcTaskName && remaining > 0) {
            strncpy(gWrapperDescription + desc_len, pcTaskName, remaining);
            gWrapperDescription[sizeof(gWrapperDescription) - 1] = '\0';
        }
        
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::STACK_OVERFLOW,
            gWrapperDescription, __FILE__, __LINE__, __func__
        );
    }

    /**
     * @brief ARM Cortex-M HardFault exception handler
     * 
     * Handles the most severe type of hardware fault in ARM Cortex-M processors.
     * HardFaults are typically escalated from other fault types when the
     * corresponding fault handler is not enabled or when a fault occurs
     * while already in a fault handler.
     * 
     * Common causes of HardFaults:
     * - Invalid memory access (NULL pointer dereference)
     * - Unaligned memory access
     * - Execution of invalid instructions
     * - Stack pointer corruption
     * - Escalation from other fault types
     * 
     * @note Never returns - triggers immediate system reset
     * @note This handler must be extremely minimal to avoid further faults
     */
    void isr_hardfault(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Hardware fault (HardFault) occurred",
            __FILE__, __LINE__, __func__
        );
    }

    /**
     * @brief ARM Cortex-M Memory Management Unit (MMU) fault handler
     * 
     * Handles memory management faults related to MPU (Memory Protection Unit)
     * violations when the MPU is enabled. These faults occur when code
     * attempts to access memory regions that violate the configured
     * memory protection rules.
     * 
     * Common causes:
     * - Access to memory regions marked as no-access
     * - Execution from non-executable memory regions
     * - Write access to read-only memory regions
     * - MPU region configuration errors
     * 
     * @note Never returns - triggers immediate system reset
     * @note Only occurs on Cortex-M processors with MPU enabled
     */
    void isr_memmanage(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Memory management fault occurred",
            __FILE__, __LINE__, __func__
        );
    }

    /**
     * @brief ARM Cortex-M Bus Fault exception handler
     * 
     * Handles bus faults which occur when the processor attempts to access
     * memory or peripherals that result in an error response from the
     * memory system or bus infrastructure.
     * 
     * Common causes:
     * - Access to invalid memory addresses
     * - Peripheral access when peripheral is disabled/not clocked
     * - Attempts to access memory during DMA transfers
     * - Bus timeout conditions
     * - Memory system errors
     * 
     * @note Never returns - triggers immediate system reset
     * @note Can be precise (exact instruction) or imprecise (delayed)
     */
    void isr_busfault(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Bus fault occurred",
            __FILE__, __LINE__, __func__
        );
    }

    /**
     * @brief ARM Cortex-M Usage Fault exception handler
     * 
     * Handles usage faults which are triggered by the processor when
     * it detects improper instruction usage or execution of undefined
     * instructions. These faults help catch programming errors and
     * invalid instruction sequences.
     * 
     * Common causes:
     * - Execution of undefined instructions
     * - Invalid exception return sequences
     * - Unaligned memory access (when alignment checking is enabled)
     * - Division by zero (when div-by-zero trapping is enabled)
     * - Use of coprocessor instructions when coprocessor is not available
     * 
     * @note Never returns - triggers immediate system reset
     * @note May indicate compiler or toolchain issues
     */
    void isr_usagefault(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Usage fault occurred",
            __FILE__, __LINE__, __func__
        );
    }

    /**
     * @brief Secure fault handler
     * 
     * Handles secure faults in ARM Cortex-M processors with TrustZone-M.
     * Secure faults occur when there are violations of security boundaries
     * between secure and non-secure code or memory regions.
     * 
     */
    void isr_securefault(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Secure fault occurred",
            __FILE__, __LINE__, __func__
        );
    }

    /**
     * @brief Standard C assert() function override
     * 
     * Replaces the standard C library assert() function to route assertion
     * failures through the safety system instead of calling abort() directly.
     * This ensures that assert() failures are properly logged and handled
     * consistently with other system faults.
     * 
     * This function is called when assert() macros fail in debug builds.
     * It captures the assertion expression and context for debugging while
     * ensuring the system fails safely through the safety mechanism.
     * 
     * @param file Source file where assertion failed
     * @param line Line number where assertion failed
     * @param func Function name where assertion failed  
     * @param expr The assertion expression that failed
     * 
     * @note Never returns - triggers system reset through safety system
     * @note Overrides newlib's default __assert_func implementation
     * @note Uses static buffer to avoid stack allocation during fault
     */
    void __assert_func(const char *file, int line, const char *func, const char *expr) {
        // Use static buffer to avoid stack allocation
        const char* desc_start = "Standard assertion failed: ";
        size_t desc_len = strlen(desc_start);
        size_t remaining = sizeof(gWrapperDescription) - desc_len - 1;
        
        // Manual string concatenation to avoid snprintf stack usage
        strcpy(gWrapperDescription, desc_start);
        if (expr && remaining > 0) {
            strncpy(gWrapperDescription + desc_len, expr, remaining);
            gWrapperDescription[sizeof(gWrapperDescription) - 1] = '\0';
        }
        
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::C_ASSERT,
            gWrapperDescription, file, static_cast<uint32_t>(line), func
        );
        
        // Never return
        while (true) {
            tight_loop_contents();
        }
    }

    /**
     * @brief Internal abort implementation with location information
     * 
     * This is the actual abort implementation function that captures file,
     * line, and function information. It should not be called directly -
     * use the abort() macro instead which will automatically capture location.
     * 
     * This function routes abort() calls through the safety system to ensure
     * proper fault logging and consistent handling. Unlike the standard abort()
     * which just terminates execution, this implementation captures diagnostic
     * information before triggering a safe system reset.
     * 
     * @param file Source file where abort was called
     * @param line Line number where abort was called
     * @param func Function name where abort was called
     * 
     * @note Never returns - triggers system reset through safety system
     * @note Do not call directly - use abort() macro which captures location
     * @note This function is compatible with the abort() macro override
     */
    void __t76_abort_impl(const char *file, int line, const char *func) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::C_ASSERT,
            "abort() called",
            file, static_cast<uint32_t>(line), func
        );
        
        // Never return
        while (true) {
            tight_loop_contents();
        }
    }

} // extern "C"


