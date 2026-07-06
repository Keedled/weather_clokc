# OTA Bring-Up Notes

## Flash Layout

```text
0x08000000 - 0x0800FFFF  Bootloader
0x08010000 - 0x0801FFFF  OTA metadata
0x08020000 - 0x0807FFFF  Runtime app
0x08080000 - 0x080DFFFF  OTA staging image
0x080E0000 - 0x080FFFFF  Reserved
```

## Build Outputs

Bootloader project:

```text
Bootloader/MDK-ARM/bootloader.uvprojx
```

Outputs:

```text
Bootloader/MDK-ARM/bootloader/bootloader.hex
Bootloader/MDK-ARM/bootloader.bin
```

App project:

```text
MDK-ARM/ZGT6_weatherClock.uvprojx
```

Outputs:

```text
MDK-ARM/ZGT6_weatherClock/ZGT6_weatherClock.hex
MDK-ARM/ZGT6_weatherClock.bin
```

The relocated app is linked at `0x08020000` and defines:

```text
USER_VECT_TAB_ADDRESS
VECT_TAB_OFFSET=0x00020000
```

## First Board Bring-Up

1. Build `Bootloader/MDK-ARM/bootloader.uvprojx`.
2. Build `MDK-ARM/ZGT6_weatherClock.uvprojx`.
3. Program `Bootloader/MDK-ARM/bootloader/bootloader.hex`.
4. Program `MDK-ARM/ZGT6_weatherClock/ZGT6_weatherClock.hex`.
5. Reset the board.

The app hex already contains `0x08020000` addresses. When programming the app,
do not use full-chip erase after the bootloader has been programmed, or the
bootloader will be erased. Use sector erase/programming for the app image.

If only the bootloader is programmed and no valid app exists at `0x08020000`,
the bootloader will stay in its fallback loop.

## Sending an OTA Image

Use the relocated app binary generated from `MDK-ARM/ZGT6_weatherClock.uvprojx`:

```powershell
python Tools\send_ota_mqtt.py `
  --bin MDK-ARM\ZGT6_weatherClock.bin `
  --host 10.59.125.248 `
  --port 1883 `
  --command-topic topic1 `
  --ack-topic weather_clock/zgt6_001/ack `
  --chunk-size 128 `
  --ack-timeout-sec 60 `
  --version 2
```

After `ota_ready:<size>:<crc>` is reported, reset the board. The bootloader will
verify the staging image, copy it to `0x08020000`, mark the metadata copy done,
and jump to the app.
