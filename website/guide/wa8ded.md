# WA8DED hostmode

AXLoRaTNC implements the WA8DED binary host mode protocol, compatible with F6FBB, TFPCX/TSTHOST, WinPack, BPQ32/LinBPQ (DED port type), Graphic Packet, PaxTerm, and JNOS.

## Enable hostmode

From the console:

```text
mode ded
```

The mode is stored in NVS and persists across reboots. To return to the interactive console from any mode, send the plain text string `console` followed by Enter (CR).

## Frame format

```
host → tnc:  [channel] [cmd] [count] [data…]
tnc  → host: [channel] [code] [data…]
```

- **channel**: 1–8 for connected channels, 0 for UI/unproto
- **cmd**: 0 = info/data frame; 1 = command frame
- **count**: number of data bytes minus 1 (data frames); for command frames this is the command length
- **code**: 0 = ack/no event; 1 = info text; 2 = error; 3 = link status; 4 = monitor header; 5 = monitor frame; 6 = connected data; 7 = UI data

## Channels

| Range | Purpose |
|---|---|
| 0 | UI / unproto (broadcast) |
| 1–8 | Connected AX.25 channels |

## Commands (hostmode)

### Connection control

| Command | Function |
|---|---|
| `G` | Poll for any pending event |
| `G0` | Poll for link-status events only (code 3) |
| `G1` | Poll for data and link-status events (codes 3, 6, 7) |
| `C <CALL>` | Connect to CALL on the selected channel |
| `D` | Disconnect the selected channel |
| `I` | Query own callsign |
| `I <CALL>` | Set own callsign |
| `L` | Query link status |
| `JHOST0` | Leave hostmode, return to terminal mode |
| `JHOST1` | Enter binary hostmode (TNC responds with ack) |
| `V` | Query firmware version string |
| `S` | Query selected channel |
| `S <n>` | Select channel n (0–8) |

### Parameters

All parameters can be queried (send without argument → TNC replies with current value as info text) or set (send with numeric argument → TNC replies with ack `0`). All settings persist in NVS.

| Cmd | Parameter | Default | Range |
|---|---|---|---|
| `A` | Auto-LF (append LF after CR) | 1 | 0–1 |
| `B` | DAMA timeout (× 100 ms) | 120 | 0–255 |
| `E` | Echo (terminal mode) | 1 | 0–1 |
| `F` | FRACK — T1 retry timer (× 100 ms) | 250 | 0–255 |
| `K` | Timestamp in monitor output | 0 | 0–1 |
| `N` | Retry limit (N2) | 10 | 0–127 |
| `O` | Max outstanding frames (window, MAXFRAME) | 2 | 1–7 |
| `P` | CSMA persistence (0–255, p = (n+1)/256) | 63 | 0–255 |
| `R` | Digipeater on/off | 0 | 0–1 |
| `T` | TX delay (× 10 ms) | 30 | 0–127 |
| `W` | Slot time (× 10 ms) | 10 | 0–127 |
| `X` | TX enable | 1 | 0–1 |
| `Y` | Max incoming connections | 4 | 0–8 |
| `Z` | Flow control: bit 0 = RTS/CTS, bit 1 = XON/XOFF | 3 | 0–3 |
| `M` | Monitor mode string | `IU` | see below |
| `U` | Unattended mode + connect text | 0 | 0–2 |

### Monitor mode (`M`)

`M` queries the current monitor mode string. `M <string>` sets it. Use `M N` to disable monitoring.

Monitor mode letters select which frame types are reported:

| Letter | Frames reported |
|---|---|
| `I` | I-frames (connected data) |
| `U` | Unnumbered frames (UI, SABM, UA, DISC, DM, FRMR) |
| `S` | Supervisory frames (RR, RNR, REJ, SREJ) |
| `N` | None (monitor disabled) |

Example: `M IU` (default) reports UI traffic and connection control frames but not RR/RNR supervisory frames.

### Buffer query (`@B`)

Query the number of free TX bytes available on the selected channel:

```
host → tnc:  [ch] [1] [2] @B\0
tnc  → host: [ch] [1] <free-bytes as text>
```

### Reset to defaults (`@Q` / `QRES`)

Resets all DED parameters to factory defaults and saves them to NVS.

### PACLEN (`IPOLL`)

Sets the maximum I-frame info field size in bytes (default 60). Large frames are automatically fragmented into PACLEN-sized chunks before being queued to the AX.25 layer.

```
host → tnc:  [ch] [1] [n] IPOLL <value>\0
```

## Terminal mode (before JHOST1)

In terminal mode (after `mode ded` but before `JHOST1`) the TNC accepts plain text commands, one per line. All single-letter parameters above are accepted, plus:

| Command | Function |
|---|---|
| `MYCALL [CALL]` | Query or set own callsign |
| `UNPROTO [DEST]` | Query or set unproto destination |
| `BTEXT [text]` | Query or set beacon text |
| `CONNECT <CALL>` | Connect on current channel |
| `DISCONNECT` | Disconnect current channel |
| `JHOST1` | Switch to binary hostmode |
| `QRES` | Reset all DED parameters to defaults |

## Event codes (TNC → host)

| Code | Meaning | Data |
|---|---|---|
| 0 | Acknowledgement / no event | none |
| 1 | Informational text | null-terminated string |
| 2 | Error text | null-terminated string |
| 3 | Link status change | null-terminated string (see below) |
| 4 | Monitor frame header (terminal mode) | null-terminated string |
| 5 | Monitor frame header (hostmode) | null-terminated string |
| 6 | Connected data received | length-prefixed bytes |
| 7 | UI data received | length-prefixed bytes |

### Link status strings (code 3)

| String | Meaning |
|---|---|
| `CONNECTED to <CALL>` | Incoming or outgoing connection established |
| `DISCONNECTED fm <CALL>` | Link cleanly disconnected |
| `DISCONNECTING fm <CALL>` | Disconnect in progress |
| `LINK FAILURE with <CALL>` | T1 retry limit exceeded (Recovery state) |
| `LINK RESET to station <CALL>` | SABM received while already connected |

In hostmode the channel number is in the frame header byte — the status string contains only the text above. In terminal mode the string is prefixed with `(n)` so the operator knows which channel changed.

### Monitor frame format (code 5)

```
FM <SRC> TO <DST> [VIA <R1>,<R2>] <TYPE> RSSI=<x> SNR=<y>[:<info>]
```

`<TYPE>` is one of: `UI`, `I`, `RR`, `RNR`, `REJ`, `SREJ`, `SABM`, `UA`, `DISC`, `DM`, `FRMR`.

## Flow control

**XON/XOFF** (enabled by default, `Z` bit 1): `0x13` (XOFF) pauses TNC output; `0x11` (XON) resumes. In binary hostmode the control bytes are detected at frame boundaries so they cannot collide with channel bytes.

**Hardware RTS/CTS** (enabled when variant defines `PIN_UART_RTS`/`PIN_UART_CTS`): the UART hardware flow control lines are asserted automatically.

## Compatible software

| Software | Setup notes |
|---|---|
| **F6FBB** | Host type `D`; set port to the ESP32 serial port |
| **TFPCX + TSTHOST** | Load TFPCX first (`tfpcx -pCOM1 -b9600`), then launch TSTHOST |
| **WinPack** | Select TNC type WA8DED, set COM port and baud rate |
| **BPQ32 / LinBPQ** | Use port type `DED`; set `PORTCOM` to the ESP32 serial device |
| **JNOS** | Interface type `asy` with `tnc` set to `ded` |
| **Graphic Packet** | Direct WA8DED port |
| **PaxTerm** | Direct WA8DED port |

## AX.25 link layer settings

The following AX.25 parameters are shared between KISS and WA8DED modes:

| Parameter | Default | DED command |
|---|---|---|
| T1 retry timer | 25 s (F × 100 ms) | `F` |
| T2 deferred-ack | 1.5 s | `@T2` |
| T3 keepalive | 180 s | `@T3` |
| N2 retry limit | 10 | `N` |
| Window size (MAXFRAME) | 2 | `O` |
| PACLEN (info field) | 60 bytes | `IPOLL` |
