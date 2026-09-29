#!/bin/sh
# Build an Alpine armv7 root filesystem for the B8000-H microSD card.
# UI_PROFILE=labwc (known-good rescue desktop) or phosh (target mobile shell).
# Runs inside WSL B8000Build. Output: artifacts/linux-port/b8000h-sd.img
set -eu

REPO="/mnt/c/Users/<user>/src/lenovo-b8000h-linux"
ROOT=/opt/b8000h/rootfs
IMG=/opt/b8000h/out/b8000h-sd.img
MIRROR=https://dl-cdn.alpinelinux.org/alpine/v3.24
IMG_MB=3072
UI_PROFILE=${UI_PROFILE:-labwc}

case "$UI_PROFILE" in
	labwc) UI_PACKAGES="labwc waybar fuzzel foot wvkbd swaybg pcmanfm" ;;
	phosh) UI_PACKAGES="phosh phoc squeekboard gnome-session gnome-control-center foot pcmanfm dbus elogind polkit upower networkmanager networkmanager-openrc
		gnome-calculator gnome-text-editor gnome-clocks gnome-console loupe epiphany phosh-wallpapers iio-sensor-proxy iio-sensor-proxy-openrc
		wpa_supplicant networkmanager-wifi networkmanager-cli iw wireless-regdb
		firefox mobile-config-firefox
		nautilus gnome-software gnome-software-plugin-apk gnome-weather gnome-maps gnome-calendar
		gnome-contacts showtime totem decibels gnome-music evince gnome-disk-utility baobab
		file-roller gnome-font-viewer gnome-characters snapshot" ;;
	*) echo "Unsupported UI_PROFILE: $UI_PROFILE (use labwc or phosh)" >&2; exit 2 ;;
esac

apk add --no-cache qemu-arm e2fsprogs sfdisk alpine-keys >/dev/null

# Run armv7 package scripts through qemu (F = interpreter preloaded, works in chroot).
[ -d /proc/sys/fs/binfmt_misc ] || mkdir -p /proc/sys/fs/binfmt_misc
mountpoint -q /proc/sys/fs/binfmt_misc || mount -t binfmt_misc none /proc/sys/fs/binfmt_misc
if [ ! -e /proc/sys/fs/binfmt_misc/qemu-arm ]; then
	echo ':qemu-arm:M::\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-arm:F' \
		> /proc/sys/fs/binfmt_misc/register
fi

rm -rf "$ROOT"
mkdir -p "$ROOT/etc/apk"
printf '%s/main\n%s/community\n' "$MIRROR" "$MIRROR" > "$ROOT/etc/apk/repositories"
mkdir -p "$ROOT/etc/apk/keys"
cp /usr/share/apk/keys/armv7/* "$ROOT/etc/apk/keys/"

apk --root "$ROOT" --arch armv7 --initdb --update-cache add \
	alpine-base openrc busybox-openrc eudev udev-init-scripts \
	seatd seatd-openrc libinput \
	$UI_PACKAGES \
	adwaita-icon-theme font-dejavu font-noto xdg-utils \
	bash htop nano e2fsprogs e2fsprogs-extra

# Basic system configuration.
echo b8000h > "$ROOT/etc/hostname"
printf 'root::0:0:root:/root:/bin/sh\n' > /tmp/root-pw
sed -i 's|^root:[^:]*:|root::|' "$ROOT/etc/shadow"
cat > "$ROOT/etc/fstab" <<'EOF'
/dev/root	/	ext4	rw,noatime	0 1
EOF
for s in devfs dmesg udev udev-trigger udev-settle; do
	ln -sf /etc/init.d/$s "$ROOT/etc/runlevels/sysinit/$s"
done
for s in hwclock modules sysctl hostname bootmisc syslog; do
	ln -sf /etc/init.d/$s "$ROOT/etc/runlevels/boot/$s"
done
for s in seatd local; do
	ln -sf /etc/init.d/$s "$ROOT/etc/runlevels/default/$s"
done
if [ "$UI_PROFILE" = phosh ]; then
	for s in dbus elogind polkit openrc-settingsd networkmanager iio-sensor-proxy; do
		[ -e "$ROOT/etc/init.d/$s" ] && ln -sf /etc/init.d/$s "$ROOT/etc/runlevels/default/$s"
	done
fi
for s in mount-ro killprocs savecache; do
	ln -sf /etc/init.d/$s "$ROOT/etc/runlevels/shutdown/$s"
done

# Kernel #19 keeps the B8000-H drivers in /root.  Kernel #20 has audio and
# battery built in, while MT6628 remains modular because it needs firmware
# from the root filesystem.  modules.builtin makes the choice deterministic
# and keeps one rootfs bootable with either kernel.
mkdir -p "$ROOT/etc/local.d" "$ROOT/etc/modprobe.d"
cat > "$ROOT/etc/modprobe.d/b8000h.conf" <<'EOF'
options wmt bgf_irq=1 debug_io=0 probe_only=0 wmt_wifi_func_ctrl=0
options wlan_mt6628 debug=47
EOF
cat > "$ROOT/etc/local.d/00-b8k-modules.start" <<'EOF'
#!/bin/sh
release=$(uname -r)
if grep -q '/b8k-battery\.ko$' "/lib/modules/$release/modules.builtin" 2>/dev/null; then
	# Audio and battery are built in; load firmware-backed Wi-Fi in order.
	modprobe wmt
	modprobe wlan_mt6628
else
	# Kernel #19 compatibility: its matching out-of-tree modules live here.
	insmod /root/b8k-battery.ko 2>/dev/null
	insmod /root/wmt.ko 2>/dev/null
	insmod /root/wlan_mt6628.ko 2>/dev/null
	for m in soundcore snd snd-timer snd-pcm mt6589_audio; do
		insmod "/root/$m.ko" 2>/dev/null
	done
fi
EOF
chmod +x "$ROOT/etc/local.d/00-b8k-modules.start"

# USB ACM console: the gadget is configured by the recovery initramfs and
# survives switch_root. Run a non-interactive shell on it (an interactive ash
# waits for terminal replies that Windows never sends).
mkdir -p "$ROOT/usr/local/bin"
cat > "$ROOT/usr/local/bin/acm-shell" <<'EOF'
#!/bin/sh
while [ ! -e /dev/ttyGS0 ]; do sleep 1; done
stty -F /dev/ttyGS0 raw -echo
echo "B8000-H microSD system shell ready" > /dev/ttyGS0
exec sh -c 'cat /dev/ttyGS0 | sh >/dev/ttyGS0 2>&1'
EOF
chmod +x "$ROOT/usr/local/bin/acm-shell"
grep -q acm-shell "$ROOT/etc/inittab" ||
	echo 'ttyGS0::respawn:/usr/local/bin/acm-shell' >> "$ROOT/etc/inittab"

# The installer is inert without its explicit destructive token and validates
# the installer-only DTB, source device and target size before formatting.
mkdir -p "$ROOT/usr/local/sbin"
cp "$REPO/linux-port/install-root-to-emmc.sh" "$ROOT/usr/local/sbin/"
chmod +x "$ROOT/usr/local/sbin/install-root-to-emmc.sh"

# Desktop: labwc (stacking compositor, wlroots pixman renderer: no GPU yet)
# + waybar panel (Start menu, windows, keyboard, battery, clock, power).
H="$ROOT/root/.config"
mkdir -p "$H/labwc" "$H/waybar" "$H/fuzzel"

cat > "$H/labwc/autostart" <<'EOF'
swaybg -c '#1e3a5f' >/dev/null 2>&1 &
waybar >/var/log/waybar.log 2>&1 &
EOF
cat > "$H/labwc/rc.xml" <<'EOF'
<?xml version="1.0"?>
<labwc_config>
  <theme><name></name><cornerRadius>6</cornerRadius>
    <font place=""><name>DejaVu Sans</name><size>13</size></font></theme>
  <placement><policy>center</policy></placement>
  <touch deviceName="Synaptics Blade10" mapToOutput="" mouseEmulation="yes"/>
</labwc_config>
EOF
cat > "$H/labwc/menu.xml" <<'EOF'
<?xml version="1.0"?>
<openbox_menu>
  <menu id="root-menu" label="">
    <item label="Terminal"><action name="Execute" command="foot"/></item>
    <item label="Files"><action name="Execute" command="pcmanfm"/></item>
    <item label="Keyboard"><action name="Execute" command="b8k-kbd"/></item>
    <separator/>
    <item label="Reboot"><action name="Execute" command="reboot"/></item>
    <item label="Power off"><action name="Execute" command="poweroff"/></item>
  </menu>
</openbox_menu>
EOF

cat > "$H/waybar/config" <<'EOF'
{
  "layer": "top",
  "position": "top",
  "height": 44,
  "modules-left": ["custom/start", "custom/kbd", "wlr/taskbar"],
  "modules-right": ["battery", "clock", "custom/power"],
  "custom/start": { "format": "  Пуск  ", "on-click": "fuzzel", "tooltip": false },
  "custom/kbd": { "format": " ⌨ ", "on-click": "b8k-kbd", "tooltip": false },
  "wlr/taskbar": { "on-click": "activate", "icon-size": 28, "format": "{title:.20}" },
  "battery": {
    "bat": "bq27541-0",
    "interval": 30,
    "format": "🔋 {capacity}%",
    "format-charging": "⚡ {capacity}%",
    "states": { "warning": 25, "critical": 10 }
  },
  "clock": { "format": "{:%H:%M  %d.%m}", "tooltip": false },
  "custom/power": { "format": " ⏻ ", "on-click": "b8k-power", "tooltip": false }
}
EOF
cat > "$H/waybar/style.css" <<'EOF'
* { font-family: "DejaVu Sans", "Noto Color Emoji", sans-serif; font-size: 18px; }
window#waybar { background: rgba(20, 24, 32, 0.92); color: #eeeeee; }
#custom-start { background: #2d6cdf; color: white; border-radius: 6px; margin: 4px; }
#custom-kbd, #custom-power, #battery, #clock { padding: 0 12px; }
#battery.warning { color: #ffb347; }
#battery.critical { color: #ff5555; }
#taskbar button { color: #eeeeee; padding: 0 8px; }
#taskbar button.active { background: rgba(255, 255, 255, 0.15); }
EOF
cat > "$H/fuzzel/fuzzel.ini" <<'EOF'
[main]
font=DejaVu Sans:size=16
lines=12
width=40
terminal=foot
[colors]
background=1e2430f0
text=eeeeeeff
selection=2d6cdfff
selection-text=ffffffff
EOF

cat > "$ROOT/usr/local/bin/b8k-kbd" <<'EOF'
#!/bin/sh
# Toggle the on-screen keyboard.
pkill -x wvkbd-mobintl || exec wvkbd-mobintl -L 280
EOF
cat > "$ROOT/usr/local/bin/b8k-power" <<'EOF'
#!/bin/sh
choice=$(printf 'Перезагрузка\nВыключение\nОтмена\n' | fuzzel --dmenu --lines 3 --prompt 'Питание: ')
case "$choice" in
	Перезагрузка) reboot ;;
	Выключение) poweroff ;;
esac
EOF
chmod +x "$ROOT/usr/local/bin/b8k-kbd" "$ROOT/usr/local/bin/b8k-power"

# Start the selected desktop from OpenRC local.d as root on seat0.
rm -f "$ROOT/etc/local.d/weston.start"
if [ "$UI_PROFILE" = phosh ]; then
	cat > "$ROOT/etc/local.d/desktop.start" <<'EOF'
#!/bin/sh
export HOME=/root XDG_RUNTIME_DIR=/run/user/0
# simpledrm names the panel "Unknown-1"; fake-builtin makes phoc map the
# touchscreen to it so touch follows screen rotation.
export PHOC_DEBUG=fake-builtin WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 LIBSEAT_BACKEND=seatd
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=Phosh:GNOME
export XDG_SESSION_DESKTOP=phosh LANG=ru_RU.UTF-8
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
# b8k-session opens a PAM session (pam_elogind) so Phosh has a logind
# session: brightness goes through login1.Session.SetBrightness.
# Restart the session if Phosh crashes instead of leaving a dead screen.
# pipewire-launcher's own cleanup (pkill -u "$USER" -fx) misses under busybox
# with USER unset: stale audio daemons of the last session would fight the
# new ones over the sound card, so stop them between sessions.
( while :; do /usr/local/bin/b8k-session dbus-run-session -- phosh-session >>/var/log/phosh.log 2>&1; killall pipewire pipewire-pulse wireplumber 2>/dev/null; sleep 2; done & ) >/dev/null 2>&1
EOF
	mkdir -p "$ROOT/etc/pam.d"
	printf 'auth     sufficient pam_permit.so\naccount  sufficient pam_permit.so\nsession  include    base-session\n' \
		> "$ROOT/etc/pam.d/b8k-session"
	# Built by linux-port/tools-arm/build-b8k-session.sh (armv7 musl, -lpam).
	install -m 755 /opt/b8000h/tools/b8k-session "$ROOT/usr/local/bin/b8k-session"

	# Russian UI: install the -lang subpackages of everything installed,
	# then keep only the ru catalogs (the full set is ~50 MB).
	LANGS=$(for p in $(apk --root "$ROOT" --arch armv7 info 2>/dev/null); do
		case "$p" in *-lang|*-doc|*-dev) continue ;; esac
		apk --root "$ROOT" --arch armv7 search -x "$p-lang" 2>/dev/null | grep -q . && echo "$p-lang"
	done)
	apk --root "$ROOT" --arch armv7 add $LANGS
	find "$ROOT/usr/share/locale" -mindepth 1 -maxdepth 1 -type d \
		! -name 'ru' ! -name 'ru_RU' ! -name 'en*' -exec rm -rf {} +
	cat > "$ROOT/usr/share/glib-2.0/schemas/90-b8000h.gschema.override" <<'EOF'
[org.gnome.desktop.input-sources]
sources=[('xkb', 'us'), ('xkb', 'ru')]

[org.gnome.system.locale]
region='ru_RU.UTF-8'

[org.gnome.desktop.background]
picture-uri='file:///usr/share/phosh/backgrounds/byzantium-abstract.xml'
picture-uri-dark='file:///usr/share/phosh/backgrounds/byzantium-abstract.xml'

[org.gnome.desktop.screensaver]
picture-uri='file:///usr/share/phosh/backgrounds/byzantium-abstract.xml'
EOF
	chroot "$ROOT" glib-compile-schemas /usr/share/glib-2.0/schemas

	# The bma255 is mounted rotated (vendor cust_acc direction=3); iio-sensor-
	# proxy needs this for Phosh auto-rotation (verified on the tablet).
	mkdir -p "$ROOT/etc/udev/rules.d"
	cat > "$ROOT/etc/udev/rules.d/61-b8000h-accel.rules" <<'EOF'
SUBSYSTEM=="iio", ATTR{name}=="bma255", ENV{ACCEL_MOUNT_MATRIX}="0, -1, 0; 1, 0, 0; 0, 0, 1"
EOF
else
	cat > "$ROOT/etc/local.d/desktop.start" <<'EOF'
#!/bin/sh
export HOME=/root XDG_RUNTIME_DIR=/run/user/0
export WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 LIBSEAT_BACKEND=seatd
export XDG_CURRENT_DESKTOP=labwc LANG=ru_RU.UTF-8
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
( labwc >/var/log/labwc.log 2>&1 & ) >/dev/null 2>&1
EOF
fi
chmod +x "$ROOT/etc/local.d/desktop.start"

# A compact Android-sparse image is written to only the beginning of the
# 13.16 GiB USRDATA partition. Grow ext4 online once after the eMMC boot.
cat > "$ROOT/etc/local.d/resize-emmc-root.start" <<'EOF'
#!/bin/sh
# initramfs mounts root as /dev/root; USRDATA is mmcblk1p20 when the SD is on.
grep -q root=PARTNAME=USRDATA /proc/cmdline || exit 0
if resize2fs "$(readlink -f /dev/root)" >/var/log/resize-emmc-root.log 2>&1; then
	mv /etc/local.d/resize-emmc-root.start /etc/local.d/resize-emmc-root.done
fi
EOF
chmod +x "$ROOT/etc/local.d/resize-emmc-root.start"

# BQ24196 input current limit. The bq24190 driver resets the charger to the
# 500 mA USB default and nothing detects a wall adapter (the vendor kernel did
# it with MT6320 BC1.2), so with the screen on the battery never charged.
# VBUS present but no USB host enumerating the gadget for 15 s means a
# dedicated charger: allow 2 A (VINDPM still backs off on a weak adapter).
cat > "$ROOT/etc/local.d/b8k-charger.start" <<'EOF'
#!/bin/sh
(
C=/sys/class/power_supply/bq24190-charger
L=/var/log/b8k-charger.log
n=0 last=
while :; do
	want=
	if [ "$(cat $C/online 2>/dev/null)" = 1 ]; then
		host=0 st=
		for u in /sys/class/udc/*/state; do
			st="$st$(cat $u 2>/dev/null) "
			case "$st" in *configured*|*addressed*|*default*|*suspended*) host=1 ;; esac
		done
		if [ $host = 1 ]; then
			n=0 want=500000
		else
			n=$((n + 1))
			[ $n -ge 3 ] && want=2000000
		fi
		if [ -n "$want" ] && [ "$(cat $C/input_current_limit)" != $want ]; then
			echo $want > $C/input_current_limit
		fi
		cur="udc=${st% } limit=$(cat $C/input_current_limit)"
	else
		n=0 cur=offline
	fi
	[ "$cur" != "$last" ] && echo "$(date '+%F %T') $cur" >> $L && last=$cur
	sleep 5
done
) </dev/null >/dev/null 2>&1 &
EOF
chmod +x "$ROOT/etc/local.d/b8k-charger.start"

# Make the SD image: one MBR partition, ext4, label B8000ROOT.
rm -f "$IMG"
truncate -s ${IMG_MB}M "$IMG"
echo 'label: dos
start=2048, type=83' | sfdisk "$IMG" >/dev/null
OFF=$((2048 * 512))
dd if=/dev/zero bs=1M count=0 seek=$((IMG_MB - 1)) of=/tmp/p1.ext4 2>/dev/null
mkfs.ext4 -q -F -L B8000ROOT -d "$ROOT" /tmp/p1.ext4
dd if=/tmp/p1.ext4 of="$IMG" bs=1M seek=1 conv=notrunc status=none
rm -f /tmp/p1.ext4

mkdir -p "$REPO/artifacts/linux-port"
cp "$IMG" "$REPO/artifacts/linux-port/b8000h-$UI_PROFILE-sd.img"
du -sh "$ROOT"
sha256sum "$IMG"
