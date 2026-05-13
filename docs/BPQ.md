# AXLoRaTNC with LinBPQ / BPQ32

This guide documents the working BPQ setup for AXLoRaTNC in KISS mode.

AXLoRaTNC behaves like a serial KISS TNC. BPQ sends complete AX.25 frames to the ESP32 over USB serial. AXLoRaTNC adds the AX.25 FCS, sends one complete AX.25 frame per LoRa packet, receives LoRa packets, checks FCS, and returns KISS frames to BPQ.

## Tested Fast Setup

Use the same RF settings on every AXLoRaTNC node:

```text
Frequency: 869.480 MHz
Bandwidth: 125 kHz
SF:        7
CR:        4/5
KISS baud: 115200
```

For bench/lab testing, the fastest working TNC profile is:

```text
radio reset
profile fast
mode kiss
```

`profile fast` sets:

```text
txdelay=0
persistence=255
slot=1
fulldup=0
duty=off
```

Important: `profile fast` disables the firmware duty-cycle guard. Use it only on a dummy load or shielded lab setup. For normal RF operation, enable a legal duty-cycle setting again.

## Prepare The TNC

Open a PlatformIO monitor or serial terminal at 115200 baud:

```sh
./venv/bin/pio device monitor -b 115200
```

Configure the node:

```text
mode console
callsign YOURCALL-0
radio reset
profile fast
radio
stats
mode kiss
```

Expected `radio` output should show roughly:

```text
freq=869.480 bw=125.0 sf=7 cr=4/5
```

Expected `stats` timing should show:

```text
kiss_txdelay=0 p=255 slot=1 fulldup=0
```

In KISS mode, type plain text `console` followed by Enter to return to the local console.

## Serial Device

Find the ESP32 serial port:

```sh
ls -l /dev/serial/by-id/
```

Prefer `/dev/serial/by-id/...` over `/dev/ttyUSB0`, because it stays stable after reboot. Use that path as `COMPORT` in `bpq32.cfg`.

## Recommended BPQ Port Settings

These settings are the important part:

```text
TYPE=ASYNC
PROTOCOL=KISS
SPEED=115200
FULLDUP=0
TXDELAY=0
PERSIST=255
SLOTTIME=10
TXTAIL=0
MAXFRAME=1
PACLEN=80
FRACK=8000
RESPTIME=1000
RETRIES=10
NOKEEPALIVES=1
```

Notes:

- `TXDELAY=0` avoids the long wait before BPQ transmits.
- `PERSIST=255` allows immediate transmit when the TNC queue is ready.
- `SLOTTIME=10` is fast enough for local LoRa bench testing.
- `FULLDUP=0` keeps the link half-duplex, which is important for LoRa.
- `MAXFRAME=1` is more reliable on LoRa. Try `MAXFRAME=2` only after the link is stable.
- `PACLEN=80` keeps LoRa airtime short. Increase slowly if RSSI/SNR are strong and retries stay low.
- `NOKEEPALIVES=1` reduces unnecessary KISS chatter.

## Complete bpq32.cfg

A complete example is provided in:

```text
examples/bpq32-axloratnc.cfg
```

Copy it to your BPQ config location and edit:

```text
NODECALL
NODEALIAS
LOCATOR
COMPORT
IDMSG / BTEXT / INFOMSG / CTEXT
```

Example install path:

```sh
sudo cp examples/bpq32-axloratnc.cfg /opt/linbpq/bpq32.cfg
```

Restart LinBPQ after editing.

## Basic Test

Start BPQ, then from another AX.25/BPQ node connect to the BPQ node call:

```text
C YOURCALL-7
```

On AXLoRaTNC console, useful diagnostics are:

```text
console
stats
radio
mheard
mode kiss
```

Watch these counters:

```text
txq
queued
deferred
qdrops
raw_tx
raw_rx
duty_drops
```

If `deferred` grows quickly, BPQ or the TNC is waiting because of CSMA or duty-cycle. For the fast lab profile, `duty_drops` should stay at zero.

If `raw_rx` increases but BPQ does not see frames, check:

```text
mode kiss
SPEED=115200
COMPORT=...
```

If `raw_tx` increases but the other node hears nothing, check both nodes have the same:

```text
freq
bw
sf
cr
sync word
```

## More Range / More Robustness

For longer range, use slower LoRa settings on every node:

```text
radio bw 62.5
radio sf 8
radio cr 8
```

Then adjust BPQ to be more patient:

```text
MAXFRAME=1
PACLEN=60
FRACK=12000
RESPTIME=1500
RETRIES=15
```

This costs speed but improves weak-link behavior.

