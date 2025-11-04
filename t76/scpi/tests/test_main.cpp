/**
 * @file test_main.cpp
 * @brief Simple test program to verify SCPI interpreter functionality.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "concrete_interpreter.hpp"
#include "stream_local.hpp"
#include <iostream>
#include <string>

int main() {
    try {
        // Create test input and output streams
        T76::SCPI::TestInputStream input(std::string("TEST:SIMPLE\n"));
        T76::SCPI::TestOutputStream output;
        
        // Create interpreter instance
        T76::SCPI::ConcreteInterpreter target;
        T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, target);
        
        // Process input
        interpreter.processInputStream();
        
        // Check output
        std::cout << "Output: " << output.getOutput() << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
