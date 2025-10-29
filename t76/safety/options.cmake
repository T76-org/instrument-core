# Configurable options for the safety library

# Fault Information Limits ===
SET(T76_SAFETY_MAX_FAULT_DESC_LEN 128 CACHE STRING "Maximum fault description string length (bytes)")
SET(T76_SAFETY_MAX_FUNCTION_NAME_LEN 64 CACHE STRING "Maximum function name string length (bytes)")
SET(T76_SAFETY_MAX_FILE_NAME_LEN 128 CACHE STRING "Maximum file name string length (bytes)")

# Safety Recovery Configuration ===
SET(T76_SAFETY_MAX_REBOOTS 3 CACHE STRING "Maximum consecutive reboots before entering safety monitor mode")
SET(T76_SAFETY_FAULTCOUNT_RESET_SECONDS 0 CACHE STRING "Number of seconds after which reboot counter resets. 0 = no reset")

# Dual-Core Watchdog System Configuration ===
SET(T76_SAFETY_DEFAULT_WATCHDOG_TIMEOUT_MS 5000 CACHE STRING "Hardware watchdog timeout in milliseconds (5 seconds)")
SET(T76_SAFETY_CORE1_HEARTBEAT_TIMEOUT_MS 2000 CACHE STRING "Core 1 heartbeat timeout in milliseconds (2 seconds)")
SET(T76_SAFETY_WATCHDOG_TASK_PERIOD_MS 500 CACHE STRING "Watchdog manager task check period in milliseconds (500ms)")
SET(T76_SAFETY_WATCHDOG_TASK_PRIORITY 1 CACHE STRING "FreeRTOS priority for watchdog manager task (lowest priority)")
SET(T76_SAFETY_WATCHDOG_TASK_STACK_SIZE "(configMINIMAL_STACK_SIZE * 2)" CACHE STRING "Stack size for watchdog task")

# Component Registry Configuration ===
SET(T76_SAFETY_MAX_REGISTERED_COMPONENTS 32 CACHE STRING "Maximum number of SafeableComponent objects that can be registered")

# Safety Monitor Configuration ===
SET(T76_SAFETY_MONITOR_USB_TASK_STACK_SIZE 256 CACHE STRING "Stack size for Safety Monitor USB task (words)")
SET(T76_SAFETY_MONITOR_USB_TASK_PRIORITY 1 CACHE STRING "FreeRTOS priority for Safety Monitor USB task")
SET(T76_SAFETY_MONITOR_REPORTER_STACK_SIZE 256 CACHE STRING "Stack size for Safety Monitor fault reporter task (words)")
SET(T76_SAFETY_MONITOR_REPORTER_PRIORITY 2 CACHE STRING "FreeRTOS priority for Safety Monitor fault reporter task")
SET(T76_SAFETY_MONITOR_REPORT_INTERVAL_MS 1000 CACHE STRING "Interval between fault reports in milliseconds (1 second)")
SET(T76_SAFETY_MONITOR_CYCLE_DELAY_MS 2000 CACHE STRING "Delay between fault reporting cycles in milliseconds (2 seconds)")
