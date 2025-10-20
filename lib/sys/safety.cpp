/**
 * @file safety.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of comprehensive fault handling for the RP2350 platform.
 */

#include "safety.hpp"

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

namespace T76::Sys::Safety {

    // Constants for shared memory magic numbers
    constexpr uint32_t FAULT_INFO_MAGIC = 0xF4010F0;      // "FAULT" in hex
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
        volatile uint32_t fault_count;              ///< Total number of faults since boot
        volatile bool is_in_fault_state;           ///< True if currently processing a fault
        volatile uint32_t last_fault_core;         ///< Core ID of last fault
        volatile uint32_t fault_lock;               ///< Simple spinlock for atomic operations
        FaultInfo last_fault_info;                 ///< Information about the last fault
        uint32_t reserved[8];                      ///< Reserved for future use
    };

    // Place shared fault system in a specific memory section for inter-core access
    static SharedFaultSystem* g_shared_fault_system = nullptr;
    static uint8_t g_shared_memory[sizeof(SharedFaultSystem)] __attribute__((aligned(4)));

    // Local fault state for each core
    static bool g_safety_initialized = false;
    static uint32_t g_local_fault_count = 0;

    /**
     * @brief Simple spinlock implementation for inter-core synchronization
     */
    class SpinLock {
    private:
        volatile uint32_t* lock_ptr;

    public:
        explicit SpinLock(volatile uint32_t* lock) : lock_ptr(lock) {
            // Acquire lock using atomic compare-and-swap
            uint32_t expected = 0;
            while (!__atomic_compare_exchange_n(lock_ptr, &expected, 1, false, 
                                               __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                expected = 0;
                // Yield to other core/tasks while waiting
                tight_loop_contents();
            }
        }

        ~SpinLock() {
            // Release lock
            __atomic_store_n(lock_ptr, 0, __ATOMIC_RELEASE);
        }
    };

    /**
     * @brief Get current system timestamp in milliseconds
     */
    static uint32_t getCurrentTimestamp() {
        return to_ms_since_boot(get_absolute_time());
    }

    /**
     * @brief Get current core ID
     */
    static uint32_t getCurrentCoreId() {
        return get_core_num();
    }

    /**
     * @brief Check if currently running in interrupt context
     */
    static bool isInInterruptContext() {
        // Check if we're in an exception/interrupt by examining the IPSR
        uint32_t ipsr;
        __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
        return (ipsr & 0x1FF) != 0;
    }

    /**
     * @brief Get current stack pointer
     */
    static uint32_t getCurrentStackPointer() {
        uint32_t sp;
        __asm volatile ("MOV %0, SP" : "=r" (sp));
        return sp;
    }

    /**
     * @brief Get current program counter (approximate)
     */
    static uint32_t getCurrentProgramCounter() {
        uint32_t pc;
        __asm volatile ("MOV %0, PC" : "=r" (pc));
        return pc;
    }

    /**
     * @brief Get current link register
     */
    static uint32_t getCurrentLinkRegister() {
        uint32_t lr;
        __asm volatile ("MOV %0, LR" : "=r" (lr));
        return lr;
    }

    /**
     * @brief Get current interrupt number (if in interrupt context)
     */
    static uint32_t getCurrentInterruptNumber() {
        if (!isInInterruptContext()) {
            return 0;
        }
        uint32_t ipsr;
        __asm volatile ("MRS %0, IPSR" : "=r" (ipsr));
        return ipsr & 0x1FF;
    }

    /**
     * @brief Get heap statistics
     */
    static void getHeapStats(uint32_t& free_bytes, uint32_t& min_free_bytes) {
        if (getCurrentCoreId() == 0) {
            // On Core 0, we can use FreeRTOS heap functions
            free_bytes = xPortGetFreeHeapSize();
            min_free_bytes = xPortGetMinimumEverFreeHeapSize();
        } else {
            // On Core 1, we need to proxy through Core 0 or use approximations
            // For now, set to zero to indicate unavailable
            free_bytes = 0;
            min_free_bytes = 0;
        }
    }

    /**
     * @brief Get task information (if running under FreeRTOS)
     */
    static void getTaskInfo(uint32_t& task_handle, char* task_name, size_t task_name_len) {
        task_handle = 0;
        task_name[0] = '\0';

        if (getCurrentCoreId() == 0 && !isInInterruptContext()) {
            // Only available on Core 0 in task context
            TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
            if (current_task != nullptr) {
                task_handle = reinterpret_cast<uint32_t>(current_task);
                const char* name = pcTaskGetName(current_task);
                if (name != nullptr) {
                    strncpy(task_name, name, task_name_len - 1);
                    task_name[task_name_len - 1] = '\0';
                }
            }
        }
    }

    /**
     * @brief Safe string copy with bounds checking
     */
    static void safeStrCopy(char* dest, const char* src, size_t dest_size) {
        if (dest == nullptr || src == nullptr || dest_size == 0) {
            return;
        }
        strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }

    /**
     * @brief Create and populate fault info structure
     */
    static void createFaultInfo(FaultInfo& info, 
                               FaultType type,
                               FaultSeverity severity,
                               const char* description,
                               const char* file,
                               uint32_t line,
                               const char* function,
                               RecoveryAction recovery_action) {
        // Clear the structure
        memset(&info, 0, sizeof(FaultInfo));

        // Fill in basic fault information
        info.timestamp = getCurrentTimestamp();
        info.core_id = getCurrentCoreId();
        info.type = type;
        info.severity = severity;
        info.recovery_action = recovery_action;
        info.line_number = line;

        // Copy strings safely
        safeStrCopy(info.file_name, file ? file : "unknown", sizeof(info.file_name));
        safeStrCopy(info.function_name, function ? function : "unknown", sizeof(info.function_name));
        safeStrCopy(info.description, description ? description : "No description", sizeof(info.description));

        // Get system state information
        info.stack_pointer = getCurrentStackPointer();
        info.program_counter = getCurrentProgramCounter();
        info.link_register = getCurrentLinkRegister();
        info.is_in_interrupt = isInInterruptContext();
        info.interrupt_number = getCurrentInterruptNumber();

        // Get heap statistics
        getHeapStats(info.heap_free_bytes, info.min_heap_free_bytes);

        // Get task information
        getTaskInfo(info.task_handle, info.task_name, sizeof(info.task_name));

        // Update fault count
        if (g_shared_fault_system) {
            SpinLock lock(&g_shared_fault_system->fault_lock);
            info.fault_count = ++g_shared_fault_system->fault_count;
        } else {
            info.fault_count = ++g_local_fault_count;
        }
    }

    /**
     * @brief Execute recovery action based on fault severity and type
     */
    static void executeRecoveryAction(const FaultInfo& info) {
        switch (info.recovery_action) {
            case RecoveryAction::CONTINUE:
                // Log and continue - do nothing special
                break;

            case RecoveryAction::HALT:
                // Disable interrupts and halt
                __asm volatile ("cpsid i");  // Disable interrupts directly
                while (true) {
                    tight_loop_contents();
                }
                break;

            case RecoveryAction::RESET:
                // Perform immediate system reset
                // Force immediate reset by triggering a fault
                __asm volatile("bkpt #0");
                while (true) {
                    tight_loop_contents();
                }
                break;

            case RecoveryAction::REBOOT:
                // Reboot into recovery mode
                // Set a flag in persistent memory if available
                __asm volatile("bkpt #0");
                while (true) {
                    tight_loop_contents();
                }
                break;

            case RecoveryAction::RESTART_TASK:
                // Only valid for FreeRTOS tasks on Core 0
                if (info.core_id == 0 && !info.is_in_interrupt && info.task_handle != 0) {
                    TaskHandle_t task = reinterpret_cast<TaskHandle_t>(info.task_handle);
                    vTaskDelete(task);
                    // Note: Task would need to be recreated by a supervisor task
                }
                break;

            case RecoveryAction::RESTART_CORE:
                if (info.core_id == 1) {
                    // Reset Core 1
                    multicore_reset_core1();
                } else {
                    // Can't restart Core 0 - fall back to system reset
                    __asm volatile("bkpt #0");
                    while (true) {
                        tight_loop_contents();
                    }
                }
                break;

            default:
                // Unknown recovery action - halt
                __asm volatile ("cpsid i");  // Disable interrupts directly
                while (true) {
                    tight_loop_contents();
                }
                break;
        }
    }

    /**
     * @brief Print fault information to console
     */
    static void printFaultInfo(const FaultInfo& info) {
        printf("\n" "=== SYSTEM FAULT DETECTED ===\n");
        printf("Timestamp: %lu ms\n", info.timestamp);
        printf("Core: %lu\n", info.core_id);
        printf("Type: %s\n", faultTypeToString(info.type));
        printf("Severity: %s\n", faultSeverityToString(info.severity));
        printf("Recovery: %s\n", recoveryActionToString(info.recovery_action));
        printf("File: %s:%lu\n", info.file_name, info.line_number);
        printf("Function: %s\n", info.function_name);
        printf("Description: %s\n", info.description);
        
        if (info.task_handle != 0) {
            printf("Task: %s (0x%08lX)\n", info.task_name, info.task_handle);
        }
        
        printf("Stack Pointer: 0x%08lX\n", info.stack_pointer);
        printf("Program Counter: 0x%08lX\n", info.program_counter);
        printf("Link Register: 0x%08lX\n", info.link_register);
        
        if (info.is_in_interrupt) {
            printf("Interrupt Context: %lu\n", info.interrupt_number);
        }
        
        if (info.heap_free_bytes > 0) {
            printf("Heap Free: %lu bytes\n", info.heap_free_bytes);
            printf("Min Heap Free: %lu bytes\n", info.min_heap_free_bytes);
        }
        
        printf("Fault Count: %lu\n", info.fault_count);
        printf("==============================\n\n");
    }

    /**
     * @brief Core fault handling function
     */
    static void handleFault(const FaultInfo& info) {
        // Update shared state
        if (g_shared_fault_system) {
            SpinLock lock(&g_shared_fault_system->fault_lock);
            g_shared_fault_system->is_in_fault_state = true;
            g_shared_fault_system->last_fault_core = info.core_id;
            g_shared_fault_system->last_fault_info = info;
        }

        // Print fault information
        printFaultInfo(info);

        // Execute recovery action
        executeRecoveryAction(info);

        // Clear fault state (may not be reached depending on recovery action)
        if (g_shared_fault_system) {
            SpinLock lock(&g_shared_fault_system->fault_lock);
            g_shared_fault_system->is_in_fault_state = false;
        }
    }

    // ========== Public API Implementation ==========

    void safetyInit() {
        if (g_safety_initialized) {
            return; // Already initialized
        }

        // Initialize shared memory on first call from either core
        g_shared_fault_system = reinterpret_cast<SharedFaultSystem*>(g_shared_memory);
        
        // Check if already initialized by other core
        if (g_shared_fault_system->magic != FAULT_SYSTEM_MAGIC) {
            // First initialization
            memset(g_shared_fault_system, 0, sizeof(SharedFaultSystem));
            g_shared_fault_system->magic = FAULT_SYSTEM_MAGIC;
            g_shared_fault_system->version = 1;
            g_shared_fault_system->fault_count = 0;
            g_shared_fault_system->is_in_fault_state = false;
            g_shared_fault_system->fault_lock = 0;
        }

        g_safety_initialized = true;
    }

    /**
     * @brief Internal function to report faults (used by system hooks)
     */
    void reportFault(FaultType type, 
                     FaultSeverity severity,
                     const char* description,
                     const char* file,
                     uint32_t line,
                     const char* function,
                     RecoveryAction recovery_action) {
        
        FaultInfo info;
        createFaultInfo(info, type, severity, description, file, line, function, recovery_action);
        handleFault(info);
    }

    bool getLastFault(FaultInfo& fault_info) {
        if (!g_shared_fault_system) {
            return false;
        }

        SpinLock lock(&g_shared_fault_system->fault_lock);
        
        if (g_shared_fault_system->fault_count == 0) {
            return false;
        }

        fault_info = g_shared_fault_system->last_fault_info;
        
        return true;
    }

    uint32_t getFaultCount() {
        if (!g_shared_fault_system) {
            return g_local_fault_count;
        }

        SpinLock lock(&g_shared_fault_system->fault_lock);
        return g_shared_fault_system->fault_count;
    }

    void clearFaultHistory() {
        if (!g_shared_fault_system) {
            g_local_fault_count = 0;
            return;
        }

        SpinLock lock(&g_shared_fault_system->fault_lock);
        g_shared_fault_system->fault_count = 0;
        g_shared_fault_system->is_in_fault_state = false;
        memset(&g_shared_fault_system->last_fault_info, 0, sizeof(FaultInfo));
    }

    bool isInFaultState() {
        if (!g_shared_fault_system) {
            return false;
        }

        SpinLock lock(&g_shared_fault_system->fault_lock);
        return g_shared_fault_system->is_in_fault_state;
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

    const char* faultSeverityToString(FaultSeverity severity) {
        switch (severity) {
            case FaultSeverity::INFO: return "INFO";
            case FaultSeverity::WARNING: return "WARNING";
            case FaultSeverity::ERROR: return "ERROR";
            case FaultSeverity::CRITICAL: return "CRITICAL";
            case FaultSeverity::FATAL: return "FATAL";
            default: return "INVALID";
        }
    }

    const char* recoveryActionToString(RecoveryAction action) {
        switch (action) {
            case RecoveryAction::CONTINUE: return "CONTINUE";
            case RecoveryAction::HALT: return "HALT";
            case RecoveryAction::RESET: return "RESET";
            case RecoveryAction::REBOOT: return "REBOOT";
            case RecoveryAction::RESTART_TASK: return "RESTART_TASK";
            case RecoveryAction::RESTART_CORE: return "RESTART_CORE";
            default: return "INVALID";
        }
    }

} // namespace T76::Sys::Safety
