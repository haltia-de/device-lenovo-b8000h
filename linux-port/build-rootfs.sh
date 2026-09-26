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
	weston weston-backend-drm weston-shell-desktop weston-terminal \
	weston-clients wvkbd font-dejavu \
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

# The GUI starts from OpenRC local.d.
mkdir -p "$ROOT/etc/xdg/weston"
cat > "$ROOT/etc/xdg/weston/weston.ini" <<'EOF'
[core]
renderer=pixman
idle-time=0

[shell]
panel-position=top
locking=false

[launcher]
icon=/usr/share/weston/terminal.png
path=/usr/bin/weston-terminal

[launcher]
icon=/usr/share/weston/icon_flower.png
path=/usr/bin/wvkbd-mobintl -L 300
EOF
cat > "$ROOT/etc/local.d/weston.start" <<'EOF'
#!/bin/sh
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
( weston --backend=drm --renderer=pixman --log=/var/log/weston.log & ) >/dev/null 2>&1
EOF
chmod +x "$ROOT/etc/local.d/weston.start"

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
