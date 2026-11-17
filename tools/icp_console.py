#!/usr/bin/env python3
"""Drive the ICP probe (icp/) from the shell. stdlib only.

    tools/icp_console.py unlock factory
    tools/icp_console.py chipid
    tools/icp_console.py dump > stock-dump.txt
    grep '^:' stock-dump.txt > stock-full.hex                # flashable as it is
    tools/icp_console.py verify firmware/build/glasses.hex
    tools/icp_console.py flash firmware/build/glasses.hex   # ICP_WRITE build
    tools/icp_console.py --port /dev/ttyACM1 khz 50

A command is sent as typed and its reply printed, up to the line starting "ok"
or "error" (icp/main.c). `verify` and `flash` take an Intel HEX file: the
script loads it into the probe first (`new`, then one record per line), then
runs the command. Exit status is 0 for ok, 1 for error or timeout.
"""

import argparse
import os
import select
import sys
import termios
import time
import tty


class Probe:
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.buf = b""

    def close(self):
        os.close(self.fd)

    def send(self, line, timeout, quiet=False):
        """Send one line; return True for ok. Prints the reply unless quiet,
        and always prints it on error."""
        os.write(self.fd, (line + "\r").encode())
        reply = []
        echo_seen = False
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.fd], [], [], 0.2)
            if not ready:
                continue
            self.buf += os.read(self.fd, 4096)
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                text = raw.decode(errors="replace").rstrip("\r")
                if not echo_seen and text.endswith(line):
                    echo_seen = True  # the probe echoes what it was sent
                    continue
                reply.append(text)
                if not quiet:
                    print(text, flush=True)
                if text.startswith("ok"):
                    return True
                if text.startswith("error"):
                    if quiet:
                        print("\n".join(reply), flush=True)
                    return False
        print(f"error: no reply to {line!r} within {timeout:g} s", file=sys.stderr)
        return False

    def load(self, path):
        try:
            with open(path) as f:
                records = [l.strip() for l in f if l.startswith(":")]
        except OSError as e:
            print(f"error: cannot read {path}: {e.strerror}", file=sys.stderr)
            return False
        if not records:
            print(f"error: no Intel HEX records in {path}", file=sys.stderr)
            return False
        if not self.send("new", 5, quiet=True):
            return False
        for i, rec in enumerate(records):
            # The last record is EOF; let its "image: N bytes" line through.
            if not self.send(rec, 5, quiet=i < len(records) - 1):
                print(f"error: record {i + 1} of {path} refused", file=sys.stderr)
                return False
        return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--timeout", type=float, default=120.0,
                    help="seconds per command; a 100 kHz flash takes ~10")
    ap.add_argument("command", nargs="+")
    args = ap.parse_args()

    try:
        probe = Probe(args.port)
    except (OSError, termios.error) as e:
        why = e.strerror if isinstance(e, OSError) else e.args[-1]
        print(f"error: cannot open {args.port}: {why}", file=sys.stderr)
        return 1
    try:
        cmd = args.command
        if cmd[0] in ("verify", "flash") and len(cmd) == 2:
            if not probe.load(cmd[1]):
                return 1
            cmd = cmd[:1]
        return 0 if probe.send(" ".join(cmd), args.timeout) else 1
    finally:
        probe.close()


if __name__ == "__main__":
    sys.exit(main())
