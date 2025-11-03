import usb
import pyvisa


# Using PyVISA, find the first 0x2E8A::0x000A device and query its IDN string

rm = pyvisa.ResourceManager()

# Find the first 0x2E8A::0x000A device
resources = rm.list_resources()
target_device = None
for resource in resources:
    if "USB0::0x2E8A::0x000A" in resource:
        target_device = resource
        break

if target_device:
    print(f"Found target device: {target_device}")
    # Open the device
    instrument = rm.open_resource(target_device)

    # Query the IDN string
    idn_string = instrument.query("*IDN?")
    print(f"IDN String: {idn_string}")
else:
    print("No device with VID:PID 0x2E8A:0x000A found.")

# print(pyvisa.ResourceManager().list_resources())

# resource_string = "USB0::0x2E8A::0x000A::7913911B4CB0B01C::4::INSTR"
# r = pyvisa.ResourceManager().open_resource(resource_string)
# result = r.query_ascii_values("*IDN?", "s")
# print(result)

# Find all USB devices
# devices = usb.core.find(find_all=True)

# for device in devices:
#     try:
#         print(f"Device: {device.idVendor:04x}:{device.idProduct:04x}")
#         print(f"  Manufacturer: {usb.util.get_string(device, device.iManufacturer) if device.iManufacturer else 'Unknown'}")
#         print(f"  Product: {usb.util.get_string(device, device.iProduct) if device.iProduct else 'Unknown'}")

#         for cfg in device:
#             print(f"  Configuration: {cfg.bConfigurationValue}")
#             for intf in cfg:
#                 print(f"    Interface: {intf.bInterfaceNumber}")
#                 print(f"               {intf}")
#                 for ep in intf:
#                     print(f"      Endpoint: {ep.bEndpointAddress:02x}")
#         print()
#     except Exception as e:
#         print(f"Error reading device {device.idVendor:04x}:{device.idProduct:04x}: {e}")
#         print()
