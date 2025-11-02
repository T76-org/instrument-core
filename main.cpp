/**
 * @file main.cpp
 * @brief Main application entry point file
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "app.hpp"


/**
 * @brief Global application instance
 * 
 * Creates the singleton App instance that will be run by main().
 * Construction registers this instance as the global singleton for
 * Core 1 entry point access.
 */
T76::App app;

/**
 * @brief Main entry point for the application.
 * 
 * @return int Exit code (not used)
 */
int main() {
    app.run();
    return 0;
}
