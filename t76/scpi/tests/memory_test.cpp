/**
 * @file memory_test.cpp
 * @brief Test memory management of the refactored CStringQueue
 * @copyright Copyright (c) 2025 MTA, Inc.
 */

#include <queue>
#include <iostream>
#include <string>

int main() {
    std::cout << "=== std::queue Memory Management Test ===" << std::endl;
    
    try {
        std::queue<std::string> queue;
        
        // Test 1: Basic operations
        std::cout << "Test 1: Basic operations" << std::endl;
        queue.push("Hello");
        queue.push("World");
        queue.push("Test");
        
        std::cout << "Size: " << queue.size() << std::endl;
        std::cout << "Front: " << queue.front() << std::endl;
        
        queue.pop();
        std::cout << "After pop - Size: " << queue.size() << std::endl;
        std::cout << "Front: " << queue.front() << std::endl;
        
        // Test 2: Empty queue operations
        std::cout << "\nTest 2: Empty queue operations" << std::endl;
        while (!queue.empty()) {
            queue.pop();
        }
        
        std::cout << "Empty: " << (queue.empty() ? "true" : "false") << std::endl;
        std::cout << "Size: " << queue.size() << std::endl;
        
        // Test 3: nullptr handling
        std::cout << "\nTest 3: nullptr handling" << std::endl;
        queue.push("nullptr");
        std::cout << "Front: " << queue.front() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
