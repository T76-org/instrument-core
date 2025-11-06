/**
 * @file buck.hpp
 * @brief Buck converter controller class header file
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file contains the declaration of the BuckConverter class which implements
 * a PID-controlled buck converter with safety features and real-time control.
 */

#pragma once

#include <t76/safety.hpp>


namespace T76 {

    /**
     * @brief PWM interrupt request handler
     * 
     * Forward declaration for the PWM IRQ handler function that manages
     * real-time PWM duty cycle updates based on PID controller output.
     */
    void _pwmIRQHandler();

    /**
     * @class BuckConverter
     * @brief PID-controlled buck converter implementation
     * 
     * This class implements a buck converter with PID control for voltage regulation.
     * It extends SafeableComponent to provide safety features and emergency shutdown
     * capabilities. The converter uses PWM to control the switching frequency and
     * maintains stable output voltage through closed-loop feedback control.
     */
    class BuckConverter : public T76::Core::Safety::SafeableComponent {
    public:
        /**
         * @brief Default constructor
         * 
         * Initializes the buck converter with default PID parameters,
         * sets up GPIO pins for PWM output, and configures ADC for
         * voltage sensing.
         */
        BuckConverter();

        /**
         * @brief Activate the buck converter
         * @return true if activation was successful, false otherwise
         * 
         * Activates the buck converter by enabling PWM output, starting the
         * PID control loop, and beginning voltage regulation. This override
         * of the SafeableComponent method ensures proper startup sequence.
         */
        bool activate() override;

        /**
         * @brief Put the buck converter in a safe state
         * 
         * Immediately disables PWM output, stops the PID controller, and
         * puts all hardware in a safe, non-operational state. This override
         * provides emergency shutdown capability.
         */
        void makeSafe() override;

        /**
         * @brief Get the component name identifier
         * @return Component name string "BuckConverter"
         * 
         * Returns the component identifier used for logging and debugging.
         * This override provides the specific name for this component type.
         */
        const char* getComponentName() const override { return "BuckConverter"; }

        /**
         * @brief Set PID controller proportional gain (Kp)
         * @param value New proportional gain value
         * 
         * Sets the proportional gain parameter for the PID controller.
         * Higher values increase the controller's immediate response to error
         * but may cause oscillation if set too high.
         */
        void kP(float value);

        /**
         * @brief Get PID controller proportional gain (Kp)
         * @return Current proportional gain value
         * 
         * Returns the current proportional gain setting of the PID controller.
         */
        float kP() const;

        /**
         * @brief Set PID controller integral gain (Ki)
         * @param value New integral gain value
         * 
         * Sets the integral gain parameter for the PID controller.
         * This term eliminates steady-state error by accumulating error
         * over time. Too high values can cause instability.
         */
        void kI(float value);

        /**
         * @brief Get PID controller integral gain (Ki)
         * @return Current integral gain value
         * 
         * Returns the current integral gain setting of the PID controller.
         */
        float kI() const;

        /**
         * @brief Set PID controller derivative gain (Kd)
         * @param value New derivative gain value
         * 
         * Sets the derivative gain parameter for the PID controller.
         * This term provides damping by responding to the rate of change
         * of error, helping to prevent overshoot and oscillation.
         */
        void kD(float value);

        /**
         * @brief Get PID controller derivative gain (Kd)
         * @return Current derivative gain value
         * 
         * Returns the current derivative gain setting of the PID controller.
         */
        float kD() const;

        /**
         * @brief Set target output voltage setpoint
         * @param value Desired output voltage in volts
         * 
         * Sets the target voltage that the buck converter should regulate to.
         * The PID controller will adjust PWM duty cycle to maintain this voltage
         * at the output.
         */
        void setPoint(float value);

        /**
         * @brief Get target output voltage setpoint
         * @return Current voltage setpoint in volts
         * 
         * Returns the current target voltage that the converter is trying
         * to maintain at its output.
         */
        float setPoint() const;

        /**
         * @brief Get actual sensed output voltage
         * @return Current measured output voltage in volts
         * 
         * Returns the actual output voltage as measured by the ADC
         * feedback circuit. This is the process variable used by
         * the PID controller for regulation.
         */
        float sensedVoltage() const;

        /**
         * @brief Start the buck converter operation
         * 
         * Begins buck converter operation by enabling PWM generation
         * and starting the control loop. This method initiates the
         * voltage regulation process.
         */
        void start();

    protected:

    };

} // namespace T76
