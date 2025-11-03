/**
 * @file test_instantiations.hpp
 * @brief Explicit template instantiation declarations for test builds
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file contains explicit template instantiation declarations to suppress
 * compiler warnings about undefined template variables. The actual definitions
 * are provided in commands.cpp.
 */

#pragma once

#include "../interpreter.hpp"

namespace T76::SCPI {
    class ConcreteInterpreter;
    
    // Explicit template instantiation declarations
    extern template const TrieNode Interpreter<ConcreteInterpreter>::_trie;
    extern template const Command<ConcreteInterpreter> Interpreter<ConcreteInterpreter>::_commands[];
}
