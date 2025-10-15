# T76 Instrument Core

This project is a template for building “prosumer”-grade firmware for measurement instruments based on RP2350-based boards, such as the Raspberry Pi Pico 2.

It provides these features:

- C++ code written to modern standards using the Pi Pico SDK
    - Support for the RP2350 only
    - No support for Arduino; use the Pico SDK only
    - No support for displays, as plenty of alternatives exist already
    - Fallback to plain C for critical tasks where warranted
    - Use of CMake for configuration through and through
- Full use of multicore:
  - Bare-metal critical tasks running on one core
  - FreeRTOS running on the other for housekeeping/communication tasks
- Reliance on FreeRTOS for all memory management
- Support for industry-standard communications
  - SCPI over USBTMC for instrument management
- USB Serial for debug / status management
- Safety features
  - Extensive unit testing harness built into the codebase
  - Last-ditch exception manager that puts the instrument in safe mode on crash and halt
