#!/bin/sh
# Build an Alpine armv7 root filesystem for the B8000-H microSD card.
# UI_PROFILE=labwc (known-good rescue desktop) or shell (Haltia, the tablet shell on
# phoc; the name is historical, Phosh itself is not installed).
# Runs inside WSL B8000Build. Output: artifacts/linux-port/b8000h-sd.img
set -eu

REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
ROOT=/opt/b8000h/rootfs

# fetch_app NAME URL TAG COMMIT [private]: a program from its own
# repository (see linux-port/apps/sources) installed into the image. The
# clone is kept in /opt/b8000h/out/apps and must be at COMMIT;
# B8K_APPS_SRC/NAME, when present, is used instead (a checkout being
# worked on). A private repository that cannot be cloned (no access) is
# left out with a warning.
fetch_app() {
	if [ -n "${B8K_APPS_SRC:-}" ] && [ -d "$B8K_APPS_SRC/$1" ]; then
		dir=$B8K_APPS_SRC/$1
	else
		dir=/opt/b8000h/out/apps/$1
		if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" != "$4" ]; then
			# Three tries: GitHub now and then resets a connection.
			tries=0
			until rm -rf "$dir" && GIT_TERMINAL_PROMPT=0 git -c advice.detachedHead=false \
					clone -q --depth 1 --branch "$3" "$2" "$dir" 2>/dev/null; do
				tries=$((tries + 1))
				[ $tries -lt 3 ] || break
				sleep 5
			done
			if [ ! -d "$dir/.git" ]; then
				if [ "${5:-}" = private ]; then
					echo "WARNING: $1 left out: no access to $2" >&2
					return 0
				fi
				echo "$1: cannot clone $2 (tag $3)" >&2
				exit 1
			fi
		fi
		if [ "$(git -C "$dir" rev-parse HEAD)" != "$4" ]; then
			echo "$1: tag $3 is not commit $4" >&2
			exit 1
		fi
	fi
	# A repository with C parts builds them first against the tablet's
	# sysroot (the shell's build.sh: keyboard and tools; Видео's
	# vplay/build.sh: b8k-vplay, which needs the keyboard's sysroot).
	for b in "$dir/build.sh" "$dir/vplay/build.sh"; do
		[ -f "$b" ] || continue
		sh "$b" || echo "build-rootfs: $name: ${b#$dir/} failed, parts left out" >&2
	done
	B8K_CACHE=/opt/b8000h/out sh "$dir/install.sh" "$ROOT"
}
IMG=/opt/b8000h/out/b8000h-sd.img
MIRROR=https://dl-cdn.alpinelinux.org/alpine/v3.24
IMG_MB=3072
UI_PROFILE=${UI_PROFILE:-labwc}
# "phosh" is the profile's name before ADR 0004 (it never ran Phosh).
[ "$UI_PROFILE" = phosh ] && UI_PROFILE=shell

case "$UI_PROFILE" in
	labwc) UI_PACKAGES="labwc waybar fuzzel foot wvkbd swaybg pcmanfm" ;;
	shell) UI_PACKAGES="phoc waybar dbus elogind polkit polkit-elogind-libs upower networkmanager networkmanager-openrc
		iio-sensor-proxy iio-sensor-proxy-openrc feedbackd feedbackd-device-themes
		pulseaudio pulseaudio-alsa pulseaudio-utils alsa-plugins-pulse alsa-utils alsa-ucm-conf
		galculator galculator-lang
		wpa_supplicant networkmanager-wifi networkmanager-cli iw wireless-regdb
		firefox mobile-config-firefox
		foot grim py3-pillow py3-cairo font-noto-emoji gtk+3.0-lang desktop-file-utils tzdata
		py3-aiohttp py3-pydantic py3-aiofiles py3-aiosqlite py3-msgpack
		py3-python-socks py3-qrcode py3-websockets py3-zstandard
		aisleriot aisleriot-lang earlyoom earlyoom-openrc fuzzel gtk-layer-shell
		py3-gobject3 openssh doas
		mpv-libs yt-dlp yt-dlp-ejs quickjs py3-mpv py3-requests nftables ffmpeg py3-mutagen mako" ;;
	*) echo "Unsupported UI_PROFILE: $UI_PROFILE (use labwc or shell)" >&2; exit 2 ;;
esac

apk add --no-cache qemu-arm e2fsprogs sfdisk alpine-keys git kmod make tar >/dev/null

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
# The interface is Russian: Moscow time until the person picks a zone in
# Настройки → Дата и время (openrc-settingsd moves the link).
ln -sf /usr/share/zoneinfo/Europe/Moscow "$ROOT/etc/localtime"
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
if [ "$UI_PROFILE" = shell ]; then
	# iio-sensor-proxy: screen rotation from the accelerometer (BMA255),
	# which the shell asks for. feedbackd starts on the session bus.
	for s in dbus elogind polkit openrc-settingsd networkmanager sshd ntpd iio-sensor-proxy; do
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
# Errors and warnings only: the default 47 also logs every packet.
options wlan_mt6628 debug=3
EOF
cat > "$ROOT/etc/local.d/00-b8k-modules.start" <<'EOF'
#!/bin/sh
release=$(uname -r)
if grep -q '/b8k-battery\.ko$' "/lib/modules/$release/modules.builtin" 2>/dev/null; then
	# Audio and battery are built in. WMT probes before the stock
	# /system firmware is mounted, so the first load misses WMT.cfg.
	# Wait, then reload. BusyBox rmmod takes one module name.
	i=0
	while [ ! -e /system/etc/firmware/WMT.cfg ] && [ "$i" -lt 30 ]; do
		i=$((i + 1))
		sleep 1
	done
	# One reload at a time: b8k-netwatch and b8k-suspend run this script
	# too, and two at once left modprobe hanging.
	exec 8>/run/b8k-wlan.lock
	flock 8
	rmmod wlan_mt6628 2>/dev/null || true
	rmmod wmt 2>/dev/null || true
	modprobe wmt
	# A fixed MAC from the machine id. Without the Android NVRAM file the
	# driver makes one up on every load, and DHCP gave a new address each boot.
	mac=$(md5sum /etc/machine-id 2>/dev/null | sed -E 's/^(..)(..)(..).*/00:08:22:\1:\2:\3/')
	modprobe wlan_mt6628 ${mac:+mac=$mac}
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

# Safety net for an older BOOT image. The FIFO kernel already writes
# 0x80f000e0. This script rewrites the register only if it still holds
# the bootloader value 0x00f00010 (256-byte threshold, scroll copies).
# Alpine's base leaves out the /usr/local folders the files below go to.
mkdir -p "$ROOT/usr/local/bin" "$ROOT/usr/local/sbin" "$ROOT/usr/local/share/b8k"
install -m 755 "$REPO/linux-port/rootfs/b8k-rdma-fifo.py" \
	"$ROOT/usr/local/sbin/b8k-rdma-fifo.py"
cat > "$ROOT/etc/local.d/zz-rdma-fifo.start" <<'EOF'
#!/bin/sh
python3 /usr/local/sbin/b8k-rdma-fifo.py >/var/log/b8k-rdma-fifo.log 2>&1 &
EOF
chmod +x "$ROOT/etc/local.d/zz-rdma-fifo.start"

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
# The compositor owns the panel, so tty2-6 are never seen; tty1 stays as
# the rescue console.
sed -i -E 's/^(tty[2-6]::)/#\1/' "$ROOT/etc/inittab"
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
if [ "$UI_PROFILE" = shell ]; then
	# Patched wpa_supplicant: Alpine's binary advertises "pmf", so
	# NetworkManager appends WPA-PSK-SHA256 and the MT6628 driver
	# rejects the association. No saved network is installed.
	WPA_OUT=${WPA_OUT:-/opt/b8000h/out/wpa-mt6628}
	test -x "$WPA_OUT/wpa_supplicant"
	test -x "$WPA_OUT/wpa_cli"
	test -x "$WPA_OUT/wpa_passphrase"
	if strings "$WPA_OUT/wpa_supplicant" | grep -qx pmf; then
		echo "error: wpa_supplicant still advertises pmf" >&2
		exit 1
	fi
	install -m 755 "$WPA_OUT/wpa_supplicant" "$ROOT/sbin/wpa_supplicant"
	install -m 755 "$WPA_OUT/wpa_cli" "$ROOT/sbin/wpa_cli"
	install -m 755 "$WPA_OUT/wpa_passphrase" "$ROOT/sbin/wpa_passphrase"
	rm -f "$ROOT/etc/runlevels/default/wpa_supplicant" \
		"$ROOT/etc/runlevels/default/wpa_cli" \
		"$ROOT/etc/runlevels/boot/wpa_supplicant" \
		"$ROOT/etc/NetworkManager/conf.d/unmanage-wlan0.conf" \
		"$ROOT/etc/local.d/10-wifi-glider.start"
	install -d -m 755 "$ROOT/etc/NetworkManager/conf.d"
	install -m 644 "$REPO/linux-port/wifi/wifi-mt6628.conf" \
		"$ROOT/etc/NetworkManager/conf.d/wifi-mt6628.conf"
	mkdir -p "$ROOT/lib/firmware"
	for f in WIFI_RAM_CODE_MT6628 WMT.cfg mt6628_ant_m1.cfg mt6628_patch_e1_hdr.bin \
			mt6628_patch_e2_0_hdr.bin mt6628_patch_e2_1_hdr.bin; do
		ln -sfn "/system/etc/firmware/$f" "$ROOT/lib/firmware/$f"
	done

	# GPU: stock Android blobs via b8k-android-system, modules and
	# Services via b8k-gpu, compositor via phoc-gpu. The Debian island
	# in /opt/debian is built on the tablet (linux-port/hybris/). The
	# musl phoc stays the fallback; phoc-gpu sets WLR_RENDERER=gles2
	# only for the glibc phoc. Clients keep drawing on the CPU.
	# The module sets (usr/lib/b8k/kN) come with the kernel, below.
	install -d -m 755 "$ROOT/usr/lib/b8k" "$ROOT/usr/local/bin" "$ROOT/etc/skel"
	install -m 755 "$REPO/linux-port/gpu/pvr-run.sh" "$ROOT/usr/lib/b8k/pvr-run.sh"
	install -m 755 "$REPO/linux-port/gpu/b8k-gpu.initd" "$ROOT/etc/init.d/b8k-gpu"
	install -m 755 "$REPO/linux-port/rootfs/b8k-android-system.initd" \
		"$ROOT/etc/init.d/b8k-android-system"
	install -m 755 "$REPO/linux-port/hybris/phoc-gpu.sh" "$ROOT/usr/local/bin/phoc-gpu"
	# The shell (b8k-shell: panel, home screen, close button, keyboard, touch
	# filter, power key, theme), the program library, Firefox's
	# customization and the programs: repositories of their own, pinned in
	# linux-port/apps/sources (ADR 0001). A repository with a build.sh (the
	# shell) builds its C parts first, against the keyboard's sysroot.
	# They live in repositories of their own (linux-port/apps/sources).
	# Released packages first (ADR 0002, 0004). apps/packages.lock names each
	# package file and its sha256: the file is fetched from the Haltia apk
	# repository (haltia-de/packages, the GitHub release as a fallback) and
	# must match the lock byte for byte, so a release asset replaced after
	# the lock was written fails the build even though the repository index
	# would be signed again. The meta packages haltia-desktop and
	# haltia-desktop-ru and the repository line (haltia-keys) come from the
	# repository. A repository the lock does not name, or every one with
	# B8K_FROM_SOURCE=1, is built from its tag in sources as before.
	install -D -m 644 "$REPO"/linux-port/apk-keys/*.rsa.pub -t "$ROOT/etc/apk/keys/"
	lock=$REPO/linux-port/apps/packages.lock
	haltia_repo=${HALTIA_REPO:-https://haltia-de.github.io/packages/alpine/v3.24}
	locked=
	if [ -z "${B8K_FROM_SOURCE:-}" ] && [ -f "$lock" ]; then
		cache=/opt/b8000h/out/apks
		mkdir -p "$cache"
		files=
		while read -r name repo version file sum; do
			case $name in ''|'#'*) continue ;; esac
			[ -f "$cache/$file" ] && echo "$sum  $cache/$file" | sha256sum -c - >/dev/null 2>&1 ||
				wget -q -O "$cache/$file" "$haltia_repo/armv7/$file" ||
				wget -q -O "$cache/$file" "https://github.com/$repo/releases/download/v$version/$file"
			echo "$sum  $cache/$file" | sha256sum -c - >/dev/null || {
				echo "$file: sha256 is not the lock's" >&2
				exit 1
			}
			files="$files $cache/$file"
			locked="$locked $name "
		done <"$lock"
		# shellcheck disable=SC2086 # the file list
		apk --root "$ROOT" --arch armv7 --repository "$haltia_repo" --update-cache \
			add $files haltia-keys haltia-desktop haltia-desktop-ru
		# A package added from a file is held at that file's checksum in
		# the world (name><Q1...); without the hold apk upgrade can bring
		# later releases from the repository on the tablet. Every hold
		# goes: the names are haltia-* and py3-haltia-kit since 2.0.0.
		sed -i -E 's/><.*//' "$ROOT/etc/apk/world"
		if grep -q '><' "$ROOT/etc/apk/world"; then
			echo "checksum holds left in the world" >&2
			exit 1
		fi
		# openrc-settingsd (timedated for the shell's Дата и время) comes
		# with haltia-shell, after the service links above were made.
		for s in openrc-settingsd; do
			[ -e "$ROOT/etc/init.d/$s" ] && ln -sf /etc/init.d/$s "$ROOT/etc/runlevels/default/$s"
		done
	fi
	grep -v '^#' "$REPO/linux-port/apps/sources" | while read -r name url tag commit private; do
		[ -n "$name" ] || continue
		case $locked in *" $name "*) continue ;; esac
		[ "$private" != apk ] || { echo "warning: $name is only released as apk, not in packages.lock: left out" >&2; continue; }
		fetch_app "$name" "$url" "$tag" "$commit" "$private"
	done
	# Speaker: hardware gain fixed at the clean top step, software volume.
	mkdir -p "$ROOT/usr/share/pulseaudio/alsa-mixer/profile-sets" \
		"$ROOT/usr/share/pulseaudio/alsa-mixer/paths" "$ROOT/etc/udev/rules.d"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k.conf" \
		"$ROOT/usr/share/pulseaudio/alsa-mixer/profile-sets/b8k.conf"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k-speaker.conf" \
		"$ROOT/usr/share/pulseaudio/alsa-mixer/paths/b8k-speaker.conf"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k-mic.conf" \
		"$ROOT/usr/share/pulseaudio/alsa-mixer/paths/b8k-mic.conf"
	install -d -m 755 "$ROOT/etc/pulse/daemon.conf.d"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k-daemon.conf" \
		"$ROOT/etc/pulse/daemon.conf.d/b8k.conf"
	install -d -m 755 "$ROOT/etc/pulse/default.pa.d"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k-mic.pa" \
		"$ROOT/etc/pulse/default.pa.d/b8k-mic.pa"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k-call.pa" \
		"$ROOT/etc/pulse/default.pa.d/b8k-call.pa"
	install -m 644 "$REPO/linux-port/rootfs/pulse/91-b8k-pulse.rules" \
		"$ROOT/etc/udev/rules.d/91-b8k-pulse.rules"
	install -d -m 755 "$ROOT/usr/local/sbin"
	install -m 755 "$REPO/linux-port/rootfs/b8k-usb" "$ROOT/usr/local/sbin/b8k-usb"
	# The DPI bypass for YouTube, off until "b8k-zapret on" (kernel 25 has
	# the netfilter it needs). nfqws is the static ARM build of a zapret
	# release, checked against the release archive's sha256.
	install -m 755 "$REPO/linux-port/rootfs/b8k-zapret" "$ROOT/usr/local/sbin/b8k-zapret"
	install -m 755 "$REPO/linux-port/rootfs/b8k-zapret.initd" "$ROOT/etc/init.d/b8k-zapret"
	install -D -m 644 "$REPO/linux-port/rootfs/zapret.hosts" "$ROOT/etc/b8k/zapret.hosts"
	zapret_ver=v72.13
	zapret_sha=25c74e6c5f48963fa244c2955e76694a07c39447245a0457e2efdc74b3317e68
	ztmp=$(mktemp -d)
	if wget -q -O "$ztmp/z.tgz" "https://github.com/bol-van/zapret/releases/download/$zapret_ver/zapret-$zapret_ver.tar.gz" &&
		echo "$zapret_sha  $ztmp/z.tgz" | sha256sum -c - >/dev/null &&
		tar -xzf "$ztmp/z.tgz" -C "$ztmp" "zapret-$zapret_ver/binaries/linux-arm/nfqws"; then
		install -m 755 "$ztmp/zapret-$zapret_ver/binaries/linux-arm/nfqws" "$ROOT/usr/local/sbin/nfqws"
	else
		echo "warning: no nfqws (zapret $zapret_ver): b8k-zapret will not start" >&2
	fi
	rm -rf "$ztmp"
	# Without this, reboot turns the tablet off (see the script).
	install -m 755 "$REPO/linux-port/rootfs/b8k-wdt-exrst" "$ROOT/usr/local/sbin/b8k-wdt-exrst"
	cat > "$ROOT/etc/local.d/b8k-wdt-exrst.start" <<'EOF'
#!/bin/sh
/usr/local/sbin/b8k-wdt-exrst
EOF
	chmod +x "$ROOT/etc/local.d/b8k-wdt-exrst.start"
	install -m 755 "$REPO/linux-port/rootfs/b8k-zram" "$ROOT/usr/local/sbin/b8k-zram"
	cat > "$ROOT/etc/local.d/b8k-zram.start" <<'EOF'
#!/bin/sh
/usr/local/sbin/b8k-zram
EOF
	chmod +x "$ROOT/etc/local.d/b8k-zram.start"
	install -m 644 "$REPO/linux-port/rootfs/earlyoom.conf" "$ROOT/etc/conf.d/earlyoom"
	ln -sfn /etc/init.d/earlyoom "$ROOT/etc/runlevels/default/earlyoom"
	install -m 755 "$REPO/linux-port/rootfs/b8k-netwatch" "$ROOT/usr/local/sbin/b8k-netwatch"
	cat > "$ROOT/etc/local.d/b8k-netwatch.start" <<'EOF'
#!/bin/sh
start-stop-daemon --start --background --exec /usr/local/sbin/b8k-netwatch \
	--pidfile /run/b8k-netwatch.pid --make-pidfile
EOF
	chmod +x "$ROOT/etc/local.d/b8k-netwatch.start"
	# (b8k-panel-signal and the NetworkManager hook that refreshes the panel
	# are the shell's, b8k-shell.)
	# The empty microSD slot polls for a card every second; switch it off.
	install -m 755 "$REPO/linux-port/rootfs/b8k-sdslot" "$ROOT/usr/local/sbin/b8k-sdslot"
	cat > "$ROOT/etc/local.d/b8k-sdslot.start" <<'EOF'
#!/bin/sh
/usr/local/sbin/b8k-sdslot </dev/null >/dev/null 2>&1 &
EOF
	chmod +x "$ROOT/etc/local.d/b8k-sdslot.start"
	# The power key's handling is the shell's (b8k-sleep.py); it puts the
	# system to sleep through the suspend hook (ADR 0004), which on this
	# device is b8k-suspend (s2idle, Wi-Fi drivers out of the way). Without
	# the hook the shell sleeps with a plain freeze and wlan_mt6628 is lost.
	install -m 755 "$REPO/linux-port/rootfs/b8k-suspend" "$ROOT/usr/local/sbin/b8k-suspend"
	install -d -m 755 "$ROOT/usr/lib/haltia/hooks"
	ln -sfn /usr/local/sbin/b8k-suspend "$ROOT/usr/lib/haltia/hooks/suspend"
	# Firefox's customization (large toolbar, prefs, policies, uBlock Origin
	# Lite) is b8k-firefox, installed with the programs (apps/sources).
	ln -sfn /etc/init.d/b8k-android-system "$ROOT/etc/runlevels/default/b8k-android-system"
	ln -sfn /etc/init.d/b8k-gpu "$ROOT/etc/runlevels/default/b8k-gpu"
	ln -sfn /opt/debian/usr/lib/arm-linux-gnueabihf "$ROOT/usr/lib/arm-linux-gnueabihf"
	ln -sfn /opt/debian/usr/local/hybris "$ROOT/usr/local/hybris"

	cat > "$ROOT/etc/local.d/desktop.start" <<'EOF'
#!/bin/sh
export HOME=/root XDG_RUNTIME_DIR=/run/user/0
# simpledrm names the panel "Unknown-1"; fake-builtin makes phoc map the
# touchscreen to it so touch follows screen rotation.
# WLR_RENDERER=pixman applies to the musl /usr/bin/phoc fallback.
# phoc-gpu sets gles2 for the glibc phoc only.
export PHOC_DEBUG=fake-builtin WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 LIBSEAT_BACKEND=seatd
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=Haltia:GNOME
export XDG_SESSION_DESKTOP=haltia LANG=ru_RU.UTF-8
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
# haltia-session opens a PAM session (pam_elogind) so the session has a
# logind session: brightness goes through login1.Session.SetBrightness.
# Restart the session if phoc crashes instead of leaving a dead screen.
# The session user is read from /etc/haltia/session-user on every start (root
# when the file or the user is missing), so switching needs no reboot.
# pipewire-launcher's own cleanup (pkill -u "$USER" -fx) misses under
# busybox: stale audio daemons of the last session would fight the new ones
# over the sound card, so stop them between sessions.
# The loop ignores SIGHUP and is put in the background outside the
# subshell, so it outlives the shell of the local service that starts it.
( trap '' HUP
while :; do
	u=$(cat /etc/haltia/session-user 2>/dev/null || cat /etc/b8k/session-user 2>/dev/null)
	id "$u" >/dev/null 2>&1 || u=root
	/usr/bin/haltia-session -u "$u" dbus-run-session -- /usr/local/bin/phoc-gpu -C /usr/share/haltia/phoc.ini -E /usr/bin/haltia-launcher >>/var/log/session.log 2>&1
	killall pipewire pipewire-pulse wireplumber 2>/dev/null
	sleep 2
done ) >/dev/null 2>&1 &
EOF
	# haltia-session and its PAM service come with the shell (haltia-shell).

	# Russian UI: install the -lang subpackages of everything installed,
	# then keep only the ru catalogs (the full set is ~50 MB).
	LANGS=$(for p in $(apk --root "$ROOT" --arch armv7 info 2>/dev/null); do
		case "$p" in *-lang|*-doc|*-dev) continue ;; esac
		# An if, not "&&": a last package without -lang would end the loop
		# false and stop the script under set -e.
		if apk --root "$ROOT" --arch armv7 search -x "$p-lang" 2>/dev/null | grep -q .; then
			echo "$p-lang"
		fi
	done)
	apk --root "$ROOT" --arch armv7 add $LANGS
	find "$ROOT/usr/share/locale" -mindepth 1 -maxdepth 1 -type d \
		! -name 'ru' ! -name 'ru_RU' ! -name 'en*' -exec rm -rf {} +
	cat > "$ROOT/usr/share/glib-2.0/schemas/90-b8000h.gschema.override" <<'EOF'
[org.gnome.desktop.input-sources]
sources=[('xkb', 'us'), ('xkb', 'ru')]

[org.gnome.system.locale]
region='ru_RU.UTF-8'

[org.gnome.desktop.interface]
enable-animations=false
cursor-blink=false
toolkit-accessibility=false

[org.gnome.desktop.background]
picture-options='none'
picture-uri=''
picture-uri-dark=''
primary-color='#1e2430'
secondary-color='#1e2430'

[org.gnome.desktop.screensaver]
picture-options='none'
picture-uri=''
primary-color='#1e2430'
secondary-color='#1e2430'

[org.gnome.desktop.wm.preferences]
num-workspaces=1
audible-bell=false
EOF
	chroot "$ROOT" glib-compile-schemas /usr/share/glib-2.0/schemas

	# The session user: uid 1000, empty password, administrator through
	# doas (wheel). make-release-rootfs.sh locks root; set a password with
	# passwd before doas. The power menu turns the machine off without one.
	grep -q '^user:' "$ROOT/etc/passwd" ||
		chroot "$ROOT" adduser -D -u 1000 -G wheel -h /home/user -s /bin/sh user
	for g in video seat input audio; do
		chroot "$ROOT" addgroup user "$g"
	done
	chroot "$ROOT" passwd -d user >/dev/null
	chmod 750 "$ROOT/home/user"
	# The person's folders in Russian (xdg-user-dirs has no translations on
	# Alpine): programs find pictures, downloads and music there, and the
	# file chooser lists them. No desktop, templates or public folder.
	for d in Загрузки Документы Изображения Музыка Видео; do
		install -d -m 755 -o 1000 -g 1000 "$ROOT/home/user/$d"
	done
	install -d -m 755 -o 1000 -g 1000 "$ROOT/home/user/.config"
	cat > "$ROOT/home/user/.config/user-dirs.dirs" <<'EOF'
XDG_DESKTOP_DIR="$HOME"
XDG_DOWNLOAD_DIR="$HOME/Загрузки"
XDG_TEMPLATES_DIR="$HOME"
XDG_PUBLICSHARE_DIR="$HOME"
XDG_DOCUMENTS_DIR="$HOME/Документы"
XDG_MUSIC_DIR="$HOME/Музыка"
XDG_PICTURES_DIR="$HOME/Изображения"
XDG_VIDEOS_DIR="$HOME/Видео"
EOF
	echo ru_RU > "$ROOT/home/user/.config/user-dirs.locale"
	chown 1000:1000 "$ROOT/home/user/.config/user-dirs.dirs" "$ROOT/home/user/.config/user-dirs.locale"
	mkdir -p "$ROOT/etc/haltia"
	echo user > "$ROOT/etc/haltia/session-user"
	install -d "$ROOT/etc/doas.d"
	echo 'permit persist :wheel' > "$ROOT/etc/doas.d/wheel.conf"
	# z- sorts after wheel.conf. doas uses the last matching rule.
	# The power menu and root terminal rules are the shell's
	# (z-haltia-shell.conf); the USB role switch is this device's.
	rm -f "$ROOT/etc/doas.d/z-b8k-power.conf"
	cat > "$ROOT/etc/doas.d/z-b8k-device.conf" <<'EOF'
permit nopass user cmd /usr/local/sbin/b8k-usb args host
permit nopass user cmd /usr/local/sbin/b8k-usb args device
EOF

	# Kernel modules, GPU module sets and the GPU island come from outside
	# this tree: KERNEL is the built kernel tree (docs/INSTALL.md §0 step 1),
	# GPU_SETS a folder of kN/ sets of the external modules built against
	# each kernel, ISLAND a tar.gz of /opt/debian from a tablet that built it
	# (linux-port/hybris/). Without them the image has no Wi-Fi, no GPU.
	KERNEL=${KERNEL:-/opt/b8000h/linux-drm}
	GPU_SETS=${GPU_SETS:-/opt/b8000h/out/gpu-sets}
	ISLAND=${ISLAND:-/opt/b8000h/out/island.tgz}
	# KERNEL_PKG instead: a kernel tarball of ci/kernel.sh, unpacked
	# (lib/modules/<rel>, usr/lib/b8k/kN), for a build without the tree.
	if [ -n "${KERNEL_PKG:-}" ]; then
		rel=$(ls "$KERNEL_PKG/lib/modules")
		install -d -m 755 "$ROOT/lib/modules"
		cp -a "$KERNEL_PKG/lib/modules/$rel" "$ROOT/lib/modules/"
		test -e "$ROOT/lib/modules/$rel/kernel/drivers/net/wireless/mediatek/mt6628/wlan_mt6628.ko"
		echo "kernel modules: $rel"
		GPU_SETS=$KERNEL_PKG/usr/lib/b8k
	elif [ -f "$KERNEL/include/generated/utsrelease.h" ]; then
		rel=$(sed -n 's/^#define UTS_RELEASE "\(.*\)"/\1/p' "$KERNEL/include/generated/utsrelease.h")
		make -C "$KERNEL" ARCH=arm LLVM=1 modules_install INSTALL_MOD_PATH="$ROOT" >/dev/null
		rm -f "$ROOT/lib/modules/$rel/build" "$ROOT/lib/modules/$rel/source"
		test -e "$ROOT/lib/modules/$rel/kernel/drivers/net/wireless/mediatek/mt6628/wlan_mt6628.ko"
		echo "kernel modules: $rel"
	else
		echo "warning: no built kernel in $KERNEL, image has no kernel modules" >&2
	fi
	for d in "$GPU_SETS"/k[0-9]*; do
		[ -d "$d" ] || continue
		install -d -m 755 "$ROOT/usr/lib/b8k/${d##*/}"
		install -m 644 "$d"/*.ko "$ROOT/usr/lib/b8k/${d##*/}/"
		echo "GPU modules: ${d##*/}"
	done
	if [ -f "$ISLAND" ]; then
		install -d -m 755 "$ROOT/opt"
		tar -xzpf "$ISLAND" -C "$ROOT/opt" --numeric-owner
		test -x "$ROOT/opt/debian/usr/local/bin/phoc"
	else
		echo "warning: no $ISLAND, the session draws on the CPU" >&2
	fi

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
# Every 5 s, so the files are read with the read builtin: no process per round.
get() { v=; read -r v < "$1" 2>/dev/null; }
while :; do
	want=
	get $C/online
	if [ "$v" = 1 ]; then
		host=0 st=
		for u in /sys/class/udc/*/state; do
			get "$u"
			st="$st$v "
			case "$st" in *configured*|*addressed*|*default*|*suspended*) host=1 ;; esac
		done
		if [ $host = 1 ]; then
			n=0 want=500000
		else
			n=$((n + 1))
			[ $n -ge 3 ] && want=2000000
		fi
		get $C/input_current_limit
		if [ -n "$want" ] && [ "$v" != $want ]; then
			echo $want > $C/input_current_limit
		fi
		get $C/input_current_limit
		cur="udc=${st% } limit=$v"
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
