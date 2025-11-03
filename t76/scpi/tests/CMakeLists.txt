cmake_minimum_required(VERSION 3.20)
project(scpi_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)  # Include parent directory for SCPI headers

# Common source files used by all tests
set(COMMON_SOURCES
    concrete_interpreter.cpp
    commands.cpp
    stream_local.cpp
    test_command.cpp
    ../trie.cpp
    ../stream.cpp
)

# Test executables
add_executable(scpi_test
    test_main.cpp
    ${COMMON_SOURCES}
)

add_executable(scpi_comprehensive_test
    comprehensive_test.cpp
    ${COMMON_SOURCES}
)

add_executable(scpi_parameter_limit_test
    parameter_limit_test.cpp
    ${COMMON_SOURCES}
)

add_executable(scpi_simple_debug_test
    simple_debug_test.cpp
    ${COMMON_SOURCES}
)

add_executable(scpi_abd_test
    abd_test.cpp
    ${COMMON_SOURCES}
)

add_executable(scpi_abd_size_limit_test
    abd_size_limit_test.cpp
    ${COMMON_SOURCES}
)

# Register tests with CTest
add_test(NAME BasicFunctionality 
         COMMAND scpi_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME ComprehensiveTest 
         COMMAND scpi_comprehensive_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME ParameterLimitTest 
         COMMAND scpi_parameter_limit_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME DebugTest 
         COMMAND scpi_simple_debug_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME ABDTest 
         COMMAND scpi_abd_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME ABDSizeLimitTest 
         COMMAND scpi_abd_size_limit_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Set test properties for better output
set_tests_properties(BasicFunctionality PROPERTIES
    TIMEOUT 30
    PASS_REGULAR_EXPRESSION "TEST:SIMPLE executed"
)

set_tests_properties(ComprehensiveTest PROPERTIES
    TIMEOUT 60
    PASS_REGULAR_EXPRESSION "=== Test Complete ==="
)

set_tests_properties(ParameterLimitTest PROPERTIES
    TIMEOUT 30
    PASS_REGULAR_EXPRESSION "=== Parameter Limit Test Complete ==="
)

set_tests_properties(DebugTest PROPERTIES
    TIMEOUT 30
)

# Custom test target for running all tests with verbose output
add_custom_target(run_tests
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS scpi_test scpi_comprehensive_test scpi_parameter_limit_test scpi_simple_debug_test
    COMMENT "Building and running all SCPI tests with verbose output"
)

# Link libraries (if any)
# target_link_libraries(scpi_test)
# target_link_libraries(scpi_comprehensive_test)
# target_link_libraries(scpi_parameter_limit_test)
# target_link_libraries(scpi_simple_debug_test)

# Optional: Add a convenience target to build all tests
add_custom_target(build_all_tests
    DEPENDS scpi_test scpi_comprehensive_test scpi_parameter_limit_test scpi_simple_debug_test
    COMMENT "Building all SCPI test executables"
)
