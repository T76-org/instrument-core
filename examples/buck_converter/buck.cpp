/**
 * @file buck.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "buck.hpp"

#include <cstdint>

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>


using namespace T76;

static uint16_t _top;
static uint8_t _pin;
static uint _slice;


BuckConverter::BuckConverter() : T76::Core::Safety::SafeableComponent() {
    _pin = 15; // Example GPIO pin for PWM output
}

bool BuckConverter::activate() {
    const float freq_hz = 300000.0f;

    gpio_set_function(_pin, GPIO_FUNC_PWM);

    // Get the PWM slice number associated with this GPIO
    _slice = pwm_gpio_to_slice_num(_pin);

    // System clock on RP2350 is typically 150 MHz (default)
    const float sys_clk_hz = clock_get_hz(clk_sys); 

    // Compute divider and top values for the target frequency
    // freq = sys_clk / (divider * (top + 1))
    float divider = 1.0f; // use default divider for simplicity
    _top = (sys_clk_hz / (freq_hz * divider)) - 1.0f;

    // Clamp to 16-bit limit
    if (_top > 65535.0f) {
        // If top exceeds 16-bit range, increase divider accordingly
        divider = sys_clk_hz / (freq_hz * 65536.0f);
        _top = 65535.0f;
    }

    pwm_set_clkdiv(_slice, divider);
    pwm_set_wrap(_slice, static_cast<uint16_t>(_top));

    // Set level to 0 initially
    pwm_set_gpio_level(_pin, 0);

    pwm_set_enabled(_slice, false);

    return true; // Return true if activation is successful
}

void BuckConverter::makeSafe() {
    pwm_set_enabled(_slice, false);
}

void BuckConverter::start() {
    pwm_clear_irq(_slice);
    pwm_set_irq_enabled(_slice, true);
    irq_set_priority(PWM_IRQ_WRAP, 1);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, _pwmIRQHandler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_set_gpio_level(_pin, 0);
    pwm_set_enabled(_slice, true);
}

void T76::_pwmIRQHandler() {
    static uint32_t interruptCounter = 0;
    static float dutyCycle = 0.0f;

    if (interruptCounter++ % 10 != 0) {
        return; // Skip this interrupt to reduce frequency
    }

    T76::Core::Safety::feedWatchdogFromCore1();

    pwm_set_gpio_level(_pin, static_cast<uint16_t>(dutyCycle * _top));
    dutyCycle += 0.000001f;
    if (dutyCycle > 1.0f) {
        dutyCycle = 0.0f;
    }

    pwm_clear_irq(_slice);
}