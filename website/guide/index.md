# Getting started

## Supported hardware

| Board | MCU | Radio | Build env |
|---|---|---|---|
| ESP32 DevKit V1 + EBYTE E22 | ESP32 | SX1262 | `devkitv1_e22` |
| Heltec WiFi LoRa 32 V3 | ESP32-S3 | SX1262 | `heltec_v3` |
| TTGO T-Beam | ESP32 | SX1276 | `tbeam` |
| LilyGo T3 LoRa32 V1.6.1 | ESP32 | SX1276 | `lilygo_t3_v161` |

## Flash via browser (easiest)

Use the [web installer](/AXLoRaTNC/flash/) — no drivers or tools needed. Requires Chrome or Edge.

## Build from source

```sh
git clone https://github.com/MichTronics/AXLoRaTNC.git
cd AXLoRaTNC
./venv/bin/pio run -e devkitv1_e22   # replace with your board
./venv/bin/pio run -e devkitv1_e22 --target upload
```

## First boot

Open a serial monitor at **115 200 baud**:

```sh
./venv/bin/pio device monitor -b 115200
```

Set your callsign and verify radio defaults:

```text
callsign N0CALL-0
radio
```

Switch to KISS mode when a host application connects:

```text
mode kiss
```

Type `console` (plain text) to return to the interactive console at any time.

## Default LoRa settings (`devkitv1_e22`)

| Parameter | Default |
|---|---|
| Frequency | 869.480 MHz |
| Bandwidth | 125 kHz |
| Spreading factor | SF7 |
| Coding rate | 4/5 |
| Sync word | 0x12 |
| TX power | 22 dBm |

All settings are stored in NVS and persist across reboots. Use `radio reset` to restore variant defaults.
