#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build the release root file system from a full tar of a working B8000-H
# (tar czf - --one-file-system / on the tablet): strip everything personal
# and every Android binary, move the GPU modules out of /root, give the
# system a fresh first-boot user, then pack the ext4 image and the microSD
# installer image (tools/mk-sd-installer.py, B8KINST1).
#
# Run inside WSL B8000Build as root:
#   sh linux-port/rootfs/make-release-rootfs.sh ROOTFS.tar.gz BOOT.img OUTDIR [AUDIT-PATTERNS]
# AUDIT-PATTERNS: file with one string per line that must not occur anywhere
# in the result (names, mail addresses, home network...).  Wi-Fi SSIDs and
# keys of the source tablet are added automatically.
set -eu

TAR=$1
BOOT=$2
OUT=$3
AUDIT=${4:-}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
R=/opt/b8000h/release-root
K=k22		# GPU module set (kernel build 22)

PAT=$(mktemp)	# audit patterns, removed at exit
trap 'rm -f "$PAT"' EXIT

rm -rf "$R"
mkdir -p "$R" "$OUT"
tar -xpzf "$TAR" -C "$R" --numeric-owner
# Mount points left out of the tablet's tar.
install -d -m 755 "$R/proc" "$R/sys" "$R/dev" "$R/run" "$R/mnt" "$R/system"
install -d -m 1777 "$R/tmp" "$R/var/tmp"

# Secrets of the source tablet, collected before they are deleted, for the
# final audit.
cat "$R"/etc/NetworkManager/system-connections/* 2>/dev/null |
	sed -n 's/^\(ssid\|psk\|password\)=//p' | sort -u > "$PAT"
[ -z "$AUDIT" ] || grep -v '^$' "$AUDIT" >> "$PAT"

# --- GPU files that live in /root on the development tablet -------------
install -d "$R/usr/lib/b8k"
cp -a "$R/root/$K" "$R/usr/lib/b8k/$K"
rm -f "$R/usr/lib/b8k/$K"/*.tar.gz
install -m 755 "$REPO/linux-port/gpu/pvr-run.sh" "$R/usr/lib/b8k/pvr-run.sh"
install -m 755 "$REPO/linux-port/gpu/b8k-gpu.initd" "$R/etc/init.d/b8k-gpu"
install -m 755 "$REPO/linux-port/rootfs/b8k-android-system.initd" \
	"$R/etc/init.d/b8k-android-system"
install -d -m 755 "$R/usr/local/bin"
install -m 755 "$REPO/linux-port/hybris/phoc-gpu.sh" "$R/usr/local/bin/phoc-gpu"
ln -sf /etc/init.d/b8k-android-system "$R/etc/runlevels/default/b8k-android-system"
ln -sf /etc/init.d/b8k-gpu "$R/etc/runlevels/default/b8k-gpu"
install -d -m 755 "$R/etc/NetworkManager/conf.d"
install -m 644 "$REPO/linux-port/wifi/wifi-mt6628.conf" \
	"$R/etc/NetworkManager/conf.d/wifi-mt6628.conf"
rm -f "$R/etc/NetworkManager/conf.d/unmanage-wlan0.conf" \
	"$R/etc/local.d/10-wifi-glider.start" \
	"$R/etc/runlevels/default/wpa_supplicant" \
	"$R/etc/runlevels/boot/wpa_supplicant"
if [ -x "$R/sbin/wpa_supplicant" ] &&
   strings "$R/sbin/wpa_supplicant" | grep -qx pmf; then
	echo "wpa_supplicant still advertises pmf; rebuild linux-port/wifi/build-wpa-nm.sh" >&2
	exit 1
fi
cat > "$R/etc/local.d/00-b8k-modules.start" <<'EOF'
#!/bin/sh
i=0
while [ ! -e /system/etc/firmware/WMT.cfg ] && [ "$i" -lt 30 ]; do
	i=$((i + 1))
	sleep 1
done
rmmod wlan_mt6628 2>/dev/null || true
rmmod wmt 2>/dev/null || true
modprobe wmt
modprobe wlan_mt6628
EOF
chmod 755 "$R/etc/local.d/00-b8k-modules.start"

# --- personal data -------------------------------------------------------
find "$R/root" -mindepth 1 -delete
rm -rf "$R/home/user"
install -d -m 750 -o 1000 -g 1000 "$R/home/user"
install -m 644 -o 1000 -g 1000 "$REPO/linux-port/hybris/phoshdebug" "$R/home/user/.phoshdebug"
find "$R/etc/NetworkManager/system-connections" -type f -delete
rm -rf "$R"/var/lib/NetworkManager/* "$R"/var/lib/upower/* "$R"/var/lib/bluetooth/*
rm -f "$R"/etc/ssh/ssh_host_* "$R/etc/machine-id" "$R/var/lib/dbus/machine-id"
: > "$R/etc/resolv.conf"
rm -f "$R/etc/b8k/wayvnc.conf" "$R/etc/b8k/cpufreq-before-stability"
find "$R" \( -name .ash_history -o -name .bash_history -o -name .python_history \
	-o -name .lesshst -o -name .viminfo -o -name .wget-hsts \) -type f -delete
find "$R/var/log" -type f -delete
rm -rf "$R"/var/cache/apk/* "$R"/var/tmp/* "$R"/tmp/* "$R/b8k-installed"

# --- Android binaries: the tablet's own system partition provides them ---
find "$R/system" -mindepth 1 -delete
[ -L "$R/vendor" ] || { rm -rf "$R/vendor"; ln -s /system/vendor "$R/vendor"; }
for f in WIFI_RAM_CODE_MT6628 WMT.cfg mt6628_ant_m1.cfg mt6628_patch_e1_hdr.bin \
		mt6628_patch_e2_0_hdr.bin mt6628_patch_e2_1_hdr.bin; do
	rm -f "$R/lib/firmware/$f"
	ln -s "/system/etc/firmware/$f" "$R/lib/firmware/$f"
done

# --- development leftovers and remote access ----------------------------
sed -i '/acm-shell/d' "$R/etc/inittab"
rm -f "$R/etc/runlevels/default/b8k-wayvnc"
rm -f "$R"/usr/local/bin/*.bak* "$R/usr/local/lib/b8k-nonroot-uid.so" \
	"$R"/etc/local.d/*.before-* "$R"/etc/local.d/*.bak*
[ -e "$R/etc/local.d/resize-emmc-root.done" ] &&
	mv "$R/etc/local.d/resize-emmc-root.done" "$R/etc/local.d/resize-emmc-root.start"
# GPU island: runtime only (no build tree, toolchain, headers, docs).
rm -rf "$R"/opt/debian/root/* "$R"/opt/debian/tmp/* \
	"$R"/opt/debian/var/cache/apt/* "$R"/opt/debian/var/lib/apt/lists/* \
	"$R"/opt/debian/usr/share/doc "$R"/opt/debian/usr/share/man \
	"$R"/opt/debian/usr/share/info "$R"/opt/debian/usr/include \
	"$R"/opt/debian/usr/local/include "$R"/opt/debian/usr/lib/gcc
find "$R/opt/debian" -name '*.a' -type f -delete
: > "$R/opt/debian/etc/resolv.conf"

# --- accounts: root locked, admin through doas for wheel ----------------
sed -i 's/^root:[^:]*:/root:*:/' "$R/etc/shadow"
grep -q '^user::' "$R/etc/shadow" || { echo "user must have an empty password" >&2; exit 1; }
[ -e /proc/sys/fs/binfmt_misc/qemu-arm ] ||
	{ echo "register qemu-arm binfmt first (see linux-port/build-rootfs.sh)" >&2; exit 1; }
apk --root "$R" --arch armv7 --no-cache del -q gdb 2>/dev/null || true
apk --root "$R" --arch armv7 --no-cache add -q doas
install -d "$R/etc/doas.d"
echo 'permit persist :wheel' > "$R/etc/doas.d/wheel.conf"
rm -rf "$R"/var/cache/apk/*

# --- audit ---------------------------------------------------------------
bad=0
sed -i '/^$/d' "$PAT"
if [ -s "$PAT" ]; then
	hits=$(grep -rlF -f "$PAT" "$R" 2>/dev/null || true)
	[ -z "$hits" ] || { echo "AUDIT: forbidden strings in:" >&2; echo "$hits" >&2; bad=1; }
fi
blobs=$(find "$R" -path "$R/proc" -prune -o \( -name 'libsrv_um*.so' -o -name 'pvrsrvctl' \
	-o -name 'libEGL_POWERVR*' -o -name 'gralloc.mt6589.so' -o -name 'WIFI_RAM_CODE*' \) \
	-type f -print)
[ -z "$blobs" ] || { echo "AUDIT: Android binaries left: $blobs" >&2; bad=1; }
[ $bad = 0 ] || { echo "release root NOT clean" >&2; exit 3; }

# --- images --------------------------------------------------------------
used=$(du -sm "$R" | cut -f1)
size=$(( (used * 112 / 100 + 63) / 64 * 64 ))
IMG="$OUT/b8000h-root.ext4"
rm -f "$IMG"
truncate -s ${size}M "$IMG"
mkfs.ext4 -q -F -L B8000ROOT -d "$R" "$IMG"
e2fsck -fn "$IMG" >/dev/null
python3 "$REPO/tools/mk-sd-installer.py" "$IMG" "$BOOT" "$OUT/b8000h-sd-installer.img"
echo "root ${used} MiB used, image ${size} MiB"
ls -l "$OUT"
