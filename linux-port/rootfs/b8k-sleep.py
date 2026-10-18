#!/usr/bin/python3
"""Power key: screen off and on, the power menu, and system sleep.

A short press of the power key turns the lamp off or on when the key is
released; holding it for a second opens the power menu (b8k-ask power)
in the user's session instead. Ten minutes without a touch or a key turn
the lamp off as a short press would, so a tablet left lying on the table
does not light the room for hours. Thirty seconds after the lamp goes off,
b8k-suspend puts the system to sleep. There is no deep sleep on this
kernel, only s2idle; the power key wakes it, and the same press turns the
lamp back on. wlan_mt6628 does not survive a sleep, so b8k-suspend moves
the Wi-Fi drivers out of the way.

The key shares its input device with Home. The device is grabbed so
elogind cannot power the tablet off, and Home is passed through.

A program holds the screen on, or the system awake, as on any desktop:
with a logind inhibitor in "block" mode — "idle" keeps the idle timer from
turning the lamp off (a video), "sleep" keeps the system from sleeping
while the lamp is off (music); b8k-compat's Awake takes "sleep:idle".
The shell's older way still counts: a file in $XDG_RUNTIME_DIR/b8k-awake/
named after the program's pid, <pid>.screen or <pid>.suspend; a file
whose process is gone counts for nothing.
"""
import ctypes
import errno
import fcntl
import os
import pwd
import select
import subprocess
import sys
import time

EV_SYN = 0
EV_KEY = 1
SYN_REPORT = 0
KEY_POWER = 116
BUS_VIRTUAL = 0x06

LEVEL_PATH = "/run/b8k-sleep/level"
LONG_PRESS = 1.0  # seconds held before the power menu opens
SUSPEND_AFTER = 30.0  # seconds with the screen off before a system sleep
IDLE_OFF = 600.0  # seconds without any input before the screen goes off
RESCAN_SEC = 30.0  # how often the input devices are looked at again
SUSPEND = "/usr/local/sbin/b8k-suspend"


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


def event_nodes():
    """The input devices there are now, to notice one coming or going (the
    touch filter b8k-touchguard replaces the screen with its own)."""
    return sorted(n for n in os.listdir("/sys/class/input") if n.startswith("event"))


def activity_nodes(power_path):
    """Every input device but the power key (read on its own), the
    vibration motor and our own Home forwarder: touch and buttons alike.
    They are opened without a grab, so the session still gets each event."""
    found = []
    for node in sorted(os.listdir("/sys/class/input")):
        if not node.startswith("event"):
            continue
        path = "/dev/input/" + node
        if path == power_path or input_name(node) in ("regulator-haptic", "b8k-home"):
            continue
        try:
            found.append(os.open(path, os.O_RDONLY | os.O_NONBLOCK))
        except OSError as exc:
            log("ACTIVITY_OPEN_FAIL %s %s" % (path, exc.errno))
    return found


def drain(fd):
    try:
        while os.read(fd, 4096):
            pass
    except OSError:
        pass


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
        self.since = time.monotonic()
        self.suspender = None
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
        kick_frame()
        self.asleep = True
        self.since = time.monotonic()
        log("SLEEP")

    def wake(self):
        write_level(self.path, saved_level(self.path))
        kick_frame()
        self._ungrab_touch()
        self.asleep = False
        log("WAKE")

    def suspend_due(self):
        """Seconds until the screen-off timer asks for a system sleep."""
        if not self.asleep or not os.access(SUSPEND, os.X_OK):
            return None
        if awake_held("suspend"):
            self.since = time.monotonic()  # check again a full delay later
        if self.suspender is not None:
            if self.suspender.poll() is None:
                return None  # asleep or just woken; it is still running
            self.suspender = None
            self.since = time.monotonic()  # woke without a key: wait again
        return max(0.0, self.since + SUSPEND_AFTER - time.monotonic())

    def suspend(self):
        # A child does the sleep, so this loop is ready for the key that
        # wakes the tablet.
        try:
            self.suspender = subprocess.Popen([SUSPEND])
            log("SUSPEND")
        except OSError as exc:
            log("SUSPEND_FAIL %s" % exc.errno)
            self.since = time.monotonic()

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


# Only these variables reach the menu; anything else in the panel's
# environment (LD_PRELOAD, PATH...) is not trusted by a root process.
SESSION_VARS = ("WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS",
                "LANG", "NO_AT_BRIDGE", "GTK_A11Y", "GTK_THEME")


def session_user():
    # desktop.start runs the session as this user, or as root without it.
    try:
        name = open("/etc/b8k/session-user").read().strip()
        pwd.getpwnam(name)
        return name
    except (OSError, KeyError):
        return "root"


def inhibited(what):
    """True while some process holds a logind "block" inhibitor for what
    ("idle" or "sleep"). Delay inhibitors (NetworkManager's) do not count."""
    try:
        from gi.repository import Gio, GLib

        bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        res = bus.call_sync("org.freedesktop.login1", "/org/freedesktop/login1",
                            "org.freedesktop.login1.Manager", "ListInhibitors", None,
                            GLib.VariantType("(a(ssssuu))"), Gio.DBusCallFlags.NONE,
                            3000, None)
    except Exception as exc:  # no logind or no gi: the files alone decide
        log("INHIBIT_LIST_FAIL %s" % type(exc).__name__)
        return False
    for whats, _who, _why, mode, _uid, _pid in res.unpack()[0]:
        if mode == "block" and what in whats.split(":"):
            return True
    return False


def awake_held(kind):
    """True while a program keeps the screen on ("screen") or the system up
    ("suspend"): a logind block inhibitor, or a live <pid>.<kind> file."""
    if inhibited("idle" if kind == "screen" else "sleep"):
        return True
    try:
        uid = pwd.getpwnam(session_user()).pw_uid
        names = os.listdir("/run/user/%d/b8k-awake" % uid)
    except (OSError, KeyError):
        return False
    for name in names:
        pid, _dot, what = name.partition(".")
        if what == kind and pid.isdigit() and os.path.exists("/proc/" + pid):
            return True
    return False


def session_env():
    """Environment, uid and gid of the session, taken from its panel.

    The panel is the process running the real /usr/bin/waybar, not any
    process that calls itself waybar, and its runtime directory must be the
    one logind made for its uid.
    """
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            if os.readlink("/proc/%s/exe" % pid) != "/usr/bin/waybar":
                continue
            with open("/proc/%s/environ" % pid, "rb") as fh:
                raw = fh.read().split(b"\0")
            st = os.stat("/proc/%s" % pid)
        except OSError:
            continue
        env = {}
        for item in raw:
            key, sep, val = item.partition(b"=")
            key = key.decode(errors="replace")
            if sep and key in SESSION_VARS:
                env[key] = val.decode(errors="replace")
        if env.get("XDG_RUNTIME_DIR") != "/run/user/%d" % st.st_uid:
            continue
        try:
            user = pwd.getpwuid(st.st_uid)
        except KeyError:
            continue
        if user.pw_name != session_user():
            continue
        env.update(HOME=user.pw_dir, USER=user.pw_name, LOGNAME=user.pw_name,
                   PATH="/usr/local/bin:/usr/bin:/bin")
        return env, st.st_uid, st.st_gid
    return None


def as_session_user(argv):
    """Start argv in the user's session; False when there is no session."""
    found = session_env()
    if not found:
        return False
    env, uid, gid = found
    groups = os.getgrouplist(env["USER"], gid)

    def demote():
        # Drop the handler's raised priority before the user's program runs.
        try:
            os.sched_setscheduler(0, os.SCHED_OTHER, os.sched_param(0))
        except (OSError, AttributeError):
            pass
        os.setpriority(os.PRIO_PROCESS, 0, 0)
        os.setgroups(groups)
        os.setgid(gid)
        os.setuid(uid)

    subprocess.Popen(argv, env=env, cwd=env["HOME"], preexec_fn=demote,
                     start_new_session=True, stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return True


def kick_frame():
    # The BLS PWM duty is latched at the start of a display frame, and phoc
    # sends no frames while nothing on screen changes, so a new brightness
    # could wait seconds for the clock to redraw. A screen capture makes
    # phoc render one frame now; the picture itself is thrown away.
    try:
        as_session_user(["grim", "-t", "ppm", "/dev/null"])
    except OSError as exc:
        log("KICK_FAIL %s" % exc.errno)


def power_menu():
    try:
        if as_session_user(["/usr/local/bin/b8k-ask", "power"]):
            log("MENU")
        else:
            log("MENU_NO_SESSION")
    except OSError as exc:
        log("MENU_FAIL %s" % exc.errno)


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
    activity = activity_nodes(path)
    seen_nodes = event_nodes()
    last_input = time.monotonic()
    last = 0.0
    down = None  # press time while the key is held and not yet handled
    log("LISTEN asleep=%s activity=%d" % (int(lamp.asleep), len(activity)))
    while True:
        if down is None:
            # A device came or went: watch the ones there are now.
            nodes = event_nodes()
            if nodes != seen_nodes:
                for fd in activity:
                    os.close(fd)
                activity = activity_nodes(path)
                seen_nodes = nodes
                log("ACTIVITY_RESCAN %d" % len(activity))
            due = lamp.suspend_due()
            idle = None
            if not lamp.asleep:
                idle = max(0.0, last_input + IDLE_OFF - time.monotonic())
            # At least every RESCAN_SEC, to notice new devices.
            waits = [t for t in (due, idle, RESCAN_SEC) if t is not None]
            ready = select.select([power] + activity, [], [], min(waits))[0]
            for fd in ready:
                if fd != power:
                    drain(fd)
                    last_input = time.monotonic()
            if power not in ready:
                if ready:
                    continue
                if lamp.asleep:
                    left = lamp.suspend_due()
                    if left is not None and left <= 0:
                        lamp.suspend()
                elif time.monotonic() - last_input >= IDLE_OFF:
                    if awake_held("screen"):
                        last_input = time.monotonic()  # a video is playing
                    else:
                        log("IDLE")
                        lamp.sleep()
                continue
        if down is not None:
            # Held: wait for the release, or open the menu at LONG_PRESS.
            left = LONG_PRESS - (time.monotonic() - down)
            if left <= 0 or not select.select([power], [], [], left)[0]:
                down = None
                if lamp.asleep:
                    lamp.wake()
                power_menu()
                continue
        ev = read_event(power)
        last_input = time.monotonic()
        if ev.type == EV_KEY and ev.code == KEY_POWER:
            now = time.monotonic()
            if ev.value == 1:
                if now - last >= 0.4:
                    down = now
            elif ev.value == 0 and down is not None:
                # A short press acts on release, so a hold for the menu
                # leaves the screen alone.
                down = None
                last = now
                lamp.press()
            continue
        if home is None or not grabbed:
            continue
        if ev.type == EV_KEY:
            emit(home, EV_KEY, ev.code, ev.value)
            emit(home, EV_SYN, SYN_REPORT, 0)


def raise_priority():
    # The press must act at once even with every core busy. The handler
    # sleeps in read() almost all the time, so real-time costs nothing.
    try:
        os.sched_setscheduler(0, os.SCHED_FIFO, os.sched_param(10))
        log("PRIO fifo")
    except (OSError, AttributeError):
        try:
            os.nice(-10)
            log("PRIO nice")
        except OSError:
            pass


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        return check()
    raise_priority()
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
