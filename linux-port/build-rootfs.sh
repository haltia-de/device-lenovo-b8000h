#!/bin/sh
# Build an Alpine armv7 root filesystem with a lightweight touch GUI
# (Weston, pixman renderer on simpledrm) for the B8000-H microSD card.
# Runs inside WSL B8000Build. Output: artifacts/linux-port/b8000h-sd.img
set -eu

REPO="/mnt/c/Users/<user>/src/lenovo-b8000h-linux"
ROOT=/opt/b8000h/rootfs
IMG=/opt/b8000h/out/b8000h-sd.img
MIRROR=https://dl-cdn.alpinelinux.org/alpine/v3.24
IMG_MB=3072

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
	labwc waybar fuzzel foot wvkbd swaybg pcmanfm \
	adwaita-icon-theme font-dejavu font-noto xdg-utils \
	bash htop nano e2fsprogs

# Basic system configuration.
echo b8000h > "$ROOT/etc/hostname"
printf 'root::0:0:root:/root:/bin/sh\n' > /tmp/root-pw
sed -i 's|^root:[^:]*:|root::|' "$ROOT/etc/shadow"
cat > "$ROOT/etc/fstab" <<'EOF'
/dev/mmcblk0p1	/	ext4	rw,noatime	0 1
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
for s in mount-ro killprocs savecache; do
	ln -sf /etc/init.d/$s "$ROOT/etc/runlevels/shutdown/$s"
done

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

# Start the desktop from OpenRC local.d as root on seat0.
rm -f "$ROOT/etc/local.d/weston.start"
cat > "$ROOT/etc/local.d/desktop.start" <<'EOF'
#!/bin/sh
export HOME=/root XDG_RUNTIME_DIR=/run/user/0
export WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 LIBSEAT_BACKEND=seatd
export XDG_CURRENT_DESKTOP=labwc LANG=ru_RU.UTF-8
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
( labwc >/var/log/labwc.log 2>&1 & ) >/dev/null 2>&1
EOF
chmod +x "$ROOT/etc/local.d/desktop.start"

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
cp "$IMG" "$REPO/artifacts/linux-port/"
du -sh "$ROOT"
sha256sum "$IMG"
