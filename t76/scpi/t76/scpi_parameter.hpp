/**
 * @file parameter.hpp
 * @brief SCPI Parameter Class
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * SCPI parameters can be of type string, number, boolean, or enum.
 * The Parameter struct is used to represent these types so that
 * they can be parsed and processed correctly by the SCPI interpreter.
 * 
 */

#pragma once

#include <string>
#include <cstdint>


namespace T76::SCPI {

    /**
     * @brief ParameterType enum
     * 
     * This enum represents the different types of parameters that can be
     * used in SCPI commands. Each type corresponds to a specific kind of
     * data that can be passed as a parameter:
     * 
     * - String: A sequence of characters, typically used for text.
     * - Number: A numeric value, which can be an integer or a floating-point number
     * - Boolean: A true/false value, often represented as 1 (true)
     * 
     */
    enum class ParameterType : uint8_t {
        String,
        Number,
        Boolean,
        Enum,
        ArbitraryData,
        Invalid // Represents an invalid or unrecognized parameter type
    };

    /**
     * @brief ParameterValue union
     * 
     * This union represents the value of a parameter, which can be one of several types:
     * - stringValue: A pointer to a string value.
     * - numberValue: A numeric value (double).
     * - booleanValue: A boolean value (true/false).
     * - enumValue: A pointer to a string representing an enum value.
     * 
     */
    struct ParameterValue {
        ParameterType type;
        union {
            double numberValue;
            bool booleanValue;
        };
        std::string stringValue;

        ParameterValue() : type(ParameterType::Number), numberValue(0) {}
        ParameterValue(ParameterType type) : type(type), numberValue(0) {
            if (type == ParameterType::String || type == ParameterType::Enum || type == ParameterType::ArbitraryData) {
                stringValue = "";
            }
        }

        ParameterValue(const ParameterValue& other)
            : type(other.type),
              numberValue(other.numberValue),
              stringValue(other.stringValue) {}

        ParameterValue(const std::string str, bool isEnum = false) {
            numberValue = 0; // Default value for number
            type = isEnum ? ParameterType::Enum : ParameterType::String;
            stringValue = std::move(str);
        }

        ParameterValue(double num) : type(ParameterType::Number), numberValue(num) {}

        ParameterValue(bool boolean) : type(ParameterType::Boolean), booleanValue(boolean) {}

        ParameterValue& operator=(const ParameterValue& other) {
            if (this != &other) {
                type = other.type;
                numberValue = other.numberValue;
                stringValue = other.stringValue;
            }
            return *this;
        }

        ParameterValue(ParameterValue&& other) noexcept
            : type(other.type),
              numberValue(other.numberValue),
              stringValue(std::move(other.stringValue)) {}

        ParameterValue& operator=(ParameterValue&& other) noexcept {
            if (this != &other) {
                type = other.type;
                numberValue = other.numberValue;
                stringValue = std::move(other.stringValue);
            }
            
            return *this;
        }

        ~ParameterValue() = default;
    };

    /**
     * @brief ParameterDescriptorValue union
     * 
     * This union represents the value of a parameter, which can be one of several types:
     * - stringValue: A pointer to a string value.
     * - numberValue: A numeric value (double).
     * - booleanValue: A boolean value (true/false).
     * - enumValue: A pointer to a string representing an enum value.
     * 
     */
    union ParameterDescriptorValue {
        const char* stringValue;            // Pointer to a string value
        double numberValue;                 //  Numeric value
        bool booleanValue;                  //  Boolean value
        const char* enumValue;              //  Pointer to an enum value (string representation)
        const uint8_t *dataValue;           // Pointer to raw data value (for block data)
    };

    /**
     * @brief ParameterDescriptor struct
     * 
     * This struct describes a parameter for SCPI commands, including its type,
     * default value, and choices for enum parameters.
     * 
     * - type: The type of the parameter (string, number, boolean, or enum).
     * - defaultValue: The default value for the parameter.
     * - choiceCount: The number of choices available for enum parameters.
     * - choices: A pointer to an array of choices for enum parameters, if applicable.
     */
    struct ParameterDescriptor {
        ParameterType type;                 // The type of the parameter
        ParameterDescriptorValue defaultValue;        // The value of the parameter
        uint8_t choiceCount;                // Number of choices for enum parameters
        const char * const *choices;       // Pointer to array of choices for enum parameters, if applicable
    };
    
} // namespace T76::SCPI
