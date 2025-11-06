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
        nargs="?",
        const="query",
        choices=["on", "off", "blink"],
        help="Control the LED state or query status. Use without argument to query, or specify 'on', 'off', or 'blink' to set state.",
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
    elif args.led is not None:
        command = "LED:STAT?" if args.led == "query" else f"LED:STAT {args.led}"
    else:
        command = "*IDN?"

    response = send_scpi(command, args.resource_fragment)
    if command.rstrip().endswith("?"):
        print(f"Response to {command}: {response}")
    else:
        print(response)


if __name__ == "__main__":
    main()
