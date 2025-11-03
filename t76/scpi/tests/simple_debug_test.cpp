/**
 * @file simple_debug_test.cpp
 * @brief Simple debug test to understand the parameter handling issue.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "concrete_interpreter.hpp"
#include "stream_local.hpp"
#include <iostream>
#include <string>

int main() {
    try {
        std::cout << "=== Debug Test ===" << std::endl;
        
        // Test with just one extra parameter  
        std::cout << "\nTesting with 1 parameter to TEST:SIMPLE (expects 0):" << std::endl;
        {
            T76::SCPI::TestInputStream input(std::string("TEST:SIMPLE param0\n"));
            T76::SCPI::TestOutputStream output;
            
            T76::SCPI::ConcreteInterpreter concreteInterpreter;
            T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, concreteInterpreter);

            interpreter.processInputStream();
            
            std::string result = output.getOutput();
            std::vector<std::string> errorQueue = interpreter.errors();
            std::string errors;
            for (const auto& error : errorQueue) {
                errors += error + "\n";
            }
            
            std::cout << "Input: TEST:SIMPLE param0" << std::endl;
            std::cout << "Output: '" << result << "'" << std::endl;
            std::cout << "Errors: '" << errors << "'" << std::endl;
            std::cout << "✓ EXPECTED: Error because TEST:SIMPLE expects 0 parameters but got 1" << std::endl;
        }
        
        // Test with exactly max parameters
        std::cout << "\nTesting with 3 parameters to TEST:SIMPLE (expects 0):" << std::endl;
        {
            T76::SCPI::TestInputStream input(std::string("TEST:SIMPLE param0 param1 param2\n"));
            T76::SCPI::TestOutputStream output;
            
            T76::SCPI::ConcreteInterpreter concreteInterpreter;
            T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, concreteInterpreter);

            interpreter.processInputStream();
            
            std::string result = output.getOutput();
            std::vector<std::string> errorQueue = interpreter.errors();
            std::string errors;
            for (const auto& error : errorQueue) {
                errors += error + "\n";
            }
            
            std::cout << "Input: TEST:SIMPLE param0 param1 param2" << std::endl;
            std::cout << "Output: '" << result << "'" << std::endl;
            std::cout << "Errors: '" << errors << "'" << std::endl;
            std::cout << "✓ EXPECTED: Error because TEST:SIMPLE expects 0 parameters but got 3" << std::endl;
        }
        
        // Test with max+1 parameters (should hit global limit)
        std::cout << "\nTesting with 4 parameters to TEST:SIMPLE (exceeds global max of 3):" << std::endl;
        {
            T76::SCPI::TestInputStream input(std::string("TEST:SIMPLE param0 param1 param2 param3\n"));
            T76::SCPI::TestOutputStream output;
            
            T76::SCPI::ConcreteInterpreter concreteInterpreter;
            T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, concreteInterpreter);

            interpreter.processInputStream();
            
            std::string result = output.getOutput();
            std::vector<std::string> errorQueue = interpreter.errors();
            std::string errors;
            for (const auto& error : errorQueue) {
                errors += error + "\n";
            }
            
            std::cout << "Input: TEST:SIMPLE param0 param1 param2 param3" << std::endl;
            std::cout << "Output: '" << result << "'" << std::endl;
            std::cout << "Errors: '" << errors << "'" << std::endl;
            std::cout << "✓ EXPECTED: Error because too many parameters exceed global limit" << std::endl;
        }
        
        std::cout << "\n=== Debug Test Complete ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
