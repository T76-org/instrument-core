#include <iostream>
#include "concrete_interpreter.hpp"
#include "stream_local.hpp"

int main() {
    std::cout << "=== Testing Configurable ABD Size Limit ===" << std::endl;
    
    // Test 1: ABD within limit (16 bytes max, 4 bytes data)
    std::cout << "\nTest 1: ABD within limit (16 bytes max, 4 bytes data)" << std::endl;
    {
        T76::SCPI::TestInputStream input("TEST:ABD:SIMPLE #14TEST\n");
        T76::SCPI::TestOutputStream output;
        T76::SCPI::ConcreteInterpreter handler;
        T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, handler, 16);
        
        interpreter.processInputStream();
        
        std::cout << "Output: " << output.getOutput() << std::endl;
        auto errors = interpreter.errors();
        std::cout << "Errors: ";
        for (const auto& error : errors) {
            std::cout << error << " ";
        }
        std::cout << std::endl;
        
        if (errors.empty()) {
            std::cout << "✓ PASS - ABD within limit accepted" << std::endl;
        } else {
            std::cout << "✗ FAIL - ABD within limit should be accepted" << std::endl;
        }
    }
    
    // Test 2: ABD exceeding limit (16 bytes max, 20 bytes data)
    std::cout << "\nTest 2: ABD exceeding limit (16 bytes max, 20 bytes data)" << std::endl;
    {
        T76::SCPI::TestInputStream input("TEST:ABD:SIMPLE #220TWENTY_BYTE_DATA_STR\n");
        T76::SCPI::TestOutputStream output;
        T76::SCPI::ConcreteInterpreter handler;
        T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, handler, 16);
        
        interpreter.processInputStream();
        
        std::cout << "Output: " << output.getOutput() << std::endl;
        auto errors = interpreter.errors();
        std::cout << "Errors: ";
        for (const auto& error : errors) {
            std::cout << error << " ";
        }
        std::cout << std::endl;
        
        bool found_size_error = false;
        for (const auto& error : errors) {
            if (error.find("ABD data size too large") != std::string::npos) {
                found_size_error = true;
                break;
            }
        }
        
        if (found_size_error) {
            std::cout << "✓ PASS - ABD exceeding limit rejected" << std::endl;
        } else {
            std::cout << "✗ FAIL - ABD exceeding limit should be rejected" << std::endl;
        }
    }
    
    // Test 3: Default limit (1MB - should accept larger data)
    std::cout << "\nTest 3: Default limit (1MB - should accept the same large data)" << std::endl;
    {
        T76::SCPI::TestInputStream input("TEST:ABD:SIMPLE #220TWENTY_BYTE_DATA_STR\n");
        T76::SCPI::TestOutputStream output;
        T76::SCPI::ConcreteInterpreter handler;
        T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, handler); // Default 1MB limit
        
        interpreter.processInputStream();
        
        std::cout << "Output: " << output.getOutput() << std::endl;
        auto errors = interpreter.errors();
        std::cout << "Errors: ";
        for (const auto& error : errors) {
            std::cout << error << " ";
        }
        std::cout << std::endl;
        
        if (errors.empty()) {
            std::cout << "✓ PASS - Same data accepted with default limit" << std::endl;
        } else {
            std::cout << "✗ FAIL - Data should be accepted with default 1MB limit" << std::endl;
        }
    }
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;
}
