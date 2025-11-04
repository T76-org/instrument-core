# SCPI Test Harness

## Overview
This test harness provides a comprehensive testing environment for the SCPI (Standard Commands for Programmable Instruments) interpreter system. It includes a complete concrete implementation of the interpreter with 20 test commands that exercise all major functionality.

## Architecture

The test harness consists of several key components:

### Core Components

- **`ConcreteInterpreter`** - A concrete implementation of the SCPI interpreter that handles test commands
- **Test Streams** - Mock input/output streams for testing command processing
- **Command Definition** - YAML-based command specification that generates the command trie
- **Test Executables** - Multiple test programs for different testing scenarios

### Test Command Structure

Commands are defined in `commands.yaml` and automatically generate:
- **Trie Structure** - Efficient command parsing using a character trie
- **Parameter Descriptors** - Type-safe parameter definitions
- **Handler Functions** - C++ methods that execute when commands are matched

## Test Organization

The test suite is organized into several categories:

### Basic Command Tests
- **Simple Commands** - Commands without parameters (`TEST:SIMPLE`)
- **Query Commands** - Commands that return responses (`TEST:SIMPLE?`)
- **Mixed Case** - Commands with optional syntax elements (`TEST:COMmand:OPTional:SYNtax`)

### Parameter Type Tests
- **Numeric Parameters** - Integer and floating-point values
- **String Parameters** - Text input handling
- **Boolean Parameters** - True/false values
- **Enum Parameters** - Predefined choice lists

### Advanced Features
- **Multiple Parameters** - Commands accepting multiple arguments
- **Optional Parameters** - Commands with optional arguments
- **Command Abbreviation** - SCPI standard abbreviation rules
- **Error Handling** - Invalid command and parameter testing

## Available Test Programs

The test harness includes four test executables, all integrated with CMake's CTest system:

### `scpi_test` (BasicFunctionality)
Basic functionality test that executes a single command to verify the interpreter is working.

### `scpi_comprehensive_test` (ComprehensiveTest)
Complete test suite that exercises all command types and variations:
- Tests all 20 defined commands
- Verifies parameter parsing for different types
- Validates query command responses
- Tests command abbreviation support

### `scpi_parameter_limit_test` (ParameterLimitTest)
Specialized test that validates parameter count limits and error handling.

### `scpi_simple_debug_test` (DebugTest)
Debug utility for testing specific parameter handling scenarios.

## Test Commands Reference

The test suite includes the following commands:

### Basic Commands
- `TEST:SIMPLE` - Simple command without parameters
- `TEST:SIMPLE?` - Simple query command
- `TEST:COMmand:OPTional:SYNtax` - Mixed case command testing

### Parameter Type Testing
- `TEST:NUMBER` - Numeric parameter handling
- `TEST:STRING` - String parameter handling  
- `TEST:BOOLEAN` - Boolean parameter handling
- `TEST:ENUM` - Enumerated parameter handling

### Multiple Parameters
- `TEST:MULTI:TWO` - Command with two parameters (number + string)
- `TEST:OPTIONAL:SINGLE` - Command with optional parameter
- `TEST:OPTIONAL:MULTIPLE` - Command with multiple optional parameters

### Numeric Variations
- `TEST:NUMERIC:INTEGER` - Integer value handling
- `TEST:NUMERIC:FLOAT` - Floating point value handling
- `TEST:NUMERIC:RANGE` - Range parameters (min/max)

### String Variations
- `TEST:STRING:QUOTED` - Quoted string handling

### Enum Variations
- `TEST:ENUM:MIXED` - Mixed case enum options
- `TEST:ENUM:NUMERIC` - Numeric-like enum options

### Query Commands
- `TEST:QUERY:PARAM?` - Query with single parameter
- `TEST:QUERY:MULTI?` - Query with multiple parameters

### Error Handling
- `TEST:ERROR:SIMULATE` - Simulated error conditions
- `TEST:ERROR:INVALID` - Invalid operation testing

## Key Features

1. **Command Abbreviation Support** - Commands can be abbreviated using SCPI standard rules
2. **Parameter Type Safety** - Proper parsing and validation of different parameter types
3. **Memory Efficiency** - Optimized for RP2040 microcontroller constraints
4. **Comprehensive Testing** - Full test coverage of command variations
5. **Error Handling** - Proper error reporting for invalid commands and parameters
6. **CMake Integration** - Full integration with CMake/CTest for automated testing
7. **Cross-Platform Build** - Uses only CMake with no make/ninja dependencies

## How to Build and Run Tests

### CMake-Only Approach

This testing harness uses only CMake for building and testing, eliminating dependencies on `make`, `ninja`, or other build systems. This provides:

- **Cross-platform compatibility** - Works on Windows, macOS, and Linux
- **Single tool dependency** - Only CMake is required
- **Consistent behavior** - Same commands work across all platforms
- **CI/CD friendly** - Standard CMake workflow for automated builds

### Building the Tests

The build process is designed to work from a clean state. The `build/` directory contains only generated artifacts and is not tracked in git.

```bash
cd lib/scpi/tests
mkdir -p build && cd build
cmake ..
cmake --build .
```

**Note:** The `build/` directory is automatically created and contains only:
- Compiled executables
- Object files
- CMake cache and configuration files
- Test artifacts

This directory can be safely deleted and recreated at any time. It should never be committed to your repository.

### Running Tests

You can run tests in several ways:

#### Option 1: Using CMake/CTest (Recommended)

```bash
# Run all tests
cmake --build . --target all && ctest

# Run tests with verbose output
cmake --build . --target all && ctest --verbose

# Run a specific test
cmake --build . --target all && ctest -R BasicFunctionality

# Run tests in parallel
cmake --build . --target all && ctest -j4

# Build and run tests in one command
cmake --build . && ctest --verbose

# Use the custom target (builds and runs with verbose output)
cmake --build . --target run_tests
```

**Advanced Options:**
```bash
# Build in release mode for performance testing
cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build .

# Build with specific generator (if needed)
cmake -G "Unix Makefiles" .. && cmake --build .

# Parallel build (uses all available cores)
cmake --build . --parallel
```

#### Option 2: Direct Execution

```bash
# Run individual test executables directly
./scpi_test
./scpi_comprehensive_test
./scpi_parameter_limit_test
./scpi_simple_debug_test
```

##### Option 3: Using VS Code Task

The workspace includes a VS Code task for running tests:

1. Open the Command Palette (`Ctrl+Shift+P` or `Cmd+Shift+P`)
2. Type "Tasks: Run Task" and select it
3. Choose "Run SCPI Tests"

This will automatically build and run all tests with verbose output.

## Test Results

CMake/CTest will show results like:
```
Test project /path/to/build
    Start 1: BasicFunctionality
1/4 Test #1: BasicFunctionality ...........   Passed    0.01 sec
    Start 2: ComprehensiveTest
2/4 Test #2: ComprehensiveTest ............   Passed    0.05 sec
    Start 3: ParameterLimitTest
3/4 Test #3: ParameterLimitTest ...........   Passed    0.02 sec
    Start 4: DebugTest
4/4 Test #4: DebugTest ....................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 4
```

### Expected Output

#### Using CTest
When running with `ctest --verbose`, you'll see detailed test output:
```
1: Test command: /path/to/scpi_test
1: Test timeout computed to be: 30
1: Output: TEST:SIMPLE executed
1: 
1/4 Test #1: BasicFunctionality ...........   Passed    0.01 sec
```

#### Direct Execution
When running executables directly, the comprehensive test should output results like:
```
=== SCPI Interpreter Comprehensive Test ===

--- Testing: TEST:SIMPLE ---
Output: TEST:SIMPLE executed
✓ PASS

--- Testing: TEST:SIMPLE? ---
Output: SIMPLE_QUERY_RESPONSE
✓ PASS

--- Testing: TEST:NUMBER 42.5 ---
Output: TEST:NUMBER executed with value: 42.500000
✓ PASS
```

## Adding New Tests

### 1. Define New Commands

Add new commands to `commands.yaml`:

```yaml
- syntax:       "YOUR:NEW:COMMAND"
  parameters:
    - name: "param1"
      type: "number"
    - name: "param2"  
      type: "string"
  description:  "Description of your new command"
  handler:      _yourNewCommandHandler
```

### 2. Implement Handler Methods

Add handler method declarations to `concrete_interpreter.hpp`:

```cpp
// In the public section of ConcreteInterpreter class
void _yourNewCommandHandler(const std::vector<ParameterValue> &params);
```

Add handler method implementations to `concrete_interpreter.cpp`:

```cpp
void ConcreteInterpreter::_yourNewCommandHandler(const std::vector<ParameterValue> &params) {
    // Your command logic here
    _output.write("YOUR:NEW:COMMAND executed");
    if (!params.empty()) {
        _output.write(" with parameters: ");
        _output.write(_formatParameters(params).c_str());
    }
    _output.write("\n");
}
```

### 3. Regenerate Command Data

After modifying `commands.yaml`, regenerate `commands.cpp` using the command generation tool (this step may require additional tooling not shown in the current files).

### 4. Add Test Cases

Add test cases to the appropriate test file (`comprehensive_test.cpp` or create a new test file):

```cpp
// Add to main() function in test file
test_command("YOUR:NEW:COMMAND 123 test", "YOUR:NEW:COMMAND executed");
```

### 5. Update CMakeLists.txt

If you create a new test executable, add it to `CMakeLists.txt`:

```cmake
# Add the executable
add_executable(your_new_test
    your_new_test.cpp
    ${COMMON_SOURCES}
)

# Register it with CTest
add_test(NAME YourNewTest 
         COMMAND your_new_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Set test properties
set_tests_properties(YourNewTest PROPERTIES
    TIMEOUT 30
    PASS_REGULAR_EXPRESSION "Expected success pattern"
)

# Update the custom targets
add_custom_target(build_all_tests
    DEPENDS scpi_test scpi_comprehensive_test scpi_parameter_limit_test scpi_simple_debug_test your_new_test
    COMMENT "Building all SCPI test executables"
)
```

### 6. Build and Test

```bash
cd build
cmake --build .
ctest
# or build and test in one command
cmake --build . && ctest --verbose
```

## Test Development Best Practices

### Command Naming
- Use hierarchical naming: `SUBSYSTEM:FUNCTION:OPTION`
- Follow SCPI conventions for abbreviations
- Use consistent parameter naming

### Handler Implementation
- Always provide meaningful output
- Handle all parameter types correctly
- Use the `_formatParameters()` helper for consistent output
- Include error handling for invalid parameters

### Test Coverage
- Test both command execution and query responses
- Test with various parameter combinations
- Test error conditions and edge cases
- Test command abbreviations

### Memory Considerations
- The system is optimized for RP2040 constraints
- Monitor memory usage when adding many commands
- Consider parameter count limits (currently 3 max)

## Troubleshooting

### Common Issues

1. **Command Not Found**: Ensure the command is properly defined in `commands.yaml` and `commands.cpp` is regenerated
2. **Handler Not Found**: Verify handler method is declared in header and implemented in cpp file
3. **Parameter Parsing Errors**: Check parameter type definitions match expected usage
4. **Build Errors**: Ensure all source files are included in `CMakeLists.txt`

### Debug Tips

- Use `ctest --verbose` to see detailed test output
- Use `ctest -R TestName` to run only specific tests
- Use `scpi_simple_debug_test` to test specific scenarios
- Check the trie structure generation for command parsing issues
- Verify parameter descriptors match the command definitions
- Use the memory test utilities to check resource usage

### CTest Integration

The test harness integrates with CMake's CTest system providing:
- **Automated test discovery** - All tests are automatically registered
- **Timeout protection** - Tests that hang will be terminated
- **Parallel execution** - Multiple tests can run simultaneously
- **Pattern matching** - Tests can be filtered by name or regex
- **Success/failure detection** - Tests validate based on output patterns and exit codes

### Build Targets

The CMakeLists.txt provides several useful targets:
- `cmake --build .` - Build all executables
- `cmake --build . --target build_all_tests` - Build all test executables
- `cmake --build . --target run_tests` - Build and run all tests with verbose output
- `ctest` - Run all tests (must build first)
- `ctest --verbose` - Run tests with detailed output

### Repository Structure

The testing harness follows best practices for build artifacts:
- **Source files** (`.cpp`, `.hpp`, `.yaml`, `CMakeLists.txt`) are tracked in git
- **Build directory** (`build/`) is ignored and contains only generated artifacts
- **Generated files** (executables, object files, CMake cache) are never committed
- **Clean builds** are supported - you can delete `build/` and recreate it anytime

### Starting Fresh

To start with a completely clean build:
```bash
cd lib/scpi/tests
rm -rf build  # Remove any existing build artifacts
mkdir -p build && cd build
cmake .. && cmake --build . && ctest
```
