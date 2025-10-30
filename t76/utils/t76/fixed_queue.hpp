/**
 * @file fixed_queue.hpp
 * @brief Fixed-size thread-safe queue implementation
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file implements a fixed-size queue that can be used in a FreeRTOS environment.
 * The queue automatically discards the oldest element when a new element is added
 * and the queue is full. It uses a mutex to ensure thread safety.
 * 
 */

#pragma once

#include <deque>

#include "FreeRTOS.h"
#include "semphr.h"


namespace T76::Core::Utils {

    /**
     * @brief A thread-safe fixed-size queue that automatically discards oldest elements when full
     * @tparam T The type of elements stored in the queue
     * 
     * This queue implementation provides thread-safe operations using FreeRTOS mutexes.
     * When the queue reaches its maximum capacity and a new element is pushed, the oldest
     * element is automatically discarded to make room. The queue tracks the number of
     * elements that have been dropped due to overflow.
     */
    template<typename T>
    class FixedSizeQueue {
    public:
        /**
         * @brief Construct a new FixedSizeQueue with the specified maximum size
         * @param maxSize Maximum number of elements the queue can hold
         */
        explicit FixedSizeQueue(std::size_t maxSize)
            : _maxSize(maxSize), _droppedCount(0) {
            _mutex = xSemaphoreCreateMutex();
        }

        /**
         * @brief Destroy the FixedSizeQueue and clean up resources
         */
        ~FixedSizeQueue() {
            if (_mutex) {
                vSemaphoreDelete(_mutex);
            }
        }

        /**
         * @brief Push a new element to the back of the queue (copy version)
         * @param value The value to be added to the queue
         * @return true if the operation was successful, false if mutex acquisition failed
         * 
         * If the queue is full, the oldest element will be discarded to make room.
         * The dropped count will be incremented when an element is discarded.
         */
        bool push(const T& value) {
            if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

            if (_queue.size() == _maxSize) {
                _queue.pop_front();  // discard oldest
                _droppedCount++;
            }
            _queue.push_back(value);

            xSemaphoreGive(_mutex);
            return true;
        }

        /**
         * @brief Push a new element to the back of the queue (move version)
         * @param value The value to be moved into the queue
         * @return true if the operation was successful, false if mutex acquisition failed
         * 
         * If the queue is full, the oldest element will be discarded to make room.
         * The dropped count will be incremented when an element is discarded.
         * This version uses move semantics for better performance with expensive-to-copy types.
         */
        bool push(T&& value) {
            if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

            if (_queue.size() == _maxSize) {
                _queue.pop_front();
                _droppedCount++;
            }
            _queue.push_back(std::move(value));

            xSemaphoreGive(_mutex);
            return true;
        }

        /**
         * @brief Try to pop an element from the front of the queue
         * @param out Reference to store the popped element
         * @return true if an element was successfully popped, false if queue is empty or mutex acquisition failed
         * 
         * If the queue is empty, the function returns false and the output parameter is unchanged.
         * The element is moved out of the queue for better performance.
         */
        bool tryPop(T& out) {
            if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

            if (_queue.empty()) {
                xSemaphoreGive(_mutex);
                return false;
            }

            out = std::move(_queue.front());
            _queue.pop_front();

            xSemaphoreGive(_mutex);
            return true;
        }

        /**
         * @brief Check if the queue is empty
         * @return true if the queue is empty, false otherwise
         * 
         * Returns true if mutex acquisition fails for safety reasons.
         */
        bool empty() const {
            bool result;
            if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return true;
            result = _queue.empty();
            xSemaphoreGive(_mutex);
            return result;
        }

        /**
         * @brief Get the current number of elements in the queue
         * @return The number of elements currently in the queue
         * 
         * Returns 0 if mutex acquisition fails for safety reasons.
         */
        std::size_t size() const {
            std::size_t result = 0;
            if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
                result = _queue.size();
                xSemaphoreGive(_mutex);
            }
            return result;
        }

        /**
         * @brief Clear all elements from the queue and reset the dropped count
         * 
         * This operation removes all elements from the queue and resets the
         * dropped count to zero. If mutex acquisition fails, the operation
         * is silently ignored.
         */
        void clear() {
            if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
                _queue.clear();
                _droppedCount = 0;
                xSemaphoreGive(_mutex);
            }
        }

        /**
         * @brief Get the number of elements that have been dropped due to overflow
         * @return The total number of elements dropped since construction or last clear
         * 
         * This count is incremented each time an element is discarded because the
         * queue is full when pushing a new element. Returns 0 if mutex acquisition fails.
         */
        std::size_t droppedCount() const {
            std::size_t result = 0;
            if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
                result = _droppedCount;
                xSemaphoreGive(_mutex);
            }
            return result;
        }

    protected:
        std::deque<T> _queue;
        std::size_t _maxSize;
        std::size_t _droppedCount;
        SemaphoreHandle_t _mutex;
    };
    
} // namespace T76::Utils

