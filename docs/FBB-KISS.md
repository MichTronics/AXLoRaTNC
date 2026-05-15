# LinFBB / F6FBB with AXLoRaTNC in KISS mode

This is the recommended FBB setup for AXLoRaTNC.

Use AXLoRaTNC as a normal serial KISS TNC, attach it to the Linux AX.25 stack
with `kissattach`, then let LinFBB use the kernel AX.25 port through interface
`9` in `port.sys`.

WA8DED hostmode remains available for compatibility testing, but KISS is the
preferred route for a stable BBS link over LoRa.

## Michel test assumptions

For Michel's current LinFBB/LoRa lab tests, optimize first for complete and
faster BBS text transfer. Airtime and duty-cycle economy are not the primary
constraints during these tests.

Use these assumptions when tuning AXLoRaTNC for LinFBB unless Michel explicitly
asks for a legal/airtime-safe profile:

- Packet loss during tuning is acceptable, similar to classic packet radio.
- The priority is complete LinFBB text, fewer `TNC BUSY - LINE IGNORED`
  responses, and higher throughput.
- Test profiles may use more aggressive values such as larger `I`/PACLEN,
  `P=255`, short slot/txdelay values, and duty-cycle guard off.
- Do not start future LinFBB/LoRa tuning by reducing airtime unless that is the
  specific goal.

## TNC setup

Configure every AXLoRaTNC node with the same radio settings:

```text
mode console
callsign YOURCALL-0
radio reset
profile fast
mode kiss
```

For real RF operation, replace `profile fast` with legal duty-cycle settings.
`profile fast` disables the duty guard and is intended for bench testing or a
dummy load.

Useful checks before switching to KISS:

```text
radio
stats
```

Expected fast-profile KISS timing:

```text
kiss_txdelay=0 p=255 slot=1 fulldup=0
```

In KISS mode, type plain text `console` followed by Enter to return to the TNC
console.

## Linux AX.25 port

Create or edit `/etc/ax25/axports`:

```text
# name    callsign    speed  paclen  window  description
axlora    YOURCALL-3  115200 128     3       AXLoRaTNC KISS
```

Notes:

- Michel's current lab profile uses `paclen=128` and `window=3` for faster
  LinFBB text transfer.
- If retries or missing text rise, first try `window=2`, then `paclen=96`.
- The `speed` field is used by Linux AX.25 tools; AXLoRaTNC itself uses USB
  serial at 115200.

Attach the TNC:

```sh
sudo kissattach /dev/ttyACM0 axlora
sudo kissparms -p axlora -c 1 -f n -t 0 -s 10 -r 255
```

Use `/dev/serial/by-id/...` instead of `/dev/ttyACM0` if you want the port name
to survive reboots.

For more robust long-range LoRa settings, use a slower host profile:

```sh
sudo kissparms -p axlora -c 1 -f n -t 20 -s 10 -r 192
```

## LinFBB port.sys

In LinFBB, use Linux interface `9` and point `MultCh` at the AX.25 port name
from `/etc/ax25/axports`.

Minimal example:

```text
# Ports TNCs
1      1

# Com Interface Address Baud
1      9         0      115200

# TNC NbCh Com MultCh Pacln Maxfr NbFwd MxBloc M/P-Fwd Mode  Freq
1     4    1   axlora   128   3     1     10     00/60   XUWY  LoRa

# TNC Nbs Callsign-SSID Mode
# 1   1   YOURCALL-1    B
```

A ready-to-edit example is also available at:

```text
examples/linfbb-axloratnc-kiss-port.sys
```

Important values:

- `Interface 9` means LinFBB uses Linux AX.25 sockets.
- `MultCh axlora` must match the name in `/etc/ax25/axports`.
- `Pacln 128` should match or stay close to the AX.25 port paclen.
- `Maxfr 3` matches the current fast lab profile.
- `NbFwd 1` keeps forwarding conservative until the link is proven stable.

## Basic test

After starting LinFBB, verify the Linux AX.25 port exists:

```sh
ip link show axlora
```

From another station, connect to the FBB callsign/SSID configured for the port.
On the AXLoRaTNC console, useful diagnostics are:

```text
console
radio
stats
mheard
mode kiss
```

Watch these counters:

```text
raw_rx
raw_tx
deferred
queued
qdrops
duty_drops
```

If Linux receives no frames, check:

```sh
axlisten -a
```

If `raw_rx` rises on AXLoRaTNC but `axlisten` is quiet, check that the TNC is
still in `mode kiss` and that `kissattach` is attached to the correct serial
device.

If LinFBB sends text too slowly, first confirm `Maxfr 3`, `Pacln 128`, and the
`kissparms` values above. Then check RF retries with AXLoRaTNC `stats` and the
remote station's monitor.

If pieces of text are missing, check `stats` on both TNCs. `qdrops` must stay at
zero. A rising `qdrops` counter means the KISS host is feeding frames faster than
the LoRa side can transmit them. First lower `Maxfr/window` to 2, then lower
`Pacln` to 96 or 64, and make sure both TNCs were configured with `profile fast`
or equivalent KISS parameters.

## Reference sources

This setup follows the FBB Linux KISS model documented in the local reference
tree:

- `source/fbb-7.0.11/doc/html/tllinux.htm`
- `source/fbb-7.0.11/doc/html/fmtport.htm`
- `source/fbb-7.0.11/etc/port.sys.sample`

See also [source-reference.md](source-reference.md).
