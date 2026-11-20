# atktool

A small C CLI toolkit for the Alientek **T90** (Nations N32L40x) soldering iron firmware images.

It packs/unpacks the device's `.atk` image format and flashes over USB HID on Linux/macOS (or
Windows with `hidapi`), so you can build and load IronOS on the T90 without the official Windows
updater.

## Build

```sh
cd Tools/atktool
make                 # pack/unpack/info always build; probe/flash need hidapi
```

`probe`/`flash` require **hidapi**:
- macOS: `brew install hidapi`
- Debian/Ubuntu: `apt install libhidapi-dev`

then `make` again (it auto-detects hidapi via `pkg-config`).

## Commands

```
atktool pack    <in.bin> <out.atk> [--date YYYY-MM-DD]   wrap a raw app image into an .atk
atktool unpack  <in.atk> <out.bin>                        extract the raw app image
atktool info    <file.atk>                                print header fields + vector table
atktool probe   [--vid V] [--pid P]                       find the iron in update mode (hidapi)
atktool flash   <in.atk> [--vid V] [--pid P] --force      flash over USB (hidapi)
```

## .atk format

Reverse-engineered and verified by byte-exact round-trip against the official factory `t90.atk`
and PanKleszcz's `t90_msdmem.atk`:

| offset | size | field |
|--------|------|-------|
| 0      | 6    | magic `08 02 0A 01 A0 20` (byte 5 `0x20` = 32-byte transfer chunk size) |
| 6      | 4    | payload size (uint32 little-endian) |
| 10     | 1    | year − 2000 (build date, metadata only) |
| 11     | 1    | month |
| 12     | 1    | day |
| 13     | N    | application image, **each byte XOR 0xFF** |

There is no payload checksum or signature; the application is linked at `0x08005000` (the 20 KB
bootloader lives below it). The older 32-byte ASCII-name header (`dozed-dev/atk-fw-util`) is the
**T80P** format and does NOT work on the T90.

## Flashing (read before using `flash`)

Entering update mode: **hold the B button, plug in USB-C, power on** — after ~4 s the screen shows
`Updating...` and the device enumerates on the USB HID bootloader at **VID `0x19F5` / PID `0x3245`**
(`N32L40xCustm HID`, serial `ATK-T90`).

The USB protocol uses 64-byte HID reports `[addr, fn, seq, len, data…, chk8]` where:
- Host-to-device `addr = 0xAF`, device-to-host `addr = 0xDF`
- `chk8` is a 1-byte additive checksum (all frame bytes including `chk8` sum to `0 mod 256`)
- Function codes: `0x10 GetDevInfo`, `0x11 SetFwInfo` (13-byte `.atk` header), `0x12 SetStartSendFW`,
  `0x13 SetSendData` (32-byte chunks), `0x14 SetSendDataEnd`, and `0x15 SetBoot` (finalizes the
  freshly flashed image and reboots into it).

Before flashing:
1. Keep a copy of the **factory `.atk` firmware** so you can always re-flash and recover.
2. Always run `atktool probe` first to confirm the device handshake.
3. First-boot a freshly flashed iron **without a soldering tip installed** until the firmware is
   validated.

## Examples

```sh
# build a flashable image from the IronOS T90 build
atktool pack ../../source/Hexfile/T90_EN.bin T90_EN.atk

# inspect an image
atktool info T90_EN.atk

# recover the factory firmware
atktool flash t90-factory.atk --force
```
