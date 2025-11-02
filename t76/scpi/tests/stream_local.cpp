/**
 * @file stream.cpp
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#include "stream_local.hpp"
#include <cstring>
#include <cstdlib>
#include <string>
#include <string.h>


using namespace T76::SCPI;


T76::SCPI::TestInputStream::TestInputStream(const std::string& data) {
    _data = data;
    _index = 0;
}

uint8_t T76::SCPI::TestInputStream::read() {
    if (_index < _data.length()) {
        return static_cast<uint8_t>(_data[_index++]);
    }

    return 0; // Return 0 if no more data is available
} 

size_t T76::SCPI::TestInputStream::available() {
    return _data.length() - _index;
}

void T76::SCPI::TestOutputStream::write(uint8_t byte, bool flush) {
    _output.push_back(static_cast<char>(byte));
}

void T76::SCPI::TestOutputStream::write(const std::string& data, bool flush) {
    _output.append(data);
}

std::string T76::SCPI::TestOutputStream::getOutput() const {
    return _output;
}

T76::SCPI::TestOutputStream::~TestOutputStream() {
    // Destructor logic if needed
}

T76::SCPI::TestOutputStream::TestOutputStream() {
    _output = "";
}

