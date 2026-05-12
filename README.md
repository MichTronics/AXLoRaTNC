# AXLoRaTNC

AXLoRaTNC is a real AX.25 packet-radio TNC for ESP32 + LoRa. AX.25 Level 2 frames remain AX.25 frames; LoRa only replaces the classic AFSK/FSK modem layer.

## Target

- ESP32 DevKit V1
- EBYTE E22 SX1262 SPI LoRa module
- PlatformIO
- Arduino framework
- RadioLib

## Build

```sh
./venv/bin/pio run -e devkitv1_e22
```

## LoRa Defaults

The `devkitv1_e22` default radio settings are:

- Frequency: `869.525 MHz`
- Bandwidth: `125 kHz`
- Spreading factor: `SF7`
- Coding rate: `4/5`
- Sync word: `0x12`
- AX.25 FCS is kept inside the LoRa payload
- RadioLib packet CRC is enabled

## Serial Console

Open the serial monitor at `115200` baud.

Commands:

- `help`
- `info`
- `mode`
- `mode console`
- `mode kiss`
- `mode ded`
- `radio`
- `ax25`
- `beacon`
- `beacon on`
- `beacon off`
- `beacon now`
- `beacon text <text>`
- `beacon dest <CALLSIGN-SSID>`
- `beacon interval <seconds>`
- `beacon path <CALL1-SSID,CALL2-SSID|off>`
- `mheard`
- `mheard clear`
- `digi`
- `digi on`
- `digi off`
- `digialias <CALLSIGN-SSID|off>`
- `connect <CALLSIGN-SSID>`
- `disconnect`
- `sendui <DEST> <message>`
- `send <message>`
- `stats`

Example:

```text
sendui PD4MV-0 hello over AX.25 LoRa
connect PD4MV-1
send connected mode test
send this line may wait behind the previous I-frame
disconnect
```

## Digipeater

Digipeating is off by default. Enable it from the serial console:

```text
digi on
digialias WIDE1-1
```

The digipeater currently relays AX.25 UI frames only. It looks for the first unrepeated repeater address in the path, matches it against the node callsign or configured alias, sets the repeated/H bit, recalculates the AX.25 FCS, and retransmits the frame over LoRa. A small duplicate cache suppresses repeat loops.

## Beacons And Mheard

AXLoRaTNC can send local AX.25 UI beacons and keep a local heard table.

```text
beacon text AXLoRaTNC test node
beacon dest CQ
beacon path WIDE1-1
beacon interval 600
beacon on
```

Manual beacon:

```text
beacon now
```

Show heard stations:

```text
mheard
```

Clear heard stations:

```text
mheard clear
```

The heard table records source, last destination, age, frame count, RSSI, SNR, whether a repeated/H-bit was seen, and path length.

## KISS

USB serial also accepts KISS frames:

- `FEND 0xC0`
- `FESC 0xDB`
- `TFEND 0xDC`
- `TFESC 0xDD`
- data frames
- TX delay parameter
- persistence parameter
- slot time parameter
- full duplex parameter

KISS data frames are treated as complete AX.25 frames from the host, with AX.25 FCS appended before LoRa transmit. Received LoRa AX.25 frames are FCS-checked and emitted back as KISS data frames without the FCS, like a normal KISS TNC.

For F6FBB, BPQ, `kissattach`, and other host software, switch to pure KISS mode:

```text
mode kiss
```

This setting is stored in ESP32 NVS and survives reboot. In pure KISS mode no console banner, debug logs, or received text messages are written to USB serial, so the host sees only KISS frames.

To recover the console, send this plain text line over the serial port:

```text
console
```

## WA8DED Hostmode

AXLoRaTNC also has an initial WA8DED-style hostmode for F6FBB testing:

```text
mode ded
```

This mode is persistent and quiet like pure KISS mode. It implements the binary host exchange:

```text
host -> tnc: {channel}{info/cmd}{count}{data...}
tnc -> host: {channel}{code...}
```

Implemented basics:

- `G`, `G0`, `G1` polling
- `C <CALLSIGN-SSID>` connect on channel `1`
- `D` disconnect on channel `1`
- `L` channel status
- channel `1` connected data
- channel `0` unproto/UI data to `CQ`
- `JHOST0` returns to console mode
- `JHOST1` acknowledged

For F6FBB direct hostmode, use host type `D` in `port.sys`. This is an initial one-channel implementation intended for testing F6FBB without Linux `kissattach`.

## Architecture

```text
Application / KISS serial
AX.25 Level 2
AX.25 frame encoder/decoder
LoRa packet transport adapter
RadioLib SX1262 driver
```

RadioLib is isolated under `src/radio/`. The AX.25 stack under `src/ax25/` does not depend on RadioLib.

## Implemented

- AX.25 callsign + SSID address encoding/decoding
- Destination/source/repeater address fields
- UI frames
- I frames
- S frames: RR, RNR, REJ
- U frames: SABM, UA, DISC, DM, UI
- AX.25 CRC-16 FCS
- Modulo-8 sequence numbers
- Basic connected-mode state machine
- Fixed connected-mode TX queue for multiple `send` lines
- UI-frame digipeater with H-bit update and duplicate suppression
- Local UI beacon
- Local mheard table with RSSI/SNR
- T1 retry timer and N2 retry limit
- KISS serial framing
- LoRa transport of one complete AX.25 frame per LoRa packet
- Variant folder for `devkitv1_e22`
