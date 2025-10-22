#include "safety_private.hpp"

namespace T76::Sys::Safety {
    
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
    uint32_t executeSafingFunctions();

    SafingResult registerSafingFunction(SafingFunction safingFunc);
    SafingResult deregisterSafingFunction(SafingFunction safingFunc);

} // namespace T76::Sys::Safety
