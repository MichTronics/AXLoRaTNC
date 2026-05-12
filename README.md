# AXLoRa

AXLoRa is a compact LoRa packet-radio mesh stack for ESP32-class boards. The first MVP targets an ESP32 DevKit V1 with an EBYTE E22 SX1262 SPI module, while the source tree is arranged around isolated hardware variants for future Heltec, LilyGo, SX1276, OLED, GPS, BLE, and custom-board support.

## Build

```sh
pio run -e devkitv1_e22
pio run -e heltec_v3
pio run -e tbeam
```

The active variant is selected by each PlatformIO environment with a build flag such as:

```ini
[env:devkitv1_e22]
extends = common
build_flags =
    -DAXLORA_VARIANT_DEVKITV1_E22
    -Ivariants/devkitv1_e22
```

Shared PlatformIO settings live in the root `platformio.ini`; each hardware variant keeps its own environment in `variants/<name>/platformio.ini`.

## Initial Wiring: DevKit V1 + EBYTE E22-900 SX1262

For the `devkitv1_e22` variant, the default target is an ESP32 DevKit V1 wired to an EBYTE E22-900M30S or E22-900M33S module.

| E22-900 pin | ESP32 DevKit V1 |
| --- | --- |
| SCK | GPIO18 |
| MISO | GPIO19 |
| MOSI | GPIO23 |
| NSS / CS | GPIO5 |
| DIO1 | GPIO33 |
| BUSY | GPIO32 |
| NRST | GPIO25 |
| RXEN | GPIO14 |
| TXEN | GPIO13 |
| Status LED | GPIO2 |
| Battery ADC | GPIO35 |
| DS18B20 data | GPIO27 |
| AM2302 data | GPIO26 |

Set your node callsign in `variants/devkitv1_e22/variant.h` before flashing multiple nodes.

The default `devkitv1_e22` RF settings are SX1262, 869.480 MHz, SF9, BW125, CR 4/7, private sync word `0x12`, 22 dBm max output power, DIO3 TCXO at 1.8 V, and external RXEN/TXEN RF switch control.

## Serial Console

Open the monitor at `115200` baud.

Commands:

- `help`
- `info`
- `neighbors`
- `send <callsign> <message>`
- `stats`
- `radio`
- `setfreq <mhz>`
- `setpower <dbm>`

Use `CQ` as a broadcast destination.

## Architecture

- `variants/<name>/` defines hardware pins, features, radio type, default frequency, and power limits.
- `src/radio/` owns all RadioLib usage and exposes a stable PHY abstraction.
- `src/protocol/` owns compact binary packets, CRCs, ACK state, duplicate suppression, and fragmentation.
- `src/mesh/` owns flooding relay, TTL enforcement, neighbor observation, and future routing hooks.
- `src/app/` owns the chat MVP and serial console.
- `src/util/` contains fixed-size helpers and timing/logging utilities.

The MVP avoids Arduino `String`, STL containers, heap-heavy structures, and blocking application delays. Packet queues, ACK trackers, dedup tables, reassembly slots, and neighbor entries use fixed storage.
