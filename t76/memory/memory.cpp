/**
 * @file memory.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 */

#include "t76/memory.hpp"

#include <cstring>
#include <FreeRTOS.h>
#include <pico/sync.h>
#include <hardware/sync.h>

#ifdef T76_USE_GLOBAL_LOCKS
#include <task.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>

// Inter-core memory allocation commands
#define MEMORY_CMD_ALLOC 0x80000000
#define MEMORY_CMD_FREE  0x80000001

// Forward declaration
static void memoryServiceTask(void* pvParameters);
#endif

void T76::Core::Memory::init() {
    #ifdef T76_USE_GLOBAL_LOCKS
        // Start memory service task on core 0 to handle core 1 requests
        xTaskCreate(memoryServiceTask, "MemSvc", 512, NULL, configMAX_PRIORITIES - 1, NULL);
    #endif
}

#ifdef T76_USE_GLOBAL_LOCKS
/**
 * @brief FreeRTOS task that handles memory allocation requests from Core 1
 * 
 * This task runs on Core 0 and processes memory allocation and deallocation
 * requests from Core 1 via the inter-core FIFO. It ensures all heap operations
 * are performed within the FreeRTOS environment on Core 0.
 * 
 * @param pvParameters Unused FreeRTOS task parameter
 */
static void memoryServiceTask(void* pvParameters) {
    while (1) {
        if (multicore_fifo_rvalid()) {
            uint32_t cmd = multicore_fifo_pop_blocking();
            
            if ((cmd & 0xFF000000) == 0x80000000) {
                // Memory command
                if (cmd == MEMORY_CMD_ALLOC) {
                    // Get size from next FIFO entry
                    size_t size = (size_t)multicore_fifo_pop_blocking();
                    void* ptr = pvPortMalloc(size);
                    multicore_fifo_push_blocking((uint32_t)ptr);
                    
                } else if (cmd == MEMORY_CMD_FREE) {
                    // Get pointer from next FIFO entry
                    void* ptr = (void*)multicore_fifo_pop_blocking();
                    if (ptr) {
                        vPortFree(ptr);
                    }
                    multicore_fifo_push_blocking(0); // Acknowledge
                }
            }
        }
        
        // Small delay to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Core 1 proxy function for memory allocation
 * 
 * Sends allocation request to Core 0 via inter-core FIFO and waits for response.
 * This function blocks until Core 0 processes the request.
 * 
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if allocation failed
 */
static void* core1_alloc_proxy(size_t size) {
    multicore_fifo_push_blocking(MEMORY_CMD_ALLOC);
    multicore_fifo_push_blocking((uint32_t)size);
    return (void*)multicore_fifo_pop_blocking();
}

/**
 * @brief Core 1 proxy function for memory deallocation
 * 
 * Sends free request to Core 0 via inter-core FIFO and waits for acknowledgment.
 * This function blocks until Core 0 processes the request.
 * 
 * @param ptr Pointer to memory to free
 */
static void core1_free_proxy(void* ptr) {
    multicore_fifo_push_blocking(MEMORY_CMD_FREE);
    multicore_fifo_push_blocking((uint32_t)ptr);
    multicore_fifo_pop_blocking(); // Wait for acknowledge
}
#endif

/**
 * @brief Core memory allocation function
 * 
 * Allocates memory from the FreeRTOS heap. Behavior depends on T76_USE_GLOBAL_LOCKS:
 * - When enabled: Core 0 allocates directly, Core 1 proxies through Core 0
 * - When disabled: Direct allocation (assumes single-core usage)
 * 
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if allocation failed
 */
void *T76MemoryAlloc(size_t size) {
    #ifdef T76_USE_GLOBAL_LOCKS
        if (get_core_num() == 0) {
            // Core 0: Direct FreeRTOS allocation
            return pvPortMalloc(size);
        } else {
            // Core 1: Proxy through core 0
            return core1_alloc_proxy(size);
        }
    #else
        // Single core mode - assume only core 0 allocates
        return pvPortMalloc(size);
    #endif
}

/**
 * @brief Core memory deallocation function
 * 
 * Frees memory back to the FreeRTOS heap. Behavior depends on T76_USE_GLOBAL_LOCKS:
 * - When enabled: Core 0 frees directly, Core 1 proxies through Core 0
 * - When disabled: Direct deallocation (assumes single-core usage)
 * 
 * @param ptr Pointer to memory to free (NULL is safely ignored)
 */
void T76MemoryFree(void* ptr) {
    if (ptr == NULL) return;
    
    #ifdef T76_USE_GLOBAL_LOCKS
        if (get_core_num() == 0) {
            // Core 0: Direct FreeRTOS free
            vPortFree(ptr);
        } else {
            // Core 1: Proxy through core 0
            core1_free_proxy(ptr);
        }
    #else
        // Single core mode - assume only core 0 frees
        vPortFree(ptr);
    #endif
}

/**
 * @brief C++ new operator override (single object)
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory
 */
void * operator new( size_t size ) { return T76MemoryAlloc( size ); } 

/**
 * @brief C++ new[] operator override (array allocation)
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory
 */
void * operator new[]( size_t size ) { return T76MemoryAlloc(size); } 

/**
 * @brief C++ delete operator override (single object)
 * @param ptr Pointer to memory to free
 */
void operator delete( void * ptr ) { T76MemoryFree ( ptr ); } 

/**
 * @brief C++ delete[] operator override (array deallocation)
 * @param ptr Pointer to memory to free
 */
void operator delete[]( void * ptr ) { T76MemoryFree ( ptr ); }

/**
 * @brief C malloc function override
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if allocation failed
 */
extern "C" void* malloc(size_t size) {
    return T76MemoryAlloc(size);
}

/**
 * @brief C free function override
 * @param ptr Pointer to memory to free
 */
extern "C" void free(void* ptr) {
    T76MemoryFree(ptr);
}

/**
 * @brief C calloc function override
 * 
 * Allocates memory for an array of elements and initializes to zero.
 * 
 * @param num Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to zero-initialized memory, or NULL if allocation failed
 */
extern "C" void* calloc(size_t num, size_t size) {
    void* ptr = T76MemoryAlloc(num * size);

    if (ptr) {
        memset(ptr, 0, num * size); // Initialize allocated memory to zero
    }

    return ptr;
}

/**
 * @brief C realloc function override
 * 
 * Resizes a previously allocated memory block. Note: This implementation 
 * always copies the entire old size, which may be inefficient for large
 * blocks being grown. Consider this limitation for performance-critical code.
 * 
 * @param ptr Pointer to previously allocated memory (NULL to allocate new)
 * @param size New size in bytes (0 to free memory)
 * @return Pointer to resized memory, or NULL if allocation failed
 */
extern "C" void* realloc(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return T76MemoryAlloc(size); // If ptr is null, allocate new memory
    }

    if (size == 0) {
        T76MemoryFree(ptr); // If size is zero, free the memory
        return nullptr;
    }
    
    void* newPtr = T76MemoryAlloc(size);
    if (newPtr) {
        memcpy(newPtr, ptr, size); // Copy old data to new memory
        T76MemoryFree(ptr); // Free old memory
    }
    return newPtr;
}

/**
 * @brief Linker wrapper for malloc (used with --wrap=malloc)
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if allocation failed
 */
extern "C" void* __wrap_malloc(size_t size) { 
    return T76MemoryAlloc(size);
}

/**
 * @brief Linker wrapper for calloc (used with --wrap=calloc)
 * @param num Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to zero-initialized memory, or NULL if allocation failed
 */
extern "C" void* __wrap_calloc(size_t num, size_t size) {
    void* ptr = T76MemoryAlloc(num * size);
    if (ptr) {
        memset(ptr, 0, num * size); // Initialize allocated memory to zero
    }
    return ptr;
}

/**
 * @brief Linker wrapper for realloc (used with --wrap=realloc)
 * 
 * Resizes a previously allocated memory block. Same limitations as regular realloc.
 * 
 * @param ptr Pointer to previously allocated memory (NULL to allocate new)
 * @param size New size in bytes (0 to free memory)
 * @return Pointer to resized memory, or NULL if allocation failed
 */
extern "C" void* __wrap_realloc(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return T76MemoryAlloc(size); // If ptr is null, allocate new memory
    }
    if (size == 0) {
        T76MemoryFree(ptr); // If size is zero, free the memory
        return nullptr;
    }
    void* newPtr = T76MemoryAlloc(size);
    if (newPtr) {
        memcpy(newPtr, ptr, size); // Copy old data to new memory
        T76MemoryFree(ptr); // Free old memory
    }
    return newPtr;
}

/**
 * @brief Linker wrapper for free (used with --wrap=free)
 * @param ptr Pointer to memory to free
 */
extern "C" void __wrap_free(void* ptr) {
    T76MemoryFree(ptr);
}
