#!/usr/bin/env python3
"""CDC echo probe for the R4 device examples.

Usage: cdc_probe.py /dev/ttyACM0 [mode]

mode: echo  -- write a probe string, require it back verbatim (default)
      bench -- stream bytes for 5 s and report what came back

All reads are non-blocking with an explicit deadline. A plain blocking
os.read() on a tty never returns when the peer is silent, which is what made
the first version of this harness hang instead of failing.
"""
import os
import select
import sys
import termios
import time
import tty


def open_port(node):
    fd = os.open(node, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[6][termios.VMIN] = 0       # non-blocking reads
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    os.set_blocking(fd, False)
    return fd


def drain(fd, settle=0.4, budget=1.0):
    """Discard anything the device sent on open (boot banners etc.).

    Hard time budget: a streaming device (the throughput-meter sketch floods)
    leaves select() permanently ready, so a drain that only stops when the
    port goes quiet never returns.
    """
    time.sleep(settle)
    end = time.time() + budget
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            return
        try:
            if not os.read(fd, 65536):
                return
        except (BlockingIOError, OSError):
            return


def read_until(fd, want, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            continue
        try:
            chunk = os.read(fd, 65536)
        except (BlockingIOError, OSError):
            continue
        if chunk:
            buf += chunk
            if want in buf:
                return buf, True
    return buf, False


def mode_echo(fd):
    msg = b"r4-device-echo"
    os.write(fd, msg)
    buf, ok = read_until(fd, msg, 4.0)
    print(f"[dev] cdc echo: got {len(buf)} B, match={ok}")
    return 0 if ok else 1


def mode_bench(fd):
    sent = got = 0
    end = time.time() + 5.0
    i = 0
    while time.time() < end:
        chunk = bytes((j & 0xFF) for j in range(i, i + 256))
        i += 256
        try:
            sent += os.write(fd, chunk)
        except (BlockingIOError, OSError):
            pass
        r, _, _ = select.select([fd], [], [], 0.01)
        if r:
            try:
                data = os.read(fd, 65536)
                if data:
                    got += len(data)
            except (BlockingIOError, OSError):
                pass
    print(f"[dev] cdc bench: sent={sent} got={got}")
    return 0 if got > 0 else 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    node = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else "echo"
    fd = open_port(node)
    try:
        drain(fd)
        return mode_echo(fd) if mode == "echo" else mode_bench(fd)
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
