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
 * Multi-Core Memory Management:
 * =============================
 * 
 * The memory system supports two modes via T76_USE_GLOBAL_LOCKS:
 * 
 * Mode 1: T76_USE_GLOBAL_LOCKS = 0 (Single-Core Mode)
 * - Assumes only Core 0 (FreeRTOS) performs memory allocation
 * - Direct calls to pvPortMalloc/vPortFree with no synchronization overhead
 * - Minimal code footprint and maximum performance
 * - Use this mode when Core 1 runs bare metal code that doesn't allocate memory
 * 
 * Mode 2: T76_USE_GLOBAL_LOCKS = 1 (Multi-Core Mode)
 * - Supports memory allocation from both Core 0 (FreeRTOS) and Core 1 (bare metal)
 * - Core 0: Direct calls to FreeRTOS heap functions, protected by FreeRTOS scheduler
 * - Core 1: Proxy requests through inter-core FIFO to a memory service task on Core 0
 * - All actual heap operations occur on Core 0, ensuring thread safety
 * - Memory service task runs at high priority to minimize allocation latency
 * - Uses hardware FIFO for efficient inter-core communication
 * 
 * In both modes, all memory comes from the single FreeRTOS heap, ensuring consistent
 * memory management across the entire system. The heap size is controlled by the
 * configTOTAL_HEAP_SIZE macro in FreeRTOSConfig.h.
 * 
 */

#pragma once

namespace T76::Sys::Memory {
    
    /**
     * @brief Initializes the memory allocation routines, which override
     *        the default malloc/free and new/delete functions and replace
     *        them with FreeRTOS's heap management. 
     *  
     *        When T76_USE_GLOBAL_LOCKS is disabled (0):
     *        - Assumes single-core operation (Core 0 only)
     *        - No initialization overhead
     *        - Direct FreeRTOS heap access
     * 
     *        When T76_USE_GLOBAL_LOCKS is enabled (1):
     *        - Enables multi-core memory allocation support
     *        - Starts a memory service task on Core 0 to handle Core 1 requests
     *        - Core 1 can safely allocate/free memory via inter-core communication
     *        - All allocations still come from the single FreeRTOS heap
     * 
     */
    void memoryInit();

} // namespace T76::IC::Sys::Memory
