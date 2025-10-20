/**
 * @file safety_wrappers.cpp
 * @brief C wrapper functions for the safety system
 * 
 * This file contains all the C-style wrapper functions that integrate
 * the safety system with FreeRTOS hooks, hardware fault handlers,
 * and standard C library functions.
 */

#include "safety.hpp"
#include <cstdio>
#include <cstring>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

// ========== C-style wrapper functions ==========

extern "C" {

    void my_assert_func(const char* file, int line, const char* func, const char* expr) {
        char description[T76::Sys::Safety::MAX_FAULT_DESC_LEN];
        snprintf(description, sizeof(description), "FreeRTOS assertion failed: %s", expr ? expr : "unknown");
        
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::FREERTOS_ASSERT,
            T76::Sys::Safety::FaultSeverity::FATAL,
            description, file, static_cast<uint32_t>(line), func,
            T76::Sys::Safety::RecoveryAction::HALT
        );
    }

    void vApplicationMallocFailedHook(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::MALLOC_FAILED,
            T76::Sys::Safety::FaultSeverity::CRITICAL,
            "FreeRTOS malloc failed - insufficient heap memory",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::HALT
        );
    }

    void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
        char description[T76::Sys::Safety::MAX_FAULT_DESC_LEN];
        snprintf(description, sizeof(description), 
                "Stack overflow detected in task: %s", pcTaskName ? pcTaskName : "unknown");
        
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::STACK_OVERFLOW,
            T76::Sys::Safety::FaultSeverity::FATAL,
            description, __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    // Hardware fault handlers
    void HardFault_Handler(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            T76::Sys::Safety::FaultSeverity::FATAL,
            "Hardware fault (HardFault) occurred",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    void MemManage_Handler(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            T76::Sys::Safety::FaultSeverity::FATAL,
            "Memory management fault occurred",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    void BusFault_Handler(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            T76::Sys::Safety::FaultSeverity::FATAL,
            "Bus fault occurred",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    void UsageFault_Handler(void) {
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::HARDWARE_FAULT,
            T76::Sys::Safety::FaultSeverity::FATAL,
            "Usage fault occurred",
            __FILE__, __LINE__, __func__,
            T76::Sys::Safety::RecoveryAction::RESET
        );
    }

    // Override the standard assert function to route through our system
    void __assert_func(const char *file, int line, const char *func, const char *expr) {
        char description[T76::Sys::Safety::MAX_FAULT_DESC_LEN];
        snprintf(description, sizeof(description), "Standard assertion failed: %s", expr ? expr : "unknown");
        
        T76::Sys::Safety::reportFault(
            T76::Sys::Safety::FaultType::C_ASSERT,
            T76::Sys::Safety::FaultSeverity::FATAL,
            description, file, static_cast<uint32_t>(line), func,
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
            T76::Sys::Safety::FaultSeverity::FATAL,
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