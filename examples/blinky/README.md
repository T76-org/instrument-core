# Blinky Instrument Core Example

This example demonstrates how to set up a basic project using the Instrument Core library on a Raspberry Pi Pico. It includes the necessary configurations for FreeRTOS and the Pico SDK.

## Prerequisites

- Raspberry Pi Pico SDK
- FreeRTOS Kernel
- Instrument Core library

## Building the Project

1. Clone the repository and navigate to the `examples/blinky` directory.
2. Create a build directory:
   ```bash
   mkdir build
   cd build
   ```
3. Run CMake to configure the project:
   ```bash
   cmake ..
   ```
4. Build the project:
   ```bash
   make
   ```
5. Flash the resulting binary to your Raspberry Pi Pico.
    ```bash
    picotool load t76-instrument-core.uf2
    ```

Alternatively, import the project into your favourite IDE that supports CMake and the Pico SDK. I used Visual Studio Code with the CMake Tools extension.

## Project Structure

- `app.cpp`: Application-specific code; this is where the T76::Core::App subclass is implemented.
- `main.cpp`: Entry point of the application; starts the app.
- `scpi.yaml`: SCPI command definitions for the instrument. Gets compiled into `scpi_commands.cpp`.

## Testing

The `scpi_test.py` script can be used to test the SCPI commands implemented in this example. Make sure to have Python installed along with any required libraries, then from your command line:

```bash
python scpi_test.py             # Runs the *IDN? command
python scpi_test.py -led        # Displays the status of the onboard LED
python scpi_test.py -led on     # Turns the onboard LED on
python scpi_test.py -led off    # Turns the onboard LED off
python scpi_test.py -led blink  # Makes the onboard LED blink
```

