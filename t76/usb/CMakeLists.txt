set(LIBRARY_NAME t76_ic_usb)

include(options.cmake)

add_library(${LIBRARY_NAME} STATIC
    callbacks.cpp
    interface.cpp
    reset_interface.c
    usb_descriptors.c
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
    T76_IC_USB_INTERFACE_BULK_IN_QUEUE_SIZE=${T76_IC_USB_INTERFACE_BULK_IN_QUEUE_SIZE}
    T76_IC_USB_RUNTIME_TASK_STACK_SIZE=${T76_IC_USB_RUNTIME_TASK_STACK_SIZE}
    T76_IC_USB_RUNTIME_TASK_PRIORITY=${T76_IC_USB_RUNTIME_TASK_PRIORITY}
    T76_IC_USB_DISPATCH_TASK_STACK_SIZE=${T76_IC_USB_DISPATCH_TASK_STACK_SIZE}
    T76_IC_USB_DISPATCH_TASK_PRIORITY=${T76_IC_USB_DISPATCH_TASK_PRIORITY}
    T76_IC_USB_TASK_TICK_DELAY=${T76_IC_USB_TASK_TICK_DELAY}
    T76_IC_USB_INTERFACE_BULK_IN_MAX_MESSAGE_SIZE=${T76_IC_USB_INTERFACE_BULK_IN_MAX_MESSAGE_SIZE}
    T76_IC_USB_URL="${T76_IC_USB_URL}"
    T76_IC_USB_VENDOR_ID=${T76_IC_USB_VENDOR_ID}
    T76_IC_USB_PRODUCT_ID=${T76_IC_USB_PRODUCT_ID}
    T76_IC_USB_MANUFACTURER_STRING="${T76_IC_USB_MANUFACTURER_STRING}"
    T76_IC_USB_PRODUCT_STRING="${T76_IC_USB_PRODUCT_STRING}"
)


# Link required libraries
target_link_libraries(${LIBRARY_NAME} PUBLIC
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    pico_stdio_usb
    pico_stdlib
    tinyusb_device 
    tinyusb_board
    t76_ic_utils
)

