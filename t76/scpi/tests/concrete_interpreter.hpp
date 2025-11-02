/**
 * @file concrete_interpreter.hpp
 * @brief Test concrete implementation of the SCPI interpreter.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#pragma once

#include "../interpreter.hpp"
#include "test_instantiations.hpp"
#include <sstream>


namespace T76::SCPI {

    class ConcreteInterpreter {
    public:
        // Test command handlers referenced in commands.cpp
        
        // Basic test commands
        void _testSimple(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _queryTestSimple(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testMixedCase(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // ABD test commands
        void _testABDSimple(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // Commands with single parameters
        void _testNumber(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testString(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testBoolean(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testEnum(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // Commands with multiple parameters
        void _testMultiTwo(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // Commands with optional parameters
        void _testOptionalSingle(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testOptionalMultiple(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // Numeric parameter variations
        void _testInteger(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testFloat(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testRange(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // String parameter variations
        void _testQuotedString(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // Enum parameter variations
        void _testEnumMixed(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testEnumNumeric(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // Query commands with parameters
        void _queryTestParam(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _queryTestMulti(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // Error handling test commands
        void _testErrorSimulate(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        void _testErrorInvalid(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);

        // System error query command
        void _querySystemError(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);

    private:
        // Helper method to format parameters for output
        std::string _formatParameters(const std::vector<ParameterValue> &params, Interpreter<ConcreteInterpreter> &interpreter);
        
        // Helper method to format a single parameter
        std::string _formatParameter(const ParameterValue &param, ParameterType type);
    };

} // namespace T76::SCPI
