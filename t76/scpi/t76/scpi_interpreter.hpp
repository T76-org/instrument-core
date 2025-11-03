/**
 * @file interpreter_base.hpp
 * @brief Based interpreter class for SCPI commands.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This is the base class for the SCPI interpreter. It handles the parding of
 * SCPI commands and parameters, and provides a framework for command execution.
 * 
 * To use this interpreter, you will need to define your own set of SCPI commands
 * inside a YAML file (typically, `commands.yaml`) and generate the command
 * definitions using the `trie_generator.py` script. The generated files will
 * include the command trie, command definitions, and parameter descriptors.
 * 
 * You will then have to implement the handler for each command in a class of 
 * your choosing whose namespace and name you provide in the YAML file.
 * 
 * At runtime, you can then instantiate a concrete version of the interpreter
 * template that uses your command handler class.
 * 
 * The interpreter works by breaking down each SCPI command into a trie structure 
 * that is traversed as data is fed into it one character at a time by a call to 
 * processInputCharacter(). The interpreter will parse the command 
 * and its parameters, and then call the appropriate command handler with 
 * the parsed parameters. 
 * 
 * Command handlers are expected to take a vector of ParameterValue objects
 * representing the parsed parameters. Note that the interpreter already verifies
 * that the correct number and types of parameters are provided before calling
 * the handler, so you can assume that the parameters are valid, but you must still
 * handle any semantic errors that may arise during command execution. For example,
 * if a command expects a parameter to be within a certain range, you must check
 * that the provided parameter is within that range and handle the error if it is not,
 * but the interpreter will ensure that the parameter is of the correct type.
 * 
 * Errors can be reported by calling the `addError` method. You will need to 
 * add a `SYSTem:ERROR?` command to your command set to retrieve errors and
 * output them to the output stream according to SCPI specifications.
 * 
 * Note that the interpreter automatically determines whether the caller has issued
 * the correct number of parameters of the appropriate type for each command, and will
 * only call the command handler if the parameters are valid. It is therefore safe
 * to assume that the command handler will always receive the appropriate number of 
 * type-correct parameters.
 * 
 */

#pragma once

#include <cstdint>
#include <memory>
#include <queue>
#include <vector>
#include <cstring>
#include <string>
#include <strings.h>

#include "scpi_trie.hpp"
#include "scpi_command.hpp"


namespace T76::SCPI {

    /**
     * @brief The interpreter status enum.
     * 
     * This enum represents the current status of the interpreter while processing
     * SCPI commands. It helps in determining whether the interpreter is currently
     * parsing a command, an argument, or if it has encountered an error.
     * 
     */
    enum class InterpreterStatus : uint8_t {
        ParsingCommand,
        ParsingArgument,
        ParsingABDSizeLength,  // Parsing the single digit that specifies data size length
        ParsingABDSize,        // Parsing the actual size digits of ABD
        ParsingABDData,        // Parsing the binary data of ABD
        Error,
    };

    template<typename TargetT>
    class Interpreter {
    public:
        std::queue<std::string> errorQueue; // Queue to store error messages.

        /**
         * @brief Constructor for the SCPI interpreter.
         * 
         * Initializes the interpreter with the specified target for command execution.
         * 
         * @param target Reference to the target interpreter implementation.
         * @param abdMaxSize Maximum allowed size for ABD (Arbitrary Data Block) parameters in bytes. Default is 256 bytes.
         */
        Interpreter(TargetT &target, size_t abdMaxSize = 256);

        /**
         * @brief Get the maximum number of parameters allowed for commands.
         * 
         * @return The maximum number of parameters allowed for commands.
         */
        size_t maxParameterCount() const;

        /**
         * @brief Process a single input character.
         * 
         * This method processes a single character from an input source,
         * updating the interpreter state and handling command parsing.
         * 
         * @param character The input character to process.
         */
        void processInputCharacter(uint8_t character);

        /**
         * @brief Fully resets the interpreter.
         * 
         * This clears the current command, parameters, and resets the
         * interpreter state to prepare for a new command input. The
         * error queue is also cleared.
         * 
         */
        void reset();

        /**
         * @brief Formats a string for output.
         * 
         * @param str 
         * @return std::string The formatted string.
         * 
         * Adds leading and training quotation marks, and escapes any
         * quotation marks within the string.
         */
        std::string formatString(const std::string &str) const;

        /**
         * @brief Generates the preamble for an Arbitrary Data Block (ABD).
         * 
         * @param size The size of the ABD data block.
         * @return std::string The formatted ABD preamble.
         */
        std::string abdPreamble(size_t size) const;

        /**
         * @brief Add an error to the error queue.
         * 
         * This method formats the error as a SCPI error reply (`number,"string"`) and
         * adds it to the `errorQueue`.
         * 
         * @param errorNumber The error number.
         * @param errorString The error string.
         */
        void addError(int errorNumber, const std::string &errorString);

        /**
         * @brief Collects all errors from the error queue.
         * 
         * This method collects all errors from the error queue and returns them
         * as a vector of strings. Each string represents an error message.
         * 
         * @return A vector of strings containing all error messages.
         */
        std::vector<std::string> errors();

    protected:
        InterpreterStatus _status; // Current status of the interpreter.
        TrieNode *_currentNode; // Current node in the trie for command parsing.

        std::vector<std::string> _parameters; // Vector to store raw parameters for the current command.

        uint8_t _buffer[256]; // Buffer for partial parameter storage.
        size_t _bufferIndex; // Current index in the buffer for partial parameter storage.

        // ABD (Arbitrary Data Block) parsing state
        uint8_t _abdSizeLength; // Number of digits that represent the data size (1-9)
        size_t _abdExpectedSize; // Expected total size of the ABD data block
        std::vector<uint8_t> _abdDataBuffer; // Buffer to store ABD binary data
        size_t _abdBytesRead; // Number of ABD data bytes read so far
        size_t _abdMaxSize; // Maximum allowed size for ABD data blocks

        TargetT &_target; // Reference to the target for command execution.

        static const TrieNode _trie; // Trie for command parsing.
        static const Command<TargetT> _commands[]; // Array of commands.
        static const size_t _commandCount; // Number of commands.
        static const size_t _maxParameterCount; // Maximum number of parameters.

        /**
         * @brief Reset the interpreter state for a new command.
         * 
         * This method resets the interpreter state, clearing the current command
         * and parameters, and preparing for the next command input.
         * 
         */
        void _resetState();

        /**
         * @brief Finalize the current command processing.
         * 
         * This method finalizes the current command processing, checking if the
         * command is valid and executing it with the parsed parameters.
         */
        void _finalizeCurrentCommand(); // Finalize the current command processing.

        /**
         * @brief Parse a parameter based on its descriptor.
         * 
         * This method parses a parameter based on its descriptor and input string,
         * converting it into a `ParameterValue` object.
         * 
         * @param descriptor The descriptor for the parameter to parse.
         * @param input The input string to parse.
         * @param value The output `ParameterValue` object to fill with the parsed value.
         * @return true if the parameter was successfully parsed, false otherwise.
         */
        ParameterValue _parseParameter(const ParameterDescriptor &descriptor, const std::string &input) const;

        /**
         * @brief Parse a number from a string
         * 
         * This method parses a number from a string input, handling scientific notation
         * and SCPI suffixes (e.g., T, G, M, k, m) 
         * 
         * @param input The input string to parse.
         * @param result The output variable to store the parsed number.
         * @return true if the number was successfully parsed, false otherwise.
         */
        ParameterValue _parseNumber(const std::string &input) const; // Parse a number without exceptions

        /**
         * @brief Parse a string parameter.
         * 
         * This method checks that strings are delimited by quotation marks and
         * handles escaped quotation marks within the string.
         * 
         * @param input The input string to parse.
         * @return A ParameterValue containing the parsed string or an invalid type if parsing fails.
         */
        ParameterValue _parseString(const std::string &input) const;

        /**
         * @brief Complete ABD parameter parsing.
         * 
         * This method creates a ParameterValue with ArbitraryData type from the
         * completed ABD data buffer and adds it to the parameters vector.
         */
        void _completeABDParameter();
    };

    // Template implementation
    template<typename TargetT>
    Interpreter<TargetT>::Interpreter(TargetT &target, size_t abdMaxSize) :
          _target(target),
          _abdMaxSize(abdMaxSize) {
        _resetState();
    }

    template<typename TargetT>
    void Interpreter<TargetT>::reset() {
        _resetState();
        errorQueue = std::queue<std::string>(); // Clear the error queue
    }

    template<typename TargetT>
    std::string Interpreter<TargetT>::formatString(const std::string &str) const {
        // Format a string for output with quotes and escaped quotes
        std::string formatted = "\"";

        for (char c : str) {
            if (c == '"') {
                formatted += "\\\""; // Escape double quotes
            } else {
                formatted += c;
            }
        }

        formatted += "\"";
        
        return formatted;
    }

    template<typename TargetT>
    std::string Interpreter<TargetT>::abdPreamble(size_t size) const {
        // Generate the ABD preamble for a given size
        std::string sizeStr = std::to_string(size);
        size_t numDigits = sizeStr.length();

        std::string preamble = "#";
        preamble += std::to_string(numDigits);
        preamble += sizeStr;

        return preamble;
    }

    template<typename TargetT>
    size_t Interpreter<TargetT>::maxParameterCount() const {
        return _maxParameterCount;
    }

    template<typename TargetT>
    void Interpreter<TargetT>::processInputCharacter(uint8_t byte) {
        // Process the byte based on the current status

        switch (_status) {
            case InterpreterStatus::ParsingCommand:
                // Handle command parsing logic

                if (byte == '\n' || byte == '\r') {
                    // End of command, finalize the current command
                    _finalizeCurrentCommand();
                } else if (byte == ' ' || byte == '\t') {
                    // Space indicates the end of the command, switch to argument parsing
                    _status = InterpreterStatus::ParsingArgument;
                } else {
                    // Continue parsing the command
                    TrieNode *nextNode = _currentNode->nextChild(byte);

                    if (nextNode) {
                        _currentNode = nextNode;
                    } else {
                        addError(102, "Unknown command");
                        _status = InterpreterStatus::Error;
                    }
                } 

                break;

            case InterpreterStatus::ParsingArgument:

                // Check for ABD start marker '#' at the beginning of a parameter
                if (byte == '#' && _bufferIndex == 0) {
                    // Start of Arbitrary Data Block
                    _status = InterpreterStatus::ParsingABDSizeLength;
                    break;
                }

                // If the byte is space or newline, attempt to parse the current parameter
                if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r') {
                    // If buffer is empty, we can ignore the byte and continue
                    if (_bufferIndex != 0) {
                        // Ensure that we are not exceeding the maximum number of parameters
                        if (_parameters.size() >= _maxParameterCount) {
                            // If we exceed the number of parameters, set the status to error
                            _status = InterpreterStatus::Error;
                            addError(100, "Too many parameters");
                            return;
                        }

                        // Make a copy of the buffer to a string
                        _buffer[_bufferIndex] = '\0'; // Null-terminate the buffer
                        const char* rawParameterValue = reinterpret_cast<const char*>(_buffer);

                        // Create a new parameter value and add it to the parameters vector
                        _parameters.push_back(rawParameterValue);
                    }
                    
                    // If we are at the end of the command, finalize it
                    if (byte == '\n' || byte == '\r') {
                        _finalizeCurrentCommand();
                    }

                    // Reset the buffer index for the next parameter
                    _bufferIndex = 0;   
                } else {
                    // Otherwise, accumulate the byte in the buffer

                    if (_bufferIndex < sizeof(_buffer) - 1) {
                        _buffer[_bufferIndex++] = byte;
                    } else {
                        // Buffer overflow, set error status
                        _status = InterpreterStatus::Error;
                        addError(101, "Parameter too long");
                    }
                }

                break;

            case InterpreterStatus::ParsingABDSizeLength:
                // Parse the single digit that specifies how many characters represent the data size
                if (byte >= '1' && byte <= '9') {
                    _abdSizeLength = byte - '0';
                    _abdExpectedSize = 0;
                    _status = InterpreterStatus::ParsingABDSize;
                } else {
                    // Invalid size length digit
                    addError(103, "Invalid ABD size length digit");
                    _status = InterpreterStatus::Error;
                }
                break;

            case InterpreterStatus::ParsingABDSize:
                // Parse the digits that represent the actual data size
                if (byte >= '0' && byte <= '9') {
                    _abdExpectedSize = _abdExpectedSize * 10 + (byte - '0');
                    _abdSizeLength--;
                    
                    if (_abdSizeLength == 0) {
                        // All size digits read, validate size and start data parsing
                        if (_abdExpectedSize == 0) {
                            addError(103, "ABD data size cannot be zero");
                            _status = InterpreterStatus::Error;
                        } else if (_abdExpectedSize > _abdMaxSize) {
                            addError(103, "ABD data size too large");
                            _status = InterpreterStatus::Error;
                        } else {
                            // Initialize data buffer and start reading data
                            _abdDataBuffer.clear();
                            _abdDataBuffer.reserve(_abdExpectedSize);
                            _abdBytesRead = 0;
                            _status = InterpreterStatus::ParsingABDData;
                        }
                    }
                } else {
                    // Invalid size digit
                    addError(103, "Invalid ABD size digit");
                    _status = InterpreterStatus::Error;
                }
                break;

            case InterpreterStatus::ParsingABDData:
                // Read binary data byte (including \n, \r, and any other byte)
                _abdDataBuffer.push_back(byte);
                _abdBytesRead++;
                
                // Check if we've now reached the expected size
                if (_abdBytesRead >= _abdExpectedSize) {
                    // All data received, complete the ABD parameter
                    _completeABDParameter();
                    _status = InterpreterStatus::ParsingArgument;
                }
                break;

            case InterpreterStatus::Error:

                // Eat all input until a newline or carriage return is found
                if (byte == '\n' || byte == '\r') {
                    // Reset the state after an error
                    _resetState();
                }

                break;
        }
    }

    template<typename TargetT>
    void Interpreter<TargetT>::addError(int errorNumber, const std::string &errorString) {
        // Add an error to the error queue
        errorQueue.push(std::to_string(errorNumber) + "," + formatString(errorString));
    }

    template<typename TargetT>
    std::vector<std::string> Interpreter<TargetT>::errors() {
        std::vector<std::string> errorMessages;

        while (!errorQueue.empty()) {
            errorMessages.push_back(errorQueue.front());
            errorQueue.pop();
        }
        
        return errorMessages;
    }

    template<typename TargetT>
    void Interpreter<TargetT>::_resetState() {
        // Reset the interpreter state for a new command
        _status = InterpreterStatus::ParsingCommand;
        _currentNode = const_cast<TrieNode*>(&_trie);
        _parameters.clear();
        _bufferIndex = 0;
        
        // Reset ABD parsing state
        _abdSizeLength = 0;
        _abdExpectedSize = 0;
        _abdDataBuffer.clear();
        _abdBytesRead = 0;
    }

    template<typename TargetT>
    void Interpreter<TargetT>::_finalizeCurrentCommand() {
        // Finalize the current command processing
        if (_currentNode->terminal()) {
            // If the command is valid, process the parameters
            Command<TargetT> command = _commands[_currentNode->commandIndex];
            std::vector<ParameterValue> parsedParameters;

            if (_parameters.size() > command.parameterCount) {
                addError(100, "Too many parameters. Expected " + std::to_string(command.parameterCount) + ", got " + std::to_string(_parameters.size()));

                _resetState();
                return;
            } 
            
            if (_parameters.size() < command.parameterCount) {
                addError(100, "Too few parameters");
                _resetState();
                return;
            }
            
            // Parse parameters only if the command expects them
            if (command.parameterCount > 0 && command.parameterDescriptors != nullptr) {
                size_t parameterIndex = 0;
                for (const auto &param : _parameters) {
                    auto value = _parseParameter(command.parameterDescriptors[parameterIndex++], param);

                    if (value.type == ParameterType::Invalid) {
                        addError(103, "Invalid parameter #" + std::to_string(parameterIndex));
                        _resetState();
                        return;
                    }

                    parsedParameters.push_back(value);
                }
            }

            (_target.*command.handler)(parsedParameters);

        } else if (_currentNode == &_trie) {
            // No command was entered (empty input), do nothing
        } else {
            addError(102, "Unknown command");
        }

        // Reset the state for the next command
        _resetState();
    }

    template<typename TargetT>
    ParameterValue Interpreter<TargetT>::_parseString(const std::string &input) const {
        if (input.size() < 2 || input.front() != '"' || input.back() != '"') {
            return ParameterValue(ParameterType::Invalid); // Invalid string format
        }

        std::string parsedString;
        bool escape = false;

        for (size_t i = 1; i < input.size() - 1; ++i) { // Skip the surrounding quotes
            char currentChar = input[i];

            if (escape) {
                parsedString.push_back(currentChar);
                escape = false;
            } else if (currentChar == '\\') {
                escape = true;
            } else {
                parsedString.push_back(currentChar);
            }
        }

        if (escape) {
            return ParameterValue(ParameterType::Invalid); // Unfinished escape sequence
        }

        return ParameterValue(parsedString);
    }

    template<typename TargetT>
    ParameterValue Interpreter<TargetT>::_parseParameter(const ParameterDescriptor &descriptor, const std::string &input) const {
        switch (descriptor.type) {
            case ParameterType::String:
                return _parseString(input);

            case ParameterType::Number:
                return _parseNumber(input);

            case ParameterType::Boolean:
                if (strcasecmp(input.c_str(), "true") == 0 || input == "1") {
                    return ParameterValue(true);
                } else if (strcasecmp(input.c_str(), "false") == 0 || input == "0") {
                    return ParameterValue(false);
                } else {
                    return ParameterValue(ParameterType::Invalid); // Invalid boolean format
                }

            case ParameterType::Enum:
                // Check if the input matches any of the choices, case-insensitive
                for (uint8_t i = 0; i < descriptor.choiceCount; ++i) {
                    if (strcasecmp(input.c_str(), descriptor.choices[i]) == 0) {
                        return ParameterValue(descriptor.choices[i], true); // Return as enum value
                    }
                }

                return ParameterValue(ParameterType::Invalid); // No matching enum value found

            case ParameterType::ArbitraryData:
                // For ABD, the input string already contains the binary data
                // Create a ParameterValue with ArbitraryData type
                {
                    ParameterValue value(ParameterType::ArbitraryData);
                    value.stringValue = input; // Binary data stored in stringValue
                    return value;
                }

            default:
                return ParameterValue(ParameterType::Invalid); // No matching parameter type found
        }
    }

    template<typename TargetT>
    ParameterValue Interpreter<TargetT>::_parseNumber(const std::string &input) const {
        if (input == "") {
            return ParameterValue(ParameterType::Invalid);
        }

        const char *ptr = input.c_str();
        double value = 0.0;
        double sign = 1.0;
        bool hasDigits = false;

        // Skip leading whitespace
        while (*ptr == ' ' || *ptr == '\t') {
            ptr++;
        }

        // Handle sign
        if (*ptr == '-') {
            sign = -1.0;
            ptr++;
        } else if (*ptr == '+') {
            ptr++;
        }

        // Parse integer part
        while (*ptr >= '0' && *ptr <= '9') {
            value = value * 10.0 + (*ptr - '0');
            hasDigits = true;
            ptr++;
        }

        // Parse decimal part
        if (*ptr == '.') {
            ptr++;
            double decimal = 0.0;
            double divisor = 1.0;
            
            while (*ptr >= '0' && *ptr <= '9') {
                decimal = decimal * 10.0 + (*ptr - '0');
                divisor *= 10.0;
                hasDigits = true;
                ptr++;
            }
            
            value += decimal / divisor;
        }

        // Handle scientific notation (e.g., 1.23e-4)
        if (*ptr == 'e' || *ptr == 'E') {
            ptr++;
            double expSign = 1.0;
            double exponent = 0.0;
            
            if (*ptr == '-') {
                expSign = -1.0;
                ptr++;
            } else if (*ptr == '+') {
                ptr++;
            }
            
            bool hasExpDigits = false;
            while (*ptr >= '0' && *ptr <= '9') {
                exponent = exponent * 10.0 + (*ptr - '0');
                hasExpDigits = true;
                ptr++;
            }
            
            if (!hasExpDigits) {
                return ParameterValue(ParameterType::Invalid); // Invalid scientific notation
            }
            
            // Apply exponent (simple power of 10 calculation)
            exponent *= expSign;
            while (exponent > 0) {
                value *= 10.0;
                exponent--;
            }
            while (exponent < 0) {
                value /= 10.0;
                exponent++;
            }
        }

        // Handle SCPI suffixes
        if (*ptr != '\0') {
            double multiplier = 1.0;
            
            switch (*ptr) {
                case 'T': case 't':  // Tera (10^12)
                    multiplier = 1e12;
                    ptr++;
                    break;
                case 'G': case 'g':  // Giga (10^9)
                    multiplier = 1e9;
                    ptr++;
                    break;
                case 'M':            // Mega (10^6) - case sensitive to avoid conflict with milli
                    multiplier = 1e6;
                    ptr++;
                    break;
                case 'k': case 'K':  // Kilo (10^3)
                    multiplier = 1e3;
                    ptr++;
                    break;
                case 'm':            // Milli (10^-3) - case sensitive to avoid conflict with Mega
                    multiplier = 1e-3;
                    ptr++;
                    break;
                case 'u': case 'U':  // Micro (10^-6)
                    multiplier = 1e-6;
                    ptr++;
                    break;
                case 'n': case 'N':  // Nano (10^-9)
                    multiplier = 1e-9;
                    ptr++;
                    break;
                case 'p': case 'P':  // Pico (10^-12)
                    multiplier = 1e-12;
                    ptr++;
                    break;
                case 'f': case 'F':  // Femto (10^-15)
                    multiplier = 1e-15;
                    ptr++;
                    break;
                case 'a': case 'A':  // Atto (10^-18)
                    multiplier = 1e-18;
                    ptr++;
                    break;
                default:
                    // No suffix found, continue with validation
                    break;
            }
            
            value *= multiplier;
        }

        // Skip trailing whitespace
        while (*ptr == ' ' || *ptr == '\t') {
            ptr++;
        }

        // Check if we've consumed the entire string and found at least one digit
        if (*ptr != '\0' || !hasDigits) {
            return ParameterValue(ParameterType::Invalid); // Invalid format or no digits found
        }

        return ParameterValue(value * sign); // Return the parsed number as a ParameterValue
    }

    template<typename TargetT>
    void Interpreter<TargetT>::_completeABDParameter() {
        // Ensure we don't exceed maximum parameter count
        if (_parameters.size() >= _maxParameterCount) {
            addError(100, "Too many parameters");
            _status = InterpreterStatus::Error;
            return;
        }

        // Create a string from the binary data buffer
        // Note: std::string can handle binary data including null bytes
        std::string abdData(reinterpret_cast<const char*>(_abdDataBuffer.data()), _abdDataBuffer.size());
        
        // Add the ABD parameter to the parameters vector
        // We store it as a string for now - the _parseParameter method will handle ArbitraryData type
        _parameters.push_back(abdData);
        
        // Reset ABD state for potential next parameter
        _abdSizeLength = 0;
        _abdExpectedSize = 0;
        _abdDataBuffer.clear();
        _abdBytesRead = 0;
    }

} // namespace T76::SCPI
