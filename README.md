# AXLoRaTNC

AXLoRaTNC is a real AX.25 packet-radio TNC for ESP32 + LoRa. AX.25 Level 2 frames stay AX.25 frames; LoRa only replaces the classic AFSK/FSK modem layer.

## Target hardware

| Variant | MCU | Radio | Build env |
|---|---|---|---|
| DevKit V1 + E22 | ESP32 | EBYTE E22 SX1262 | `devkitv1_e22` |
| Heltec V3 | ESP32-S3 | SX1262 | `heltec_v3` |
| T-Beam | ESP32 | SX1276 | `tbeam` |

## Build

```sh
./venv/bin/pio run -e devkitv1_e22
```

## LoRa defaults

The `devkitv1_e22` defaults are stored in `src/variant/`. They can be overridden at runtime (see [Radio config](#radio-config)) and persist across reboots.

| Parameter | Default |
|---|---|
| Frequency | 869.525 MHz |
| Bandwidth | 125 kHz |
| Spreading factor | SF7 |
| Coding rate | 4/5 |
| Sync word | 0x12 |
| TX power | 14 dBm |
| AX.25 FCS | inside LoRa payload |
| RadioLib packet CRC | enabled |

---

## Serial console

Open the serial monitor at **115 200 baud**. All settings are stored in ESP32 NVS and survive reboots.

### Callsign

```text
callsign              → show current callsign
callsign PD4MV-0      → set and persist callsign
```

### Radio config

```text
radio                         → show freq / bw / sf / cr / power + live RSSI/SNR
radio freq 869.525            → set frequency (MHz)
radio bw   125                → set bandwidth (kHz)
radio sf   7                  → set spreading factor (6–12)
radio cr   5                  → set coding rate denominator (5–8, meaning 4/5…4/8)
radio power 14                → set TX power (dBm)
```

Changes are applied immediately and written to NVS.

### KISS parameters

KISS TxDelay, persistence, SlotTime, and FullDuplex are set by the host over the KISS protocol. In half-duplex mode these drive the p-persistent CSMA algorithm inside `transmitRaw()`.

### AX.25 connected mode

```text
connect PD4MV-1           → connect on channel 1
connect 2 PD4MV-2         → connect on channel 2 (1–8)
disconnect                → disconnect channel 1
disconnect 2              → disconnect channel 2
send hello                → send on channel 1
send 2 hello              → send on channel 2
sendui CQ hello world     → send UI frame
ax25                      → show status of all active channels
stats                     → full statistics
```

### Beacon

```text
beacon                          → show config
beacon on / off
beacon now                      → transmit immediately
beacon text <text>              → set info field
beacon dest <CALLSIGN-SSID>     → set destination (default CQ)
beacon interval <seconds>       → 10–86400 s
beacon path <CALL1,CALL2|off>   → set repeater path
```

**APRS position beacon** — auto-formats an uncompressed APRS position info field and sets the destination to `APRS`:

```text
beacon aprs 52.0167 4.7000 /> LoRa TNC
```

Arguments: `lat lon [symbol-table+code] [comment]`. Default symbol is `/>` (car). The beacon must also be enabled with `beacon on` unless it was already on.

### Mheard

```text
mheard          → list stations heard with uptime timestamps, RSSI, SNR
mheard clear    → reset the table
```

The table stores first-heard and last-heard times formatted as `HH:MM:SS` uptime since boot, plus RSSI, SNR, frame count, and whether the station was heard via a digipeater.

### Digipeater

```text
digi on / off
digialias WIDE1-1         → set secondary alias (callsign or alias matched)
digialias off
```

The digipeater relays AX.25 UI frames. It matches the first unrepeated repeater address against the node callsign or configured alias, sets the H-bit, recalculates FCS, and retransmits. A 30-second duplicate cache (keyed on source + destination + control + info CRC) suppresses loops.

### NET/ROM node

```text
node                          → show config
node on / off
node alias AXLORA             → set NET/ROM alias (up to 6 chars)
node ident <text>             → set node identification string
node interval <seconds>       → NODES broadcast interval (60–86400 s)
node broadcast                → send NODES broadcast now
nodes                         → show learned route table
routes                        → same as nodes
```

Routes expire automatically after `interval × 6` seconds (NET/ROM obsolescence rule).

When a station connects over AX.25 connected mode the node shell answers:

```text
?         → command list
INFO      → node identification
NODES     → known NET/ROM routes
ROUTES    → same
MHEARD    → heard station table
BBS       → enter mailbox shell
BYE / B   → disconnect
```

### Mailbox (BBS)

From the connected node shell, type `BBS` to enter the mailbox. Available commands:

```text
L         → list all messages (number, timestamp, from, to, status)
LT        → list messages addressed to you
R <n>     → read message n
S <CALL>  → compose a message to CALL (end with an empty line)
K <n>     → delete message n (only from/to own callsign)
X / EXIT  → back to node shell
B / BYE   → disconnect
```

Messages are stored in NVS with sender, recipient, body, read flag, and uptime timestamp. Up to 12 messages, 200 characters each.

Sysop listing from the local console:

```text
bbs        → list all messages regardless of recipient
```

### Serial mode

```text
mode              → show current mode
mode console      → switch to console (default)
mode kiss         → switch to pure KISS
mode ded          → switch to WA8DED hostmode
```

The mode is stored in NVS. In KISS and WA8DED modes, type `console` (plain text) to recover the console.

---

## KISS

USB serial accepts standard KISS framing (`0xC0` FEND). KISS data frames are treated as complete AX.25 frames from the host; FCS is appended before LoRa transmit. Received AX.25 frames are FCS-checked and emitted as KISS data frames (without FCS), exactly as a normal KISS TNC.

KISS parameter frames (TxDelay, Persistence, SlotTime, FullDuplex) are accepted and drive the CSMA algorithm.

Compatible with: F6FBB, BPQ/LinBPQ, `kissattach`, Dire Wolf, APRS clients.

---

## WA8DED hostmode

Switch with `mode ded`. The binary host frame format is:

```
host → tnc:  {channel} {info/cmd} {count} {data…}
tnc  → host: {channel} {code}  [data…]
```

**8 connected channels** (1–8). Channel 0 = UI/unproto.

| Command | Function |
|---|---|
| `G` / `G0` / `G1` | Poll for events |
| `C <CALL>` | Connect on channel n |
| `D` | Disconnect channel n |
| `L` | Channel link status |
| `M` / `M1` | Enable monitor mode |
| `M0` | Disable monitor mode |
| `JHOST0` | Return to console mode |
| `JHOST1` | Enter hostmode (acknowledged) |

**Monitor mode** (`M` or `M1`): all received and decoded AX.25 frames are emitted as DED event code 5 in the format:

```
FM SRC TO DST [VIA R1,R2] <type> RSSI=x SNR=y[:info]
```

Event codes returned by the TNC:

| Code | Meaning |
|---|---|
| 0 | Acknowledgement / no event |
| 1 | Informational text |
| 2 | Error text |
| 3 | Link status change |
| 5 | Monitor frame |
| 6 | Connected data received |
| 7 | UI data received |

Compatible with: F6FBB (host type `D`), Graphic Packet, PaxTerm.

---

## APRS

APRS frames are AX.25 UI frames with PID `0xF0`. AXLoRaTNC decodes incoming APRS frames automatically and logs the parsed content (position, message, status, weather) via the console log.

To send APRS position beacons, use the `beacon aprs` shortcut which sets the destination to `APRS` and formats the info field:

```text
beacon aprs 52.0167 4.7000 /> LoRa TNC on 869.525 MHz
beacon interval 600
beacon on
```

For a custom APRS info field (compressed position, weather, etc.) set `beacon dest APRS` and `beacon text <full-info-field>` manually.

---

## Architecture

```
Console / KISS / WA8DED serial
        │
      TNC (tnc.cpp)
        │
  ┌─────┴─────┐
AX.25 L2     Beacon / Digi / Mheard / NET/ROM / BBS / APRS
  │
AX.25 frame encoder/decoder + FCS
  │
Radio driver (radio_sx1262.cpp / radio_sx1276.cpp)
  │
RadioLib → SX1262 / SX1276
```

RadioLib is isolated under `src/radio/`. The AX.25 stack under `src/ax25/` has no dependency on RadioLib. The APRS helper under `src/aprs/` is a pure C++ module with no hardware dependency.

---

## Implemented

**AX.25 framing**
- Callsign + SSID address encoding / decoding
- Destination, source, repeater address fields
- C/R (command/response) bit per AX.25 2.2 spec
- UI, I, S (RR, RNR, REJ), U (SABM, UA, DISC, DM) frames
- AX.25 CRC-16 FCS; RadioLib packet CRC additionally enabled
- Modulo-8 sequence numbers, window size 4

**Connected-mode (Level 2)**
- Full state machine: Disconnected → Connecting → Connected → Disconnecting / Recovery
- T1 retry timer, T2 deferred-ack timer, T3 keepalive timer
- N2 retry limit with automatic Recovery state on T1 timeout
- Sliding window with selective retransmit after REJ
- RNR / peer-busy flow control
- TX queue per channel (depth 6); data sent from queue after ack

**TNC**
- 8 independent connected channels
- CSMA p-persistent listen-before-talk (TxDelay, Persistence, SlotTime, FullDuplex from KISS)
- Interrupt-driven RX on SX1262 DIO1 pin
- Duty-cycle enforcement (configurable ppm limit)
- UI-frame digipeater with H-bit update and 30-second duplicate cache
- UI beacon with configurable destination, path, interval, and text
- APRS position beacon shortcut (`beacon aprs lat lon sym comment`)
- APRS frame detection and decode on receive
- Mheard table (20 entries) with uptime timestamps, RSSI, SNR, via flag
- NET/ROM NODES broadcast, route table (20 entries), route expiry
- Connected node shell: INFO, NODES, ROUTES, MHEARD, BBS, BYE
- NVS mailbox BBS: L, LT, R, S, K, X, B commands; timestamps
- Callsign, radio config, serial mode, beacon, digi, node, BBS all persistent in NVS

**Serial interfaces**
- Console (human-readable, all commands)
- KISS (compatible with all standard KISS software)
- WA8DED hostmode: 8 channels, G/C/D/L/M polling, event codes 1-7, monitor mode
