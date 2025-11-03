#include "test_command.hpp"
#include <iostream>

int main() {
    std::cout << "=== Quick Parameter Validation Test ===" << std::endl;
    
    // Test with correct parameter count (0)
    std::cout << "\nTest 1: Correct parameter count (0)" << std::endl;
    test_command("TEST:SIMPLE", "TEST:SIMPLE executed");
    
    // Test with incorrect parameter count (1)
    std::cout << "\nTest 2: Incorrect parameter count (1)" << std::endl;
    test_command("TEST:SIMPLE param1", "", "102");  // Expecting some error code
    
    // Test with incorrect parameter count (2)
    std::cout << "\nTest 3: Incorrect parameter count (2)" << std::endl;
    test_command("TEST:SIMPLE param1 param2", "", "102");  // Expecting some error code
    
    return 0;
}
