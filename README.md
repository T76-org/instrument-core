# T76 Instrument Core

**Work in progress.** This project is under active development and incomplete as of October 2025. Follow the repository for updates.

This project is a template for building “prosumer”-grade firmware for measurement instruments based on RP2350-based boards, such as the Raspberry Pi Pico 2.

It provides these features:

- C++ code written to modern standards using the Pi Pico SDK
    - Support for the RP2350 only
    - No support for Arduino; use the Pico SDK only
    - No support for displays, as plenty of alternatives exist already
    - Fallback to plain C for critical tasks where warranted
    - Use of CMake for configuration through and through
- Full use of multicore:
  - Bare-metal critical tasks running on one core
  - FreeRTOS running on the other for housekeeping/communication tasks
- Reliance on FreeRTOS for all memory management
- Support for industry-standard communications
  - SCPI over USBTMC for instrument management
- USB Serial for debug / status management
- Safety features
  - Extensive unit testing harness built into the codebase
  - Last-ditch exception manager that puts the instrument in safe mode on crash and halt

## FreeRTOS and multitasking support

The IC uses FreeRTOS to manage all non-critical tasks. FreeRTOS is configured to run in single-core mode on core 0, and takes care of all memory management.

Core 1 is reserved for bare-metal critical tasks that have strict timing and priority requirements.

TODO: Expand this with more information on task management, inter-core communication, etc., especially with regards to core 1.

## Memory management

FreeRTOS normally manages its own heap for dynamic memory allocation. However, this requires replacing all calls to `malloc`, `free`, `new`, and `delete` with FreeRTOS equivalents like `pvPortMalloc` and `vPortFree`. This can be error-prone and tedious, especially in large codebases or when using third-party libraries.

To simplify this process, a set of wrapper functions is provided to handle memory allocation and deallocation in a way that is compatible with FreeRTOS. These wrappers ensure that all memory operations are performed within the FreeRTOS environment, reducing the risk of errors and improving code maintainability. Thus, code can continue to use standard memory management functions while benefiting from FreeRTOS's capabilities.

However, since FreeRTOS is only running on core 0, special care must be taken when allocating memory from bare-metal code on core 1; otherwise, memory corruption may occur because FreeRTOS's heap manager is unaware of the code executing on core 1 and cannot account for its memory usage. To mitigate this issue, the memory wrappers can operate in one of two modes, controlled by the `T76_USE_GLOBAL_LOCKS` macro:

- When `T76_USE_GLOBAL_LOCKS` is disabled (set to 0):
  - The memory wrappers assume that all memory allocation calls are made from core 0 only.
  - No additional synchronization mechanisms are used, which reduces overhead and improves performance.
  - This mode is suitable for applications where core 1 does not perform any dynamic memory allocation; since core 1 is dedicated to critical tasks, this _should_ often be the case.

- When `T76_USE_GLOBAL_LOCKS` is enabled (set to 1):
  - A dedicated memory service task is started on core 0 to handle memory allocation requests from core 1.
  - A bare-metal queue is used to send allocation and deallocation requests from core 1 to the memory service task on core 0. This code runs within FreeRTOS and, therefore, can safely call FreeRTOS memory management functions.
  - This mode allows core 1 to safely allocate and free memory, albeit with some performance overhead due to inter-core communication.
  - This mode is suitable for applications where core 1 needs to perform dynamic memory allocation.

### Configuration

The memory management system can be configured by defining the `T76_USE_GLOBAL_LOCKS` macro in the project's configuration files or build system. Set it to `0` to disable global locks (single-core mode) or `1` to enable them (multi-core mode).

At startup, the memory management system must be initialized by calling the `T76::Sys::Memory::memoryInit()` function. This function sets up the necessary data structures and starts the memory service task if global locks are enabled.

# Safety features

The T76 Instrument Core includes a robust safety system designed to handle faults and ensure the instrument operates reliably. The safety system provides mechanisms for fault detection, logging, and recovery, allowing the instrument to enter a safe mode in the event of critical errors.

### Fault handling

The safety system includes functions for triggering faults, logging fault information, and managing system reboots. When a fault is triggered, the system captures relevant information, such as the fault type, location, and context, and stores it in a shared fault structure.

The system also implements a reboot limiting mechanism to prevent continuous reboot loops in the event of persistent faults. If the number of consecutive reboots exceeds a predefined threshold, the system enters a safety monitor mode, where it halts normal operation and waits for user intervention.

Most system-level faults, such as memory allocation failures, panics, and unhandled exceptions, are automatically captured and processed by the safety system. Developers can also trigger custom faults using the provided fault handling functions.

### Watchdog support

The safety system includes support for a dual-core watchdog mechanism. A hardware watchdog timer is configured to monitor the system's health and ensure that it remains responsive. If the watchdog timer expires, indicating that the system is unresponsive, the watchdog triggers a system reset.

A low-priority FreeRTOS task is created on core 0 to periodically “feed” the watchdog timer, ensuring that it is reset as long as the system is functioning correctly. This task runs when the system is idle, minimizing its impact on overall performance.

Should any task on core 0 become unresponsive, the watchdog will not be fed, leading to a system reset and allowing the safety system to take appropriate action.

On core 1, bare-metal critical tasks must also ensure that they remain responsive and do not block indefinitely by periodically sending a signal to the watchdog task running on core 0. This ensures that the watchdog mechanism effectively monitors the entire system, including both cores, with core 0 being the primary monitor so that as much of core 1's capacity is available for critical tasks.

### Safing

The safety system provides a safing mechanism that puts the instrument into a safe state when a critical fault occurs. In safe mode, the instrument disables non-essential functions and enters a low-power state, allowing for safe recovery and troubleshooting.

## Configuration

The safety system can be configured through a set of macros, which are defined in the `lib/sys/safety_private.hpp` file. These macros allow developers to customize the behavior of the safety system, including fault handling, reboot limits, and watchdog settings.

## Including and using the safety system

To include the safety system in your project, you simply need to include the main safety header file, `safety.hpp`, in your source files.

At launch, initialize the safety system by calling the `T76::Sys::Safety::safetyInit()` function. This function sets up the necessary data structures and prepares the system for fault handling, and should be called early in the system initialization process from core 0. It also sets up the watchdog timer, and kicks off the watchdog feeding task.

On core 1, you need to ensure that the bare-metal critical tasks periodically signal the watchdog feeding task on core 0 to indicate that they are still responsive. This can be done by calling the `T76::Sys::Safety::signalWatchdog()` function from within the critical tasks. It is good practice to call this function at regular intervals within portions of the critical core whose failure would compromise system safety or functionality; typically, this may mean within the main loop or other long-running sections of the code.

## Triggering faults

While the system catches system-level faults automatically, developers can also trigger custom faults using the `T76::Sys::Safety::reportFault()` function. This function allows you to specify the fault type, location, and additional context information.

When a fault is reported, the safety system captures the relevant information and takes appropriate action based on the fault type and system state before safing the system and rebooting.

A convenient macro, `T76_PANIC_IF_NOT(expr, reason)`, is provided to simplify fault triggering based on conditions. This macro evaluates the given expression `expr`, and if it evaluates to false, it triggers a panic fault with the specified `reason`. This allows for concise and readable code when checking for critical conditions.

### Enhanced abort() function

The safety system provides an enhanced `abort()` function that captures detailed location information when called. Unlike the standard C library `abort()` function, which cannot capture caller information, the safety system's version automatically records the file, line number, and function name where `abort()` was called.

To use this enhanced functionality, you must include `safety.hpp` in your source files **after** including any standard library headers that declare `abort()` (such as `<cstdlib>` or `<stdlib.h>`). The correct include order is:

```cpp
#include <cstdlib>      // Include system headers first
#include "safety.hpp"   // Then include safety header

void my_function() {
    if (critical_error) {
        abort();  // Automatically captures file, line, and function
    }
}
```

When `abort()` is called, the safety system will log the fault with complete diagnostic information before triggering a system reset. This information is preserved in persistent memory and can be retrieved after reboot for debugging purposes.

**Note:** If you call `abort()` without including `safety.hpp`, it will use the standard library version and will not capture location information, falling back to a hardfault. The system will still enter safe mode, but without the detailed diagnostics.

## Fault recovery and reboot limiting

The safety system implements a reboot limiting mechanism to prevent continuous reboot loops in the event of persistent faults. The maximum number of consecutive reboots allowed before entering safety monitor mode can be configured using the `T76_SAFETY_MAX_REBOOTS` macro in the `safety_private.hpp` file.

After the appropriate number of reboots, the system implements a lockout mechanism that prevents further restarts until the user intervenes by cycling power or issuing a hardware reset. This ensures that the instrument does not enter an endless reboot cycle, allowing for safe recovery and troubleshooting.

After a successful, stable runtime without faults, the reboot counter can be automatically reset after a configurable period. This period can be set using the `T76_SAFETY_FAULTCOUNT_RESET_SECONDS` macro in the `safety_private.hpp` file. If set to `0`, the auto-reset feature is disabled.

## System safing and startup sequence

When a critical fault occurs, a reboot is not sufficient to ensure safety, since external devices may still be powered and operating in an unsafe manner. In addition, it is important to bring up the system in a known safe state before attempting normal operation; after all, the failure of a single component should not compromise the safety of the entire instrument.

To address these concerns, the safety system implements a two-shot startup process:

- Upon boot, the system first enters safe mode by asking all components to enter their safe states. This ensures that any external devices are powered down or put into a safe configuration before normal operation begins.

- Next, the system determines whether the previous shutdown was due to a fault. If so, it remains in safe mode, allowing the user to investigate and address the issue before proceeding.

- Otherwise, it proceeds to activate all components and enter normal operation mode.

To participate in this safing mechanisms, components need to implement the `T76::Sys::Safety::SafeableComponent` interface, which defines a `makeSafe()` method, which is called during the safing process, and an `activate()` method, which is called to bring the component back to normal operation and returns a boolean indicating success or failure.

Components should register themselves with the safety system using the `T76::Sys::Safety::registerSafeableComponent()` function.

Note that the system makes no guarantees about the order in which components are safed or activated. Therefore, components should be designed to handle safing and activation independently of other components. Also, the system assumes that components are statically allocated and remain in memory for the lifetime of the program. This is, generally, a good practice for safety-critical components, especially those involved in the safing process.

Finally, because the system uses statically allocated memory to manage the list of registered components, there is a maximum number of components that can be registered. This limit can be configured using the `T76_SAFETY_MAX_REGISTERED_COMPONENTS` macro in the `safety_private.hpp` file.

## Dual-core watchdog initialization

If enabled, the watchdog system must be initialized at startup by calling the `T76::Sys::Safety::watchdogInit()` function from core 0. This function sets up the hardware watchdog timer and starts the watchdog feeding task. 

On core 1, bare-metal critical tasks must periodically signal the watchdog feeding task on core 0 by calling the `T76::Sys::Safety::signalWatchdog()` function to ensure that the watchdog timer is reset and the system remains responsive.

Note that the watchdog system requires both FreeRTOS and the main loop on core 1 to be running in order to function. If you require a lengthy setup process on either core that must block both cores at startup, consider initializing the watchdog system after the setup is complete; otherwise, the watchdog may trigger a reset during the setup phase. (This is also the reason why the watchdog initialization is not included in the main safety initialization function.)

