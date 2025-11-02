/**
 * @file comprehensive_test.cpp
 * @brief Comprehensive test program for SCPI interpreter functionality.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "concrete_interpreter.hpp"
#include "stream_local.hpp"
#include "test_command.hpp" // Include the extracted test_command header
#include <iostream>
#include <cstring> // For strncpy

int main() {
    std::cout << "=== SCPI Interpreter Comprehensive Test ===" << std::endl;
    
    try {
        // Test basic commands
        test_command("TEST:SIMPLE", "TEST:SIMPLE executed");
        test_command("TEST:SIMPLE?", "SIMPLE_QUERY_RESPONSE");
        test_command("TEST:COMmand:OPTional:SYNtax", "TEST:COMmand:OPTional:SYNtax executed");
        
        // Test abbreviated commands
        test_command("TEST:COM:OPT:SYN", "TEST:COMmand:OPTional:SYNtax executed");
        
        // Test commands with parameters
        test_command("TEST:NUMBER 42.5", "TEST:NUMBER executed with value:");
        test_command("TEST:STRING \"hello_world\"", "TEST:STRING executed with text:");
        test_command("TEST:BOOLEAN true", "TEST:BOOLEAN executed with state:");
        test_command("TEST:ENUM OPTION1", "TEST:ENUM executed with option:");
        
        // Test ABD commands
        test_command("TEST:ABD:SIMPLE #14TEST", "TEST:ABD:SIMPLE executed with ABD data: size=4 bytes");
        
        // Test commands with multiple parameters
        test_command("TEST:MULTI:TWO 123 \"test_string\"", "TEST:MULTI:TWO executed with first: 123.000000, second: test_string");
        
        // Test numeric variations
        test_command("TEST:NUMERIC:INTEGER 42", "TEST:NUMERIC:INTEGER executed with value:");
        test_command("TEST:NUMERIC:FLOAT 3.14159", "TEST:NUMERIC:FLOAT executed with value:");
        test_command("TEST:NUMERIC:RANGE 0 100", "TEST:NUMERIC:RANGE executed with");
        
        // Test query commands
        test_command("TEST:QUERY:PARAM? 1", "QUERY_PARAM_RESPONSE:");
        test_command("TEST:QUERY:MULTI? 2 VOLTAGE", "QUERY_MULTI_RESPONSE:");
        
        // Test error conditions
        test_command("TEST:ERROR:SIMULATE 404", "ERROR 404:");
        test_command("TEST:ERROR:INVALID", "ERROR 200:");
        
        std::cout << "\n=== Test Complete ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
