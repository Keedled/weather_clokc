# OTA MQTT Protocol

This is the first OTA transport used by the app-side implementation. It sends
firmware bytes as hex text through the existing MQTT command topic so it can
reuse the line-based ESP32 receive demux safely.

## Topics

- Command topic subscribed by the device: `topic1`
- ACK/status topic published by the device: `weather_clock/zgt6_001/ack`

## Command Flow

1. Start a transfer:

   ```text
   ota_begin:<image_size_dec>:<crc32_hex>[:image_version_dec]
   ```

   Example:

   ```text
   ota_begin:34567:89ABCDEF:2
   ```

2. Send chunks in order:

   ```text
   ota_chunk:<offset_dec>:<hex_bytes>
   ```

   Example for 8 bytes at offset 0:

   ```text
   ota_chunk:0:2000012001020304
   ```

   The current implementation requires strictly sequential offsets. The next
   offset is the received byte count returned by `ota_chunk_ok:<received>`.

3. Finish and mark the staging image as pending:

   ```text
   ota_end
   ```

4. Optional commands:

```text
ota_status
ota_abort
ota_reboot
```

## ACK Examples

```text
ota_begin_ok
ota_chunk_ok:128
ota_ready:34567:89ABCDEF
ota_rebooting
ota_status:receiving:128/34567:12345678/89ABCDEF
```

## PC Sender Script

After generating a raw binary file, the recommended sender is the Python script:

```powershell
python Tools\send_ota_mqtt.py --bin MDK-ARM\ZGT6_weatherClock.bin
```

Optional parameters:

```powershell
--host 10.59.125.248 --port 1883 --command-topic topic1 --ack-topic weather_clock/zgt6_001/ack --chunk-size 128 --delay-ms 0 --ack-timeout-sec 15 --version 2
```

The Python script is self-contained and uses only the Python standard library.
It connects to the MQTT broker directly, subscribes to the ACK topic, publishes
`ota_begin`, waits for `ota_begin_ok`, sends one `ota_chunk`, waits for the
matching `ota_chunk_ok:<received>`, and only then sends the next chunk. After
all chunks are accepted it publishes `ota_end` and waits for `ota_ready`.

The PowerShell sender is still available:

```powershell
powershell -ExecutionPolicy Bypass -File Tools\send_ota_mqtt.ps1 -BinPath MDK-ARM\ZGT6_weatherClock.bin
```

Optional parameters:

```powershell
-HostName 10.59.125.248 -Port 1883 -CommandTopic topic1 -AckTopic weather_clock/zgt6_001/ack -ChunkSize 128 -DelayMs 0 -AckTimeoutSec 15 -Version 2
```

The PowerShell sender uses the same ACK-driven flow as the Python sender.

`DelayMs` is now only an extra pause after each successful ACK. The normal flow
control is ACK-driven, not timer-driven.

## Current Safety Model

The current app writes the image only to the staging area:

```text
0x08080000 - 0x080DFFFF
```

It then writes OTA metadata to:

```text
0x08010000 - 0x0801FFFF
```

The running app is not overwritten by these MQTT commands. A bootloader is still
needed to copy a verified staging image into the runtime app area at
`0x08020000`.
