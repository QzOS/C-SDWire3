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
csdwire3 state [-s id]
csdwire3 switch [-s id] host
csdwire3 switch [-s id] target
```

Aliases `ts` and `dut` are accepted for `host` and `target`, and `--serial`
for `-s`. `-s` may be left out when only one SDWire3 is connected.

`list` shows one line per device:

```
PORT         SERIAL                   STATE    BLOCK
2-1          20120501030900000        host     /dev/sdb
2-2          20120501030900000        target   -
```

`id` can be:

- the USB port from `list`, e.g. `2-1` or `2-1.3`. This always works.
- the serial number, if no other connected SDWire3 has the same one.
  Several units can share a serial (see the example above); csdwire3 then
  refuses to guess and lists the matching ports.
- the ID printed by `sdwire-cli`, `<serial>.<ports>` (e.g.
  `20120501030900000.1.3` for port `2-1.3`), so existing scripts keep
  working.

Exit status is 0 on success, 1 on failure and 2 on a usage error.

## Linux implementation

Device discovery uses:

```
/sys/bus/usb/devices/
```

The SDWire3 USB interface with `bInterfaceNumber == 00` is treated as the
switching interface. A device is in host mode while a kernel driver
(`usb-storage` or `uas`) is bound to that interface, and in target mode
otherwise. Like libusb, a userspace claim through `usbfs` does not count as
a kernel driver.

Switching uses the same kernel requests as the reference `sdwire-cli`
(PyUSB/libusb), issued directly on the device node under:

```
/dev/bus/usb/
```

- target: `USBDEVFS_DISCONNECT` detaches the kernel driver from interface 0,
  then `USBDEVFS_RESET` resets the device.
- host: `USBDEVFS_CONNECT` lets the kernel bind a driver to interface 0, then
  `USBDEVFS_RESET` resets the device. If no driver accepts the interface,
  the switch fails without a reset.

After the reset the new state is verified for up to five seconds; `switch`
fails if it is not reached. Switching to the current state does nothing.

Block-device correlation is topology based. Entries under:

```
/sys/class/block/
```

are resolved to their real sysfs paths and matched against the SDWire3 USB
device subtree. Partition entries are ignored. In host mode the block device
appears a moment after `switch` returns (usb-storage waits about a second
before scanning), so scripts should wait for it, e.g. with `udevadm settle`.

## Build

```
make
make check
```

The code is C99 and currently targets Linux only. `make check` runs the
tests in `tests/`. They use a fake sysfs tree and never touch real USB
devices, so they run without hardware or root. CI builds with gcc and clang
using `-Werror` and runs the tests under AddressSanitizer and UBSan.

## Permissions

Listing and reading state need no special permissions. Switching needs write
access to the device node `/dev/bus/usb/BBB/DDD`, so run it as root or give
a group access with a udev rule, for example
`/etc/udev/rules.d/60-csdwire3.rules`:

```
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0bda", ATTR{idProduct}=="0316", MODE="0660", GROUP="plugdev"
```

Reload the rules (`udevadm control --reload`) and replug the SDWire3.

## Known limitations

- `0bda:0316` is a Realtek card reader ID. Any other card reader with the
  same ID is listed and switched as if it were an SDWire3.
- The host cannot see the physical mux. The reported state is the driver
  binding that selects it, as in `sdwire-cli`.
- `switch target` does not check whether the card is mounted. Unmount it
  first.

## Development status

The layers are kept small:

- `src/main.c` contains CLI parsing only.
- `src/sdwire3.c` contains Linux discovery, state switching, USB reset, and
  block-device correlation.
- `include/sdwire3.h` is the public interface.

The switching sequence has not yet been run against physical SDWire3
hardware. Suggested first checks:

1. `csdwire3 list` shows the SDWire3 in host mode with a block device.
2. `cat /sys/bus/usb/devices/<port>/{manufacturer,product,serial}` shows
   whether the SDWire3 can be told apart from a plain Realtek reader, and
   whether serials differ between units.
3. `sudo csdwire3 switch target`: `csdwire3 state` reports `target`, the
   target sees the card, and `dmesg` shows the driver detaching and a reset
   without the device re-enumerating.
4. `sudo csdwire3 switch host`: the state is `host` again and the block
   device returns.
