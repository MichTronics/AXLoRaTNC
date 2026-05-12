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
- `radio`
- `ax25`
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
disconnect
```

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
- T1 retry timer and N2 retry limit
- KISS serial framing
- LoRa transport of one complete AX.25 frame per LoRa packet
- Variant folder for `devkitv1_e22`

