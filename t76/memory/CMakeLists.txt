# sys library CMakeLists.txt

set(LIBRARY_NAME t76_ic_memory)

add_library(${LIBRARY_NAME} STATIC
    memory.cpp
)

option(T76_USE_GLOBAL_LOCKS "Enable the use of global locks to allow safe memory allocation from core 1" OFF)

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

# Convert options to defines

target_compile_definitions(${LIBRARY_NAME} PRIVATE
    $<$<BOOL:${T76_USE_GLOBAL_LOCKS}>:T76_USE_GLOBAL_LOCKS>
)

# Link required libraries
target_link_libraries(${LIBRARY_NAME} PUBLIC
    pico_stdlib
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

