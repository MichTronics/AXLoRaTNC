#!/usr/bin/env python3
"""KISS proxy sniffer for AXLoRaTNC.

Sits between the physical serial port (AXLoRaTNC) and kissattach.
Decodes KISS frames and AX.25 headers in real-time. All data passes
through unchanged — read-only, no interference.

Usage:
    python3 scripts/kiss-sniffer.py [/dev/ttyACM0] [115200]

Then run kissattach on the PTY that is printed on startup:
    sudo kissattach <PTY> axlora

Requirements:
    pip install pyserial
"""

import os
import pty
import select
import sys
import termios
import tty
from datetime import datetime

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed — run: pip install pyserial")

FEND = 0xC0
FESC = 0xDB
TFEND = 0xDC
TFESC = 0xDD

KISS_CMD = {
    0: "DATA", 1: "TXDELAY", 2: "PERSIST", 3: "SLOTTIME",
    4: "TXTAIL", 5: "FULLDUPLEX", 6: "SETHARDWARE", 15: "RETURN",
}

U_FRAMES = {
    0x2F: "SABM", 0x6F: "SABME", 0x43: "DISC", 0x0F: "DM",
    0x63: "UA",   0x87: "FRMR", 0x03: "UI",   0xAF: "XID", 0xE3: "TEST",
}

S_FRAMES = {0: "RR", 1: "RNR", 2: "REJ", 3: "SREJ"}


def _addr(data, pos):
    if pos + 7 > len(data):
        return None, pos, True, False
    call = "".join(chr((data[pos + i] >> 1) & 0x7F) for i in range(6)).rstrip()
    ssid_byte = data[pos + 6]
    ssid = (ssid_byte >> 1) & 0x0F
    last = bool(ssid_byte & 0x01)
    repeated = bool(ssid_byte & 0x80)
    label = f"{call}-{ssid}" if ssid else call
    return label, pos + 7, last, repeated


def _unstuff(raw):
    out = bytearray()
    i = 0
    while i < len(raw):
        if raw[i] == FESC and i + 1 < len(raw):
            i += 1
            out.append(FEND if raw[i] == TFEND else FESC if raw[i] == TFESC else raw[i])
        else:
            out.append(raw[i])
        i += 1
    return bytes(out)


CTRL_ESCAPES = {
    0x00: "<NUL>", 0x01: "<SOH>", 0x02: "<STX>", 0x03: "<ETX>",
    0x04: "<EOT>", 0x05: "<ENQ>", 0x06: "<ACK>", 0x07: "<BEL>",
    0x08: "<BS>",  0x09: "<HT>",  0x0A: "<LF>",  0x0B: "<VT>",
    0x0C: "<FF>",  0x0D: "<CR>",  0x0E: "<SO>",  0x0F: "<SI>",
    0x1A: "<SUB>", 0x1B: "<ESC>", 0x7F: "<DEL>",
}


def _show_payload(payload, verbose=False):
    """Format payload bytes as readable text with visible control chars."""
    if not payload:
        return "0b"
    if verbose:
        hex_rows = []
        for i in range(0, len(payload), 16):
            chunk = payload[i:i + 16]
            hex_part = " ".join(f"{b:02X}" for b in chunk)
            txt_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
            hex_rows.append(f"  {i:04X}  {hex_part:<47}  {txt_part}")
        return f"{len(payload)}b\n" + "\n".join(hex_rows)
    out = []
    for b in payload:
        if b in CTRL_ESCAPES:
            out.append(CTRL_ESCAPES[b])
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append(f"<{b:02X}>")
    text = "".join(out)
    if len(text) > 120:
        text = text[:120] + "…"
    return f"{len(payload)}b \"{text}\""


def decode_ax25(data, verbose=False):
    if len(data) < 15:
        return None
    try:
        dst, pos, _, _ = _addr(data, 0)
        src, pos, last, _ = _addr(data, pos)
        if dst is None or src is None:
            return None

        digis = []
        while not last and pos + 7 <= len(data):
            digi, pos, last, rep = _addr(data, pos)
            if digi is None:
                break
            digis.append(("*" if rep else "") + digi)

        if pos >= len(data):
            return f"{src} > {dst}"

        ctrl = data[pos]
        pos += 1
        pf = bool(ctrl & 0x10)

        if ctrl & 0x01 == 0:
            ns = (ctrl >> 1) & 0x07
            nr = (ctrl >> 5) & 0x07
            pid = data[pos] if pos < len(data) else 0
            payload = data[pos + 1:] if pos + 1 < len(data) else b""
            pf_str = ",P" if pf else ""
            if pid == 0xF0:
                info = f"  {_show_payload(payload, verbose)}"
            elif payload:
                info = f"  PID=0x{pid:02X} {_show_payload(payload, verbose)}"
            else:
                info = ""
            frame_type = f"I({ns}/{nr}{pf_str}){info}"
        elif ctrl & 0x03 == 0x01:
            nr = (ctrl >> 5) & 0x07
            stype = S_FRAMES.get((ctrl >> 2) & 0x03, "S")
            pf_str = ",P" if pf else ""
            frame_type = f"{stype}({nr}{pf_str})"
        else:
            frame_type = U_FRAMES.get(ctrl & 0xEF, f"U(0x{ctrl:02X})")

        via = " via " + ",".join(digis) if digis else ""
        return f"{src} > {dst}{via}  [{frame_type}]"
    except Exception:
        return None


class KISSParser:
    def __init__(self, label, verbose=False):
        self.label = label
        self.verbose = verbose
        self._buf = bytearray()
        self._active = False

    def feed(self, chunk):
        for b in chunk:
            if b == FEND:
                if self._active and self._buf:
                    self._emit(bytes(self._buf))
                self._buf.clear()
                self._active = True
            elif self._active:
                self._buf.append(b)

    def _emit(self, raw):
        if not raw:
            return
        cmd_byte = raw[0]
        kiss_type = cmd_byte & 0x0F
        port = (cmd_byte >> 4) & 0x0F
        payload = _unstuff(raw[1:])
        type_name = KISS_CMD.get(kiss_type, f"CMD({kiss_type})")
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        port_warn = " !" if port != 0 else ""

        if kiss_type == 0:
            ax25 = decode_ax25(payload, self.verbose)
            detail = ax25 if ax25 else f"DECODE-FAIL {len(payload)}b  raw={payload[:64].hex()}"
            print(f"[{ts}]  {self.label:>10}  port={port}{port_warn}  {type_name}  {detail}", flush=True)
        else:
            val = payload.hex() if payload else "-"
            print(f"[{ts}]  {self.label:>10}  port={port}{port_warn}  {type_name}  {val}", flush=True)


def main():
    import argparse
    ap = argparse.ArgumentParser(description="AXLoRaTNC KISS proxy sniffer")
    ap.add_argument("dev",  nargs="?", default="/dev/ttyACM0", help="Serial device")
    ap.add_argument("baud", nargs="?", type=int, default=115200, help="Baud rate")
    ap.add_argument("-v", "--verbose", action="store_true", help="Hex dump I-frame payloads")
    args = ap.parse_args()

    dev = args.dev
    baud = args.baud

    master_fd, slave_fd = pty.openpty()
    # Raw mode: no line-discipline processing of binary KISS bytes.
    # Without this, 0x0D (CR) and other bytes can be mangled before kissattach
    # sets its own raw mode, causing frame loss.
    tty.setraw(slave_fd)
    pty_name = os.ttyname(slave_fd)

    print("=" * 60)
    print("  AXLoRaTNC KISS sniffer")
    print(f"  TNC serial : {dev}  @ {baud}")
    print(f"  PTY        : {pty_name}")
    print(f"  Verbose    : {'yes (hex dump)' if args.verbose else 'no (-v for hex)'}")
    print()
    print(f"  Run kissattach on the PTY above, e.g.:")
    print(f"    sudo kissattach {pty_name} axlora")
    print("  Note: port=N! means LinFBB used a non-zero KISS port (channel mismatch risk)")
    print("=" * 60)
    print()

    try:
        ser = serial.Serial(dev, baud, timeout=0)
    except serial.SerialException as exc:
        sys.exit(f"Cannot open {dev}: {exc}")

    tnc_rx = KISSParser("TNC→HOST", args.verbose)
    host_tx = KISSParser("HOST→TNC", args.verbose)

    try:
        while True:
            r, _, _ = select.select([ser.fileno(), master_fd], [], [], 0.1)
            for fd in r:
                if fd == ser.fileno():
                    data = ser.read(4096)
                    if data:
                        tnc_rx.feed(data)
                        try:
                            os.write(master_fd, data)
                        except OSError:
                            pass
                elif fd == master_fd:
                    try:
                        data = os.read(master_fd, 4096)
                    except OSError:
                        continue
                    if data:
                        host_tx.feed(data)
                        ser.write(data)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        ser.close()
        os.close(master_fd)
        try:
            os.close(slave_fd)
        except OSError:
            pass


if __name__ == "__main__":
    main()
