# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Disable malloc/free overrides so that we can provide our own
set(SKIP_PICO_MALLOC 1)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Pull in FreeRTOS Kernel (must be before project)
include(FreeRTOS_Kernel_import.cmake)

project(t76-instrument-core C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

# Include all library source files
file(GLOB app_sources
    "lib/**/*.c*"
)

add_executable(t76-instrument-core 
        main.cpp
        freertos/rp2350.c
        ${app_sources}
)

add_compile_definitions(
    PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1 # Disable new/delete overrides so that we can provide our own.
    NDEBUG                                   # Ensure debug assertions are enabled.
)

pico_set_program_name(t76-instrument-core "t76-instrument-core")
pico_set_program_version(t76-instrument-core "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_usb(t76-instrument-core 1)

# Add the standard library to the build
target_link_libraries(t76-instrument-core
        pico_stdlib
)

# Add the standard include files to the build
target_include_directories(t76-instrument-core PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/freertos
)

# Add any user requested libraries
target_link_libraries(t76-instrument-core 
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap4
        pico_cyw43_arch_none
        pico_status_led
        pico_multicore
        hardware_sync_spin_lock
)

pico_add_extra_outputs(t76-instrument-core)

