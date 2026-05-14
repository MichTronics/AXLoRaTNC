# BPQ / LinBPQ setup

AXLoRaTNC behaves like a serial KISS TNC. BPQ sends complete AX.25 frames to the ESP32 over USB serial. AXLoRaTNC adds the AX.25 FCS, sends one complete AX.25 frame per LoRa packet, receives LoRa packets, checks FCS, and returns KISS frames to BPQ.

## Tested fast setup

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

`profile fast` sets txdelay=0, persistence=255, slot=1, fulldup=0, duty=off.

::: warning
`profile fast` disables the firmware duty-cycle guard. Use it only on a dummy load or shielded lab setup. For normal RF operation, re-enable a legal duty-cycle setting.
:::

## Prepare the TNC

Open a serial monitor at 115 200 baud:

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

Expected `radio` output:

```text
freq=869.480 bw=125.0 sf=7 cr=4/5
```

Expected `stats` timing:

```text
kiss_txdelay=0 p=255 slot=1 fulldup=0
```

## Serial device

```sh
ls -l /dev/serial/by-id/
```

Prefer `/dev/serial/by-id/...` over `/dev/ttyUSB0` for a stable path after reboot. Use that path as `COMPORT` in `bpq32.cfg`.

## Recommended BPQ port settings

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

- `TXDELAY=0` — avoids a long wait before BPQ transmits.
- `PERSIST=255` — allows immediate transmit when the TNC queue is ready.
- `FULLDUP=0` — keeps the link half-duplex, important for LoRa.
- `MAXFRAME=1` — more reliable on LoRa; try `MAXFRAME=2` only after the link is stable.
- `PACLEN=80` — keeps LoRa airtime short; increase slowly if RSSI/SNR are strong.
- `NOKEEPALIVES=1` — reduces unnecessary KISS chatter.

## Complete bpq32.cfg

A complete example is in `examples/bpq32-axloratnc.cfg`. Copy it and edit:

```text
NODECALL   NODEALIAS   LOCATOR   COMPORT
IDMSG / BTEXT / INFOMSG / CTEXT
```

```sh
sudo cp examples/bpq32-axloratnc.cfg /opt/linbpq/bpq32.cfg
```

Restart LinBPQ after editing.

## Basic test

From another AX.25 / BPQ node:

```text
C YOURCALL-7
```

Useful diagnostics on the AXLoRaTNC console:

```text
console
stats
radio
mheard
mode kiss
```

Watch these counters: `txq` `queued` `deferred` `qdrops` `raw_tx` `raw_rx` `duty_drops`.

If `deferred` grows quickly, CSMA or duty-cycle is holding back frames. For the fast lab profile, `duty_drops` should stay at zero.

## Extending range / robustness

For longer range, use slower LoRa settings on every node:

```text
radio bw 62.5
radio sf 8
radio cr 8
```

Adjust BPQ to be more patient:

```text
MAXFRAME=1
PACLEN=60
FRACK=12000
RESPTIME=1500
RETRIES=15
```
