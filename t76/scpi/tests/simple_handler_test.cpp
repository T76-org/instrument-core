/**
 * @file simple_handler_test.cpp
 * @brief Test to verify that the ABD handler can be called directly.
 */

#include "concrete_interpreter.hpp"
#include "stream_local.hpp"
#include "parameter.hpp"
#include <iostream>
#include <vector>

int main() {
    std::cout << "=== Simple Handler Test ===" << std::endl;
    
    T76::SCPI::TestInputStream input("dummy");
    T76::SCPI::TestOutputStream output;
    T76::SCPI::ConcreteInterpreter target;
    T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, target);
    
    // Test 1: Create a parameter manually to verify handler works
    std::vector<T76::SCPI::ParameterValue> params;
    T76::SCPI::ParameterValue abdParam(T76::SCPI::ParameterType::ArbitraryData);
    abdParam.stringValue = "TEST";
    params.push_back(abdParam);
    
    std::cout << "Test 1: Calling handler directly with manual param..." << std::endl;
    target._testABDSimple(params, interpreter);
    
    std::string result = output.getOutput();
    std::cout << "Handler Output: '" << result << "'" << std::endl;
    
    // Test 2: Test what _parseParameter returns for ArbitraryData
    // We can't access _parseParameter directly, but we can test the constructor
    std::cout << "\nTest 2: Testing ParameterValue constructor..." << std::endl;
    T76::SCPI::ParameterValue testParam(T76::SCPI::ParameterType::ArbitraryData);
    testParam.stringValue = "TEST";
    std::cout << "Created param type: " << static_cast<int>(testParam.type) << std::endl;
    std::cout << "Param string value: '" << testParam.stringValue << "'" << std::endl;
    
    return 0;
}
