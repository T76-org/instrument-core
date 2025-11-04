/**
 * @file trie.hpp
 * @brief Trie data structure for SCPI interpreter.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file defines the TrieNode structure used in the SCPI interpreter.
 * A trie node represents a single character in a SCPI command and contains
 * information about its children, flags indicating whether it is a terminal
 * or invalid node, and the index of the command it represents.
 * 
 */


#pragma once

#include <cstdint>
#include <functional>
#include <vector>


namespace T76::SCPI {

    /**
     * @brief TrieNodeFlags enum
     * 
     * This enum represents the flags that can be set for a TrieNode.
     * - Terminal: Indicates that this node is a terminal node (i.e., it represents a complete command).
     * - Invalid: Indicates that this node is a null node (i.e., it does not represent a valid command).
     */
    enum class TrieNodeFlags : uint8_t {
        Terminal = 0x01,                            // Indicates that this node is a terminal node
    };

    /**
     * @brief TrieNode structure
     * 
     * This structure represents a node in the trie data structure used for SCPI command parsing.
     * Each node contains:
     * - character: The character represented by this node.
     * - flags: Flags indicating whether this node is terminal or invalid.
     * - childCount: The number of children this node has.
     * - children: A pointer to an array of child nodes.
     * - commandIndex: The index of the command this node represents (if any).
     */
    struct TrieNode {
        const uint8_t character;                    // The character represented by this node
        const uint8_t flags;                        // Flags indicating whether this node is terminal or invalid
        const uint8_t childCount = 0;               // The number of children this node has
        const TrieNode *children;                   // Pointer to an array of child nodes
        const uint8_t commandIndex = 0;             // The index of the command this node represents (if any)

        bool terminal();                            // Check if this node is a terminal node (i.e., it represents a complete command)

        TrieNode *nextChild(uint8_t character);     // Get the next child node with the specified character, or nullptr if not found
    };

} // namespace T76::SCPI
