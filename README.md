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

## Using the template

The IC includes a convenient template that makes it easy to build your own firmware that implements the T76 framework.

The template is designed to be used as a git submodule within your own project repository. This allows you to easily track changes to the IC framework while keeping your own code separate.

To create a new project using the IC template, follow these steps:

- Create a new git repository for your project.
- Use the Pico SDK to create a new project; make sure that you enable C++ and stdio-over-USB support (`pico_enable_stdio_usb`).
- Add the IC repository as a git submodule within your project repository:

  ```bash
  git submodule add https://github.com/t76-org/instrument-core.git
  ```

- Modify the CMakeLists.txt file in your project to include the IC template and link against the application library:

  ```cmake
  add_subdirectory(instrument-core)
  target_link_libraries(your_project_name PRIVATE t76_ic)
  ```

  Ensure that you replace `your_project_name` with the actual name of your project target, and that this code appears after the call to `pico_sdk_init()`

- Create your own application class by inheriting from `T76::Core::App` and implementing the required virtual methods.

## Application lifecycle

The `T76::Core::App` class defines the lifecycle of the application, ensuring that initialization and main loop execution are handled correctly across both cores. You will typically want to create a derived class and statically instantiate it in your main application file. You can then execute its `run()` method from `main()` to start the application.

You will need to implement the following methods in your derived application class:

- `_init()`: This method is called on core 0 after the IC has been initialized but before `_initCore0()` and `_startCore1()` are called. Use this method to perform any early initialization required before core launch, such as setting up standard I/O or initializing hardware components needed by both cores.
- `_initCore0()`: This method is called on core 0 after core 1 has been launched but before the watchdog is initialized and the FreeRTOS scheduler starts. Use this method to perform any initialization specific to core 0, such as setting up FreeRTOS tasks.
- `_startCore1()`: This method is called on core 0 to start core 1. You should implement this method to launch core 1 code directly. Remember that, if you have enabled memory allocation on core 1, it will not be available until after the FreeRTOS scheduler has started.

The safety system is not initialized until after `run()` is called; this gives you the opportunity to create any components that cannot be statically allocated within your application class's constructor. `T76::Core::App` is also a subclass of `T76::Core::Safety::SafeableComponent`, so you can override the `makeSafe()` and `activate()` methods to implement application-level safing and activation logic.

The app template defaults to a sane configuration that includes:

- Memory management _without_ support for core 1 allocations (to maximize performance)
- Safety system with watchdog support
- USBTMC, CDC, and SCPI support

If you with to modify this configuration, details about each subsystem are provided in the relevant sections below.

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

These functions can be enabled by adding the `t76_memory` library to your project and then including `<t76/memory.hpp>` to your source code.

The memory management system can be configured by changing the `T76_USE_GLOBAL_LOCKS` CMake variable in the project's configuration files or build system. Set it to `OFF` to disable global locks (single-core mode) or `ON` to enable them (multi-core mode).

At startup, the memory management system must be initialized by calling the `T76::Core::Memory::init()` function. This function sets up the necessary data structures and starts the memory service task if global locks are enabled.

## Safety features

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

### Including and using the safety system

You can add the `t76_safety` library to your project to enable the safety system, and include `<t76/safety.hpp>` to your source code.

At launch, initialize the safety system by calling the `T76::Core::Safety::init()` function. This function sets up the necessary data structures and prepares the system for fault handling, and should be called early in the system initialization process from core 0. It also sets up the watchdog timer, and kicks off the watchdog feeding task.

On core 1, you need to ensure that the bare-metal critical tasks periodically signal the watchdog feeding task on core 0 to indicate that they are still responsive. This can be done by calling the `T76::Core::Safety::feedWatchdogFromCore1()` function from within the critical tasks. It is good practice to call this function at regular intervals within portions of the critical core whose failure would compromise system safety or functionality; typically, this may mean within the main loop or other long-running sections of the code.

### Configuration

The library provides a comprehensive list of settings that can be used to alter its behaviour. These can all be changed in the CMake configuration file for your project:

- `T76_SAFETY_MAX_FAULT_DESC_LEN` - Maximum fault description string length (bytes)
- `T76_SAFETY_MAX_FUNCTION_NAME_LEN` - Maximum function name string length (bytes)"
- `T76_SAFETY_MAX_FILE_NAME_LEN` - Maximum file name string length (bytes)
- `T76_SAFETY_MAX_REBOOTS` - Maximum consecutive reboots before entering safety monitor mode
- `T76_SAFETY_FAULTCOUNT_RESET_SECONDS` - Number of seconds after which reboot counter resets. 0 = no reset
- `T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS` - Hardware watchdog timeout in milliseconds
- `T76_SAFETY_CORE1_HEARTBEAT_TIMEOUT_MS` - Core 1 heartbeat timeout in milliseconds
- `T76_SAFETY_WATCHDOG_TASK_PERIOD_MS` - Watchdog manager task check period in milliseconds
- `T76_SAFETY_WATCHDOG_TASK_PRIORITY` - FreeRTOS priority for watchdog manager task
- `T76_SAFETY_WATCHDOG_TASK_STACK_SIZE` Stack size for watchdog task
- `T76_SAFETY_MAX_REGISTERED_COMPONENTS` - Maximum number of SafeableComponent objects that can be registered
- `T76_SAFETY_MONITOR_USB_TASK_STACK_SIZE` - Stack size for Safety Monitor USB task (words)
- `T76_SAFETY_MONITOR_USB_TASK_PRIORITY` - FreeRTOS priority for Safety Monitor USB task
- `T76_SAFETY_MONITOR_REPORTER_STACK_SIZE` - Stack size for Safety Monitor fault reporter task (words)
- `T76_SAFETY_MONITOR_REPORTER_PRIORITY` - FreeRTOS priority for Safety Monitor fault reporter task
- `T76_SAFETY_MONITOR_REPORT_INTERVAL_MS` - Interval between fault reports in milliseconds
- `T76_SAFETY_MONITOR_CYCLE_DELAY_MS` - Delay between fault reporting cycles in milliseconds

### Triggering faults

While the system catches system-level faults automatically, developers can also trigger custom faults using the `T76::Core::Safety::reportFault()` function. This function allows you to specify the fault type, location, and additional context information.

When a fault is reported, the safety system captures the relevant information and takes appropriate action based on the fault type and system state before safing the system and rebooting.

A convenient macro, `T76_ASSERT(expr, reason)`, is provided to simplify fault triggering based on conditions. This macro evaluates the given expression `expr`, and if it evaluates to false, it triggers a panic fault with the specified `reason`. This allows for concise and readable code when checking for critical conditions.

Unlike the standard C library `assert()` macro, which may be disabled in release builds, the `T76_ASSERT` macro is always active, ensuring that critical safety checks are enforced in all build configurations.

### Providing feedback on faults

The safety system calls the _t76_status_update() function periodically whenever a fault is active. You can override this function in your application code to provide custom feedback on the system status during fault conditions. This can be useful for updating status indicators, logging information, or notifying users of the fault state.

You can see how this can be done in the `blinky` example application included with the IC.

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

### Fault recovery and reboot limiting

The safety system implements a reboot limiting mechanism to prevent continuous reboot loops in the event of persistent faults. The maximum number of consecutive reboots allowed before entering safety monitor mode can be configured using the `T76_SAFETY_MAX_REBOOTS` setting.

After the appropriate number of reboots, the system implements a lockout mechanism that prevents further restarts until the user intervenes by cycling power or issuing a hardware reset. This ensures that the instrument does not enter an endless reboot cycle, allowing for safe recovery and troubleshooting.

After a successful, stable runtime without faults, the reboot counter can be automatically reset after a configurable period. This period can be set using the `T76_SAFETY_FAULTCOUNT_RESET_SECONDS` setting. If set to `0`, the auto-reset feature is disabled.

### System safing and startup sequence

When a critical fault occurs, a reboot is not sufficient to ensure safety, since external devices may still be powered and operating in an unsafe manner. In addition, it is important to bring up the system in a known safe state before attempting normal operation; after all, the failure of a single component should not compromise the safety of the entire instrument.

To address these concerns, the safety system implements a two-shot startup process:

- Upon boot, the system first enters safe mode by asking all components to enter their safe states. This ensures that any external devices are powered down or put into a safe configuration before normal operation begins.

- Next, the system determines whether the previous shutdown was due to a fault. If so, it remains in safe mode, allowing the user to investigate and address the issue before proceeding.

- Otherwise, it proceeds to activate all components and enter normal operation mode.

To participate in this safing mechanisms, components need to implement the `T76::Core::Safety::SafeableComponent` interface, which defines a `makeSafe()` method, which is called during the safing process, and an `activate()` method, which is called to bring the component back to normal operation and returns a boolean indicating success or failure.

Components should register themselves with the safety system using the `T76::Core::Safety::registerSafeableComponent()` function. By default, this is done automatically when the component is constructed, so typically you only need to ensure that your components are statically instantiated before the safety system is initialized.

Note that the system makes no guarantees about the order in which components are safed or activated. Therefore, components should be designed to handle safing and activation independently of other components. Also, the system assumes that components are statically allocated and remain in memory for the lifetime of the program. This is, generally, a good practice for safety-critical components, especially those involved in the safing process.

Finally, because the system uses statically allocated memory to manage the list of registered components, there is a maximum number of components that can be registered. This limit can be configured using the `T76_SAFETY_MAX_REGISTERED_COMPONENTS` macro in the `safety_private.hpp` file.

### Dual-core watchdog initialization

If enabled, the watchdog system must be initialized at startup by calling the `T76::Core::Safety::watchdogInit()` function from core 0. This function sets up the hardware watchdog timer and starts the watchdog feeding task. 

On core 1, bare-metal critical tasks must periodically signal the watchdog feeding task on core 0 by calling the `T76::Core::Safety::feedWatchdogFromCore1()` function to ensure that the watchdog timer is reset and the system remains responsive.

Note that the watchdog system requires both FreeRTOS and the main loop on core 1 to be running in order to function. If you require a lengthy setup process on either core that must block both cores at startup, consider initializing the watchdog system after the setup is complete; otherwise, the watchdog may trigger a reset during the setup phase. (This is also the reason why the watchdog initialization is not included in the main safety initialization function.)

## USB Interface

The IC provides a custom USB interface that supports multiple USB classes:

- USBCDC (Communications Device Class) for serial communication
- USBTMC (Test and Measurement Class) for SCPI command handling
- A vendor class for optional custom functionality and support for Pi Pico's built-in reset functionality

The interface uses TinyUSB as the underlying USB stack, and is designed to provide the same functionality as the standard Pico SDK CDC interface, while adding support for USBTMC, which can then be used to implement SCPI command handling.

### Including and using the USB interface

The interface is already built into the application template, and as such you do not need to do anything special to include it in your project. If you are not using the application template, you can add the `t76_ic_usb` library to your project and add `<t76/usb_interface.hpp>` to your source code.

### Configuration

The USB interface can be configured by changing the following CMake variables in your project's configuration files or build system:

- `T76_IC_USB_INTERFACE_BULK_IN_QUEUE_SIZE` - Size of the bulk IN queue for USB interface (number of messages)
- `T76_IC_USB_RUNTIME_TASK_STACK_SIZE` - Stack size for the USB runtime task (in words)
- `T76_IC_USB_RUNTIME_TASK_PRIORITY` - Priority for the USB runtime task
- `T76_IC_USB_DISPATCH_TASK_STACK_SIZE` - Stack size for the USB dispatch task (in words)
- `T76_IC_USB_DISPATCH_TASK_PRIORITY` - Priority for the USB dispatch task
- `T76_IC_USB_TASK_DELAY_MS` - Delay (in ms) for the USB task loop when idle
- `T76_IC_USB_INTERFACE_BULK_IN_MAX_MESSAGE_SIZE` - Maximum size of a single USB bulk IN message (in bytes)
- `T76_IC_USB_URL` - URL string for the USB WebUSB descriptor
- `T76_IC_USB_VENDOR_ID` - USB Vendor ID
- `T76_IC_USB_PRODUCT_ID` - USB Product ID
- `T76_IC_USB_MANUFACTURER_STRING` - USB Manufacturer String
- `T76_IC_USB_PRODUCT_STRING` - USB Product String

This allows to completely customize the interface to suit your specific application needs, including changing the way it appears to the host system. Note, however, that the reboot functionality relies on the use of the Pi Pico's built-in USB vendor class, so if you change the vendor ID or product ID, you may need to implement your own reboot mechanism.

## SCPI Command Interface

The IC provides a complete SCPI (Standard Commands for Programmable Instruments) command interpreter that allows you to implement industry-standard instrument control over USBTMC. The SCPI system uses a declarative YAML-based approach for defining commands, with automatic code generation for efficient command parsing and execution.

### Overview

The SCPI interpreter uses a trie data structure for efficient command parsing and supports:

- Hierarchical command structures with optional portions (e.g., `LED:STATe` can be entered as `LED:STAT`)
- Multiple parameter types: strings, numbers, booleans, enums, and arbitrary binary data blocks
- Optional parameters with default values
- Automatic parameter validation (type and count checking)
- Query commands (ending with `?`)
- Standard IEEE 488.2 commands (like `*IDN?` and `*RST`)
- Error queue management

### Adding SCPI to Your Project

#### Step 1: Define Your Commands in YAML

Create a YAML file (typically named `scpi.yaml`) that defines all the SCPI commands your instrument will support. Here's the structure:

```yaml
class_name: App              # Your application class name
namespace: T76               # Your namespace
output_file: scpi_commands.cpp  # Generated C++ file name

commands:
  - syntax:       "*IDN?"
    description:  "Query the instrument identification string."
    handler:      _queryIDN

  - syntax:       "*RST"
    description:  "Reset the instrument to its power-on state."
    handler:      _resetInstrument

  - syntax:       "LED:STATe"
    description:  "Set the status of the instrument's LED."
    handler:      _setLEDState
    parameters:
      - name:        state
        type:        enum
        choices:     ["ON", "OFF", "BLINK"]
        description: "The desired LED state."

  - syntax:       "LED:STATe?"
    description:  "Query the current status of the instrument's LED."
    handler:      _queryLEDState
```

**Command Syntax:**
- Use colons (`:`) to separate command hierarchy levels
- Lowercase letters indicate optional portions (e.g., `STATe` can be abbreviated as `STAT`)
- Append `?` for query commands

**Parameter Types:**
- `string`: Text values
- `number`: Numeric values (integers or floating-point)
- `boolean`: True/false values
- `enum`: One of a predefined set of choices
- `arbitrarydata`: Binary data blocks

**Optional Parameters:**
- Add a `default` field to make parameters optional
- All optional parameters must come at the end of the parameter list

#### Step 2: Configure CMake to Generate the Command Trie

Add the following to your `CMakeLists.txt` file to specify your SCPI configuration:

```cmake
set(T76_SCPI_CONFIGURATION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/scpi.yaml)
set(T76_SCPI_OUTPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/scpi_commands.cpp)
```

The build system will automatically run the trie generator script during compilation to create `scpi_commands.cpp` containing:
- The command trie data structure
- Command definitions
- Parameter descriptors

You must also add the generated file to your executable in `CMakeLists.txt`:

```cmake
add_executable(your_project_name
    main.cpp
    app.cpp
    scpi_commands.cpp  # Add the generated file here
    # ... other source files
)
```

#### Step 3: Create Your Application Class

Create an application class that includes the SCPI interpreter and implements command handlers:

```cpp
#include <t76/app.hpp>
#include <t76/scpi_interpreter.hpp>

namespace T76 {
    class App : public T76::Core::App {
    public:
        // Instantiate the interpreter with your class as the template parameter
        T76::SCPI::Interpreter<T76::App> _interpreter;

        App() : _interpreter(*this) {}

        // Override to handle incoming USBTMC data
        void _onUSBTMCDataReceived(const std::vector<uint8_t> &data, 
                                    bool transfer_complete) override {
            // Feed each character to the interpreter
            for (const auto &byte : data) {
                _interpreter.processInputCharacter(byte);
            }
            
            // Finalize command processing when transfer completes
            if (transfer_complete) {
                _interpreter.processInputCharacter('\n');
            }
        }

        // Implement your command handlers
        void _queryIDN(const std::vector<T76::SCPI::ParameterValue> &params) {
            _usbInterface.sendUSBTMCBulkData("MyCompany,MyInstrument,0001,1.0");
        }

        void _resetInstrument(const std::vector<T76::SCPI::ParameterValue> &params) {
            _interpreter.reset();
            // Reset your instrument state here
        }

        void _setLEDState(const std::vector<T76::SCPI::ParameterValue> &params) {
            // Parameters are automatically validated by the interpreter
            std::string state = params[0].stringValue;
            
            if (state == "ON") {
                // Turn LED on
            } else if (state == "OFF") {
                // Turn LED off
            } else if (state == "BLINK") {
                // Make LED blink
            } else {
                // Report semantic error
                _interpreter.addError(202, "Invalid LED state");
            }
        }

        void _queryLEDState(const std::vector<T76::SCPI::ParameterValue> &params) {
            std::string currentState = "OFF"; // Get your actual state
            _usbInterface.sendUSBTMCBulkData(currentState);
        }

        // ... other methods
    };
}
```

#### Step 4: Implement Command Handlers

Each command handler receives a vector of `ParameterValue` objects. The interpreter guarantees that:
- The correct number of parameters is provided
- Each parameter is of the correct type
- Required parameters are present

Access parameter values using:
- `params[i].stringValue` for strings and enums
- `params[i].numberValue` for numbers
- `params[i].boolValue` for booleans
- `params[i].arbitraryData` for binary data

**Error Handling:**
The interpreter handles syntax errors automatically. For semantic errors (invalid values, out-of-range parameters, etc.), report errors using:

```cpp
_interpreter.addError(errorNumber, "Error description");
```

Standard SCPI error codes:
- `-100` series: Command errors
- `-200` series: Execution errors  
- `-300` series: Device-specific errors
- `-400` series: Query errors

#### Step 5: Send Responses

For query commands (ending with `?`), send responses using the USB interface:

```cpp
// For simple strings
_usbInterface.sendUSBTMCBulkData("response string");

// For formatted strings (with quotes and escaping)
std::string formatted = _interpreter.formatString("my string");
_usbInterface.sendUSBTMCBulkData(formatted);

// For arbitrary binary data
std::string preamble = _interpreter.abdPreamble(dataSize);
_usbInterface.sendUSBTMCBulkData(preamble);
_usbInterface.sendUSBTMCBulkData(binaryData, dataSize);
```

### Best Practices

1. **Implement Standard Commands**: Always implement at minimum:
   - `*IDN?`: Identification query
   - `*RST`: Reset command
   - `SYSTem:ERRor?`: Error queue query (for retrieving errors)

2. **Handler Naming**: Use a consistent prefix (like `_query` or `_set`) to distinguish handlers from other methods.

3. **State Management**: The interpreter is stateless between commands. Maintain any necessary state in your application class.

4. **Thread Safety**: The interpreter processes one character at a time and is designed for single-threaded use within the USB task context.

5. **Error Reporting**: Report errors immediately when detected. The error queue is automatically managed by the interpreter.

6. **Testing**: Use the included `scpi_test.py` script or similar tools to test your SCPI implementation over USBTMC.

### Configuration

The SCPI system is integrated with the USB interface. No additional configuration is required beyond what's provided by the `t76_ic_usb` library.

For advanced use cases, you can customize the maximum arbitrary data block size when constructing the interpreter:

```cpp
T76::SCPI::Interpreter<T76::App> _interpreter(*this, 4096);  // 4KB max ABD size
```

