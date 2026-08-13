import usb.core
import usb.util

VID = 0x1a86
PID = 0x7523

dev = usb.core.find(idVendor=VID, idProduct=PID)
if dev is None:
    print("Không tìm thấy CH340 (1a86:7523)!")
    exit(1)

print(f"Tìm thấy: {dev.manufacturer} {dev.product}")

# Detach kernel driver (ch341) nếu đang giữ
for cfg in dev:
    for intf in cfg:
        n = intf.bInterfaceNumber
        if dev.is_kernel_driver_active(n):
            print(f"Detach kernel driver interface {n}...")
            dev.detach_kernel_driver(n)

# Gửi vendor control transfer – RS485 mode
dev.ctrl_transfer(0x40, 0x0B, 0x0101, 0, b'')
print("RS485 mode set!")

usb.util.dispose_resources(dev)
