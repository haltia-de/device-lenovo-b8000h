#!/usr/bin/python3
"""Toggle the backlight from the power key.

Kernel suspend is not used. The DSI panel is left in the bootloader video
mode and the Wi-Fi driver's suspend path does nothing, so a real sleep
does not come back. The power key only turns the lamp off and on.

The key shares its input device with Home. The device is grabbed so
elogind cannot power the tablet off, and Home is passed through.
"""
import ctypes
import errno
import fcntl
import os
import sys
import time

EV_SYN = 0
EV_KEY = 1
SYN_REPORT = 0
KEY_POWER = 116
BUS_VIRTUAL = 0x06

LEVEL_PATH = "/run/b8k-sleep/level"


class Timeval(ctypes.Structure):
    _fields_ = [("sec", ctypes.c_long), ("usec", ctypes.c_long)]


class InputEvent(ctypes.Structure):
    _fields_ = [
        ("time", Timeval),
        ("type", ctypes.c_uint16),
        ("code", ctypes.c_uint16),
        ("value", ctypes.c_int32),
    ]


class InputId(ctypes.Structure):
    _fields_ = [
        ("bustype", ctypes.c_uint16),
        ("vendor", ctypes.c_uint16),
        ("product", ctypes.c_uint16),
        ("version", ctypes.c_uint16),
    ]


class UinputSetup(ctypes.Structure):
    _fields_ = [
        ("id", InputId),
        ("name", ctypes.c_char * 80),
        ("ff_effects_max", ctypes.c_uint32),
    ]


def _ioc(direction, type_, nr, size):
    return (direction << 30) | (size << 16) | (type_ << 8) | nr


def _iow(type_, nr, size):
    return _ioc(1, type_, nr, size)


def _io(type_, nr):
    return _ioc(0, type_, nr, 0)


EVIOCGRAB = _iow(ord("E"), 0x90, ctypes.sizeof(ctypes.c_int))
UI_SET_EVBIT = _iow(ord("U"), 100, ctypes.sizeof(ctypes.c_int))
UI_SET_KEYBIT = _iow(ord("U"), 101, ctypes.sizeof(ctypes.c_int))
UI_DEV_SETUP = _iow(ord("U"), 3, ctypes.sizeof(UinputSetup))
UI_DEV_CREATE = _io(ord("U"), 1)


def log(msg):
    print(msg, flush=True)


def backlight_path():
    root = "/sys/class/backlight"
    names = sorted(os.listdir(root))
    if not names:
        raise SystemExit("NO_BACKLIGHT")
    return os.path.join(root, names[0], "brightness")


def read_level(path):
    with open(path) as fh:
        return int(fh.read().strip() or "0")


def write_level(path, level):
    with open(path, "w") as fh:
        fh.write(str(int(level)))


def input_name(node):
    path = "/sys/class/input/%s/device/name" % node
    try:
        with open(path) as fh:
            return fh.read().strip()
    except OSError:
        return ""


def find_event(name):
    for node in sorted(os.listdir("/sys/class/input")):
        if node.startswith("event") and input_name(node) == name:
            return "/dev/input/" + node
    return ""


def touch_nodes():
    found = []
    for node in sorted(os.listdir("/sys/class/input")):
        if not node.startswith("event"):
            continue
        name = input_name(node).lower()
        if "synaptics" in name or "touch" in name:
            found.append("/dev/input/" + node)
    return found


def saved_level(path):
    try:
        level = int(open(LEVEL_PATH).read().strip())
    except (OSError, ValueError):
        level = 0
    if level <= 0:
        max_path = os.path.join(os.path.dirname(path), "max_brightness")
        try:
            level = max(1, read_level(max_path) // 2)
        except OSError:
            level = 116
    return level


def remember(level):
    os.makedirs(os.path.dirname(LEVEL_PATH), exist_ok=True)
    tmp = LEVEL_PATH + ".tmp"
    with open(tmp, "w") as fh:
        fh.write(str(int(level)))
    os.rename(tmp, LEVEL_PATH)


class Lamp:
    def __init__(self):
        self.path = backlight_path()
        self.touch = []
        self.asleep = read_level(self.path) <= 0
        if self.asleep:
            self._grab_touch()

    def press(self):
        if self.asleep:
            self.wake()
        else:
            self.sleep()

    def sleep(self):
        current = read_level(self.path)
        if current > 0:
            remember(current)
        self._grab_touch()
        write_level(self.path, 0)
        self.asleep = True
        log("SLEEP")

    def wake(self):
        write_level(self.path, saved_level(self.path))
        self._ungrab_touch()
        self.asleep = False
        log("WAKE")

    def _grab_touch(self):
        self._ungrab_touch()
        for path in touch_nodes():
            try:
                fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
                fcntl.ioctl(fd, EVIOCGRAB, 1)
            except OSError as exc:
                log("TOUCH_GRAB_FAIL %s %s" % (path, exc.errno))
                continue
            self.touch.append(fd)

    def _ungrab_touch(self):
        for fd in self.touch:
            try:
                fcntl.ioctl(fd, EVIOCGRAB, 0)
            except OSError:
                pass
            os.close(fd)
        self.touch = []


def check():
    path = backlight_path()
    current = read_level(path)
    off = current
    try:
        write_level(path, 0)
        time.sleep(0.35)
        off = read_level(path)
    finally:
        write_level(path, current if current > 0 else saved_level(path))
    restored = read_level(path)
    log("CHECK off=%s restored=%s was=%s" % (off, restored, current))
    if off != 0 or restored != (current if current > 0 else saved_level(path)):
        return 1
    return 0


def open_home_forward():
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    fcntl.ioctl(fd, UI_SET_KEYBIT, 102)
    setup = UinputSetup()
    setup.id.bustype = BUS_VIRTUAL
    setup.name = b"b8k-home"
    raw = ctypes.string_at(ctypes.addressof(setup), ctypes.sizeof(setup))
    fcntl.ioctl(fd, UI_DEV_SETUP, raw)
    fcntl.ioctl(fd, UI_DEV_CREATE, 0)
    return fd


def emit(fd, ev_type, code, value):
    ev = InputEvent()
    ev.type = ev_type
    ev.code = code
    ev.value = value
    os.write(fd, ctypes.string_at(ctypes.addressof(ev), ctypes.sizeof(ev)))


def read_event(fd):
    need = ctypes.sizeof(InputEvent)
    buf = b""
    while len(buf) < need:
        chunk = os.read(fd, need - len(buf))
        if not chunk:
            raise EOFError("input closed")
        buf += chunk
    return InputEvent.from_buffer_copy(buf)


def listen():
    path = find_event("mtk-pmic-keys")
    if not path:
        log("NO_POWER_KEY")
        return 1
    power = os.open(path, os.O_RDONLY)
    home = None
    grabbed = False
    try:
        home = open_home_forward()
        log("HOME_OK")
    except OSError as exc:
        log("HOME_FAIL %s" % exc.errno)
        home = None
    if home is not None:
        try:
            fcntl.ioctl(power, EVIOCGRAB, 1)
            grabbed = True
            log("GRAB_OK %s" % path)
        except OSError as exc:
            log("GRAB_FAIL %s" % exc.errno)
    else:
        log("NO_GRAB")
    lamp = Lamp()
    last = 0.0
    log("LISTEN asleep=%s" % int(lamp.asleep))
    while True:
        ev = read_event(power)
        if ev.type == EV_KEY and ev.code == KEY_POWER:
            if ev.value != 1:
                continue
            now = time.monotonic()
            if now - last < 0.4:
                continue
            last = now
            lamp.press()
            continue
        if home is None or not grabbed:
            continue
        if ev.type == EV_KEY:
            emit(home, EV_KEY, ev.code, ev.value)
            emit(home, EV_SYN, SYN_REPORT, 0)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        return check()
    lock_path = "/run/b8k-sleep.lock"
    os.makedirs("/run", exist_ok=True)
    lock = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o644)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as exc:
        if exc.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
            log("ALREADY_RUNNING")
            return 0
        raise
    try:
        return listen()
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        os.close(lock)


if __name__ == "__main__":
    try:
        sys.exit(main() or 0)
    except KeyboardInterrupt:
        sys.exit(0)
