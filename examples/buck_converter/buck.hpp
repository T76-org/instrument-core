/**
 * @file buck.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#pragma once

#include <t76/safety.hpp>


namespace T76 {

    void _pwmIRQHandler(); // Forward declaration for PWM IRQ handler

    class BuckConverter : public T76::Core::Safety::SafeableComponent {
    public:

        BuckConverter();

        bool activate() override;
        void makeSafe() override;
        const char* getComponentName() const override { return "BuckConverter"; }

        void start();

    protected:

    };

} // namespace T76
