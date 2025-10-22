#include "safety_private.hpp"
#include "safety.hpp"

namespace T76::Sys::Safety {

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

