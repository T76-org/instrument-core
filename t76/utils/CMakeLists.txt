set(LIBRARY_NAME t76_ic_utils)

add_library(${LIBRARY_NAME} STATIC
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

# Pass all configuration values as compile definitions
target_compile_definitions(${LIBRARY_NAME} PUBLIC
)


# Link required libraries
target_link_libraries(${LIBRARY_NAME} PUBLIC
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

