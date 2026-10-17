#!/bin/sh
# Build an Alpine armv7 root filesystem for the B8000-H microSD card.
# UI_PROFILE=labwc (known-good rescue desktop) or phosh (the tablet shell on
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
	B8K_CACHE=/opt/b8000h/out sh "$dir/install.sh" "$ROOT"
}
IMG=/opt/b8000h/out/b8000h-sd.img
MIRROR=https://dl-cdn.alpinelinux.org/alpine/v3.24
IMG_MB=3072
UI_PROFILE=${UI_PROFILE:-labwc}

case "$UI_PROFILE" in
	labwc) UI_PACKAGES="labwc waybar fuzzel foot wvkbd swaybg pcmanfm" ;;
	phosh) UI_PACKAGES="phoc waybar gnome-control-center gnome-settings-daemon gnome-shell-schemas dbus elogind polkit polkit-elogind-libs upower networkmanager networkmanager-openrc network-manager-applet
		pulseaudio pulseaudio-alsa pulseaudio-utils alsa-plugins-pulse alsa-utils alsa-ucm-conf
		galculator galculator-lang
		wpa_supplicant networkmanager-wifi networkmanager-cli iw wireless-regdb
		firefox mobile-config-firefox
		foot grim py3-pillow py3-cairo font-noto-emoji
		py3-aiohttp py3-pydantic py3-aiofiles py3-aiosqlite py3-msgpack
		py3-python-socks py3-qrcode py3-websockets py3-zstandard
		aisleriot aisleriot-lang earlyoom earlyoom-openrc fuzzel gtk-layer-shell
		py3-gobject3 openssh doas" ;;
	*) echo "Unsupported UI_PROFILE: $UI_PROFILE (use labwc or phosh)" >&2; exit 2 ;;
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
	for s in dbus elogind polkit openrc-settingsd networkmanager sshd ntpd; do
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
if [ "$UI_PROFILE" = phosh ]; then
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
	install -d -m 755 "$ROOT/usr/lib/b8k/k22" "$ROOT/usr/local/bin" "$ROOT/etc/skel"
	install -m 644 "$REPO/linux-port/drivers/ion-legacy/ion_legacy.ko" \
		"$ROOT/usr/lib/b8k/k22/ion_legacy.ko"
	install -m 644 "$REPO/linux-port/drivers/pvrsrvkm/pvrsrvkm.ko" \
		"$ROOT/usr/lib/b8k/k22/pvrsrvkm.ko"
	install -m 644 "$REPO/linux-port/drivers/dc_nohw/dc_nohw.ko" \
		"$ROOT/usr/lib/b8k/k22/dc_nohw.ko"
	install -m 755 "$REPO/linux-port/gpu/pvr-run.sh" "$ROOT/usr/lib/b8k/pvr-run.sh"
	install -m 755 "$REPO/linux-port/gpu/b8k-gpu.initd" "$ROOT/etc/init.d/b8k-gpu"
	install -m 755 "$REPO/linux-port/rootfs/b8k-android-system.initd" \
		"$ROOT/etc/init.d/b8k-android-system"
	install -m 755 "$REPO/linux-port/hybris/phoc-gpu.sh" "$ROOT/usr/local/bin/phoc-gpu"
	install -m 755 "$REPO/linux-port/rootfs/b8k-launcher" "$ROOT/usr/local/bin/b8k-launcher"
	install -m 755 "$REPO/linux-port/rootfs/b8k-keyboard" "$ROOT/usr/local/bin/b8k-keyboard"
	if [ -x /opt/b8000h/out/wvkbd-b8k ]; then
		install -m 755 /opt/b8000h/out/wvkbd-b8k "$ROOT/usr/local/bin/wvkbd-b8k"
	fi
	# Tap feedback on the panel (tools-arm/build-b8k-press.sh).
	if [ -x /opt/b8000h/tools/b8k-press ]; then
		install -m 755 /opt/b8000h/tools/b8k-press "$ROOT/usr/local/bin/b8k-press"
	fi
	# The curtain with the program's icon while it starts
	# (tools-arm/build-b8k-splash.sh).
	if [ -x /opt/b8000h/tools/b8k-splash ]; then
		install -m 755 /opt/b8000h/tools/b8k-splash "$ROOT/usr/local/bin/b8k-splash"
	fi
	# The home screen (tools-arm/build-b8k-home.sh).
	if [ -x /opt/b8000h/tools/b8k-home ]; then
		install -m 755 /opt/b8000h/tools/b8k-home "$ROOT/usr/local/bin/b8k-home"
	fi
	# The tab that brings the hidden panel back (tools-arm/build-b8k-tab.sh).
	if [ -x /opt/b8000h/tools/b8k-tab ]; then
		install -m 755 /opt/b8000h/tools/b8k-tab "$ROOT/usr/local/bin/b8k-tab"
	fi
	install -m 755 "$REPO/linux-port/rootfs/b8k-launch" "$ROOT/usr/local/bin/b8k-launch"
	install -m 755 "$REPO/linux-port/rootfs/b8k-volume" "$ROOT/usr/local/bin/b8k-volume"
	install -m 755 "$REPO/linux-port/rootfs/b8k-brightness" "$ROOT/usr/local/bin/b8k-brightness"
	# The programs on the panel that are not the shell itself: each has its
	# own install.sh, which adds its section to /usr/local/share/b8k/apps.d/.
	# They use the shell's shared module b8k_ui when it is there.
	install -D -m 644 "$REPO/linux-port/rootfs/python/b8k_ui.py" \
		"$ROOT/usr/local/share/b8k/python/b8k_ui.py"
	# They live in repositories of their own (linux-port/apps/sources).
	grep -v '^#' "$REPO/linux-port/apps/sources" | while read -r name url tag commit private; do
		[ -n "$name" ] || continue
		fetch_app "$name" "$url" "$tag" "$commit" "$private"
	done
	# Speaker: hardware gain fixed at the clean top step, software volume.
	mkdir -p "$ROOT/usr/share/pulseaudio/alsa-mixer/profile-sets" \
		"$ROOT/usr/share/pulseaudio/alsa-mixer/paths" "$ROOT/etc/udev/rules.d"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k.conf" \
		"$ROOT/usr/share/pulseaudio/alsa-mixer/profile-sets/b8k.conf"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k-speaker.conf" \
		"$ROOT/usr/share/pulseaudio/alsa-mixer/paths/b8k-speaker.conf"
	install -d -m 755 "$ROOT/etc/pulse/daemon.conf.d"
	install -m 644 "$REPO/linux-port/rootfs/pulse/b8k-daemon.conf" \
		"$ROOT/etc/pulse/daemon.conf.d/b8k.conf"
	install -m 644 "$REPO/linux-port/rootfs/pulse/91-b8k-pulse.rules" \
		"$ROOT/etc/udev/rules.d/91-b8k-pulse.rules"
	install -m 755 "$REPO/linux-port/rootfs/b8k-focus" "$ROOT/usr/local/bin/b8k-focus"
	install -m 755 "$REPO/linux-port/rootfs/b8k-meter" "$ROOT/usr/local/bin/b8k-meter"
	install -m 755 "$REPO/linux-port/rootfs/b8k-clock" "$ROOT/usr/local/bin/b8k-clock"
	install -m 755 "$REPO/linux-port/rootfs/b8k-ask" "$ROOT/usr/local/bin/b8k-ask"
	install -m 755 "$REPO/linux-port/rootfs/b8k-apps" "$ROOT/usr/local/bin/b8k-apps"
	install -m 644 "$REPO/linux-port/rootfs/fuzzel-ask.ini" "$ROOT/usr/local/share/b8k/fuzzel-ask.ini"
	install -d -m 755 "$ROOT/usr/local/sbin"
	install -m 755 "$REPO/linux-port/rootfs/b8k-poweroff" "$ROOT/usr/local/sbin/b8k-poweroff"
	install -m 755 "$REPO/linux-port/rootfs/b8k-usb" "$ROOT/usr/local/sbin/b8k-usb"
	install -m 755 "$REPO/linux-port/rootfs/b8k-rootsh" "$ROOT/usr/local/sbin/b8k-rootsh"
	install -m 755 "$REPO/linux-port/rootfs/b8k-term" "$ROOT/usr/local/bin/b8k-term"
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
	# Panel refresh from root helpers, and the NetworkManager hook that uses it.
	install -m 755 "$REPO/linux-port/rootfs/b8k-panel-signal" "$ROOT/usr/local/sbin/b8k-panel-signal"
	install -d -m 755 "$ROOT/etc/NetworkManager/dispatcher.d"
	install -m 755 "$REPO/linux-port/rootfs/nm-dispatcher-b8k-panel" \
		"$ROOT/etc/NetworkManager/dispatcher.d/90-b8k-panel"
	# The empty microSD slot polls for a card every second; switch it off.
	install -m 755 "$REPO/linux-port/rootfs/b8k-sdslot" "$ROOT/usr/local/sbin/b8k-sdslot"
	cat > "$ROOT/etc/local.d/b8k-sdslot.start" <<'EOF'
#!/bin/sh
/usr/local/sbin/b8k-sdslot </dev/null >/dev/null 2>&1 &
EOF
	chmod +x "$ROOT/etc/local.d/b8k-sdslot.start"
	# The power key turns the screen off; 30 s later b8k-suspend puts the
	# system into s2idle. elogind's default would power the tablet off.
	install -m 755 "$REPO/linux-port/rootfs/b8k-sleep.py" "$ROOT/usr/local/sbin/b8k-sleep.py"
	install -m 755 "$REPO/linux-port/rootfs/b8k-sleep-loop" "$ROOT/usr/local/sbin/b8k-sleep-loop"
	install -m 755 "$REPO/linux-port/rootfs/b8k-suspend" "$ROOT/usr/local/sbin/b8k-suspend"
	cat > "$ROOT/etc/local.d/b8k-sleep.start" <<'EOF'
#!/bin/sh
start-stop-daemon --start --background \
	--exec /usr/local/sbin/b8k-sleep-loop \
	--pidfile /run/b8k-sleep.pid --make-pidfile \
	--stdout /var/log/b8k-sleep.log --stderr /var/log/b8k-sleep.log
EOF
	chmod +x "$ROOT/etc/local.d/b8k-sleep.start"
	install -d -m 755 "$ROOT/etc/elogind/logind.conf.d"
	cat > "$ROOT/etc/elogind/logind.conf.d/b8k-power-key.conf" <<'EOF'
[Login]
HandlePowerKey=ignore
HandlePowerKeyLongPress=ignore
EOF
	install -d -m 755 "$ROOT/usr/local/share/b8k"
	install -m 644 "$REPO"/linux-port/rootfs/icons/*.svg "$REPO"/linux-port/rootfs/icons/*.png "$ROOT/usr/local/share/b8k/"
	install -m 644 "$REPO/linux-port/rootfs/foot.ini" "$ROOT/usr/local/share/b8k/foot.ini"
	install -m 644 "$REPO/linux-port/rootfs/phoc.ini" "$ROOT/usr/local/share/b8k/phoc.ini"
	install -m 644 "$REPO/linux-port/rootfs/firefox/userChrome.css" "$ROOT/usr/local/share/b8k/userChrome.css"
	install -m 644 "$REPO/linux-port/rootfs/firefox/userContent.css" "$ROOT/usr/local/share/b8k/userContent.css"
	# The program registry read by b8k-launch, b8k-focus, b8k-ask and b8k-webapp.js.
	install -m 644 "$REPO/linux-port/rootfs/apps.conf" "$ROOT/usr/local/share/b8k/apps.conf"
	install -m 644 "$REPO/linux-port/rootfs/panel" "$ROOT/usr/local/share/b8k/panel"
	install -m 644 "$REPO/linux-port/rootfs/firefox/b8k-webapp.js" "$ROOT/usr/local/share/b8k/b8k-webapp.js"
	if [ -d "$ROOT/usr/lib/firefox/defaults/pref" ]; then
		install -m 644 "$REPO/linux-port/rootfs/firefox/b8k-webapp.cfg" \
			"$ROOT/usr/lib/firefox/b8k-webapp.cfg"
		install -m 644 "$REPO/linux-port/rootfs/firefox/zz-b8k-webapp.js" \
			"$ROOT/usr/lib/firefox/defaults/pref/zz-b8k-webapp.js"
		install -m 644 "$REPO/linux-port/rootfs/firefox/b8k-ram.js" \
			"$ROOT/usr/lib/firefox/defaults/pref/b8k-ram.js"
		install -m 644 "$REPO/linux-port/rootfs/firefox/b8k-lean.js" \
			"$ROOT/usr/lib/firefox/defaults/pref/b8k-lean.js"
		# uBlock Origin Lite in complete mode instead of uBlock Origin, in a
		# copy of mobile-config-firefox's policies that Firefox reads instead.
		# The extension goes into the image: addons.mozilla.org answers 406
		# from some networks, and Firefox then never installed it. The
		# release on GitHub is the same file, signed by Mozilla.
		ubol_ver=2026.1012.1545
		ubol_sha=0a55b67791c0987bae74ae4648cd2034e47fcc33ebd2a45e2ea0706b13af0e3d
		ubol=/usr/local/share/b8k/firefox/ubol.xpi
		install -d -m 755 "$ROOT${ubol%/*}"
		if wget -q -O "$ROOT$ubol" "https://github.com/uBlockOrigin/uBOL-home/releases/download/$ubol_ver/uBOLite_$ubol_ver.firefox.signed.xpi" &&
			echo "$ubol_sha  $ROOT$ubol" | sha256sum -c - >/dev/null; then
			chmod 644 "$ROOT$ubol"
		else
			echo "warning: no uBlock Origin Lite $ubol_ver, Firefox will download it" >&2
			rm -f "$ROOT$ubol"
			ubol=
		fi
		# Russian menus: Alpine packages no Firefox language packs, so
		# Mozilla's signed one, which must match Firefox's version.
		ru_for=151.0.3
		ru_sha=61d3a59c7e98798f5bac288964764d1ff6b709ca860088a5d87a39ddc377bc85
		ru=/usr/local/share/b8k/firefox/langpack-ru.xpi
		ff_ver=$(sed -n 's/^Version=//p' "$ROOT/usr/lib/firefox/application.ini" | head -1)
		if [ "$ff_ver" = "$ru_for" ] &&
			wget -q -O "$ROOT$ru" "https://releases.mozilla.org/pub/firefox/releases/$ru_for/linux-x86_64/xpi/ru.xpi" &&
			echo "$ru_sha  $ROOT$ru" | sha256sum -c - >/dev/null; then
			chmod 644 "$ROOT$ru"
		else
			echo "warning: no Russian language pack for Firefox $ff_ver (pinned $ru_for)" >&2
			rm -f "$ROOT$ru"
			ru=
		fi
		mkdir -p "$ROOT/etc/firefox/policies"
		python3 "$REPO/linux-port/rootfs/firefox/b8k-policies.py" \
			"$ROOT/usr/lib/firefox/distribution/policies.json" \
			"$ROOT/etc/firefox/policies/policies.json" "$ubol" "$ru"
		mc="$ROOT/usr/lib/firefox/mobile-config-autoconfig.js"
		if [ -f "$mc" ] && ! grep -q 'b8k-webapp.js' "$mc"; then
			cat >> "$mc" <<'EOF'
try {
  Components.classes["@mozilla.org/moz/jssubscript-loader;1"]
    .getService(Components.interfaces.mozIJSSubScriptLoader)
    .loadSubScript("file:///usr/local/share/b8k/b8k-webapp.js");
} catch (e) {}
EOF
		fi
	fi
	# Aisleriot for fingers: no menu bar or title, a large toolbar and chooser.
	install -d -m 755 "$ROOT/usr/share/themes/b8k-cards/gtk-3.0"
	install -m 644 "$REPO/linux-port/rootfs/themes/b8k-cards/gtk-3.0/gtk.css" \
		"$ROOT/usr/share/themes/b8k-cards/gtk-3.0/gtk.css"
	install -d -m 755 "$ROOT/usr/share/themes/b8k-galculator/gtk-3.0"
	install -m 644 "$REPO/linux-port/rootfs/themes/b8k-galculator/gtk-3.0/gtk.css" \
		"$ROOT/usr/share/themes/b8k-galculator/gtk-3.0/gtk.css"
	install -d -m 755 "$ROOT/etc/skel/.config/galculator"
	install -m 644 "$REPO/linux-port/rootfs/galculator.conf" \
		"$ROOT/etc/skel/.config/galculator/galculator.conf"
	if [ -d "$ROOT/home/user" ]; then
		install -d -m 755 -o 1000 -g 1000 "$ROOT/home/user/.config/galculator"
		install -m 644 -o 1000 -g 1000 "$REPO/linux-port/rootfs/galculator.conf" \
			"$ROOT/home/user/.config/galculator/galculator.conf"
	fi
	install -d -m 755 "$ROOT/etc/xdg/waybar"
	install -m 644 "$REPO/linux-port/rootfs/waybar/config" "$ROOT/etc/xdg/waybar/config"
	install -m 644 "$REPO/linux-port/rootfs/waybar/style.css" "$ROOT/etc/xdg/waybar/style.css"
	ln -sfn /etc/init.d/b8k-android-system "$ROOT/etc/runlevels/default/b8k-android-system"
	ln -sfn /etc/init.d/b8k-gpu "$ROOT/etc/runlevels/default/b8k-gpu"
	ln -sfn /opt/debian/usr/lib/arm-linux-gnueabihf "$ROOT/usr/lib/arm-linux-gnueabihf"
	ln -sfn /opt/debian/usr/local/hybris "$ROOT/usr/local/hybris"
	install -m 644 "$REPO/linux-port/hybris/phoshdebug" "$ROOT/etc/skel/.phoshdebug"
	install -m 644 "$REPO/linux-port/hybris/phoshdebug" "$ROOT/root/.phoshdebug"
	if [ -d "$ROOT/home/user" ]; then
		install -m 644 -o 1000 -g 1000 "$REPO/linux-port/hybris/phoshdebug" \
			"$ROOT/home/user/.phoshdebug"
	fi

	cat > "$ROOT/etc/local.d/desktop.start" <<'EOF'
#!/bin/sh
export HOME=/root XDG_RUNTIME_DIR=/run/user/0
# simpledrm names the panel "Unknown-1"; fake-builtin makes phoc map the
# touchscreen to it so touch follows screen rotation.
# WLR_RENDERER=pixman applies to the musl /usr/bin/phoc fallback.
# phoc-gpu (selected by ~/.phoshdebug) sets gles2 for the glibc phoc only.
export PHOC_DEBUG=fake-builtin WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 LIBSEAT_BACKEND=seatd
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=Phosh:GNOME
export XDG_SESSION_DESKTOP=phosh LANG=ru_RU.UTF-8
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
# b8k-session opens a PAM session (pam_elogind) so the session has a
# logind session: brightness goes through login1.Session.SetBrightness.
# Restart the session if phoc crashes instead of leaving a dead screen.
# The session user is read from /etc/b8k/session-user on every start (root
# when the file or the user is missing), so switching needs no reboot.
# pipewire-launcher's own cleanup (pkill -u "$USER" -fx) misses under
# busybox: stale audio daemons of the last session would fight the new ones
# over the sound card, so stop them between sessions.
# The loop ignores SIGHUP and is put in the background outside the
# subshell, so it outlives the shell of the local service that starts it.
( trap '' HUP
while :; do
	u=$(cat /etc/b8k/session-user 2>/dev/null)
	id "$u" >/dev/null 2>&1 || u=root
	/usr/local/bin/b8k-session -u "$u" dbus-run-session -- /usr/local/bin/phoc-gpu -C /usr/local/share/b8k/phoc.ini -E /usr/local/bin/b8k-launcher >>/var/log/phosh.log 2>&1
	killall pipewire pipewire-pulse wireplumber 2>/dev/null
	sleep 2
done ) >/dev/null 2>&1 &
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
	mkdir -p "$ROOT/etc/b8k"
	echo user > "$ROOT/etc/b8k/session-user"
	install -d "$ROOT/etc/doas.d"
	echo 'permit persist :wheel' > "$ROOT/etc/doas.d/wheel.conf"
	# z- sorts after wheel.conf. doas uses the last matching rule.
	cat > "$ROOT/etc/doas.d/z-b8k-power.conf" <<'EOF'
permit nopass user cmd /usr/local/sbin/b8k-poweroff args off
permit nopass user cmd /usr/local/sbin/b8k-poweroff args reboot
permit nopass user cmd /usr/local/sbin/b8k-usb args host
permit nopass user cmd /usr/local/sbin/b8k-usb args device
permit nopass user cmd /usr/local/sbin/b8k-rootsh
EOF

	# Kernel modules, GPU module sets and the GPU island come from outside
	# this tree: KERNEL is the built kernel tree (docs/INSTALL.md §0 step 1),
	# GPU_SETS a folder of kN/ sets of the external modules built against
	# each kernel, ISLAND a tar.gz of /opt/debian from a tablet that built it
	# (linux-port/hybris/). Without them the image has no Wi-Fi, no GPU.
	KERNEL=${KERNEL:-/opt/b8000h/linux-drm}
	GPU_SETS=${GPU_SETS:-/opt/b8000h/out/gpu-sets}
	ISLAND=${ISLAND:-/opt/b8000h/out/island.tgz}
	if [ -f "$KERNEL/include/generated/utsrelease.h" ]; then
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
