/**
 * @file abd_size_limit_test.cpp
 * @brief Comprehensive test program for ABD size limit compliance and deviance.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "concrete_interpreter.hpp"
#include "stream_local.hpp"
#include "test_command.hpp"
#include <iostream>
#include <string>

void test_abd_size_limit(const char* command, size_t abdMaxSize, const std::string& expected_output_search = "", const std::string& expected_error_search = "") {
    std::cout << "\n--- Testing: " << command << " (ABD limit: " << abdMaxSize << " bytes) ---" << std::endl;

    T76::SCPI::TestInputStream input((std::string(command) + "\n").c_str());
    T76::SCPI::TestOutputStream output;

    T76::SCPI::ConcreteInterpreter target;
    T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, target, abdMaxSize);

    interpreter.processInputStream();

    std::string result = output.getOutput();
    std::cout << "Output: " << result << std::endl;

    std::string errors = "";

    std::vector<std::string> errorQueue = interpreter.errors();

    for (const auto& error : errorQueue) {
        errors += error + "\n";
    }

    std::cout << "Errors: " << errors << std::endl;

    if (expected_output_search.size() > 0) {
        if (result.find(expected_output_search) == 0) {
            std::cout << "✓ OUTPUT PASS" << std::endl;
        } else {
            std::cout << "✗ OUTPUT FAIL (expected to start with: " << expected_output_search << ")" << std::endl;
        }
    }

    if (expected_error_search.size() > 0) {
        if (errors.find(expected_error_search) != std::string::npos) {
            std::cout << "✓ ERROR PASS" << std::endl;
        } else {
            std::cout << "✗ ERROR FAIL (expected error: " << expected_error_search << ")" << std::endl;
        }
    }
}

int main() {
    std::cout << "=== SCPI ABD Size Limit Compliance and Deviance Test ===" << std::endl;
    
    try {
        // === COMPLIANCE TESTS (data within limits) ===
        std::cout << "\n=== COMPLIANCE TESTS (data within limits) ===" << std::endl;
        
        // Test 1: Default 256-byte limit with small data (4 bytes)
        test_abd_size_limit("TEST:ABD:SIMPLE #14TEST", 256, "TEST:ABD:SIMPLE executed with ABD data: size=4 bytes");
        
        // Test 2: Default 256-byte limit with medium data (16 bytes)
        test_abd_size_limit("TEST:ABD:SIMPLE #216SIXTEEN_BYTE_STR", 256, "TEST:ABD:SIMPLE executed with ABD data: size=16 bytes");
        
        // Test 3: Default 256-byte limit with large but acceptable data (100 bytes)
        std::string large_data_100 = "#2100" + std::string(100, 'A'); // 100 'A' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + large_data_100).c_str(), 256, "TEST:ABD:SIMPLE executed with ABD data: size=100 bytes");
        
        // Test 4: Default 256-byte limit with exactly maximum data (256 bytes)
        std::string max_data_256 = "#3256" + std::string(256, 'B'); // 256 'B' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + max_data_256).c_str(), 256, "TEST:ABD:SIMPLE executed with ABD data: size=256 bytes");
        
        // Test 5: Custom smaller limit (64 bytes) with acceptable data (32 bytes)
        std::string small_data_32 = "#232" + std::string(32, 'C'); // 32 'C' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + small_data_32).c_str(), 64, "TEST:ABD:SIMPLE executed with ABD data: size=32 bytes");
        
        // Test 6: Custom larger limit (1024 bytes) with large data (512 bytes)
        std::string large_data_512 = "#3512" + std::string(512, 'D'); // 512 'D' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + large_data_512).c_str(), 1024, "TEST:ABD:SIMPLE executed with ABD data: size=512 bytes");
        
        // === DEVIANCE TESTS (data exceeding limits) ===
        std::cout << "\n=== DEVIANCE TESTS (data exceeding limits) ===" << std::endl;
        
        // Test 7: Default 256-byte limit with slightly oversized data (257 bytes)
        std::string over_data_257 = "#3257" + std::string(257, 'E'); // 257 'E' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + over_data_257).c_str(), 256, "", "ABD data size too large");
        
        // Test 8: Default 256-byte limit with much larger data (1000 bytes)
        std::string over_data_1000 = "#41000" + std::string(1000, 'F'); // 1000 'F' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + over_data_1000).c_str(), 256, "", "ABD data size too large");
        
        // Test 9: Custom small limit (16 bytes) with oversized data (20 bytes)
        std::string over_data_20 = "#220" + std::string(20, 'G'); // 20 'G' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + over_data_20).c_str(), 16, "", "ABD data size too large");
        
        // Test 10: Custom large limit (1024 bytes) with extremely large data (2048 bytes)
        std::string over_data_2048 = "#42048" + std::string(2048, 'H'); // 2048 'H' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + over_data_2048).c_str(), 1024, "", "ABD data size too large");
        
        // === EDGE CASE TESTS ===
        std::cout << "\n=== EDGE CASE TESTS ===" << std::endl;
        
        // Test 11: Very small limit (1 byte) with single byte data
        test_abd_size_limit("TEST:ABD:SIMPLE #11X", 1, "TEST:ABD:SIMPLE executed with ABD data: size=1 bytes");
        
        // Test 12: Very small limit (1 byte) with two bytes data
        test_abd_size_limit("TEST:ABD:SIMPLE #12XY", 1, "", "ABD data size too large");
        
        // Test 13: Zero size limit (should always fail for any non-zero data)
        test_abd_size_limit("TEST:ABD:SIMPLE #11X", 0, "", "ABD data size too large");
        
        // Test 14: Large limit (10MB) with small data to ensure no false positives
        test_abd_size_limit("TEST:ABD:SIMPLE #14TEST", 10485760, "TEST:ABD:SIMPLE executed with ABD data: size=4 bytes");
        
        // === MULTI-DIGIT SIZE TESTS ===
        std::cout << "\n=== MULTI-DIGIT SIZE TESTS ===" << std::endl;
        
        // Test 15: 2-digit size within limit
        std::string data_50 = "#250" + std::string(50, 'I'); // 50 'I' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + data_50).c_str(), 256, "TEST:ABD:SIMPLE executed with ABD data: size=50 bytes");
        
        // Test 16: 3-digit size within limit
        std::string data_200 = "#3200" + std::string(200, 'J'); // 200 'J' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + data_200).c_str(), 256, "TEST:ABD:SIMPLE executed with ABD data: size=200 bytes");
        
        // Test 17: 3-digit size exceeding limit
        std::string data_300 = "#3300" + std::string(300, 'K'); // 300 'K' characters
        test_abd_size_limit(("TEST:ABD:SIMPLE " + data_300).c_str(), 256, "", "ABD data size too large");
        
        std::cout << "\n=== ABD Size Limit Test Complete ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
