/**
 * @file stream.hpp
 * @brief Implementation of input and output streams for SCPI interpreter.
 * @copyright Copyright (c) 2025 MTA, Inc.
 * 
 * This file defines the base classes for input and output streams used by the SCPI interpreter.
 * The `InputStreamBase` class provides an interface for reading bytes from a stream,
 * while the `OutputStreamBase` class provides an interface for writing bytes and strings to a
 * stream.
 * 
 * You will have to provide concrete implementations of these classes for your specific
 * input and output mechanisms, such as serial communication, file I/O, or network sockets.
 * 
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace T76::SCPI {
    
    class OutputStreamBase {
    public:

        /**
         * @brief Write a byte to the output stream.
         * 
         * This method writes a single byte to the output stream. Note that
         * the implementation should handle buffering and flushing as necessary,
         * 
         * @param byte The byte to write to the output stream.
         * @param flush If true, the output stream should be flushed after writing the byte.
         *              Default is false. Note that flushing is required in order for a
         *              command response to be sent back to the host.
         */
        virtual void write(uint8_t byte, bool flush = false) = 0;

        /**
         * @brief Write a string to the output stream.
         * 
         * This method writes a string to the output stream. The implementation
         * should handle any necessary encoding or formatting. Like the byte write method,
         * it should not block and should return immediately, providing support for
         * asynchronous or buffered output.
         * 
         * @param str The string to write to the output stream.
         * @param flush If true, the output stream should be flushed after writing the string.
         *              Default is false. Note that flushing is required in order for a
         *              command response to be sent back to the host.
         */
        virtual void write(const std::string &str, bool flush = false) = 0;

        /**
         * @brief Formats a string for output.
         * 
         * @param str 
         * @return std::string The formatted string.
         * 
         * Adds leading and training quotation marks, and escapes any
         * quotation marks within the string.
         */
        std::string formatString(const std::string &str) const;

        /**
         * @brief Write an Arbitrary Block Data to the output stream.
         * 
         * This method writes a block of data to the output stream in
         * accordance with the SCPI standard for Arbitrary Block Data.
         * 
         * It outputs an octothorpe (#), followed by the number of 
         * digits in the integer representation of the size of the data,
         * followed by the size itself, and then the data itself.
         * 
         * For example, if the size of the data is 1234 bytes,
         * the output will be:
         * 
         * #41234<1234 bytes of data>
         * 
         * Note that no newline character is added at the end of the output.
         * 
         * @param data Pointer to the data to write.
         * @param size The size of the data in bytes.
         */
        void writeArbitraryBlockData(const uint8_t *data, size_t size);
    };

} // namespace T76::SCPI
