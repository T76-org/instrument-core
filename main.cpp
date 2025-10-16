#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/status_led.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tusb.h"
#include "pico/multicore.h"

/**
 * @brief Task to handle TinyUSB events. This will run on core 0 because it is
 *        executed in a FreeRTOS task.
 * 
 * @param params Unused parameter for task function signature.
 */
void tinyUSBTask(void *params) {
    (void) params;

    // Initialize TinyUSB
    tusb_init();

    // Main loop for TinyUSB task
    while (true) {
        tud_task(); // tinyusb device task
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Task to print messages to the console. This will run on core 0 and
 *        confirms that FreeRTOS is functioning correctly.
 * 
 * @param params 
 */
void printTask(void *params) {
    int count = 0;

    while (true) {
        printf("Hello from core %d! Count: %d\n", get_core_num(), count++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief This task runs on core 1 and prints messages to the console. It
 *        does not use FreeRTOS and demonstrates that both cores are active.
 * 
 */
void core1Task() {
    int count = 0;

    while (true) {
        printf("Hello from core %d! Count: %d\n", get_core_num(), count++);
        sleep_ms(1000);
    }
}

/**
 * @brief Main entry point for the application.
 * 
 * @return int Exit code (not used)
 */
int main() {
    // Initialize stdio and status LED
    stdio_init_all();
    status_led_init();

    // Reset core 1 and start a new task in it.
    // This must be done before starting the FreeRTOS scheduler.
    multicore_reset_core1();
    multicore_launch_core1(core1Task);

    // Create FreeRTOS tasks
    xTaskCreate(
        tinyUSBTask,
        "tusb",
        256,
        NULL,
        1,
        NULL
    );

    xTaskCreate(
        printTask,
        "print",
        256,
        NULL,
        1,
        NULL
    );

    // Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here
    for(;;){}
}
