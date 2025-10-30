# Configurable options for the USB interface library

set(T76_IC_USB_INTERFACE_BULK_IN_QUEUE_SIZE 8 CACHE STRING "Size of the bulk IN queue for USB interface (number of messages)")
set(T76_IC_USB_RUNTIME_TASK_STACK_SIZE 256 CACHE STRING "Stack size for the USB runtime task (in words)")
set(T76_IC_USB_RUNTIME_TASK_PRIORITY 1 CACHE STRING "Priority for the USB runtime task")

set(T76_IC_USB_DISPATCH_TASK_STACK_SIZE 256 CACHE STRING "Stack size for the USB dispatch task (in words)")
set(T76_IC_USB_DISPATCH_TASK_PRIORITY 1 CACHE STRING "Priority for the USB dispatch task")
set(T76_IC_USB_TASK_TICK_DELAY 10/portTICK_PERIOD_MS CACHE STRING "Delay (in ticks) for the USB task loop when idle")

set(T76_IC_USB_INTERFACE_BULK_IN_MAX_MESSAGE_SIZE 255 CACHE STRING "Maximum size of a single USB bulk IN message (in bytes)")

set(T76_IC_USB_URL "t76.org" CACHE STRING "URL string for the USB WebUSB descriptor")
set(T76_IC_USB_VENDOR_ID "0x2E8A" CACHE STRING "USB Vendor ID")
set(T76_IC_USB_PRODUCT_ID "0x000A" CACHE STRING "USB Product ID")
set(T76_IC_USB_MANUFACTURER_STRING "Raspberry Pi" CACHE STRING "USB Manufacturer String")
set(T76_IC_USB_PRODUCT_STRING "Pico" CACHE STRING "USB Product String")

