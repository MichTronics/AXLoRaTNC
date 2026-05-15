# LinFBB / F6FBB over KISS

This is the recommended FBB setup for AXLoRaTNC.

Use AXLoRaTNC as a normal serial KISS TNC, attach it to the Linux AX.25 stack
with `kissattach`, then let LinFBB use the kernel AX.25 port through interface
`9` in `port.sys`.

WA8DED hostmode remains available for compatibility testing, but KISS is the
preferred route for a stable BBS link over LoRa.

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

## Linux AX.25 port

Create or edit `/etc/ax25/axports`:

```text
# name    callsign    speed  paclen  window  description
axlora    YOURCALL-3  115200 64      1       AXLoRaTNC KISS
```

Attach the TNC:

```sh
sudo kissattach /dev/serial/by-id/YOUR_AXLORATNC axlora
sudo kissparms -p axlora -c 1 -t 0 -s 10 -r 255
```

Use `/dev/serial/by-id/...` instead of `/dev/ttyUSB0` when possible so the port
name survives reboots.

For more robust long-range LoRa settings:

```sh
sudo kissparms -p axlora -c 1 -t 20 -s 10 -r 192
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
1     4    1   axlora   64    1     1     10     00/60   XUWY  LoRa

# TNC Nbs Callsign-SSID Mode
# 1   1   YOURCALL-1    B
```

A ready-to-edit example is included in the repository:

```text
examples/linfbb-axloratnc-kiss-port.sys
```

Important values:

- `Interface 9` means LinFBB uses Linux AX.25 sockets.
- `MultCh axlora` must match the name in `/etc/ax25/axports`.
- `Pacln 64` keeps LoRa airtime short.
- `Maxfr 1` is recommended for LoRa.
- `NbFwd 1` keeps forwarding conservative until the link is proven stable.

## Basic test

After starting LinFBB, verify the Linux AX.25 port exists:

```sh
ip link show axlora
axlisten -a
```

On the AXLoRaTNC console, useful diagnostics are:

```text
console
radio
stats
mheard
mode kiss
```

If `raw_rx` rises on AXLoRaTNC but `axlisten` is quiet, check that the TNC is
still in `mode kiss` and that `kissattach` is attached to the correct serial
device.

If pieces of text are missing, check `stats` on both TNCs. `qdrops` must stay at
zero. A rising `qdrops` counter means the KISS host is feeding frames faster than
the LoRa side can transmit them. Lower `Pacln` to 40-64, keep `Maxfr/window` at
1, and make sure both TNCs were configured with `profile fast` or equivalent
KISS parameters.
