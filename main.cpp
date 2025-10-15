#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/status_led.h"



int main()
{
    stdio_init_all();
    status_led_init();

    while (true) {
        printf("Hello, world!\n");
        status_led_set_state(!status_led_get_state());
        sleep_ms(100);
    }
}
