# C-SDWire3

Linux-native SDWire3 control in C.

This project controls Badger SDWire3 devices directly through Linux sysfs and
usbfs. It does not require Python, PyUSB, or libusb.

SDWire3 is identified by USB VID/PID:

- VID: `0x0bda`
- PID: `0x0316`

## Current commands

```
csdwire3 list
csdwire3 state [-s serial-or-port]
csdwire3 switch [-s serial-or-port] host
csdwire3 switch [-s serial-or-port] target
```

Aliases `ts` and `dut` are accepted for `host` and `target`.

## Linux implementation

Device discovery uses:

```
/sys/bus/usb/devices/
```

The SDWire3 USB interface with `bInterfaceNumber == 00` is treated as the
switching interface.

Target mode unbinds the interface from its current kernel driver through the
driver's sysfs `unbind` attribute.

Host mode asks the USB bus to probe the interface again through:

```
/sys/bus/usb/drivers_probe
```

After a state transition the USB device is reset with `USBDEVFS_RESET` using
the corresponding node under:

```
/dev/bus/usb/
```

Block-device correlation is topology based. Entries under:

```
/sys/class/block/
```

are resolved to their real sysfs paths and matched against the SDWire3 USB
device subtree. Partition entries are ignored.

## Build

```
make
```

The code is C99 and currently targets Linux only.

## Permissions

Listing and reading state normally require no special permissions. Switching
mode requires permission to write the relevant sysfs bind/unbind nodes and to
reset the USB device through usbfs. During development, run the switching
commands as root.

A dedicated udev/policy setup can be added once the exact desired deployment
model is settled.

## Development status

The first implementation intentionally keeps the layers small:

- `src/main.c` contains CLI parsing only.
- `src/sdwire3.c` contains Linux discovery, state switching, USB reset, and
  block-device correlation.
- `include/sdwire3.h` is the public interface.

The next validation step is testing the sysfs interface name and reset sequence
against physical SDWire3 hardware and tightening behaviour around re-enumeration
if needed.
