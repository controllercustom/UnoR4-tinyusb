#!/usr/bin/env python3
"""Input-class probe for the R4 HID device examples.

Usage: input_probe.py <usb-sysfs-dev> <mode>

usb-sysfs-dev  name under /sys/bus/usb/devices (e.g. 1-3.2)
mode:  rel   -- require EV_REL movement events
       key   -- require EV_KEY press and release events
       both  -- require key events and rel events (any node each)

Reads evdev directly rather than parsing evtest output. No EVGrab, so the
device stays usable by the rest of the system.
"""
import os
import sys
import time

from evdev import InputDevice, ecodes

SAMPLE_SECONDS = 6.0


def input_nodes_for(usb_dev):
    """Event nodes whose sysfs path is below the given usb device.

    os.walk, not glob('**'): sysfs is full of symlinks to parent directories,
    and a recursive glob follows them and never terminates. os.walk does not
    follow symlinks by default, so it stays a tree walk.
    """
    root = f"/sys/bus/usb/devices/{usb_dev}"
    nodes = []
    for dirpath, dirnames, _filenames in os.walk(root):
        # The event node is a DIRECTORY: .../input/inputNN/eventM
        name = os.path.basename(dirpath)
        if not name.startswith("event"):
            continue
        if os.path.basename(os.path.dirname(os.path.dirname(dirpath))) != "input":
            continue
        nodes.append("/dev/input/" + name)
    return sorted(nodes)


def collect(nodes, seconds):
    """Sample every node concurrently.

    Returns {path: {"rel": n, "key_press": n, "key_release": n, "total": n}}.
    """
    handles = []
    for path in nodes:
        try:
            handles.append((path, InputDevice(path)))
        except OSError as e:
            print(f"[dev] input: cannot open {path} ({e})")

    stats = {
        p: {"rel": 0, "key_press": 0, "key_release": 0, "total": 0}
        for p, _ in handles
    }
    end = time.time() + seconds
    while time.time() < end:
        for path, dev in handles:
            try:
                events = list(dev.read())
            except OSError:
                continue
            for ev in events:
                s = stats[path]
                s["total"] += 1
                if ev.type == ecodes.EV_REL:
                    s["rel"] += 1
                elif ev.type == ecodes.EV_KEY:
                    if ev.value == 1:
                        s["key_press"] += 1
                    elif ev.value == 0:
                        s["key_release"] += 1
        time.sleep(0.02)

    for _, dev in handles:
        try:
            dev.close()
        except Exception:
            pass
    return stats


def describe(stats):
    parts = []
    for path, s in sorted(stats.items()):
        if s["total"]:
            parts.append(
                f"{os.path.basename(path)}[rel={s['rel']} "
                f"down={s['key_press']} up={s['key_release']}]"
            )
    return " ".join(parts) or "(no events)"


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    usb_dev, mode = sys.argv[1], sys.argv[2]

    nodes = input_nodes_for(usb_dev)
    if not nodes:
        print(f"[dev] input: no event nodes under {usb_dev}")
        return 1

    stats = collect(nodes, SAMPLE_SECONDS)
    print(f"[dev] input: {describe(stats)}")

    has_rel = any(s["rel"] > 4 for s in stats.values())
    has_key = any(
        s["key_press"] > 0 and s["key_release"] > 0 for s in stats.values()
    )

    ok = {"rel": has_rel, "key": has_key, "both": has_rel and has_key}.get(mode)
    if ok is None:
        print(f"[dev] input: unknown mode {mode}")
        return 2

    print(f"[dev] input: mode={mode} rel={has_rel} key={has_key} -> "
          f"{'OK' if ok else 'NO'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
