/**
 * @file log.hpp
 * @brief Logging utilities for the DRPD firmware.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file provides a set of macros for logging messages at difference
 * levels of severity: Debug, Warning, Error, Critical.
 * 
 * The verbosity of the logging library can be set using the `LOG_LEVEL`
 * macro. The available levels are:
 * 
 * - `LOG_LEVEL_DEBUG`: Debug messages (lowest level, most verbose)
 * - `LOG_LEVEL_WARNING`: Warning messages
 * - `LOG_LEVEL_ERROR`: Error messages
 * - `LOG_LEVEL_CRITICAL`: Critical messages (highest level, least verbose)
 * 
 */

#pragma once

#include <cstdio>


// Define log levels
#define LOG_LEVEL_DEBUG     0
#define LOG_LEVEL_WARNING   1
#define LOG_LEVEL_ERROR     2
#define LOG_LEVEL_CRITICAL  3

// Set the current log level
#ifndef LOG_LEVEL
#warning "LOG_LEVEL is not defined, defaulting to LOG_LEVEL_CRITICAL"
#define LOG_LEVEL LOG_LEVEL_CRITICAL
#endif

// Logging macros

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOGD(fmt, ...) \
    printf("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define LOGD(fmt, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARNING
#define LOGW(fmt, ...) \
    printf("[WARNING] " fmt, ##__VA_ARGS__)
#else
#define LOGW(fmt, ...)
#endif  

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOGE(fmt, ...) \
    printf("[ERROR] " fmt, ##__VA_ARGS__)
#else
#define LOGE(fmt, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_CRITICAL
#define LOGC(fmt, ...) \
    printf("[CRITICAL] " fmt, ##__VA_ARGS__)
#else
#define LOGC(fmt, ...)
#endif


