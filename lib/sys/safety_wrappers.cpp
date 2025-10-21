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

#include "safety.hpp"
#include <cstring>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

// Static buffers for string operations to avoid stack usage
static char gWrapperDescription[T76::Sys::Safety::MAX_FAULT_DESC_LEN];

// ========== C-style wrapper functions ==========

extern "C" {

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
            gWrapperDescription, file, static_cast<uint32_t>(line), func,
            T76::Sys::Safety::RecoveryAction::HALT
        );
    }

    void vApplicationMallocFailedHook(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::MALLOC_FAILED,
            "FreeRTOS malloc failed - insufficient heap memory",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::HALT
        );
    }

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
            gWrapperDescription, __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    // Hardware fault handlers
    void HardFault_Handler(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Hardware fault (HardFault) occurred",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    void MemManage_Handler(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Memory management fault occurred",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    void BusFault_Handler(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Bus fault occurred",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    void UsageFault_Handler(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            "Usage fault occurred",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    // Override the standard assert function to route through our system
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
            gWrapperDescription, file, static_cast<uint32_t>(line), func,
            T76::Sys::Safety::RecoveryAction::HALT
        );
        
        // Never return
        while (true) {
            tight_loop_contents();
        }
    }

    // Override abort() to capture exit-like conditions
    void abort(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::C_ASSERT,
            "Program called abort()",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
        
        // Never return
        while (true) {
            tight_loop_contents();
        }
    }

} // extern "C"