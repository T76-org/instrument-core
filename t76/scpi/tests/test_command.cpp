#include "test_command.hpp"
#include "concrete_interpreter.hpp"
#include "stream_local.hpp"
#include <iostream>

void test_command(const char* command, const std::string& expected_output_search, const std::string& expected_error_search) {
    std::cout << "\n--- Testing: " << command << " ---" << std::endl;

    T76::SCPI::TestInputStream input((std::string(command) + "\n").c_str());
    T76::SCPI::TestOutputStream output;

    T76::SCPI::ConcreteInterpreter target;
    T76::SCPI::Interpreter<T76::SCPI::ConcreteInterpreter> interpreter(input, output, target);

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
