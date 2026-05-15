#!/usr/bin/env python3
"""WA8DED hostmode proxy sniffer for AXLoRaTNC / LinFBB.

Sits between LinFBB and the TNC serial port, decoding WA8DED host mode
frames in both directions. All data passes through unchanged — read-only.

Frame format (host mode, after JHOST1):
  HOST → TNC : [channel][0x00][len-1][data...]  — data to transmit
               [channel][0x01][len-1][cmd...]   — command string (G, L, C, ...)
  TNC  → HOST: [channel][0x00]                  — ACK (2 bytes only)
               [channel][1-5][text...][0x00]    — null-terminated event text
               [channel][6-7][len-1][data...]   — binary counted data

Usage:
    python3 scripts/ded-sniffer.py [/dev/ttyACM0] [9600]
    python3 scripts/ded-sniffer.py /dev/ttyACM0 9600 -p   # show G/L polls
    python3 scripts/ded-sniffer.py /dev/ttyACM0 9600 -v   # hex dump payloads

Then point LinFBB at the PTY device shown on startup (update port.sys or
symlink the PTY to the device LinFBB expects, e.g. /dev/ttyS0).

Requirements:
    pip install pyserial
"""

import os
import pty
import select
import sys
import tty
from datetime import datetime

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed — run: pip install pyserial")

# TNC→Host event codes
TNC_CODES = {
    0: "ACK",
    1: "PARAM",
    2: "ERROR",
    3: "LINK-STATUS",
    4: "MON-HDR",
    5: "MON-HDR+D",
    6: "MON-DATA",
    7: "I-DATA",
}

# Host→TNC command strings that are routine polls (hidden unless -p)
_POLL_PREFIXES = ("G", "L", "@B")

# Terminal-mode JHOST1 sequence LinFBB sends before entering binary hostmode
_JHOST1_TERM = b'\x1bJHOST1\r'

CTRL_MAP = {
    0x00: "<NUL>", 0x01: "<SOH>", 0x02: "<STX>", 0x03: "<ETX>",
    0x04: "<EOT>", 0x06: "<ACK>", 0x07: "<BEL>", 0x08: "<BS>",
    0x09: "<HT>",  0x0A: "<LF>",  0x0B: "<VT>",  0x0C: "<FF>",
    0x0D: "<CR>",  0x0E: "<SO>",  0x0F: "<SI>",  0x1B: "<ESC>",
    0x7F: "<DEL>",
}


def _fmt(data, verbose=False):
    if not data:
        return "0b"
    if verbose:
        rows = []
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            h = " ".join(f"{b:02X}" for b in chunk)
            t = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
            rows.append(f"  {i:04X}  {h:<47}  {t}")
        return f"{len(data)}b\n" + "\n".join(rows)
    out = []
    for b in data:
        if b in CTRL_MAP:
            out.append(CTRL_MAP[b])
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append(f"<{b:02X}>")
    s = "".join(out)
    if len(s) > 120:
        s = s[:120] + "…"
    return f'{len(data)}b "{s}"'


def _fmt_terminal(data):
    """Format pre-hostmode terminal bytes with visible control characters."""
    out = []
    for b in data:
        if   b == 0x1B: out.append('<ESC>')
        elif b == 0x18: out.append('<CAN>')
        elif b == 0x0D: out.append('<CR>')
        elif b == 0x0A: out.append('<LF>')
        elif 0x20 <= b < 0x7F: out.append(chr(b))
        else: out.append(f'<{b:02X}>')
    return '"' + ''.join(out) + '"'


class DedParser:
    """WA8DED frame state machine.

    HOST→TNC framing (infoCmd in header[1]):
        infoCmd=0 → data frame  (AX.25 I-frame payload)
        infoCmd=1 → command     (G, L, C callsign, D, JHOST, …)

    TNC→HOST framing depends on event code:
        code=0          → ACK, no payload (2-byte frame total)
        code=1..5       → null-terminated ASCII text, NO length byte
        code=6..7       → [len-1] + binary data

    FBB→TNC starts in terminal mode (ASCII) and switches to binary WA8DED
    framing after detecting the JHOST1 escape sequence from LinFBB.
    """

    _S_CH   = 0
    _S_CODE = 1
    _S_LEN  = 2
    _S_TEXT = 3
    _S_BIN  = 4

    def __init__(self, label, verbose=False, show_polls=False):
        self.label      = label
        self.verbose    = verbose
        self.show_polls = show_polls
        self._from_tnc  = label.startswith("TNC")
        # FBB→TNC begins in terminal mode; switch to binary after JHOST1
        self._in_terminal = not self._from_tnc
        self._term_buf    = bytearray()
        self._reset()

    def _reset(self):
        self._state = self._S_CH
        self._ch    = 0
        self._code  = 0
        self._need  = 0
        self._buf   = bytearray()

    def feed(self, data):
        for b in data:
            if self._in_terminal:
                self._step_terminal(b)
            else:
                self._step(b)

    def _step_terminal(self, b):
        """Buffer pre-hostmode bytes; switch to binary framing after JHOST1."""
        self._term_buf.append(b)
        n = len(_JHOST1_TERM)
        if len(self._term_buf) >= n and bytes(self._term_buf[-n:]) == _JHOST1_TERM:
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            pre = bytes(self._term_buf[:-n])
            if pre:
                print(f"[{ts}]  {self.label:>7}  ch=-  TERMINAL  {_fmt_terminal(pre)}", flush=True)
            print(f"[{ts}]  {self.label:>7}  ch=0  JHOST  \"JHOST1\"", flush=True)
            self._in_terminal = False
            self._term_buf.clear()
            self._reset()

    def _step(self, b):
        s = self._state

        if s == self._S_CH:
            self._ch  = b
            self._buf = bytearray()
            self._state = self._S_CODE

        elif s == self._S_CODE:
            self._code = b
            if self._from_tnc:
                if b == 0:
                    self._emit()
                    self._state = self._S_CH
                elif 1 <= b <= 5:
                    self._state = self._S_TEXT
                elif b in (6, 7):
                    self._state = self._S_LEN
                else:
                    self._emit()          # unknown short
                    self._state = self._S_CH
            else:
                # Host→TNC always has 3-byte header
                self._state = self._S_LEN

        elif s == self._S_LEN:
            self._need  = b + 1
            self._state = self._S_BIN

        elif s == self._S_TEXT:
            if b == 0x00:
                self._emit()
                self._state = self._S_CH
            else:
                self._buf.append(b)

        elif s == self._S_BIN:
            self._buf.append(b)
            if len(self._buf) >= self._need:
                self._emit()
                self._state = self._S_CH

    def _emit(self):
        ts   = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        ch   = self._ch
        code = self._code
        data = bytes(self._buf)

        if self._from_tnc:
            self._print_tnc(ts, ch, code, data)
        else:
            self._print_host(ts, ch, code, data)

    # ------------------------------------------------------------------ #
    # TNC → HOST                                                           #
    # ------------------------------------------------------------------ #

    def _print_tnc(self, ts, ch, code, data):
        name = TNC_CODES.get(code, f"CODE{code}")

        if code == 0:
            if self.show_polls:
                print(f"[{ts}]  {self.label:>7}  ch={ch}  {name}", flush=True)
            return

        if code == 3:
            text = data.decode("ascii", errors="replace")
            print(f"[{ts}]  {self.label:>7}  ch={ch}  {name}  \"{text}\"", flush=True)
            return

        if code == 7:
            print(f"[{ts}]  {self.label:>7}  ch={ch}  {name}  {_fmt(data, self.verbose)}", flush=True)
            return

        if code in (1, 2):
            text = data.decode("ascii", errors="replace")
            # Suppress pure-numeric L-response strings unless verbose/polls shown
            if not self.show_polls and text.replace(" ", "").replace("\n", "").isdigit():
                return
            print(f"[{ts}]  {self.label:>7}  ch={ch}  {name}  \"{text}\"", flush=True)
            return

        if code in (4, 5, 6):
            print(f"[{ts}]  {self.label:>7}  ch={ch}  MON    {_fmt(data, self.verbose)}", flush=True)
            return

        print(f"[{ts}]  {self.label:>7}  ch={ch}  {name}  {_fmt(data, self.verbose)}", flush=True)

    # ------------------------------------------------------------------ #
    # HOST → TNC                                                           #
    # ------------------------------------------------------------------ #

    def _print_host(self, ts, ch, code, data):
        if code == 0:
            # Data to transmit on channel ch
            print(f"[{ts}]  {self.label:>7}  ch={ch}  TX-DATA  {_fmt(data, self.verbose)}", flush=True)
            return

        if code == 1:
            # Command string
            cmd_str = data.decode("ascii", errors="replace").rstrip("\x00\n\r")
            cmd_upper = cmd_str.upper()

            is_poll = any(cmd_upper.startswith(p) for p in _POLL_PREFIXES)
            if is_poll and not self.show_polls:
                return

            label = _cmd_label(cmd_upper)
            print(f"[{ts}]  {self.label:>7}  ch={ch}  {label}  \"{cmd_str}\"", flush=True)
            return

        print(f"[{ts}]  {self.label:>7}  ch={ch}  code={code}  {_fmt(data, self.verbose)}", flush=True)


def _cmd_label(cmd):
    if cmd.startswith("G"):   return "POLL"
    if cmd.startswith("L"):   return "STATS"
    if cmd.startswith("@B"):  return "FREEBUF"
    if cmd.startswith("@D"):  return "DIAG"
    if cmd.startswith("C "):  return "CONNECT"
    if cmd == "D":            return "DISC"
    if cmd.startswith("JHOST"): return "JHOST"
    if cmd.startswith("M "):  return "MONITOR"
    if cmd.startswith("I "):  return "CALLSIGN"
    if cmd.startswith("F "):  return "FRACK"
    if cmd.startswith("O "):  return "MAXFRAME"
    if cmd.startswith("N "):  return "RETRY"
    if cmd.startswith("Y "):  return "CHANNELS"
    return "CMD"


class _Tee:
    """Write to multiple streams at once (stdout + log file)."""
    def __init__(self, *streams):
        self._streams = streams
    def write(self, data):
        for s in self._streams:
            s.write(data)
    def flush(self):
        for s in self._streams:
            s.flush()


def main():
    import argparse
    ap = argparse.ArgumentParser(description="AXLoRaTNC WA8DED hostmode proxy sniffer")
    ap.add_argument("dev",  nargs="?", default="/dev/ttyACM0", help="TNC serial device")
    ap.add_argument("baud", nargs="?", type=int, default=9600,  help="Baud rate")
    ap.add_argument("-v", "--verbose",    action="store_true", help="Hex dump payloads")
    ap.add_argument("-p", "--show-polls", action="store_true", help="Show G/L/@B poll frames")
    ap.add_argument("-o", "--output",     metavar="FILE",      help="Also write log to FILE")
    args = ap.parse_args()

    log_fh = None
    if args.output:
        log_fh = open(args.output, "w", buffering=1, encoding="utf-8")
        sys.stdout = _Tee(sys.__stdout__, log_fh)

    master_fd, slave_fd = pty.openpty()
    tty.setraw(slave_fd)
    pty_name = os.ttyname(slave_fd)

    print("=" * 60)
    print("  AXLoRaTNC WA8DED hostmode sniffer")
    print(f"  TNC serial : {args.dev}  @ {args.baud}")
    print(f"  PTY        : {pty_name}")
    print(f"  Polls      : {'visible' if args.show_polls else 'hidden  (add -p to show)'}")
    print(f"  Hex dump   : {'yes' if args.verbose else 'no  (add -v to enable)'}")
    if log_fh:
        print(f"  Log file   : {args.output}")
    print()
    print(f"  Point LinFBB at: {pty_name}")
    print("=" * 60)
    print()

    try:
        ser = serial.Serial(args.dev, args.baud, timeout=0)
    except serial.SerialException as exc:
        sys.exit(f"Cannot open {args.dev}: {exc}")

    tnc_to_fbb = DedParser("TNC→FBB", args.verbose, args.show_polls)
    fbb_to_tnc = DedParser("FBB→TNC", args.verbose, args.show_polls)

    try:
        while True:
            r, _, _ = select.select([ser.fileno(), master_fd], [], [], 0.1)
            for fd in r:
                if fd == ser.fileno():
                    data = ser.read(4096)
                    if data:
                        tnc_to_fbb.feed(data)
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
                        fbb_to_tnc.feed(data)
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
        if log_fh:
            log_fh.close()


if __name__ == "__main__":
    main()
