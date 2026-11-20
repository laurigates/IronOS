# Alientek T90

> **OS Support:**
> - **Linux / macOS:** Supported via `atktool` (`Tools/atktool/`, requires `hidapi`).
> - **Windows:** Flash `.atk` images using Alientek's official Windows updater (`ATK-T90 Upgrade` from Alientek's download page), or build `atktool` with `hidapi` under MSYS2/MinGW. `atktool` has primarily been tested on macOS and Linux.

The T90 ships a closed **USB HID "update-mode" bootloader** (it does **not** enumerate as a USB mass
storage drive like the Miniware irons, and there is no exposed SWD header by default). IronOS builds
produce a `T90_<Language-Code>.atk` image that can be flashed over the stock USB HID bootloader.

> Flashing is hardware-affecting and only works while the iron is in update-mode. Always keep a copy
> of the stock factory `.atk` firmware (see [Section 5: Recovery](#5-recovery--factory-firmware)) so
> you can restore the original application if needed.

## 1. Get the firmware

Download `T90.zip` from the [releases page](https://github.com/Ralim/IronOS/releases) (or the build
artifacts for a mainline build) and extract the `T90_<Language-Code>.atk` file.

To build it yourself, from the repository root:

```sh
docker run --rm -v "$PWD":/build/ironos -w /build/ironos/source ironos-builder:latest \
  make model=T90 -j8 firmware-EN
```

The output is `source/Hexfile/T90_EN.atk` (plus `.bin/.hex/.elf`). `firmware-<LANG>` selects other
languages. The application links to flash base `0x08005000` (the 20 KB bootloader sits below it).

## 2. Build `atktool` (Linux / macOS)

`atktool` lives in `Tools/atktool/` (a small C CLI with a Makefile; USB flashing requires `hidapi`).
Install `hidapi` (`brew install hidapi` on macOS, `apt install libhidapi-dev` on Debian/Ubuntu) and
build with `make -C Tools/atktool`.

## 3. Enter update-mode and probe

To enter update-mode: **hold button B, plug in USB, and keep power for ~4 seconds** — the screen shows
`Updating...`. Confirm over HID:

```sh
atktool probe
```

A good reply shows `found device at VID 19F5 PID 3245`, a valid 21-byte `GET_INFO` reply and
`PROTOCOL CONFIRMED`. If `probe` finds nothing, the iron is not in update-mode — redo the
B + USB + power sequence.

## 4. Flash

```sh
atktool flash source/Hexfile/T90_EN.atk --force
```

It streams the image in 32-byte chunks and commits with **SetBoot (fn 0x15)**, which finalises the
freshly written image and reboots into it. On success it prints `flashed N bytes in M chunks` and the
iron boots the new firmware.

On Windows, you can alternatively load `T90_EN.atk` directly in the official Alientek T90 firmware
upgrade utility while the iron is in `Updating...` mode.

## 5. Recovery & Factory Firmware

The official factory `.atk` firmware and Windows updater package are distributed by Alientek on the
[OpenEdv / Alientek T90 download page](http://www.openedv.com/docs/tool/hanjie/T90.html) (and
community mirrors in [IronOS Issue #2195](https://github.com/Ralim/IronOS/issues/2195)). You can also
dump a full backup of your iron's internal flash (`FLASH.BIN`) before flashing by loading
`t90_msdmem_0.0.5.zip` from [Issue #2195](https://github.com/Ralim/IronOS/issues/2195#issuecomment-3962598616).

If the new app does not boot (black/blank screen, or stuck on `Updating...`), re-enter update mode
(hold B while plugging in USB-C) and reflash the factory `.atk` image:

```sh
atktool flash t90-factory.atk --force
```

## Notes

- `atktool unpack <in.atk> <out.bin>` strips the 13-byte header and de-obfuscates the payload
  (XOR 0xFF) to the raw application, which maps to `0x08005000` — useful for inspecting the stock
  firmware. `atktool info <file.atk>` prints the header and vector table without unpacking.
- The T90A / T90B / T90C use different cartridge families (C210 / C245 / T65) and are **not**
  interchangeable. `model=T90` targets the **T90B** (C245) variant.
