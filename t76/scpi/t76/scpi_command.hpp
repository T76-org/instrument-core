/**
 * @file command.hpp
 * @brief Command structure for SCPI interpreter.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * A command definition consists of a handler that is called when the command
 * is executed, the number of parameters expected for the command, and a pointer
 * to an array of parameter descriptors that define the types and default values
 * of the parameters.
 * 
 * Command definitions are generated automatically by the trie_generator.py script.
 * 
 */

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "scpi_parameter.hpp"


namespace T76::SCPI {

    template <typename TargetT> class Interpreter; // Forward declaration of Interpreter template class
    class ConcreteInterpreter; // Forward declaration of ConcreteInterpreter class

    template <typename TargetT>
    using CommandHandler = void (TargetT::*)(const std::vector<ParameterValue> &, Interpreter<TargetT> &);

    template <typename TargetT>
    struct Command {
        CommandHandler<TargetT> handler;  // The function to call when the command is executed.
        uint8_t parameterCount; // The number of parameters for the command.
        const ParameterDescriptor *parameterDescriptors; // Pointer to parameter descriptors
    };

} // namespace T76::SCPI
