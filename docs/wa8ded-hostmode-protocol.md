# WA8DED Host Mode Protocol — Reference Notes

Source references analysed: `source/fbb-7.0.11/src/drv_ded.c`, `kernel.c`, `tncio.c`, `drv_aea.c`; `source/linbpq/L2Code.c`, `FBBRoutines.c`.

---

## Frame format (binary, host mode active after `JHOST1`)

Every frame exchanged between host and TNC has the same structure:

```
[channel] [code] [length-1] [data ...]
```

- **channel**: 1-based channel number (1 = first AX.25 channel).  
  Channel 0 is used for unproto (UI) frames and global commands.
- **code**: see table below.
- **length-1**: `(len - 1)` byte. For codes 0–5, data is a null-terminated string.  
  For codes 6–7, data is counted binary.
- **data**: payload.

---

## Event codes (TNC → host, response to G/L poll)

| Code | Direction | Meaning |
|------|-----------|---------|
| 0    | TNC→host  | No event pending (NAK/empty reply) |
| 1    | TNC→host  | Echo / parameter value (response to B, L, @B, etc.) |
| 2    | TNC→host  | Error text |
| 3    | TNC→host  | Link-status string (see below) |
| 4    | TNC→host  | Monitor header without data (UI frame header only) |
| 5    | TNC→host  | Monitor header with data following as code 6 |
| 6    | TNC→host  | Monitor data payload (follows code 5) or UI-frame data |
| 7    | TNC→host  | Connected I-frame data (received from remote station) |

---

## Host commands (host → TNC)

| Command | Channel | Effect |
|---------|---------|--------|
| `G`     | n       | Poll for any pending event on channel n |
| `G0`    | n       | Poll for data events (code 7) only |
| `G1`    | n       | Poll for status events (code 3) only |
| `L`     | n       | Query link statistics for channel n |
| `@B`    | n       | Query free TX buffer bytes |
| `C CALL`| n       | Connect to CALL on channel n |
| `D`     | n       | Disconnect channel n |
| `I CALL`| 0       | Set local callsign |
| `Y n`   | 0       | Set maximum number of channels |
| `O n`   | 0       | Set MAXFRAME (window size) |
| `JHOST1`| 0       | Enter host mode |
| `JHOST0`| 0       | Leave host mode |
| `M IUS` | 0       | Set monitor filter (I=I-frames, U=UI-frames, S=supervisory) |
| `N n`   | 0       | Set retry limit (N2) |
| `F n`   | 0       | Set T1/FRACK in units of 10 ms |
| `T2 n`  | 0       | Set T2 in units of 10 ms |
| `T3 n`  | 0       | Set T3 in units of 10 ms |

Data frames (I-frame TX): host sends `[channel][0x00][length-1][data...]`.

---

## LinFBB polling sequence (`drv_ded.c: ded_sonde`)

LinFBB cycles through channels in a fixed pattern:

1. Every ~50 cycles: sends `@B` on `cur_can`, sets `cptr = 50`.
2. Otherwise: advances `cur_can` to next channel (wraps at `tt_can = nb_voies`),
   sends `L` on the new channel.
3. If `wait[can] > 0` after an L response: immediately sends `G` on the same channel.
4. After a G that returns code 0 (no event): sets `wait[can] = 0`.

The `wait[can]` counter tracks how many events are still expected:
- Set by `L` response: `wait = nbmes + nbtra` (first two fields of L status line).
- Decremented by 1 on each received event (codes 3–7).
- Reset to 0 if G returns code 0.

**Channel matching is strict**: LinFBB sends the poll on channel N and expects the
response to carry the same channel byte. A mismatch triggers a resync/error.

---

## L command response format

The TNC must return code 1 with a space-separated ASCII string:

```
nbmes nbtra nbatt nback nbret con [peer]
```

| Field  | Our mapping | LinFBB uses it for |
|--------|-------------|-------------------|
| nbmes  | pending events in ring buffer | `wait[can] = nbmes + nbtra` |
| nbtra  | 0 (always)  | `wait[can] += nbtra` |
| nbatt  | outstanding I-frames (unACKed) | `sta.ack` |
| nback  | TX queue depth | `sta.ack` |
| nbret  | retry count | `sta.ret` |
| con    | 1=connected, 0=not | `sta.connect` |
| peer   | peer callsign (optional extra) | ignored |

The string must end with `\n\0` (null-terminated, LF before null).

---

## Code-3 link-status strings and LinFBB reactions

Parsed by `kernel.c: message()`. The channel digit in position 2 is overwritten
with `'*'` before parsing, so the format `(n) VERB rest` must be exact.

| String sent by TNC | LinFBB reaction |
|--------------------|-----------------|
| `(n) CONNECTED to CALL` | `con_voie()` — opens a new user session |
| `(n) LINK RESET to station CALL` | Ignored (no session change) |
| `(n) LINK FAILURE with CALL` | `dec_voie()` — closes the session |
| `(n) DISCONNECTED fm CALL` | `dec_voie()` — closes the session |
| `(n) DISCONNECTING fm CALL` | `dec_voie()` — closes session **prematurely** |

**DO NOT send "DISCONNECTING fm" or "LINK FAILURE" prematurely.**  
LinFBB closes the session immediately on receipt — if the link can still recover,
the session will be gone on LinFBB's side while the TNC is still connected on the
radio side.

---

## Correct event mapping for AX.25 state transitions

| AX.25 transition | WA8DED event to send | Reason |
|------------------|----------------------|--------|
| Disconnected → Connecting (outgoing SABM) | `LINK RESET to station CALL` | LinFBB ignores it, session stays open |
| Connecting → Connected (UA received) | `CONNECTED to CALL` | Opens session |
| Disconnected → Connected (incoming SABM, UA sent) | `CONNECTED to CALL` | Opens session |
| Connected → Recovery (T1 timeout, retrying) | **nothing** | Recovery is transient; sending LINK FAILURE closes session too early |
| Recovery → Connected (ACK received, link healed) | **nothing** | Session was never closed, just continue |
| Recovery → Disconnected (N2 retries exhausted) | `LINK FAILURE with CALL` | Session must be closed |
| Connected/Connecting/Disconnecting → Disconnected (DISC/UA clean) | `DISCONNECTED fm CALL` | Session must be closed |
| Disconnecting state entry | **nothing** | Sending "DISCONNECTING" causes premature session close |

---

## T1 / FRACK timing for LoRa

The firmware default is `dedFrack_ = 800` → `T1 = 8000 ms`.

For LoRa at moderate data rates (SF10, BW125), an AX.25 packet can take
500–1500 ms on air. Round-trip time (TX + remote processing + RX of ACK)
easily exceeds 2500 ms, causing spurious T1 timeouts and Recovery loops.

### T1 restart on actual TX

The firmware restarts T1 in `serviceRawTx()` **after** the radio `send()` returns,
not when the frame is first queued. This corrects for the CSMA waiting time and
the LoRa on-air duration (both can be 1–3 s). Without this fix, T1 would expire
before the remote station had even received the frame.

### Default LoRa profile (firmware defaults)

| Parameter | Default | Notes |
|-----------|---------|-------|
| FRACK (T1) | 800 → 8000 ms | Via `F 800` |
| Window (MAXFRAME) | 1 | Via `O 1` — never pipeline on LoRa |
| Retries (N2) | 10 | Via `N 10` |
| PACLEN | 64 | Per I-frame payload |
| TxDelay | 100 → 1000 ms | RX→TX guard time |
| Persistence | 255 | Transmit immediately |
| SlotTime | 20 → 200 ms | CSMA slot |

---

## Monitor output (codes 4/5/6)

Monitor frames use channel **0** regardless of which physical channel the frame
arrived on. The header is sent as code 5, followed immediately by the payload
as code 6 (if the frame has an information field). If there is no payload,
only code 4 (header) is sent.

LinFBB's monitor filter is set by the `M` command:
- `M IU` — monitor I-frames and UI-frames (default our firmware uses)
- `M IUS` — add supervisory frames (RR, RNR, REJ)
- `M N` or `M 0` — monitor off

Flood guard: we limit monitor events to 8 per second and reserve half the
event queue (16 of 32 slots) for link-status and data events.

---

## LoRa airtime budget and timing analysis

### Airtime per frame (SF7, BW=125 kHz, CR=4/5, preamble=8 symbols)

| Payload (bytes) | Frame type          | Airtime (ms) |
|-----------------|---------------------|--------------|
| 15              | RR / UA / SABM      | ~46 ms       |
| 32              | small I-frame       | ~70 ms       |
| 64              | PACLEN=64 I-frame   | ~118 ms      |
| 128             | large I-frame       | ~210 ms      |

For SF10/BW125 multiply by ~8×; for SF12/BW125 multiply by ~27×.

### RTT budget for SABM→UA (both nodes SF7/BW125, TxDelay=1000 ms)

```
Node A:  SABM TX        ~46 ms  (t=0 .. 46)
Node A:  call startReceive()    (t=46)
Node B:  receives SABM          (t=46)
Node B:  waits TxDelay       1000 ms
Node B:  UA TX          ~46 ms  (t=1046 .. 1092)
Node A:  receives UA            (t=1092)
Total RTT:                     ~1092 ms  <<  T1=8000 ms  ✓
```

### RTT budget for I-frame→RR ACK (SF7/BW125, TxDelay=1000 ms, T2=300 ms)

```
Node A:  I-frame TX    ~118 ms  (t=0 .. 118)
Node B:  receives I-frame       (t=118)
Node B:  T2 deferred ACK    300 ms
Node B:  RR TX          ~46 ms  (t=418 .. 464)
Node A:  receives RR            (t=464)
Total RTT:                      ~464 ms  <<  T1=8000 ms  ✓
```

### RF overload warning — 1-meter bench testing

**Problem:** At 1 meter, with default TX power = +22 dBm at 869 MHz:

```
Free-space path loss (1 m, 869 MHz) = 20·log10(4π·1·869e6/3e8) ≈ 31 dB
Received power = 22 dBm − 31 dB = −9 dBm
SX1262 LNA linear range: typically < −10 dBm
```

At −9 dBm the SX1262 LNA is at or beyond its linear boundary.  
Symptoms: intermittent CRC failures → `rxFail` counter increments → remote never gets UA → remote retries SABM after T1 → sequence numbers reset → `RR0+` appears after `RR2-` in monitor → apparent "multiple connects".

**Fix:** Reduce TX power before close-range bench testing:

```
radio power 5      (from console mode, or WA8DED terminal mode)
```

+5 dBm → received power ≈ −26 dBm → well within LNA linear range.
Check `@D` command response for `rxFail` count to confirm overload.

### Diagnosing instability — @D command

In WA8DED host mode, send `@D` on any channel. The TNC responds with a code-1 string showing:

```
ch1 CON PD4MV ret=0
radio rxOk=45 rxFail=8 txOk=47 txFail=0 rssi=-67 snr=9
```

A high `rxFail` count (especially at close range) indicates RF overload.  
A non-zero `ret=` count indicates T1 retries are happening.

---

## KISS monitor output (TX echo)

In WA8DED and Console modes the firmware echoes **received** radio frames as
KISS data frames on the serial port, so a monitoring tool (e.g. GraphicPacket)
connected to the same or a second port can observe traffic.

Since firmware version after 2026-05, **transmitted** frames are also echoed
as KISS frames immediately after successful radio TX (`serviceRawTx`).
This means both sides of every QSO appear in the KISS stream:

```
Radio RX  →  emitKissData() called in serviceRadio()
Radio TX  →  emitKissData() called in serviceRawTx() after send() succeeds
```

In pure KISS mode the TX echo is suppressed (the KISS host already knows
what it sent).
