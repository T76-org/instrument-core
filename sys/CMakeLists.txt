# sys library CMakeLists.txt

add_library(sys STATIC
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
target_include_directories(sys PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# Private include directories (only needed for building this library)
target_include_directories(sys PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../freertos
)

# Link required libraries
target_link_libraries(sys PUBLIC
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    hardware_sync_spin_lock
    pico_cyw43_arch_none
    pico_multicore
    pico_status_led
    pico_stdlib
    tinyusb_device 
    tinyusb_board    
)

# Set C++ standard if needed
target_compile_features(sys PUBLIC cxx_std_17)
