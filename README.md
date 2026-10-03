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
appears 1.5 to 4.5 seconds after `switch` returns (see
[Hardware test results](#hardware-test-results)). `udevadm settle` returns
before that, so scripts should poll until the block device exists, e.g.
`until [ -b /dev/sdb ]; do sleep 0.1; done`.

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
  same ID is listed and switched as if it were an SDWire3. The SDWire3 does
  not help here: it reports manufacturer `Generic`, product `USB3.0-CRW` and
  the serial `20120501030900000`, the same strings as a plain reader.
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

## Hardware test results

Tested on 2026-10-03 against one physical SDWire3, side by side with the
reference `sdwire-cli` 0.3.1 (PyPI package `sdwire`).

Setup:

- Host: Raspberry Pi 400, Debian 13, kernel 6.18 (aarch64), gcc 14.2.
- SDWire3 USB on a USB 2.0 port (port `1-1.2`, high speed).
- Target: the Pi 400's own microSD slot (`mmc0`, `sdhci-iproc`), so the
  same machine is both host and target.
- Card: 128 GB SDXC.

Functional checks, all passed:

1. `csdwire3 list` shows the SDWire3 in host mode with `/dev/sdb`.
2. `switch target`: `state` reports `target`, `/dev/sdb` goes away and the
   target sees the card as `/dev/mmcblk0`. `dmesg` shows a reset of the same
   USB device number, without re-enumeration.
3. `switch host`: `state` reports `host`, the target drops the card and
   `/dev/sdb` returns.
4. Switching to the current state does nothing (about 20 ms).
5. The `<serial>.<ports>` ID printed by `sdwire-cli` is accepted by `-s`.

Comparison with `sdwire-cli`:

- `strace` shows the same requests in the same order: `USBDEVFS_IOCTL`
  with `USBDEVFS_CONNECT` or `USBDEVFS_DISCONNECT` on interface 0, then
  `USBDEVFS_RESET`.
- The two tools agree on the state whichever of them did the switch.
- From the reset to `/dev/sdb` both take the same time (2.71 s in a
  logged side-by-side run).

Timing, 10 cycles per tool, usb-storage `delay_use=0`. "cmd" is the run time
of the switch command; the other columns are measured from its start:

| Tool       | cmd target | `/dev/mmcblk0` | cmd host | `/dev/sdb`         |
|------------|-----------:|---------------:|---------:|-------------------:|
| csdwire3   |     0.30 s |         1.19 s |   0.21 s | 1.87 to 3.64 s     |
| sdwire-cli |     0.73 s |         1.41 s |   0.64 s | 2.36 to 2.48 s     |

csdwire3 itself takes about 0.2 s. With the default `delay_use=1` the block
device appeared 4.46 s after `switch host` in all 10 runs. The time until
the block device appears is spent outside csdwire3:

- usb-storage waits `delay_use` seconds (default 1) before scanning a new
  device. usb-storage is often built into the kernel, so set it at runtime,
  e.g. with `/etc/tmpfiles.d/usb-storage-delay.conf`:

  ```
  w /sys/module/usb_storage/parameters/delay_use - - - - 0
  ```

- The card reader reports "not ready" until the target has released the
  card. Here the Pi's SD controller polls for card removal about once a
  second. `/dev/sdb` appeared 1.3 to 1.6 s after the target logged
  `card removed`, in every run. The polling explains why the totals vary in
  steps of about a second.

The measurements come from this setup only. A target that does not poll the
card, or is powered off while switching, should give a shorter and steadier
host switch. Repeat the timing with a separate target board.
