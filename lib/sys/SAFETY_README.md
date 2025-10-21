# T76 Comprehensive Safety System

## Overview

The T76 Comprehensive Safety System provides robust fault detection, reporting, and recovery capabilities for the RP2350 dual-core platform running FreeRTOS on Core 0 and bare-metal code on Core 1.

## Features

### Fault Detection
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

### Multi-Core Support

The safety system is designed for the RP2350's dual-core architecture:

- **Core 0**: Runs FreeRTOS, manages watchdog timer, handles FreeRTOS-specific faults
- **Core 1**: Runs bare-metal code, reports faults to shared memory, supports all fault types

Inter-core communication uses atomic operations and spinlocks for thread-safe operation.

### Recovery Strategies

The system supports multiple recovery actions:

- `CONTINUE`: Log fault and continue execution
- `HALT`: Stop execution, wait for external intervention
- `RESET`: Perform immediate system reset
- `REBOOT`: Reboot into recovery mode
- `RESTART_TASK`: Restart affected FreeRTOS task
- `RESTART_CORE`: Restart Core 1 (Core 0 resets system)

## Usage

### Initialization

```cpp
#include <lib/sys/safety.hpp>

// In main() on Core 0
T76::Sys::Safety::safetyInit(true, 5000); // Enable watchdog, 5-second timeout

// In Core 1 entry point
T76::Sys::Safety::safetyInit(false, 0); // Core 1 doesn't manage watchdog
```

### Basic Fault Reporting

```cpp
// Using macros (recommended)
CRITICAL_FAULT("Something went critically wrong");
FATAL_FAULT("System must reset");

// Using function calls
REPORT_FAULT(FaultType::CUSTOM_FAULT, FaultSeverity::ERROR, 
           "Custom error description", RecoveryAction::HALT);

// Direct function call
T76::Sys::Safety::reportFault(
    FaultType::MEMORY_CORRUPTION,
    FaultSeverity::CRITICAL,
    "Buffer overflow detected",
    __FILE__, __LINE__, __func__,
    RecoveryAction::RESET
);
```

### Custom Fault Handlers

```cpp
bool myFaultHandler(const FaultInfo& fault_info) {
    printf("Custom handler: %s\n", fault_info.description);
    
    // Return true if handled, false for default handling
    return fault_info.severity <= FaultSeverity::WARNING;
}

// Register the handler
T76::Sys::Safety::registerFaultHandler(myFaultHandler);
```

### System Monitoring

```cpp
// Check fault status
if (T76::Sys::Safety::isInFaultState()) {
    // System is currently processing a fault
}

// Get last fault information
FaultInfo last_fault;
if (T76::Sys::Safety::getLastFault(&last_fault)) {
    printf("Last fault: %s\n", last_fault.description);
}

// Update watchdog (call regularly from main loops)
T76::Sys::Safety::updateWatchdog();
```

### FreeRTOS Integration

The system automatically integrates with FreeRTOS:

```cpp
// FreeRTOSConfig.h configuration:
#define configASSERT(x) ((x) ? (void)0 : my_assert_func(__FILE__, __LINE__, __func__, #x))
#define configUSE_MALLOC_FAILED_HOOK 1
#define configCHECK_FOR_STACK_OVERFLOW 2

// Hook functions are automatically provided by the safety system
```

## Fault Information Structure

Each fault captures comprehensive system state:

```cpp
struct FaultInfo {
    uint32_t timestamp;           // When fault occurred
    uint32_t core_id;            // Which core (0 or 1)
    FaultType type;              // Type of fault
    FaultSeverity severity;      // Severity level
    RecoveryAction recovery_action; // Recommended action
    
    // Location information
    uint32_t line_number;
    char file_name[128];
    char function_name[64];
    char description[128];
    
    // System state
    uint32_t stack_pointer;
    uint32_t program_counter;
    uint32_t link_register;
    bool is_in_interrupt;
    uint32_t interrupt_number;
    
    // Memory information
    uint32_t heap_free_bytes;
    uint32_t min_heap_free_bytes;
    
    // Task information (if applicable)
    uint32_t task_handle;
    char task_name[configMAX_TASK_NAME_LEN];
    
    // Metadata
    uint32_t crc32;              // For integrity checking
};
```

## Configuration

### Watchdog Timer

```cpp
// Enable with custom timeout
T76::Sys::Safety::safetyInit(true, 10000); // 10-second timeout

// Disable watchdog
T76::Sys::Safety::safetyInit(false, 0);

// Update watchdog regularly
void mainLoop() {
    while (true) {
        // ... application code ...
        T76::Sys::Safety::updateWatchdog();
        // ... more application code ...
    }
}
```

### Memory Requirements

- Shared memory structure: ~1KB
- Per-fault information: ~500 bytes
- Code size: ~8-10KB (depending on optimization)

## Best Practices

1. **Initialize Early**: Call `safetyInit()` as early as possible in system startup
2. **Update Watchdog**: Call `updateWatchdog()` regularly from main loops
3. **Use Macros**: Use `CRITICAL_FAULT()` and `FATAL_FAULT()` macros for convenience
4. **Custom Handlers**: Implement custom handlers for application-specific recovery
5. **Monitor Regularly**: Check system state periodically
6. **Test Recovery**: Test different recovery strategies during development

## Thread Safety

The safety system is designed to be thread-safe across both cores:

- Atomic operations for shared state
- Spinlocks for critical sections
- Memory barriers for consistency
- Interrupt-safe operation

## Performance Impact

The safety system is designed for minimal performance impact:

- Fast-path operations use atomic instructions
- Fault reporting has controlled overhead
- Watchdog updates are lightweight
- Shared memory access is optimized

## Debugging

Enable verbose fault reporting by modifying the `printFaultInfo()` function in `safety.cpp` to include additional debug information.

## Integration with Other Systems

The safety system integrates cleanly with:

- FreeRTOS kernel and tasks
- Memory management system
- Hardware abstraction layers
- Custom application frameworks

## Examples

See `safety_demo.cpp` for comprehensive usage examples including:

- Basic fault reporting
- Custom fault handlers
- Multi-core fault handling
- System monitoring
- Recovery strategies

## Limitations

1. Core 1 cannot restart Core 0 (system reset required)
2. Watchdog timer managed only by Core 0
3. FreeRTOS-specific features only available on Core 0
4. Hardware fault handlers require proper vector table setup
5. Some recovery actions may not be possible in all contexts

## Future Enhancements

Potential future improvements:

1. Persistent fault logging across resets
2. Network-based fault reporting
3. Advanced fault prediction
4. Performance profiling integration
5. Remote debugging support