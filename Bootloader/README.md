# OTA Bootloader Skeleton

This folder contains the bootloader-side source for the STM32F407ZG OTA flow.

Expected Flash layout:

```text
0x08000000 - 0x0800FFFF  Bootloader
0x08010000 - 0x0801FFFF  OTA metadata
0x08020000 - 0x0807FFFF  Runtime app
0x08080000 - 0x080DFFFF  OTA staging image
0x080E0000 - 0x080FFFFF  Reserved
```

Boot flow:

```text
1. Read OTA metadata at 0x08010000.
2. If a valid pending image exists, verify staging CRC.
3. Erase the needed app sectors starting at sector 5.
4. Copy the staging image into the app area.
5. Verify copied app CRC.
6. Mark metadata copy-done.
7. Jump to the app at 0x08020000.
```

The Keil project is:

```text
Bootloader/MDK-ARM/bootloader.uvprojx
```

It uses `Bootloader/MDK-ARM/bootloader.sct`, so the bootloader is linked only
into sectors 0-3.

The app project is:

```text
MDK-ARM/ZGT6_weatherClock.uvprojx
```

See `Docs/ota_bringup.md` for the first flashing sequence and OTA test command.
