# T76 Comprehensive Safety System

## Overview

The T76 Comprehensive Safety System provides robust fault detection, reporting, and recovery capabilities for the RP2350 dual-core platform running FreeRTOS on Core 0 and bare-metal code on Core 1.

**Key Features:**
- Comprehensive fault detection and reporting
- Inter-core fault communication
- Persistent fault information across reboots
- Safe-by-default design (system starts in safe state)
- Reboot limiting to prevent infinite reboot loops
- Minimal stack usage (<48 bytes) and static memory allocation
- Safety Monitor for persistent fault reporting

## Fault Detection

The system catches and handles the following types of faults:

1. **FreeRTOS Faults**
   - `configASSERT` failures
   - Stack overflow detection (`vApplicationStackOverflowHook`)
   - Memory allocation failures (`vApplicationMallocFailedHook`)

2. **Standard C Faults**
   - Standard `assert()` failures
   - `abort()` calls

3. **Hardware Faults**
   - HardFault exceptions
   - MemManage faults
   - BusFault exceptions
   - UsageFault exceptions

4. **Application-Defined Faults**
   - Custom fault reporting
   - Resource exhaustion
   - Memory corruption detection
   - Invalid state detection
   - Inter-core communication failures

5. **System Monitoring**
   - Watchdog timeout protection
   - System state monitoring

## Multi-Core Support

The safety system is designed for the RP2350's dual-core architecture:

- **Core 0**: Runs FreeRTOS, manages watchdog timer, handles FreeRTOS-specific faults
- **Core 1**: Runs bare-metal code, reports faults to shared memory, supports all fault types

Inter-core communication uses atomic operations and spinlocks for thread-safe operation.

## Recovery Strategy

The system uses a safe-by-default recovery strategy:

1. **Fault Detection**: When a fault occurs, comprehensive information is captured
2. **System Reset**: Perform immediate watchdog-based system reset
3. **Safe State**: System automatically returns to safe state upon reset
4. **Reboot Limiting**: Track consecutive reboots to prevent infinite loops
5. **Safety Monitor**: Display fault information when reboot limit is exceeded

### Reboot Limiting

The system tracks consecutive reboots and enters Safety Monitor mode when a configurable limit is reached:

- **Default Limit**: 3 consecutive reboots (`T76_SAFETY_MAX_REBOOTS`)
- **Fault History**: Stores fault information for each reboot
- **Safety Monitor**: Displays all faults when limit is exceeded
- **Reset Counter**: Application can reset counter after successful operation

## Quick Start

### 1. Initialization

```cpp
#include "safety.hpp"

void main() {
    // Initialize safety system early in boot process
    T76::Sys::Safety::safetyInit();
    
    // Your application initialization...
}
```

### 2. Basic Fault Reporting

```cpp
// Simple fault with minimal information
T76::Sys::Safety::reportFault(__FILE__, __LINE__, __FUNCTION__, 
                              T76::Sys::Safety::FaultType::INVALID_STATE,
                              "System entered invalid state");

// The function does not return - system will reset and return to safe state
```

### 3. Safe-by-Default Design

With the safe-by-default approach, the system always starts in a safe state:

```cpp
void main() {
    T76::Sys::Safety::safetyInit();
    
    // System starts in safe state by default
    // Hardware is configured for safe operation
    // Only transition to operational state through explicit initialization
    
    // Your safe initialization code here...
    initializePeripheralsInSafeMode();
    
    // Transition to operational state only after verification
    if (systemSelfTestPassed()) {
        transitionToOperationalMode();
    }
    
    // Your application...
}
```

### 4. Reset Reboot Counter

After successful operation, reset the reboot counter:

```cpp
void applicationMain() {
    // System initialization and startup...
    
    // After successful operation (e.g., after 5 minutes of runtime)
    T76::Sys::Safety::resetRebootCounter();
    
    // Continue normal operation...
}
```

## API Reference

### Core Functions

#### `void safetyInit()`
Initialize the safety system. Must be called early in system initialization.

#### `void reportFault(const char* fileName, uint32_t lineNumber, const char* functionName, FaultType faultType, const char* description)`
Report a fault and trigger system reset. System will return to safe state upon reset.

**Parameters:**
- `fileName`: Source file where fault occurred
- `lineNumber`: Line number in source file
- `functionName`: Function name where fault occurred
- `faultType`: Type of fault (see FaultType enum)
- `description`: Human-readable description

#### `void clearFaultHistory()`
Clear stored fault information.

### Reboot Limiting

#### `void resetRebootCounter()`
Reset the consecutive reboot counter after successful operation.

## Fault Types

```cpp
enum class FaultType : uint8_t {
    UNKNOWN = 0,
    FREERTOS_ASSERT,          // FreeRTOS configASSERT failure
    STACK_OVERFLOW,           // FreeRTOS stack overflow detection
    MALLOC_FAILED,            // FreeRTOS malloc failure
    C_ASSERT,                 // Standard C assert() failure
    PICO_HARD_ASSERT,         // Pico SDK hard_assert failure
    HARDWARE_FAULT,           // Hardware exception (HardFault, etc.)
    INTERCORE_FAULT,          // Inter-core communication failure
    MEMORY_CORRUPTION,        // Detected memory corruption
    INVALID_STATE,            // Invalid system state detected
    RESOURCE_EXHAUSTED,       // System resource exhaustion
};
```

## Configuration

### Reboot Limiting
```cpp
#define T76_SAFETY_MAX_REBOOTS 3  // Maximum consecutive reboots before entering safety monitor
```

### String Limits
```cpp
#define T76_SAFETY_MAX_FILE_NAME_LEN 32
#define T76_SAFETY_MAX_FUNCTION_NAME_LEN 32  
#define T76_SAFETY_MAX_FAULT_DESC_LEN 64
```

## Safety Monitor

When the reboot limit is exceeded, the system enters Safety Monitor mode:

- **Reboot Limit Trigger**: Only activates when `T76_SAFETY_MAX_REBOOTS` consecutive reboots occur
- **Fault History Display**: Shows all faults that led to the reboot limit
- **Visual Indication**: Status LED blinks to indicate fault state
- **System Halt**: Prevents further reboot attempts
- **Manual Reset Required**: System remains in this mode until manually reset

### Sample Output

When the reboot limit is exceeded, the Safety Monitor displays:

```
=========================================
   REBOOT LIMIT EXCEEDED
   MULTIPLE CONSECUTIVE FAULTS DETECTED
=========================================

Consecutive reboots: 3 (limit: 3)

--- FAULT #1 ---
=== SYSTEM FAULT DETECTED ===
Timestamp: 15420 ms
Core: 0
Type: STACK_OVERFLOW
File: main.cpp:156
Function: processingTask
Description: Task stack overflow detected
...

--- FAULT #2 ---
...

REBOOT LIMIT EXCEEDED - System Halted
Consecutive faults: 3 (limit: 3)
Manual reset required to clear fault state.
```

## Performance Characteristics

### Stack Usage
The safety system is optimized for minimal stack usage:
- `reportFault()`: ~24 bytes
- `populateFaultInfo()`: ~12 bytes  
- `handleFault()`: ~8 bytes
- **Total**: ~48 bytes maximum

### Memory Usage
- **Shared Memory**: ~2KB persistent storage
- **Static Allocation**: No dynamic memory allocation
- **Cross-Reset Persistence**: Fault data survives system resets

## Best Practices

1. **Early Initialization**: Call `safetyInit()` as early as possible
2. **Safing Functions**: Keep safing functions simple and fast
3. **Reset Counter**: Reset reboot counter after successful operation periods
4. **Descriptive Faults**: Provide clear, concise fault descriptions
5. **Test Fault Paths**: Regularly test fault detection and recovery

## Limitations

1. **Reboot Limit**: System requires manual reset when reboot limit is exceeded
2. **Safing Function Count**: Limited to 8 registered safing functions
3. **String Lengths**: Fault descriptions limited to 64 characters
4. **Memory Persistence**: Requires .uninitialized_data section support
5. **Single Recovery**: Only supports reset recovery (no in-place recovery)

## Testing

Use the provided test function to validate the system:

```cpp
T76::Sys::Safety::testStackCapture(); // Triggers controlled fault for testing
```

This will cause a system reset and allow you to verify fault capture and Safety Monitor operation.