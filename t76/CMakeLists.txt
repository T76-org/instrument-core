add_subdirectory(memory)
add_subdirectory(safety)
add_subdirectory(scpi)
add_subdirectory(usb)
add_subdirectory(utils)

# sys library CMakeLists.txt

set(LIBRARY_NAME t76_ic)

add_library(${LIBRARY_NAME} STATIC
    app.cpp
)

# Public include directories (headers that consumers of this library need)
target_include_directories(${LIBRARY_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Ensure FREERTOS_CONFIG_DIR is set
if(NOT FREERTOS_CONFIG_DIR)
    message(FATAL_ERROR "FreeRTOSConfig.h not found — please set FREERTOS_CONFIG_DIR")
endif()

# Private include directories (only needed for building this library)
target_include_directories(${LIBRARY_NAME} PRIVATE
    ${FREERTOS_CONFIG_DIR}
    freertos_kernel
)

# Link required libraries
target_link_libraries(${LIBRARY_NAME} PUBLIC
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    t76_ic_memory
    t76_ic_safety
    t76_ic_scpi
    t76_ic_usb
)

