/**
 * @file trie.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */


#include "t76/scpi_trie.hpp"


using namespace T76::SCPI;


bool TrieNode::terminal() {
    return (flags & static_cast<uint8_t>(TrieNodeFlags::Terminal)) != 0;
}

TrieNode *TrieNode::nextChild(uint8_t character) {
    for (uint8_t i = 0; i < childCount; ++i) {
        if (children[i].character == character) {
            return const_cast<TrieNode*>(&children[i]);
        }
    }
    
    // If no child with the given character is found, return the null node
    return nullptr;
}

