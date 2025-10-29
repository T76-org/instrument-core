/**
 * @file safety_components.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * Implementation of the SafeableComponent registry system for the RP2350 platform.
 * 
 * This system provides a thread-safe registry for components that need to participate
 * in safety operations. Components can register themselves and provide:
 * - activate() method for initialization/activation
 * - makeSafe() method for entering a safe state
 * 
 * The registry is self-initializing and works from the very beginning of the
 * application's lifetime, even before C++ runtime initialization is complete.
 * It uses its own dedicated critical section for thread synchronization.
 * 
 * Key design principles:
 * - Self-initializing memory structures
 * - Thread-safe for multi-core operation
 * - Minimal memory footprint
 * - No persistence across reboots
 * - Uses dedicated spinlock for better isolation
 * - No exception handling (not supported by target system)
 */

#include <cstring>
#include <cstdint>

// Pico SDK includes
#include <pico/stdlib.h>
#include <pico/critical_section.h>

#include "t76/safety.hpp"
#include "safety_private.hpp"

namespace T76::Sys::Safety {

    /**
     * @brief Component registry structure
     * 
     * Simple array-based storage for registered components.
     * Self-initializes on first use.
     */
    struct ComponentRegistry {
        uint32_t magic;                                                         ///< Magic number for validation
        uint32_t componentCount;                                                ///< Number of registered components
        SafeableComponent* components[T76_SAFETY_MAX_REGISTERED_COMPONENTS];   ///< Array of registered components
        bool initialized;                                                       ///< Registry initialization flag
    };

    // Global component registry (normal memory, not persistent)
    static ComponentRegistry gComponentRegistry = {0};

    // Dedicated critical section for component registry synchronization
    static critical_section_t gComponentRegistryCriticalSection;

    /**
     * @brief Initialize the component registry if not already initialized
     * 
     * Self-initializing function that sets up the registry structure.
     * Safe to call multiple times - will only initialize once.
     * Thread-safe through dedicated component registry spinlock protection.
     */
    static void ensureRegistryInitialized() {
        // Initialize the critical section if not already done
        if (!critical_section_is_initialized(&gComponentRegistryCriticalSection)) {
            // This is not thread-safe, but it's called during early initialization
            // before multi-core operation begins
            critical_section_init(&gComponentRegistryCriticalSection);
        }
        
        // Use dedicated component registry critical section for synchronization
        critical_section_enter_blocking(&gComponentRegistryCriticalSection);
        
        if (!gComponentRegistry.initialized) {
            // Clear the entire structure
            memset(&gComponentRegistry, 0, sizeof(ComponentRegistry));
            
            // Set up the registry
            gComponentRegistry.magic = COMPONENT_REGISTRY_MAGIC;
            gComponentRegistry.componentCount = 0;
            gComponentRegistry.initialized = true;
        }
        
        critical_section_exit(&gComponentRegistryCriticalSection);
    }

    /**
     * @brief Validate that the registry is properly initialized
     * 
     * @return true if registry is valid, false otherwise
     */
    static bool isRegistryValid() {
        return (gComponentRegistry.initialized && 
                gComponentRegistry.magic == COMPONENT_REGISTRY_MAGIC &&
                gComponentRegistry.componentCount <= T76_SAFETY_MAX_REGISTERED_COMPONENTS);
    }

    bool registerComponent(SafeableComponent* component) {
        if (component == nullptr) {
            return false;
        }

        // Ensure registry is initialized
        ensureRegistryInitialized();

        // Thread-safe registration
        critical_section_enter_blocking(&gComponentRegistryCriticalSection);
        
        bool success = false;
        
        if (isRegistryValid()) {
            // Check if we have space for another component
            if (gComponentRegistry.componentCount < T76_SAFETY_MAX_REGISTERED_COMPONENTS) {
                // Check if component is already registered
                bool alreadyRegistered = false;
                for (uint32_t i = 0; i < gComponentRegistry.componentCount; i++) {
                    if (gComponentRegistry.components[i] == component) {
                        alreadyRegistered = true;
                        break;
                    }
                }
                
                if (!alreadyRegistered) {
                    // Add component to registry
                    gComponentRegistry.components[gComponentRegistry.componentCount] = component;
                    gComponentRegistry.componentCount++;
                    success = true;
                }
            }
        }
        
        critical_section_exit(&gComponentRegistryCriticalSection);
        return success;
    }

    bool unregisterComponent(SafeableComponent* component) {
        if (component == nullptr) {
            return false;
        }

        // Ensure registry is initialized
        ensureRegistryInitialized();

        // Thread-safe unregistration
        critical_section_enter_blocking(&gComponentRegistryCriticalSection);
        
        bool success = false;
        
        if (isRegistryValid()) {
            // Find the component in the registry
            for (uint32_t i = 0; i < gComponentRegistry.componentCount; i++) {
                if (gComponentRegistry.components[i] == component) {
                    // Remove component by shifting remaining components down
                    for (uint32_t j = i; j < gComponentRegistry.componentCount - 1; j++) {
                        gComponentRegistry.components[j] = gComponentRegistry.components[j + 1];
                    }
                    
                    // Clear the last slot and decrement count
                    gComponentRegistry.components[gComponentRegistry.componentCount - 1] = nullptr;
                    gComponentRegistry.componentCount--;
                    success = true;
                    break;
                }
            }
        }
        
        critical_section_exit(&gComponentRegistryCriticalSection);
        return success;
    }

    bool activateAllComponents(const char** failingComponentName) {
        // Ensure registry is initialized
        ensureRegistryInitialized();

        // Clear the output parameter initially
        if (failingComponentName != nullptr) {
            *failingComponentName = nullptr;
        }

        // Thread-safe activation
        critical_section_enter_blocking(&gComponentRegistryCriticalSection);
        
        if (isRegistryValid()) {
            // Create a local copy of components to avoid holding the lock during activation
            SafeableComponent* localComponents[T76_SAFETY_MAX_REGISTERED_COMPONENTS];
            uint32_t localCount = gComponentRegistry.componentCount;
            
            for (uint32_t i = 0; i < localCount; i++) {
                localComponents[i] = gComponentRegistry.components[i];
            }
            
            critical_section_exit(&gComponentRegistryCriticalSection);
            
            // Call activate() on each component (outside the critical section)
            for (uint32_t i = 0; i < localCount; i++) {
                if (localComponents[i] != nullptr) {
                    if (!localComponents[i]->activate()) {
                        // If any component fails to activate, capture the component name
                        if (failingComponentName != nullptr) {
                            *failingComponentName = localComponents[i]->getComponentName();
                        }
                        
                        // Make all components safe and return false
                        makeAllComponentsSafe();
                        return false;
                    }
                }
            }
        } else {
            critical_section_exit(&gComponentRegistryCriticalSection);
        }
        
        return true;
    }

    void makeAllComponentsSafe() {
        // Ensure registry is initialized
        ensureRegistryInitialized();

        // Thread-safe safing
        critical_section_enter_blocking(&gComponentRegistryCriticalSection);
        
        if (isRegistryValid()) {
            // Create a local copy of components to avoid holding the lock during safing
            SafeableComponent* localComponents[T76_SAFETY_MAX_REGISTERED_COMPONENTS];
            uint32_t localCount = gComponentRegistry.componentCount;
            
            for (uint32_t i = 0; i < localCount; i++) {
                localComponents[i] = gComponentRegistry.components[i];
            }
            
            critical_section_exit(&gComponentRegistryCriticalSection);
            
            // Call makeSafe() on each component (outside the critical section)
            for (uint32_t i = 0; i < localCount; i++) {
                if (localComponents[i] != nullptr) {
                    localComponents[i]->makeSafe();
                    // makeSafe() must be reliable - continue safing other components
                }
            }
        } else {
            critical_section_exit(&gComponentRegistryCriticalSection);
        }
    }

} // namespace T76::Sys::Safety