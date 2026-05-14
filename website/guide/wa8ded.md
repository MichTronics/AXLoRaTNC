# WA8DED hostmode

Switch with `mode ded`. In WA8DED mode, type the plain text string `console` followed by Enter to return to the interactive console.

## Frame format

```
host → tnc:  {channel} {info/cmd} {count} {data…}
tnc  → host: {channel} {code}  [data…]
```

## Channels

8 connected channels (1–8). Channel 0 = UI / unproto.

## Commands

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

## Monitor mode

`M` or `M1` enables monitor mode. All received AX.25 frames are emitted as event code 5:

```
FM SRC TO DST [VIA R1,R2] <type> RSSI=x SNR=y[:info]
```

## Event codes

| Code | Meaning |
|---|---|
| 0 | Acknowledgement / no event |
| 1 | Informational text |
| 2 | Error text |
| 3 | Link status change |
| 5 | Monitor frame |
| 6 | Connected data received |
| 7 | UI data received |

## Compatible software

- F6FBB (host type `D`)
- Graphic Packet
- PaxTerm
