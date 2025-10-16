/**
 * @file memory.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 */

#include "memory.hpp"

#include <cstring>
#include <FreeRTOS.h>
#include <pico/sync.h>


#ifdef T76_USE_GLOBAL_LOCKS
static mutex_t T76ICGlobalMemoryMutex;
#endif

void T76::Sys::Memory::memoryInit() {
    #ifdef T76_USE_GLOBAL_LOCKS
        mutex_init(&T76ICGlobalMemoryMutex);
    #endif
}

inline void *T76ICMemoryAlloc(size_t size) {
    #if T76_USE_GLOBAL_LOCKS
        mutex_enter_blocking(&T76ICGlobalMemoryMutex);
    #endif
    
    void *result = pvPortMalloc(size);

    #if T76_USE_GLOBAL_LOCKS
        mutex_exit(&T76ICGlobalMemoryMutex);
    #endif

    return result;
}

inline void T76ICMemoryFree(void* ptr) {
    #if T76_USE_GLOBAL_LOCKS
        mutex_enter_blocking(&T76ICGlobalMemoryMutex);
    #endif

    vPortFree(ptr);

    #if T76_USE_GLOBAL_LOCKS
        mutex_exit(&T76ICGlobalMemoryMutex);
    #endif
}

void * operator new( size_t size ) { return T76ICMemoryAlloc( size ); } 
void * operator new[]( size_t size ) { return T76ICMemoryAlloc(size); } 
void operator delete( void * ptr ) { T76ICMemoryFree ( ptr ); } 
void operator delete[]( void * ptr ) { T76ICMemoryFree ( ptr ); }

extern "C" void* malloc(size_t size) {
    return T76ICMemoryAlloc(size);
}

extern "C" void free(void* ptr) {
    T76ICMemoryFree(ptr);
}

extern "C" void* calloc(size_t num, size_t size) {
    void* ptr = T76ICMemoryAlloc(num * size);

    if (ptr) {
        memset(ptr, 0, num * size); // Initialize allocated memory to zero
    }

    return ptr;
}

extern "C" void* realloc(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return nullptr; // If ptr is null, allocate new memory
    }
    if (size == 0) {
        T76ICMemoryFree(ptr); // If size is zero, free the memory
        return nullptr;
    }
    
    void* newPtr = T76ICMemoryAlloc(size);
    if (newPtr) {
        memcpy(newPtr, ptr, size); // Copy old data to new memory
        T76ICMemoryFree(ptr); // Free old memory
    }
    return newPtr;
}

extern "C" void* __wrap_malloc(size_t size) { 
    return T76ICMemoryAlloc(size);
}

extern "C" void* __wrap_calloc(size_t num, size_t size) {
    void* ptr = T76ICMemoryAlloc(num * size);
    if (ptr) {
        memset(ptr, 0, num * size); // Initialize allocated memory to zero
    }
    return ptr;
}

extern "C" void* __wrap_realloc(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return T76ICMemoryAlloc(size); // If ptr is null, allocate new memory
    }
    if (size == 0) {
        T76ICMemoryFree(ptr); // If size is zero, free the memory
        return nullptr;
    }
    void* newPtr = T76ICMemoryAlloc(size);
    if (newPtr) {
        memcpy(newPtr, ptr, size); // Copy old data to new memory
        T76ICMemoryFree(ptr); // Free old memory
    }
    return newPtr;
}

extern "C" void __wrap_free(void* ptr) {
    T76ICMemoryFree(ptr);
}
