/**
 * @file safety.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file provides comprehensive fault handling for the RP2350 platform using FreeRTOS.
 * It catches all possible faults (asserts, FreeRTOS hooks, panics, allocation failures, etc.),
 * saves information about the fault, and triggers system reset for recovery.
 * 
 * The system follows a safe-by-default design philosophy where the system always
 * starts in a safe state upon reset, eliminating the need for active safing functions.
 * This approach is more reliable and handles all reset scenarios (including hardware
 * watchdog timeouts) uniformly.
 * 
 * This safety system is optimized for minimal stack usage with static-only memory
 * allocation throughout fault handling paths.
 * 
 * Multi-Core Fault Handling:
 * ==========================
 * 
 * This system is designed to handle faults from both cores of the RP2350:
 * - Core 0: Runs FreeRTOS tasks and handles FreeRTOS-specific faults
 * - Core 1: Runs bare-metal code and handles system-level faults
 * 
 * The fault information is stored in a shared memory structure that can be
 * accessed from both cores. Critical sections ensure thread-safe access.
 * 
 * Fault Types Handled:
 * ====================
 * 
 * 1. FreeRTOS Assertions (configASSERT)
 * 2. Stack Overflow (vApplicationStackOverflowHook)
 * 3. Memory Allocation Failures (vApplicationMallocFailedHook)
 * 4. C Standard Assertions (assert.h)
 * 5. Pico SDK Hard Assertions
 * 6. Hardware Faults (HardFault, MemManage, BusFault, UsageFault)
 * 7. Inter-core Communication Failures
 * 
 * Recovery Strategies:
 * ===================
 * 
 * The system uses a safe-by-default design where the system automatically
 * returns to a safe state upon any reset. This eliminates the need for
 * active safing functions and provides more reliable safety behavior.
 * 
 * The system implements two recovery strategies based on fault history:
 * 
 * - RESET: Normal fault recovery (< T76_SAFETY_MAX_REBOOTS consecutive faults)
 *   * Triggers immediate system reset via hardware watchdog
 *   * System automatically returns to safe state upon reset
 *   * Preserves fault information in persistent memory for analysis
 * 
 * - SAFETY MONITOR: Persistent fault protection (≥ T76_SAFETY_MAX_REBOOTS faults)
 *   * Enters a safe monitoring mode with continuous fault reporting
 *   * Halts normal operation to prevent infinite reboot loops
 *   * Provides detailed fault history output via USB console
 *   * Requires manual system reset to clear fault state and resume operation
 * 
 *
 */

#pragma once

#include <cstdint>
#include <cstring>

#include "FreeRTOS.h"
#include "task.h"

#include "safety_private.hpp"
#include "safety.hpp"


namespace T76::Sys::Safety {

    /**
     * @brief Abstract base class for components that can participate in safety operations
     * 
     * Components that inherit from this class can register themselves with the safety
     * system and participate in activation and safing operations. This provides a
     * standardized interface for managing component lifecycle and safety states.
     * 
     * Implementations must provide:
     * - activate(): Called during system initialization to activate the component
     * - makeSafe(): Called to put the component into a safe state (must be idempotent)
     */
    class SafeableComponent {
    public:
        /**
         * @brief Virtual destructor for proper cleanup
         */
        virtual ~SafeableComponent() = default;

        /**
         * @brief Activate the component
         * 
         * This method is called during system initialization to activate the component.
         * The component should perform any necessary initialization and return true
         * if activation was successful.
         * 
         * @return true if component was activated successfully, false otherwise
         */
        virtual bool activate() = 0;

        /**
         * @brief Get the component name for identification
         * 
         * This method returns a human-readable name for the component that can be
         * used in error reporting and debugging. The returned string should be a
         * static string literal or have a lifetime at least as long as the component.
         * 
         * @return Null-terminated string with the component name
         */
        virtual const char* getComponentName() const = 0;

        /**
         * @brief Put the component into a safe state
         * 
         * This method is called to put the component into a safe state. It must be
         * idempotent (safe to call multiple times) and should not throw exceptions.
         * The component should disable any potentially dangerous operations and
         * enter a known safe state.
         */
        virtual void makeSafe() = 0;
    };

    /**
     * @brief Initialize the safety system
     * 
     * This function must be called early in system initialization, before
     * any other safety functions are used. It sets up shared memory structures
     * and configures the default fault handlers.
     */
    void safetyInit();

    /**
     * @brief Initialize dual-core watchdog protection system
     * 
     * Sets up a dual-core watchdog system where Core 0 manages the hardware watchdog
     * and Core 1 sends periodic heartbeats. The system creates a FreeRTOS task on
     * Core 0 that monitors both cores and only feeds the hardware watchdog when both
     * cores are confirmed healthy.
     * 
     * @note Must be called on Core 0 during system initialization
     * @note Core 1 must call sendCore1Heartbeat() regularly after this initialization
     * @note Uses T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS for hardware watchdog timeout
     * @note Creates a low-priority FreeRTOS task that only runs when system is idle
     * 
     * @return true if watchdog system was successfully initialized, false on error
     */
    bool watchdogInit();

    /**
     * @brief Send heartbeat from Core 1 to indicate it's alive
     * 
     * This function should be called periodically by Core 1 to indicate that
     * it's still operational. The heartbeat updates a shared memory timestamp
     * that is monitored by the watchdog manager task running on Core 0.
     * 
     * @note Should be called only from Core 1 at least every 1 second
     * @note Safe to call from any context on Core 1 (interrupt or main thread)
     * @note No-op if called from Core 0 or if watchdog system not initialized
     */
    void feedWatchdogFromCore1();

    /**
     * @brief Register a component with the safety system
     * 
     * Registers a SafeableComponent instance with the safety system so it can
     * participate in activation and safing operations. The component must remain
     * valid for as long as it is registered.
     * 
     * @param component Pointer to the component to register (must not be null)
     * @return true if component was successfully registered, false otherwise
     * 
     * @note Thread-safe for multi-core operation
     * @note Will not register the same component twice
     * @note Registry has a maximum capacity limit
     */
    bool registerComponent(SafeableComponent* component);

    /**
     * @brief Unregister a component from the safety system
     * 
     * Removes a previously registered SafeableComponent from the safety system.
     * After unregistration, the component will not participate in activation
     * or safing operations.
     * 
     * @param component Pointer to the component to unregister (must not be null)
     * @return true if component was successfully unregistered, false if not found
     * 
     * @note Thread-safe for multi-core operation
     * @note Safe to call even if component was never registered
     */
    bool unregisterComponent(SafeableComponent* component);

    /**
     * @brief Activate all registered components
     * 
     * Calls the activate() method on all registered SafeableComponent instances.
     * If any component fails to activate, all components are made safe.
     * 
     * @param failingComponentName Optional output parameter to receive the name of the
     *                            component that failed activation (if any). Can be nullptr
     *                            if caller doesn't need this information.
     * @return True if all components were successfully activated, false otherwise.
     * 
     * @note Thread-safe for multi-core operation
     * @note If any activation fails, this function automatically calls makeAllComponentsSafe()
     */
    bool activateAllComponents(const char** failingComponentName = nullptr);

    /**
     * @brief Make all registered components safe
     * 
     * Calls the makeSafe() method on all registered SafeableComponent instances.
     * This is typically called during system shutdown or fault recovery to ensure
     * all components enter a safe state.
     * 
     * @note Thread-safe for multi-core operation
     * @note Components are safed outside of critical sections to avoid deadlocks
     * @note Continues safing other components even if some fail
     */
    void makeAllComponentsSafe();

} // namespace T76::Sys::Safety

// ========== SAFETY MACROS ==========

/**
 * @brief Panic macro that triggers a fault if condition is false.
 * 
 * Evaluates the given expression and, if it evaluates to false (or 0),
 * triggers a fault with the provided reason message. This macro is useful
 * for runtime assertions and precondition checks throughout your code.
 * 
 * @param expr Boolean expression to evaluate
 * @param reason Human-readable string describing the expected condition
 * 
 * Example usage:
 * @code
 * T76_PANIC_IF_NOT(ptr != nullptr, "Pointer must not be null");
 * T76_PANIC_IF_NOT(init_successful, "Initialization failed");
 * @endcode
 * 
 * @note This macro never returns if the condition fails - triggers system reset
 * @note Captures file, line, and function information automatically
 */
#define T76_PANIC_IF_NOT(expr, reason) \
    do { \
        if (!(expr)) { \
            T76::Sys::Safety::reportFault( \
                T76::Sys::Safety::FaultType::INVALID_STATE, \
                reason, \
                __FILE__, \
                __LINE__, \
                __FUNCTION__ \
            ); \
        } \
    } while(0)

// ========== C STANDARD LIBRARY OVERRIDES ==========

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal abort implementation function (do not call directly)
 * 
 * This function is used by the abort() macro to capture location information.
 * Always use the abort() macro instead of calling this directly.
 */
void __t76_abort_impl(const char *file, int line, const char *func);

#ifdef __cplusplus
}
#endif

/**
 * @brief Standard C abort() function override
 * 
 * Overrides the standard C library abort() function to route through
 * the safety system. This ensures that calls to abort() are properly
 * logged and handled consistently with other system faults.
 * 
 * @note Always use this macro instead of calling __t76_abort_impl() directly
 * @note Never returns - triggers system reset through safety system
 * 
 */
#define abort() __t76_abort_impl(__FILE__, __LINE__, __FUNCTION__)

