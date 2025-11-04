/**
 * @file parameter_limit_test.cpp
 * @brief Test to validate that the parameter count limit is enforced correctly.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "concrete_interpreter.hpp"
#include "stream_local.hpp"
#include "test_command.hpp"
#include <iostream>
#include <sstream>

void test_parameter_count_limit() {
    T76::SCPI::TestInputStream input("TEST:MULTI:TWO 123 \"test_string\"\n");
    T76::SCPI::TestOutputStream output;
    T76::SCPI::ConcreteInterpreter target;

    T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, target);


    std::cout << "\n=== Testing Parameter Count Limit ===" << std::endl;

    // First, let's verify what the maximum parameter count is
    std::cout << "Maximum parameter count defined: " << interpreter.maxParameterCount() << std::endl;

    // Test 1: Command with valid number of parameters (should succeed)
    std::cout << "\nTest 1: Valid parameter count" << std::endl;
    test_command("TEST:MULTI:TWO 123 \"test_string\"", "TEST:MULTI:TWO executed");

    // Test 2: Command with too many parameters (should fail)
    std::cout << "\nTest 2: Excessive parameter count" << std::endl;
    {
        std::string excessive_params = "TEST:SIMPLE";
        for (size_t i = 0; i <= interpreter.maxParameterCount(); ++i) {
            excessive_params += " \"param" + std::to_string(i) + "\"";
        }
        test_command(excessive_params.c_str(), "", "100,\"Too many parameters\"");
    }

    // Test 3: Command with exactly the maximum number of parameters
    std::cout << "\nTest 3: Exactly maximum parameter count" << std::endl;
    test_command("TEST:OPTIONAL:MULTIPLE 42 \"param1\" RED", "TEST:OPTIONAL:MULTIPLE executed");
}

int main() {
    try {
        test_parameter_count_limit();
        
        std::cout << "\n=== Parameter Limit Test Complete ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
