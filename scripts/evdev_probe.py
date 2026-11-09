#!/usr/bin/env python3
"""Probe /dev/input for multitouch touchpads and dump a few absolute frames.

Run this in a normal desktop terminal (not a sandbox) while touching the pad:

  python3 scripts/evdev_probe.py

If it prints "permission denied", add yourself to the input group and re-login:

  sudo usermod -aG input "$USER"
"""

from __future__ import annotations

import array
import fcntl
import os
import struct
import sys
import time

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT = 0
ABS_MT_SLOT = 0x2F
ABS_MT_POSITION_X = 0x35
ABS_MT_POSITION_Y = 0x36
ABS_MT_TRACKING_ID = 0x39
ABS_MT_PRESSURE = 0x3A
BTN_TOUCH = 0x14A
BTN_TOOL_FINGER = 0x145
INPUT_PROP_POINTER = 0
INPUT_PROP_DIRECT = 1

# ioctl helpers (from linux/input.h / asm-generic/ioctl.h)
_IOC_READ = 2
def _IOC(dir_, type_, nr, size):
    return (dir_ << 30) | (ord(type_) << 8) | nr | (size << 16)

def EVIOCGNAME(length):
    return _IOC(_IOC_READ, "E", 0x06, length)

def EVIOCGBIT(ev, length):
    return _IOC(_IOC_READ, "E", 0x20 + ev, length)

def EVIOCGPROP(length):
    return _IOC(_IOC_READ, "E", 0x09, length)

def EVIOCGABS(code):
    return _IOC(_IOC_READ, "E", 0x40 + code, 24)

def test_bit(bit, buf: bytes) -> bool:
    return bool(buf[bit // 8] & (1 << (bit % 8)))


def list_pads():
    base = "/dev/input"
    if not os.path.isdir(base):
        print(f"{base} missing")
        return []
    found = []
    for name in sorted(os.listdir(base)):
        if not name.startswith("event"):
            continue
        path = os.path.join(base, name)
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        except PermissionError:
            print(f"permission denied: {path}")
            continue
        except OSError as e:
            print(f"skip {path}: {e}")
            continue
        try:
            namebuf = array.array("B", [0] * 256)
            fcntl.ioctl(fd, EVIOCGNAME(len(namebuf)), namebuf, True)
            devname = bytes(namebuf).split(b"\0", 1)[0].decode("utf-8", "replace")
            absbits = bytes(256)
            absbuf = array.array("B", [0] * 256)
            fcntl.ioctl(fd, EVIOCGBIT(EV_ABS, len(absbuf)), absbuf, True)
            propbuf = array.array("B", [0] * 8)
            try:
                fcntl.ioctl(fd, EVIOCGPROP(len(propbuf)), propbuf, True)
            except OSError:
                pass
            keybuf = array.array("B", [0] * 256)
            fcntl.ioctl(fd, EVIOCGBIT(EV_KEY, len(keybuf)), keybuf, True)
            has_mt = test_bit(ABS_MT_POSITION_X, absbuf) and test_bit(ABS_MT_POSITION_Y, absbuf)
            has_slot = test_bit(ABS_MT_SLOT, absbuf)
            looks = (
                test_bit(INPUT_PROP_POINTER, propbuf)
                or test_bit(BTN_TOOL_FINGER, keybuf)
                or test_bit(BTN_TOUCH, keybuf)
            )
            direct = test_bit(INPUT_PROP_DIRECT, propbuf)
            ok = has_mt and has_slot and looks and not direct
            print(
                f"{path}: {devname!r} mt={has_mt} slot={has_slot} padlike={looks} "
                f"direct={direct} -> {'CANDIDATE' if ok else 'skip'}"
            )
            if ok:
                absinfo = array.array("i", [0] * 6)  # value,min,max,fuzz,flat,resolution
                # struct input_absinfo is 6 ints on most arches
                raw = array.array("B", [0] * 24)
                fcntl.ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), raw, True)
                xmin, xmax = struct.unpack("ii", bytes(raw[4:12]))
                raw = array.array("B", [0] * 24)
                fcntl.ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), raw, True)
                ymin, ymax = struct.unpack("ii", bytes(raw[4:12]))
                found.append((path, devname, xmin, xmax, ymin, ymax))
                print(f"  absX=[{xmin},{xmax}] absY=[{ymin},{ymax}]")
        finally:
            os.close(fd)
    return found


def dump_frames(path: str, seconds: float = 5.0):
    print(f"\nReading {path} for {seconds:.0f}s — move one finger on the pad…")
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    # struct input_event: timeval + type + code + value. Use kernel size via ioctl EVIOCGVERSION
    # On 64-bit userspace with 64-bit time_t, input_event is typically 24 bytes.
    fmt = "llHHi"  # sec, usec, type, code, value — may be wrong on some libc; try both
    size = struct.calcsize(fmt)
    if size < 24:
        fmt = "qqHHi"
        size = struct.calcsize(fmt)
    end = time.time() + seconds
    slots = {}
    current = 0
    frames = 0
    try:
        while time.time() < end and frames < 40:
            try:
                data = os.read(fd, size * 32)
            except BlockingIOError:
                time.sleep(0.01)
                continue
            if not data:
                time.sleep(0.01)
                continue
            for off in range(0, len(data) - size + 1, size):
                chunk = data[off : off + size]
                if len(chunk) < size:
                    break
                if fmt.startswith("ll"):
                    _sec, _usec, typ, code, value = struct.unpack(fmt, chunk)
                else:
                    _sec, _usec, typ, code, value = struct.unpack(fmt, chunk)
                if typ == EV_ABS:
                    if code == ABS_MT_SLOT:
                        current = value
                        slots.setdefault(current, {})
                    elif code == ABS_MT_TRACKING_ID:
                        slots.setdefault(current, {})["id"] = value
                    elif code == ABS_MT_POSITION_X:
                        slots.setdefault(current, {})["x"] = value
                    elif code == ABS_MT_POSITION_Y:
                        slots.setdefault(current, {})["y"] = value
                    elif code == ABS_MT_PRESSURE:
                        slots.setdefault(current, {})["p"] = value
                elif typ == EV_KEY and code in (BTN_TOUCH, BTN_TOOL_FINGER):
                    print(f"  KEY code={code} value={value}")
                elif typ == EV_SYN and code == SYN_REPORT:
                    contacts = {
                        s: d
                        for s, d in slots.items()
                        if d.get("id", -1) >= 0
                    }
                    print(f"  SYN contacts={contacts}")
                    frames += 1
    finally:
        os.close(fd)
    print(f"done ({frames} frames)")


def main():
    print("groups:", end=" ")
    sys.stdout.flush()
    os.system("id -nG")
    pads = list_pads()
    if not pads:
        print(
            "\nNo readable multitouch touchpad. Join the input group, then log out/in:\n"
            '  sudo usermod -aG input "$USER"'
        )
        return 1
    # Prefer a device with touchpad in the name
    pads.sort(key=lambda p: (("touchpad" not in p[1].lower()), p[0]))
    dump_frames(pads[0][0])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
