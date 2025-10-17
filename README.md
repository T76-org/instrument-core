# T76 Instrument Core

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
