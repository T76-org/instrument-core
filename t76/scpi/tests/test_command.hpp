#ifndef TEST_COMMAND_HPP
#define TEST_COMMAND_HPP

#include <string>

/**
 * @brief Executes a SCPI command and validates its output and errors.
 * 
 * @param command The SCPI command to execute.
 * @param expected_output_search Optional string to search for in the output.
 * @param expected_error_search Optional string to search for in the errors.
 */
void test_command(const char* command, const std::string& expected_output_search = "", const std::string& expected_error_search = "");

#endif // TEST_COMMAND_HPP
