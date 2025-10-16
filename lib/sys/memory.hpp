/**
 * @file memory.hpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file provides custom memory management functions for the RP2350 platform using FreeRTOS.
 * It overrides the global new and delete operators to use FreeRTOS's pvPortMalloc and
 * vPortFree functions for dynamic memory allocation and deallocation.
 * 
 * It also provides C-style memory management functions like malloc, free, calloc, and realloc
 * to ensure compatibility with C code and libraries.
 * 
 * This ensures that all dynamic memory operations in the project are consistent and 
 * managed by FreeRTOS.
 * 
 * Optionally, by setting T76_USE_GLOBAL_LOCKS to 1, calls to FreeRTOS's memory allocations
 * are wrapped in an SDK mutex to ensure thread safety in a multi-core environment
 * in which FreeRTOS is not aware of the second core.
 * 
 * Do note that using FreeRTOS's memory heap means that the memory avilable for allocation
 * will be limit by the configTOTAL_HEAP_SIZE configuration macro that must be set in 
 * FreeRTOSConfig.h.
 * 
 */

#pragma once


namespace T76::Sys::Memory {
    
    /**
     * @brief Initializes the memory allocation routines, which override
     *        the default malloc/free and new/delete functions and replace
     *        them with FreeRTOS's heap management. 
     *  
     *        By default, the system assumes that _all_ memory allocation
     *        calls will be made either within a FreeRTOS task or in an
     *        otherwise single-threaded context.
     * 
     *        If you with to allocate memory outside of FreeRTOS, you can
     *        set T76_USE_GLOBAL_LOCKS, and the memory functions will use
     *        a bare-metal mutex to gate all allocation calls and prevent
     *        race conditions.
     * 
     */
    void memoryInit();

} // namespace T76::IC::Sys::Memory
