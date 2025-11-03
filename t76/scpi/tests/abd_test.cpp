/**
 * @file abd_test.cpp
 * @brief Simple test program for ABD (Arbitrary Data Block) functionality.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "concrete_interpreter.hpp"
#include "stream_local.hpp"
#include "test_command.hpp"
#include <iostream>
#include <string>

int main() {
    std::cout << "=== SCPI ABD (Arbitrary Data Block) Test ===" << std::endl;
    
    try {
        // Test basic ABD functionality
        std::cout << "\n--- Testing Basic ABD Commands ---" << std::endl;
        
        // Test 1: Simple 4-byte ABD with text "TEST" 
        // Format: #<length_digits><size><data> = #14TEST (1 digit for size, size=4, data=TEST)
        test_command("TEST:ABD:SIMPLE #14TEST", "TEST:ABD:SIMPLE executed with ABD data: size=4 bytes");
        
        // Test 2: ABD with text "HELLO"
        // Format: #15HELLO (1 digit for size, size=5, data=HELLO)
        test_command("TEST:ABD:SIMPLE #15HELLO", "TEST:ABD:SIMPLE executed with ABD data: size=5 bytes");
        
        // Test 3: ABD with numbers as text
        // Format: #14ABCD (1 digit for size, size=4, data=ABCD)
        test_command("TEST:ABD:SIMPLE #14ABCD", "TEST:ABD:SIMPLE executed with ABD data: size=4 bytes");
        
        // Test 4: Single character ABD
        // Format: #11X (1 digit for size, size=1, data=X)
        test_command("TEST:ABD:SIMPLE #11X", "TEST:ABD:SIMPLE executed with ABD data: size=1 bytes");
        
        std::cout << "\n--- Testing ABD Error Conditions ---" << std::endl;
        
        // Test 5: Invalid size length digit (0)
        test_command("TEST:ABD:SIMPLE #0", "", "103");
        
        // Test 6: Invalid size length digit (non-numeric)
        test_command("TEST:ABD:SIMPLE #A15HELLO", "", "103");
        
        std::cout << "\n--- Testing ABD Edge Cases ---" << std::endl;
        
        // Test 8: Two-digit size length
        // Format: #216SIXTEEN_BYTE_STR (2 digits for size, size=16, data=SIXTEEN_BYTE_STR)
        test_command("TEST:ABD:SIMPLE #216SIXTEEN_BYTE_STR", "TEST:ABD:SIMPLE executed with ABD data: size=16 bytes");
        
        // Test 9: Two-digit size with smaller data
        // Format: #210HELLO_TEST (2 digits for size, size=10, data=HELLO_TEST)  
        std::string medium_command = "TEST:ABD:SIMPLE #210HELLO_TEST";
        test_command(medium_command.c_str(), "TEST:ABD:SIMPLE executed with ABD data: size=10 bytes");
        
        std::cout << "\n=== ABD Test Complete ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
