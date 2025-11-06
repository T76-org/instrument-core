/**
 * @file buck.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "buck.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>


using namespace T76;

typedef struct {
    float rawSetPoint           = 0.0f;
    float filteredSetPoint      = 0.0f;

    float rawMeasurement        = 0.0f;
    float filteredMeasurement   = 0.0f;

    float error                 = 0.0f;
    float previousError         = 0.0f;

    float integratorState       = 0.0f;
    float filteredDerivativeState = 0.0f;
    float dutyCycle             = 0.0f;
} PIDControllerState;

typedef struct {
    float kP; ///< Proportional coefficient
    float kI; ///< Integral coefficient
    float kD; ///< Derivative coefficient
    float filterTau; ///< Filter time constant
    float filterAlphaR; ///< Filter setpoint coefficient / tick
    float filterAlphaY; ///< Filter measurement coefficient / tick
    float kAntiwindupGain; ///< Anti-windup gain
    float dutyMin; ///< Minimum duty cycle
    float dutyMax; ///> Maximum duty cycle
} PIDControllerParameters;


static uint16_t _pwmTop;
static uint8_t _pwmPin = 15;
static uint _pwmSlice;

static float _sliceFrequency = 30000.0f;
static uint _adcInputPin = 26;
static uint _adcInputChannel = 0;

static PIDControllerParameters _pidParams;
static PIDControllerState _pidControllerState;


BuckConverter::BuckConverter() : T76::Core::Safety::SafeableComponent() {
}

bool BuckConverter::activate() {
    gpio_set_function(_pwmPin, GPIO_FUNC_PWM);

    // Get the PWM slice number associated with this GPIO
    _pwmSlice = pwm_gpio_to_slice_num(_pwmPin);

    // System clock on RP2350 is typically 150 MHz (default)
    const float sys_clk_hz = clock_get_hz(clk_sys); 

    // Compute divider and top values for the target frequency
    // freq = sys_clk / (divider * (top + 1))
    float divider = 1.0f; // use default divider for simplicity
    _pwmTop = (sys_clk_hz / (_sliceFrequency * divider)) - 1.0f;

    // Clamp to 16-bit limit
    if (_pwmTop > 65535.0f) {
        // If top exceeds 16-bit range, increase divider accordingly
        divider = sys_clk_hz / (_sliceFrequency * 65536.0f);
        _pwmTop = 65535.0f;
    }

    pwm_set_clkdiv(_pwmSlice, divider);
    pwm_set_wrap(_pwmSlice, static_cast<uint16_t>(_pwmTop));

    // Set level to 0 initially
    pwm_set_gpio_level(_pwmPin, 0);

    pwm_set_enabled(_pwmSlice, false);

    // Set up ADC

    adc_init();
    adc_gpio_init(_adcInputPin); // Example GPIO for ADC input

    // Set parameters

    const float sliceTime = 1.0f / _sliceFrequency;

    _pidParams = {
        .kP = 0.08f,
        .kI = 754.0f,
        .kD = 27e-6f / 10.0f,
        .filterTau = 3.18e-6f, // 50kHz corner
        .filterAlphaR = 2.0f * (float)M_PI * 800.0f * sliceTime,
        .filterAlphaY = 2.0f * (float)M_PI * 4000.0f * sliceTime,
        .kAntiwindupGain = _sliceFrequency * 0.1f,
        .dutyMin = 0.0f,
        .dutyMax = 0.95f
    };

    if (_pidParams.filterAlphaR > 1.0f) {
        _pidParams.filterAlphaR = 1.0f;
    }

    if (_pidParams.filterAlphaY > 1.0f) {
        _pidParams.filterAlphaY = 1.0f;
    }

    return true; // Return true if activation is successful
}

void BuckConverter::makeSafe() {
    pwm_set_enabled(_pwmSlice, false);
}

void BuckConverter::start() {
    pwm_clear_irq(_pwmSlice);
    pwm_set_irq_enabled(_pwmSlice, true);
    irq_set_priority(PWM_IRQ_WRAP, 1);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, _pwmIRQHandler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_set_gpio_level(_pwmPin, 0);
    pwm_set_enabled(_pwmSlice, true);
}

void BuckConverter::kP(float value) {
    _pidParams.kP = value;
}

float BuckConverter::kP() const {
    return _pidParams.kP;
}

void BuckConverter::kI(float value) {
    _pidParams.kI = value;
}

float BuckConverter::kI() const {
    return _pidParams.kI;
}

void BuckConverter::kD(float value) {
    _pidParams.kD = value;
}

float BuckConverter::kD() const {
    return _pidParams.kD;
}

void BuckConverter::setPoint(float value) {
    _pidControllerState.rawSetPoint = value;
}

float BuckConverter::setPoint() const {
    return _pidControllerState.filteredSetPoint;
}

float BuckConverter::sensedVoltage() const {
    return _pidControllerState.filteredMeasurement;
}

/**
 * @brief PWM interrupt handler implementing the PID control loop
 * 
 * This function is called at the PWM frequency (30kHz) to execute the PID control algorithm
 * that regulates the buck converter output voltage. The PID controller uses a discrete-time
 * implementation with the following key features:
 * 
 * 1. Input/Output Filtering: Both setpoint and measurement are low-pass filtered to reduce
 *    noise and prevent derivative kick on setpoint changes.
 * 
 * 2. PID Terms:
 *    - P (Proportional): Provides immediate response proportional to current error
 *    - I (Integral): Eliminates steady-state error by accumulating error over time
 *    - D (Derivative): Provides damping based on rate of error change to reduce overshoot
 * 
 * 3. Numerical Integration: Uses trapezoidal rule for more accurate integral calculation
 *    compared to simple rectangular integration.
 * 
 * 4. Derivative Filtering: The derivative term is low-pass filtered to reduce high-frequency
 *    noise amplification that could destabilize the system.
 * 
 * 5. Anti-windup: Prevents integrator windup during output saturation using back-calculation
 *    method, keeping the controller responsive when saturation limits are removed.
 * 
 * The control equation is: u(t) = Kp*e(t) + Ki*∫e(t)dt + Kd*de(t)/dt
 * Where: e(t) = setpoint - measurement (error signal)
 */
void T76::_pwmIRQHandler() {
 
    // Calculate the time step for this control iteration (constant at 30kHz = 33.33μs)
    static const float sliceTime = 1.0f / _sliceFrequency;

    // Feed safety watchdog to prevent system reset due to tight timing loop
    T76::Core::Safety::feedWatchdogFromCore1();

    // === STEP 1: INPUT FILTERING ===
    // Apply first-order low-pass filters to both setpoint and measurement to reduce noise
    // and prevent derivative kick on setpoint changes. The filter equation is:
    // y[n] = y[n-1] + α*(x[n] - y[n-1]) where α determines the filter bandwidth
    _pidControllerState.filteredSetPoint += _pidParams.filterAlphaR * (_pidControllerState.rawSetPoint - _pidControllerState.filteredSetPoint);

    // === STEP 2: MEASUREMENT ACQUISITION ===
    // Read ADC value and convert to actual voltage considering:
    // - 12-bit ADC: 4095 max value
    // - 3.3V reference voltage
    // - 2x voltage divider in hardware (so multiply by 2 to get actual voltage)
    adc_select_input(_adcInputChannel);
    _pidControllerState.rawMeasurement = adc_read() * 3.3f / 4095.0f * 2.0f;
    
    // Filter the measurement to reduce ADC noise and improve control stability
    _pidControllerState.filteredMeasurement += _pidParams.filterAlphaY * (_pidControllerState.rawMeasurement - _pidControllerState.filteredMeasurement);

    // === STEP 3: ERROR CALCULATION ===
    // Store previous error for derivative and integral calculations
    _pidControllerState.previousError = _pidControllerState.error;
    // Calculate current error: positive error means output voltage is too low
    _pidControllerState.error = _pidControllerState.filteredSetPoint - _pidControllerState.filteredMeasurement;

    // === STEP 4: INTEGRAL TERM (I) ===
    // Use trapezoidal integration rule for better accuracy: ∫e dt ≈ (e[n] + e[n-1]) * Δt / 2
    // This accumulates error over time to eliminate steady-state offset
    _pidControllerState.integratorState += _pidParams.kI * sliceTime * 0.5f * (_pidControllerState.error + _pidControllerState.previousError);

    // === STEP 5: DERIVATIVE TERM (D) ===
    // Calculate filtered derivative using first-order low-pass filter to reduce noise amplification
    // The derivative is computed on the error signal: de/dt ≈ (e[n] - e[n-1]) / Δt
    // Filter coefficients for discrete first-order LPF: a = τ/(τ+Δt), b = Kd/(τ+Δt)
    const float a = _pidParams.filterTau / (_pidParams.filterTau + sliceTime);
    const float b = _pidParams.kD / (_pidParams.filterTau + sliceTime);
    
    // Apply the filtered derivative: y[n] = a*y[n-1] + b*(x[n] - x[n-1])
    _pidControllerState.filteredDerivativeState = a * _pidControllerState.filteredDerivativeState + b * (_pidControllerState.error - _pidControllerState.previousError);

    // === STEP 6: PID OUTPUT CALCULATION ===
    // Combine all three PID terms: u = Kp*e + Ki*∫e + Kd*de/dt
    float unclampedDutyCycle = _pidParams.kP * _pidControllerState.error + _pidControllerState.integratorState + _pidControllerState.filteredDerivativeState;
    
    // === STEP 7: OUTPUT SATURATION ===
    // Clamp the duty cycle to physical limits (0% to 95% typically for buck converters)
    // This prevents damage and ensures stable operation
    float dutyCycle = std::clamp(unclampedDutyCycle, _pidParams.dutyMin, _pidParams.dutyMax);

    // === STEP 8: ANTI-WINDUP (BACK-CALCULATION) ===
    // When the output saturates, the integrator can continue to accumulate error (windup),
    // causing poor transient response when saturation is removed. Back-calculation method
    // feeds the difference between clamped and unclamped output back to the integrator
    // with gain kAntiwindupGain to prevent this windup condition.
    _pidControllerState.integratorState += _pidParams.kAntiwindupGain * (dutyCycle - unclampedDutyCycle);

    // Store the final duty cycle for next iteration
    _pidControllerState.dutyCycle = dutyCycle;

    // === STEP 9: PWM OUTPUT UPDATE ===
    // Convert duty cycle (0.0-1.0) to PWM compare value and update hardware
    // The PWM peripheral compares this value against the counter to generate the switching signal
    pwm_set_gpio_level(_pwmPin, static_cast<uint16_t>(_pidControllerState.dutyCycle * _pwmTop));
    
    // Clear the PWM interrupt flag to acknowledge this interrupt and prepare for the next cycle
    pwm_clear_irq(_pwmSlice);
}