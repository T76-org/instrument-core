# sys library CMakeLists.txt

set(LIBRARY_NAME t76_ic_safety)

add_library(${LIBRARY_NAME} STATIC
    safety.cpp
    safety_components.cpp
    safety_monitor.cpp
    safety_print.cpp
    safety_private.cpp
    safety_watchdog.cpp
    safety_wrappers.cpp
)

# Ensure FREERTOS_CONFIG_DIR is set

if(NOT FREERTOS_CONFIG_DIR)
    message(FATAL_ERROR "FreeRTOSConfig.h not found — please set FREERTOS_CONFIG_DIR")
endif()

# Public include directories (headers that consumers of this library need)
target_include_directories(${LIBRARY_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Private include directories (only needed for building this library)
target_include_directories(${LIBRARY_NAME} PRIVATE
    ${FREERTOS_CONFIG_DIR}
    freertos_kernel
)

# Link required libraries
target_link_libraries(${LIBRARY_NAME} PUBLIC
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    pico_cyw43_arch_none
    pico_multicore
    pico_status_led
    pico_stdio_usb
    pico_stdlib
)

