/**
 * @file stream.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "t76/scpi_stream.hpp"


using namespace T76::SCPI;


std::string OutputStreamBase::formatString(const std::string &str) const {
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

void OutputStreamBase::writeArbitraryBlockData(const uint8_t *data, size_t size) {
    // Write an Arbitrary Block Data to the output stream
    if (size == 0) {
        return; // Nothing to write
    }

    std::string sizeStr = std::to_string(size);
    size_t numDigits = sizeStr.length();

    // Write the octothorpe and size
    write('#');
    write(std::to_string(numDigits));
    write(std::to_string(size));

    // Write the data itself
    for (size_t i = 0; i < size; ++i) {
        write(data[i]);
    }
}

