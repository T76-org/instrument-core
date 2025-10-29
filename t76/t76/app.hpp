/**
 * @file app.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * 
 * 
 */

#pragma once
#include <stdio.h>
#include <cstdlib>
#include <functional>

#include <FreeRTOS.h>
#include <task.h>
#include <tusb.h>

#include <pico/cyw43_arch.h>
#include <pico/multicore.h>
#include <pico/status_led.h>

#include <t76/memory.hpp>
#include <t76/safety.hpp>


namespace T76::Sys {

    class App : public T76::Sys::Safety::SafeableComponent {
    public:
        App();

        virtual void run();

    protected:
        static App *_globalInstance;

        static void _core1EntryPoint() {
            if (_globalInstance) {
                _globalInstance->_startCore1();
            }
        }

        virtual void _init() {};
        virtual void _initCore0() = 0;
        virtual void _startCore1() = 0;
    };
    
} // namespace T76::Sys
