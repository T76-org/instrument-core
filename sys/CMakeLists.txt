# sys library CMakeLists.txt

set(LIBRARY_NAME sys)

add_library(${LIBRARY_NAME} STATIC
    memory.cpp
    safety.cpp
    safety_components.cpp
    safety_monitor.cpp
    safety_print.cpp
    safety_private.cpp
    safety_watchdog.cpp
    safety_wrappers.cpp
)

# Public include directories (headers that consumers of this library need)
target_include_directories(${LIBRARY_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Private include directories (only needed for building this library)
target_include_directories(${LIBRARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../freertos
)

# Link required libraries
target_link_libraries(${LIBRARY_NAME} PUBLIC
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    hardware_sync_spin_lock
    pico_cyw43_arch_none
    pico_multicore
    pico_status_led
    pico_stdio_usb
    pico_stdlib
)

