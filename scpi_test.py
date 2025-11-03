import argparse
import usb
import pyvisa

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
        "-led",
        dest="led_query",
        action="store_true",
        help="Query the LED status by sending LED:STAT?. Cannot be combined with other commands.",
    )
    parser.add_argument(
        "command",
        nargs="*",
        help="SCPI command to send (default: *IDN?). Use 'led on|off|blink' to control the LED.",
    )
    parser.add_argument(
        "-r",
        "--resource-fragment",
        default=DEFAULT_RESOURCE_FRAGMENT,
        help="Substring used to select the VISA resource.",
    )
    args = parser.parse_args()

    if args.reset:
        if args.led_query or args.command:
            parser.error("Do not combine -reset with other command options.")
        command = "*RST"
    elif args.led_query:
        if args.command:
            parser.error("Do not combine -led with other command options.")
        command = "LED:STAT?"
    elif args.command:
        if args.command[0].lower() == "led":
            if len(args.command) != 2:
                parser.error("Use 'led on', 'led off', or 'led blink'.")
            state = args.command[1].lower()
            if state not in {"on", "off", "blink"}:
                parser.error("LED state must be one of: on, off, blink.")
            command = f"LED:STAT {state.upper()}"
        else:
            command = " ".join(args.command)
    else:
        command = "*IDN?"

    response = send_scpi(command, args.resource_fragment)
    if command.rstrip().endswith("?"):
        print(f"Response to {command}: {response}")
    else:
        print(response)


if __name__ == "__main__":
    main()
