/**
 * @file concrete_interpreter.cpp
 * @brief Test concrete implementation of the SCPI interpreter.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "concrete_interpreter.hpp"
#include <sstream>
#include <iomanip>
#include <functional>


using namespace T76::SCPI;


// Helper method to format parameters for output
std::string ConcreteInterpreter::_formatParameters(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    if (params.empty()) {
        return "NO_PARAMS";
    }
    
    std::ostringstream oss;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        // Note: For testing purposes, we'll assume parameter types based on context
        // In a real implementation, we'd need to track parameter types
        oss << "PARAM" << i;
    }
    return oss.str();
}

std::string ConcreteInterpreter::_formatParameter(const ParameterValue &param, ParameterType type) {
    std::ostringstream oss;
    
    switch (type) {
        case ParameterType::String:
        case ParameterType::Enum:
            oss << "\"" << param.stringValue << "\"";
            break;
        case ParameterType::Number:
            oss << std::fixed << std::setprecision(6) << param.numberValue;
            break;
        case ParameterType::Boolean:
            oss << (param.booleanValue ? "TRUE" : "FALSE");
            break;
        default:
            oss << "UNKNOWN";
            break;
    }
    
    return oss.str();
}

// Basic test commands
void ConcreteInterpreter::_testSimple(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    interpreter.outputStream.write("TEST:SIMPLE executed\n");
}

void ConcreteInterpreter::_queryTestSimple(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    interpreter.outputStream.write("SIMPLE_QUERY_RESPONSE\n");
}

void ConcreteInterpreter::_testMixedCase(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    interpreter.outputStream.write("TEST:COMmand:OPTional:SYNtax executed\n");
}

// ABD test commands
void ConcreteInterpreter::_testABDSimple(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:ABD:SIMPLE executed with ABD data: ";
    
    if (!params.empty() && params[0].type == ParameterType::ArbitraryData) {
        const std::string& abdData = params[0].stringValue;
        oss << "size=" << abdData.size() << " bytes, ";
        
        // Display first 8 bytes as hex for debugging
        oss << "hex=[";
        for (size_t i = 0; i < std::min(abdData.size(), size_t(8)); ++i) {
            if (i > 0) oss << " ";
            oss << std::hex << std::setfill('0') << std::setw(2) 
                << (static_cast<unsigned char>(abdData[i]) & 0xFF);
        }
        if (abdData.size() > 8) oss << "...";
        oss << "]";
        
        // Display as text if all bytes are printable
        bool isPrintable = true;
        for (char c : abdData) {
            if (c < 32 || c > 126) {
                isPrintable = false;
                break;
            }
        }
        
        if (isPrintable) {
            oss << ", text=\"" << abdData << "\"";
        }
    } else {
        oss << "NO_ABD_PARAM";
    }
    
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

// Commands with single parameters
void ConcreteInterpreter::_testNumber(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:NUMBER executed with value: ";
    if (!params.empty()) {
        oss << std::fixed << std::setprecision(6) << params[0].numberValue;
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_testString(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:STRING executed with text: ";
    if (!params.empty()) {
        oss << "\"" << params[0].stringValue << "\"";
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_testBoolean(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:BOOLEAN executed with state: ";
    if (!params.empty()) {
        oss << (params[0].booleanValue ? "TRUE" : "FALSE");
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_testEnum(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:ENUM executed with option: ";
    if (!params.empty()) {
        oss << params[0].stringValue;
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

// Commands with multiple parameters
void ConcreteInterpreter::_testMultiTwo(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:MULTI:TWO executed with ";
    if (params.size() >= 2) {
        oss << "first: " << std::fixed << std::setprecision(6) << params[0].numberValue;
        oss << ", second: " << params[1].stringValue;
    } else {
        oss << "insufficient parameters (" << params.size() << "/2)";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

// Commands with optional parameters
void ConcreteInterpreter::_testOptionalSingle(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:OPTIONAL:SINGLE executed with ";
    if (params.size() >= 1) {
        oss << "required: " << std::fixed << std::setprecision(6) << params[0].numberValue;
        if (params.size() >= 2) {
            oss << ", optional: \"" << params[1].stringValue << "\"";
        } else {
            oss << ", optional: NOT_PROVIDED";
        }
    } else {
        oss << "missing required parameter";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_testOptionalMultiple(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:OPTIONAL:MULTIPLE executed with ";
    if (params.size() >= 1) {
        oss << "required: " << std::fixed << std::setprecision(6) << params[0].numberValue;
        
        if (params.size() >= 2) {
            oss << ", optional1: \"" << params[1].stringValue << "\"";
        } else {
            oss << ", optional1: NOT_PROVIDED";
        }
        
        if (params.size() >= 3) {
            oss << ", optional2: " << params[2].stringValue;
        } else {
            oss << ", optional2: NOT_PROVIDED";
        }
    } else {
        oss << "missing required parameter";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

// Numeric parameter variations
void ConcreteInterpreter::_testInteger(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:NUMERIC:INTEGER executed with value: ";
    if (!params.empty()) {
        oss << static_cast<int>(params[0].numberValue);
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_testFloat(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:NUMERIC:FLOAT executed with value: ";
    if (!params.empty()) {
        oss << std::fixed << std::setprecision(6) << params[0].numberValue;
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_testRange(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:NUMERIC:RANGE executed with ";
    if (params.size() >= 2) {
        oss << "min: " << std::fixed << std::setprecision(6) << params[0].numberValue;
        oss << ", max: " << std::fixed << std::setprecision(6) << params[1].numberValue;
    } else {
        oss << "insufficient parameters (" << params.size() << "/2)";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

// String parameter variations
void ConcreteInterpreter::_testQuotedString(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:STRING:QUOTED executed with text: ";
    if (!params.empty()) {
        oss << "\"" << params[0].stringValue << "\"";
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

// Enum parameter variations
void ConcreteInterpreter::_testEnumMixed(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:ENUM:MIXED executed with option: ";
    if (!params.empty()) {
        oss << params[0].stringValue;
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_testEnumNumeric(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "TEST:ENUM:NUMERIC executed with option: ";
    if (!params.empty()) {
        oss << params[0].stringValue;
    } else {
        oss << "NO_PARAM";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

// Query commands with parameters
void ConcreteInterpreter::_queryTestParam(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "QUERY_PARAM_RESPONSE:";
    if (!params.empty()) {
        oss << static_cast<int>(params[0].numberValue);
    } else {
        oss << "NO_CHANNEL";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_queryTestMulti(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    oss << "QUERY_MULTI_RESPONSE:";
    if (params.size() >= 2) {
        oss << "CH" << static_cast<int>(params[0].numberValue);
        oss << ":" << params[1].stringValue;
    } else {
        oss << "INSUFFICIENT_PARAMS";
    }
    oss << "\n";
    interpreter.outputStream.write(oss.str().c_str());
}

// Error handling test commands
void ConcreteInterpreter::_testErrorSimulate(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    std::ostringstream oss;
    if (!params.empty()) {
        int errorCode = static_cast<int>(params[0].numberValue);
        oss << "ERROR " << errorCode << ": Simulated error condition\n";
    } else {
        oss << "ERROR 102: No error code provided\n";
    }
    interpreter.outputStream.write(oss.str().c_str());
}

void ConcreteInterpreter::_testErrorInvalid(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    interpreter.outputStream.write("ERROR 200: Invalid operation executed\n");
}

// System error query command

void ConcreteInterpreter::_querySystemError(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter) {
    if (!interpreter.errorQueue.empty()) {
        interpreter.outputStream.write(interpreter.errorQueue.front());
        interpreter.errorQueue.pop();
    } else {
        interpreter.outputStream.write("0,\"NO ERROR\"\n");
    }
}
