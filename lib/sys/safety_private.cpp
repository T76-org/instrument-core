#include "safety_private.hpp"

namespace T76::Sys::Safety { 

    SharedFaultSystem* gSharedFaultSystem = nullptr;
    uint8_t gSharedMemory[sizeof(SharedFaultSystem)] __attribute__((section(".uninitialized_data"))) __attribute__((aligned(4)));
    bool gSafetyInitialized = false;
    spin_lock_t* gSafetySpinlock = nullptr;
    char gStaticFileName[T76_SAFETY_MAX_FILE_NAME_LEN];
    char gStaticFunctionName[T76_SAFETY_MAX_FUNCTION_NAME_LEN];
    char gStaticDescription[T76_SAFETY_MAX_FAULT_DESC_LEN];
    bool gWatchdogInitialized = false;

}