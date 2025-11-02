/**
 * @file stream.hpp
 * @brief Concrete stream implementation for testing SCPI commands.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 */

#pragma once

#include "../stream.hpp"

#include <string>


namespace T76::SCPI {
    
    class TestInputStream : public InputStreamBase {
    public:
        TestInputStream(const std::string& data);

        uint8_t read() override;
        size_t available() override;

    protected:
        std::string _data;
        size_t _index;
    };

    class TestOutputStream : public OutputStreamBase {
    public:
        TestOutputStream(); // Explicitly declare the default constructor
        void write(uint8_t byte, bool flush = false) override;
        void write(const std::string& data, bool flush = false) override;

        std::string getOutput() const;

        ~TestOutputStream(); // Explicitly declare the destructor to resolve the error

    private:
        std::string _output;
    };

} // namespace T76::SCPI
