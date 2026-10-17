# SPDX-License-Identifier: GPL-2.0
"""What the tablet shell's programs share, installed as
/usr/local/share/b8k/python/b8k_ui.py.

A program puts that folder on sys.path and imports this module when it is
there; without it (on another desktop) it runs with no haptics, no panel
badge and GTK's own dialogs. Everything here is optional in that sense:
each call fails quietly where the shell is missing.

  Buzz(ms)            the vibration motor (regulator-haptic), one pulse per call
  HEADER_CSS          the header all programs share: 88 px, grey, "←" button
  panel_signal(id)    the program's waybar signal, 0 when it has no button
  badge(id, count)    the red number on the program's panel button
  ask(question, yes)  a question in the shell's style (fuzzel): True for yes,
                      False for Отмена, None when fuzzel cannot be run
  open_url(url)       a page in Firefox through b8k-launch (start-up curtain,
                      one tab per link); False when b8k-launch is missing
"""
import fcntl
import os
import signal
import struct
import subprocess

SHARE = "/usr/local/share/b8k"
WAYBAR = "/usr/bin/waybar"

HEADER_CSS = b"""
headerbar {
  background-color: #dcdcda;
  background-image: none;
  border: none;
  box-shadow: none;
  min-height: 88px;
  /* The right end is the shell's close button (b8k-close): an 88 px button
     like these, 6 px after the last of them and 16 px from the edge. */
  padding: 8px 110px 8px 16px;
}
button.back {
  background-image: none;
  background-color: #f2f2f0;
  border: 1px solid #c0bfbc;
  box-shadow: none;
  border-radius: 16px;
  min-height: 72px;
  min-width: 96px;
  font-size: 30px;
  color: #2e3436;
}
/* Every header button fills the header's 88 px, as the close button in
   the corner does; an icon alone makes an 88 px square. The :not()
   outweighs a program's own button.<class> rules, loaded after this. */
headerbar button:not(.b8k-any) {
  min-height: 72px;
  padding-top: 7px;
  padding-bottom: 7px;
  margin-top: 0;
  margin-bottom: 0;
}
headerbar button.image-button:not(.b8k-any) {
  min-width: 72px;
  padding: 7px;
}
"""


class Buzz:
    """A short pulse on the regulator-haptic motor, set up once. Pulses
    under some 50 ms are not felt. A call returns True, so it can serve as
    a GObject emission hook."""

    EVIOCSFF = 0x40000000 | (0x2C << 16) | (ord("E") << 8) | 0x80

    def __init__(self, ms=70):
        self.fd = -1
        self.effect = -1
        try:
            for node in sorted(os.listdir("/sys/class/input")):
                if not node.startswith("event"):
                    continue
                with open("/sys/class/input/%s/device/name" % node) as fh:
                    if fh.read().strip() != "regulator-haptic":
                        continue
                self.fd = os.open("/dev/input/" + node, os.O_RDWR | os.O_CLOEXEC)
                # struct ff_effect, 32-bit: type, id, direction, trigger,
                # replay, then the rumble magnitudes and padding.
                buf = bytearray(struct.pack("<HhHHHHHHH", 0x50, -1, 0, 0, 0, ms, 0,
                                            0xFFFF, 0xFFFF) + bytes(32))
                fcntl.ioctl(self.fd, self.EVIOCSFF, buf)
                self.effect = struct.unpack_from("<h", buf, 2)[0]
                break
        except OSError:
            self.fd = -1

    def __call__(self, *_args):
        if self.fd >= 0:
            try:
                os.write(self.fd, struct.pack("<llHHi", 0, 0, 0x15, self.effect, 1))
            except OSError:
                pass
        return True


def apps_conf():
    """The session's apps.conf (b8k-apps), else the built-in one."""
    run = os.environ.get("XDG_RUNTIME_DIR", "")
    session = os.path.join(run, "b8k", "apps.conf")
    return session if run and os.path.exists(session) else os.path.join(SHARE, "apps.conf")


def panel_signal(app_id):
    """The program's waybar signal (b8k-apps numbers the buttons by their
    place on the panel); 0 when it has no button."""
    try:
        with open(apps_conf(), encoding="utf-8") as fh:
            inside = False
            for line in fh:
                line = line.strip()
                if line.startswith("["):
                    inside = line == "[%s]" % app_id
                elif inside and line.replace(" ", "").startswith("signal="):
                    return int(line.split("=", 1)[1])
    except (OSError, ValueError):
        pass
    return 0


def badge(app_id, count):
    """Write the number b8k-launch shows on the program's button, then tell
    waybar to redraw it. An uncaught real-time signal would kill the bar,
    and the whole session with it, so only a waybar that catches it gets
    one."""
    run = os.environ.get("XDG_RUNTIME_DIR")
    if not run:
        return
    folder = os.path.join(run, "b8k-badge")
    try:
        os.makedirs(folder, mode=0o700, exist_ok=True)
        path = os.path.join(folder, app_id)
        with open(path + ".new", "w") as fh:
            fh.write("%d\n" % count)
        os.replace(path + ".new", path)
    except OSError:
        return
    sig = panel_signal(app_id)
    if sig <= 0:
        return
    signum = signal.SIGRTMIN + sig
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            if os.readlink("/proc/%s/exe" % pid) != WAYBAR:
                continue
            if os.stat("/proc/" + pid).st_uid != os.getuid():
                continue
            with open("/proc/%s/status" % pid) as fh:
                caught = next((int(l.split()[1], 16) for l in fh if l.startswith("SigCgt:")), 0)
            if caught >> (signum - 1) & 1:
                os.kill(int(pid), signum)
        except (OSError, ValueError):
            continue


def ask(question, yes):
    """A yes/no question on the overlay layer, in the style of b8k-ask;
    Отмена is preselected so that Enter does no harm."""
    try:
        res = subprocess.run(
            ["fuzzel", "--config", os.path.join(SHARE, "fuzzel-ask.ini"), "--dmenu",
             "--index", "--mesg", question, "--cache", "/dev/null", "--select-index", "1"],
            input="%s\nОтмена\n" % yes, capture_output=True, text=True, timeout=600)
    except (OSError, subprocess.SubprocessError):
        return None
    return res.stdout.strip() == "0"


def open_url(url):
    """Open a page the shell's way; False when that is not available."""
    launch = "/usr/local/bin/b8k-launch"
    if not os.access(launch, os.X_OK):
        return False
    subprocess.Popen([launch, "url", url], start_new_session=True,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return True
