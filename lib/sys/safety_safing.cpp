#include <cstring>
#include <cstdio>

#include <FreeRTOS.h>
#include <task.h>

#include <pico/stdlib.h>
#include <pico/time.h>
#include <hardware/sync/spin_lock.h>
#include <hardware/watchdog.h>
#include <hardware/irq.h>

#include "safety_private.hpp"

namespace T76::Sys::Safety {
    
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

} // namespace T76::Sys::Safety