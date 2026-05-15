# KISS mode

AXLoRaTNC implements standard KISS framing over USB serial (`0xC0` FEND delimiters).

## Switch to KISS

```text
mode kiss
```

The mode is stored in NVS. To recover the console from KISS mode, type the plain text string `console` followed by Enter.

## Protocol details

- KISS data frames (type 0) are treated as complete AX.25 frames from the host; AXLoRaTNC appends the FCS before LoRa transmit.
- Received AX.25 frames are FCS-checked and emitted as KISS data frames without FCS — exactly as a standard KISS TNC.
- KISS parameter frames (TxDelay=1, Persistence=2, SlotTime=3, FullDuplex=5) are accepted and drive the CSMA algorithm.

## Compatible software

| Application | Notes |
|---|---|
| kissattach (AX.25 for Linux) | Standard usage |
| Dire Wolf | Set `ADEVICE` to the ESP32 serial port |
| LinBPQ / BPQ32 | See [BPQ setup guide](/guide/bpq) |
| F6FBB / LinFBB | Recommended via Linux AX.25; see [FBB KISS guide](/guide/fbb-kiss) |
| APRS clients | Any client supporting KISS TNCs |

## kissattach example (Linux)

```sh
sudo kissattach /dev/ttyUSB0 radio 44.137.x.x
sudo kissparms -c 1 -t 0 -s 10 -r 255 -p radio
```

Replace `/dev/ttyUSB0` with the actual device path. Prefer `/dev/serial/by-id/...` for a stable name across reboots.

For connected-mode BBS traffic over LoRa, use `PACLEN` around 40-64 and
`MAXFRAME`/window 1 on the host. If `stats` shows `qdrops`, the host is sending
faster than the radio queue can drain.

## Baud rate

Always 115 200 baud. The ESP32 USB CDC port auto-negotiates; the baud rate set on the host is ignored.
