# Local Packet Radio Source References

This repository keeps local reference source trees under `source/`. Use these
when checking host protocol behavior, AX.25 edge cases, and compatibility with
real packet-radio applications.

## F6FBB / LinFBB 7.0.11

- Path: `source/fbb-7.0.11`
- Version/source marker: FBB 7.0.11 tree
- Main files:
  - `source/fbb-7.0.11/src/drv_ded.c` - WA8DED hostmode driver, polling, `@B`,
    `L`, `G`, code 3/6/7 event handling.
  - `source/fbb-7.0.11/src/kernel.c` - processing of driver events such as
    `NBBUF`, `STATS`, `DATA`, and link-status messages.
  - `source/fbb-7.0.11/src/tncio.c` - higher-level send buffering toward TNC
    drivers.
- Notes for AXLoRaTNC:
  - LinFBB polls DED channels with `@B` every roughly 50 cycles and otherwise
    rotates `L` across channels.
  - LinFBB treats the `@B` reply as a number of transmit buffers. `kernel.c`
    stores it as `mem = atoi(reply) << 5`, then decrements memory once per DATA
    frame sent to the driver.
  - Channel bytes must match exactly. A DED response for the wrong channel can
    trigger resync behavior.

## LinBPQ

- Path: `source/linbpq`
- Git revision: `225fbb1`
- Main files:
  - `source/linbpq/L2Code.c` - AX.25 Level 2 state machine behavior.
  - `source/linbpq/FBBRoutines.c` - FBB forwarding/protocol compatibility.
  - `source/linbpq/SerialPort.c` and related port code - serial/KISS/host port
    handling.
- Notes for AXLoRaTNC:
  - Use as a reference for KISS and DED host interoperability with BPQ/LinBPQ.
  - Good source for AX.25 retry, acknowledgement, and busy-state behavior used
    by real node/BBS deployments.

## JNOS2

- Path: `source/jnos2`
- Git revision: `5555864`
- Main files:
  - `source/jnos2/ax25.c`, `source/jnos2/ax25subr.c`, `source/jnos2/ax25user.c`
    - AX.25 connection handling and user/session behavior.
  - `source/jnos2/kisspoll.c`, `source/jnos2/kissdump.c` - KISS interaction and
    diagnostics.
  - `source/jnos2/forward.c`, `source/jnos2/mailbox.h` - BBS forwarding and
    mailbox behavior.
- Notes for AXLoRaTNC:
  - Use as an independent reference for AX.25 connected-mode behavior and JNOS
    host expectations.
  - Useful when checking how mailbox/forwarding traffic behaves over constrained
    links.

## Dire Wolf

- Path: `source/direwolf`
- Git revision: `a231971`
- Main files:
  - `source/direwolf/src/kiss.c` - KISS framing and host behavior.
  - `source/direwolf/src/ax25_link.c` - AX.25 connected-mode logic.
  - `source/direwolf/src/ax25_pad.c` - AX.25 frame encode/decode details.
- Notes for AXLoRaTNC:
  - Use as a reference for standard KISS behavior, frame formatting, and monitor
    expectations.
  - Useful for checking interoperability with common APRS and packet clients.

## Working Rule

When changing KISS, WA8DED, or AX.25 Level 2 behavior, first check the relevant
reference files above and update this document if a new compatibility rule is
discovered.
