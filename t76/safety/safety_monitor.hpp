/**
 * @file safety_monitor.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Safety Monitor - Handles persistent fault detection and reporting
 * 
 * The Safety Monitor is responsible for:
 * - Detecting persistent faults from previous boot cycles
 * - Running a simplified fault reporting application
 * - Providing continuous fault information output
 * 
 * This module is separate from the core safety system to maintain modularity
 * and allow for different fault reporting strategies.
 */

#pragma once

#include "t76/safety.hpp"

namespace T76::Core::SafetyMonitor {

    /**
     * @brief Run the Safety Monitor fault reporting mode
     * 
     * This function runs when a persistent fault is detected at boot time.
     * It initializes minimal systems (stdio, USB, LED) and creates FreeRTOS tasks
     * to continuously output fault information. This function does not return.
     * 
     * The Safety Monitor will:
     * - Initialize USB and stdio for output
     * - Flash the status LED to indicate fault state
     * - Output comprehensive fault information every second
     * - Continue reporting indefinitely until manual system reset
     * 
     * @param fault_info The fault information to report
     */
    void runSafetyMonitor();

} // namespace T76::Core::SafetyMonitor