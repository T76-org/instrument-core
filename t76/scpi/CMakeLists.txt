# SCPI Library CMake Configuration

set(LIBRARY_NAME t76_ic_scpi)

# Ensure required Python modules are installed
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(NOT T76_SCPI_OUTPUT_FILE)
    message(FATAL_ERROR "Please set T76_SCPI_OUTPUT_FILE")
endif()

if(NOT T76_SCPI_CONFIGURATION_FILE)
    message(FATAL_ERROR "Please set T76_SCPI_CONFIGURATION_FILE")
endif()

# Generate commands.cpp from commands.yaml using trie_generator.py
# Use a virtual environment for Python dependencies
add_custom_command(
    OUTPUT ${T76_SCPI_OUTPUT_FILE}
    COMMAND python3 -m venv ${CMAKE_BINARY_DIR}/venv && ${CMAKE_BINARY_DIR}/venv/bin/pip install -r ${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt && ${CMAKE_BINARY_DIR}/venv/bin/python ${CMAKE_CURRENT_SOURCE_DIR}/trie_generator.py ${T76_SCPI_CONFIGURATION_FILE} -o ${T76_SCPI_OUTPUT_FILE}
    DEPENDS ${T76_SCPI_CONFIGURATION_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt
    COMMENT "Setting up virtual environment, installing Python dependencies, and generating SCPI commands.cpp from commands.yaml"
)

# Add a custom target to ensure commands.cpp is built
add_custom_target(
    GenerateSCPICommands
    DEPENDS ${T76_SCPI_OUTPUT_FILE}
)

# Include SCPI library files into the firmware target
set(SCPI_SOURCES
    ${T76_SCPI_OUTPUT_FILE}
    ${CMAKE_CURRENT_SOURCE_DIR}/stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/trie.cpp
)

set(SCPI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/t76/scpi_command.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/t76/scpi_interpreter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/t76/scpi_parameter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/t76/scpi_stream.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/t76/scpi_trie.hpp
)

# Add sources and headers to the firmware target
add_library(${LIBRARY_NAME} STATIC ${SCPI_SOURCES} ${SCPI_HEADERS})

# Explicitly include pico-sdk include directories for SCPI library
target_include_directories(${LIBRARY_NAME} PUBLIC 
    ${PICO_SDK_PATH}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Explicitly link pico_unique_id and other required libraries to SCPI library
target_link_libraries(${LIBRARY_NAME} PUBLIC pico_unique_id pico_stdlib)
