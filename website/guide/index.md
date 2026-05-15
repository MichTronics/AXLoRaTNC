# Getting started

## Supported hardware

| Board | MCU | Radio | Build env | Status |
|---|---|---|---|---|
| ESP32 DevKit V1 + EBYTE E22 | ESP32 | SX1262 | `devkitv1_e22` | ✅ tested |
| Heltec WiFi LoRa 32 V3 | ESP32-S3 | SX1262 | `heltec_v3` | ⚠ not tested |
| TTGO T-Beam | ESP32 | SX1276 | `tbeam` | ⚠ not tested |
| LilyGo T3 LoRa32 V1.6.1 | ESP32 | SX1276 | `lilygo_t3_v161` | ✅ tested |

---

## Option 1 — Flash via browser (easiest)

No software installation needed. Works in **Chrome** and **Edge** (Web Serial API required).

👉 **[Open the web installer](/AXLoRaTNC/flash/)**

1. Select your board
2. Click **Install AXLoRaTNC**
3. Pick the COM port of your ESP32
4. Wait for the flash to complete (~30 s)

---

## Option 2 — Build from source

### Prerequisites

- Python 3.8+
- Git

### Setup

```sh
git clone https://github.com/MichTronics/AXLoRaTNC.git
cd AXLoRaTNC
python3 -m venv venv
./venv/bin/pip install platformio
```

### Build & flash

Replace `devkitv1_e22` with your board's build env from the table above.

```sh
# Build only
./venv/bin/pio run -e devkitv1_e22

# Build and flash over USB
./venv/bin/pio run -e devkitv1_e22 --target upload

# Open serial monitor (115 200 baud)
./venv/bin/pio device monitor -b 115200
```

::: tip Windows
Use `venv\Scripts\pio` instead of `./venv/bin/pio`.
:::

---

## First boot

After flashing, open a serial monitor at **115 200 baud** and configure the TNC:

```text
callsign N0CALL-0     ← set your callsign (required)
radio                 ← verify LoRa settings
```

Switch to KISS mode when connecting a host application:

```text
mode kiss
```

Type `console` (plain text, followed by Enter) at any time to return to the interactive console.

---

## Default LoRa settings (`devkitv1_e22`)

| Parameter | Default |
|---|---|
| Frequency | 869.480 MHz |
| Bandwidth | 125 kHz |
| Spreading factor | SF7 |
| Coding rate | 4/5 |
| Sync word | 0x12 |
| TX power | 22 dBm |

All settings persist across reboots (ESP32 NVS). Use `radio reset` to restore variant defaults.

---

## Compatible host software

AXLoRaTNC works with any standard KISS or WA8DED host application:

| Software | Mode | Notes |
|---|---|---|
| kissattach (Linux) | KISS | Standard AX.25 stack |
| Dire Wolf | KISS | Set device to the ESP32 serial port |
| LinBPQ / BPQ32 | KISS | See [BPQ setup guide](/guide/bpq) |
| F6FBB / LinFBB | KISS | Recommended: Linux AX.25 + [FBB KISS guide](/guide/fbb-kiss) |
| TFPCX + TSTHOST | WA8DED | See [WA8DED guide](/guide/wa8ded) |
| WinPack | WA8DED | TNC type WA8DED |
| JNOS | WA8DED | Interface type `asy`, tnc `ded` |
| Graphic Packet | WA8DED | |
| PaxTerm | WA8DED | |
| APRS clients | KISS | Any client supporting KISS TNCs |

---

## What's next

- [Radio config](/guide/radio-config) — change frequency, bandwidth, SF, TX power
- [KISS mode](/guide/kiss) — kissattach, Dire Wolf, port settings
- [LinFBB / F6FBB KISS](/guide/fbb-kiss) — recommended BBS setup
- [APRS](/guide/aprs) — position beacons, digipeater, mheard table
- [WA8DED hostmode](/guide/wa8ded) — legacy hostmode compatibility
- [NET/ROM & BBS](/guide/netrom-bbs) — node shell, mailbox
- [BPQ / LinBPQ setup](/guide/bpq) — complete working config
