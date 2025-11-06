import argparse
import fcntl
import pyvisa
from time import sleep
import sys
import termios
import os

DEFAULT_RESOURCE_FRAGMENT = "USB0::0x2E8A::0x000A"


def send_scpi(command: str, resource_fragment: str = DEFAULT_RESOURCE_FRAGMENT) -> str:
    rm = pyvisa.ResourceManager()
    resource = next((res for res in rm.list_resources()
                    if resource_fragment in res), None)
    if resource is None:
        return "Device not found."
    instrument = rm.open_resource(resource)
    try:
        if command.rstrip().endswith("?"):
            return instrument.query(command)
        instrument.write(command)
        return "Command sent."
    finally:
        instrument.close()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Send a SCPI command to the connected instrument.")
    parser.add_argument(
        "-reset",
        action="store_true",
        help="Reset the instrument by sending *RST. Cannot be combined with other commands.",
    )
    parser.add_argument(
        "-pid",
        action="store_true",
        help="Continuously output all PID parameters and current voltage.",
    )
    parser.add_argument(
        "-kp",
        type=float,
        help="Set the proportional gain (Kp) parameter.",
    )
    parser.add_argument(
        "-ki",
        type=float,
        help="Set the integral gain (Ki) parameter.",
    )
    parser.add_argument(
        "-kd",
        type=float,
        help="Set the derivative gain (Kd) parameter.",
    )
    parser.add_argument(
        "-setpoint",
        type=float,
        help="Set the target voltage setpoint.",
    )
    parser.add_argument(
        "command",
        nargs="?",
        help="SCPI command to send to the instrument.",
    )
    parser.add_argument(
        "-r", "--resource-fragment",
        default=DEFAULT_RESOURCE_FRAGMENT,
        help="Fragment to identify the instrument resource.",
    )
    args = parser.parse_args()

    if args.reset:
        if args.led_query or args.command:
            parser.error("Do not combine -reset with other command options.")
        command = "*RST"
    elif args.pid:
        def get_char():
            """Return the next pending keyboard character, or None if no input."""
            fd = sys.stdin.fileno()
            old_settings = termios.tcgetattr(fd)
            old_flags = fcntl.fcntl(fd, fcntl.F_GETFL)

            try:
                # Set the terminal to raw mode (unbuffered, no echo)
                new_settings = termios.tcgetattr(fd)
                new_settings[3] = new_settings[3] & ~termios.ICANON & ~termios.ECHO
                termios.tcsetattr(fd, termios.TCSANOW, new_settings)

                # Set stdin to non-blocking
                old_flags = fcntl.fcntl(fd, fcntl.F_GETFL)
                fcntl.fcntl(fd, fcntl.F_SETFL, old_flags | os.O_NONBLOCK)

                try:
                    char = sys.stdin.read(1)
                    return char
                except IOError:
                    return None
            finally:
                # Restore the terminal settings
                termios.tcsetattr(fd, termios.TCSAFLUSH, old_settings)
                fcntl.fcntl(fd, fcntl.F_SETFL, old_flags)

        print("PID Monitor - Press P/p for Kp, I/i for Ki, D/d for Kd, q to quit")
        try:
            while True:
                # Get current values
                kp = send_scpi("PID:KP?", args.resource_fragment).strip()
                ki = send_scpi("PID:KI?", args.resource_fragment).strip()
                kd = send_scpi("PID:KD?", args.resource_fragment).strip()
                voltage = send_scpi(
                    "MEAS:VOLT?", args.resource_fragment).strip()
                setpoint = send_scpi(
                    "SET:VOLT?", args.resource_fragment).strip()
                print(
                    f"\rKp: {kp}, Ki: {ki}, Kd: {kd}, Setpoint: {setpoint}, Voltage: {voltage} V\r", end="", flush=True)

                # Check for key press
                ch = get_char()
                if ch:
                    match ch:
                        case 'q':
                            break
                        case 'P':
                            current_kp = float(kp)
                            send_scpi(
                                f"PID:KP {current_kp + 0.1}", args.resource_fragment)
                        case 'p':
                            current_kp = float(kp)
                            send_scpi(
                                f"PID:KP {current_kp - 0.1}", args.resource_fragment)
                        case 'I':
                            current_ki = float(ki)
                            send_scpi(
                                f"PID:KI {current_ki + 0.01}", args.resource_fragment)
                        case 'i':
                            current_ki = float(ki)
                            send_scpi(
                                f"PID:KI {current_ki - 0.01}", args.resource_fragment)
                        case 'D':
                            current_kd = float(kd)
                            send_scpi(
                                f"PID:KD {current_kd + 0.001}", args.resource_fragment)
                        case 'd':
                            current_kd = float(kd)
                            send_scpi(
                                f"PID:KD {current_kd - 0.001}", args.resource_fragment)
                        case 'V':
                            current_setpoint = float(setpoint)
                            send_scpi(
                                f"SET:VOLT {current_setpoint + 0.1}", args.resource_fragment)
                        case 'v':
                            current_setpoint = float(setpoint)
                            send_scpi(
                                f"SET:VOLT {current_setpoint - 0.1}", args.resource_fragment)

                sleep(0.01)
        except KeyboardInterrupt:
            print("\nExiting PID monitoring.")
        return
    elif args.kp is not None:
        command = f"PID:KP {args.kp}"
    elif args.ki is not None:
        command = f"PID:KI {args.ki}"
    elif args.kd is not None:
        command = f"PID:KD {args.kd}"
    elif args.setpoint is not None:
        command = f"SET:VOLT {args.setpoint}"
    elif args.command:
        command = args.command
    else:
        command = "*IDN?"

    response = send_scpi(command, args.resource_fragment)
    if command.rstrip().endswith("?"):
        print(f"Response to {command}: {response}")
    else:
        print(response)


if __name__ == "__main__":
    main()
