# Buck Converter Example

This example demonstrates the implementation of a buck converter control system using a PID controller. The code includes the initialization of PID parameters, the PWM interrupt handler for real-time control, and the calculation of control outputs based on feedback from the system.

Note that this is a toy example meant for educational purposes only. It's missing important features required for a production-level buck converter, such as current sensing, advanced protection mechanisms, and robust tuning methods. Do not use this code in real hardware without significant modifications and safety considerations.

## Key Features

The example uses a realtime filtered PID controller to regulate the output voltage of the buck converter. The control loop runs on core 1 at a fixed frequency of 30kHz, ensuring timely adjustments to the PWM duty cycle based on the measured output voltage.

Meanwhile, core 0 handles non-time-critical tasks such as user interface and telemetry through SCPI, allowing you to monitor voltage and tune the PID parameters on the fly. Because all the parameters are encapsulated in 32-bit floats, they can be modified from core 0 without any special synchronization mechanisms, since all read/write operations are atomic on the RP2350.

## Safety mechanisms

The example also demonstrates the use of the built-in safety watchdog to ensure that the control loop is running as expected. If the control loop fails to feed the watchdog within the specified timeout, the system will reset to prevent potential damage.

If a fault occurs, the buck converter will immediately disable the PWM outputs to ensure safe operation.
