#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build the glibc phoc + libhybris island for the B8000-H (ADR 0003), from
# public sources only: Debian trixie armhf (debootstrap, run through
# qemu-arm), libhybris @ 7079712, the Android 4.4.2 headers extracted from
# AOSP android-4.4.2_r1, phoc 0.55.1 with wlroots 0.20 and our hybris
# patch, and the newer wayland/pixman/libdrm/xkbcommon/wayland-protocols
# from the Debian pool, each checked against its published sha256.
# Then trim it to what phoc uses (docs/INSTALL.md §0 step 8) and pack it:
#   /opt/b8000h/dist/b8000h-island.tar.gz  (unpacks to debian/ = /opt/debian)
# Runs as root in the ci/Dockerfile environment, privileged (chroot, mounts,
# binfmt), with this repository at /src. Hours under emulation.
set -eu

# Abort a transfer that stays under 1 KiB/s. sources.phosh.mobi accepts the
# connection and then sends nothing; curl would otherwise wait forever.
export GIT_HTTP_LOW_SPEED_LIMIT=1000
export GIT_HTTP_LOW_SPEED_TIME=120

G=/opt/b8000h/g5
ROOT=$G/debian
SRC=$G/src
AOSP=$G/aosp
REPO=/src
JOBS=$(nproc)

log() { echo "b8k-island: $*"; }

apk add --no-cache debootstrap debian-archive-keyring git ca-certificates curl tar xz python3 wget
# BusyBox wget gives up after one "connection reset" from deb.debian.org.
# GNU wget retries. debootstrap calls whichever wget is on PATH.
printf '%s\n' 'tries = 20' 'retry_connrefused = on' 'read_timeout = 30' > /etc/wgetrc

if [ ! -e /usr/share/debootstrap/scripts/trixie ]; then
	log "debootstrap has no trixie script, linking sid"
	ln -s sid /usr/share/debootstrap/scripts/trixie
fi

mkdir -p /proc/sys/fs/binfmt_misc "$SRC" "$AOSP"
mountpoint -q /proc/sys/fs/binfmt_misc || mount -t binfmt_misc none /proc/sys/fs/binfmt_misc

mkdir -p "$SRC/g5"
cp "$REPO/linux-port/hybris/wlroots-0.20-hybris.patch" "$SRC/g5/"
cp "$REPO/linux-port/hybris/tlspad.c" "$SRC/tlspad.c"
cp "$REPO/linux-port/hybris/build-hybris.sh" "$SRC/build-hybris.sh"
cp "$REPO/linux-port/hybris/build-phoc.sh" "$SRC/build-phoc.sh"

fetch() {
	url=$1 dest=$2
	if [ -f "$dest" ]; then
		log "have $(basename "$dest")"
		return 0
	fi
	log "get $url"
	curl -fL --retry 5 --retry-delay 2 --connect-timeout 20 \
		--speed-time 120 --speed-limit 1024 -o "$dest.part" "$url"
	mv "$dest.part" "$dest"
}

log "sources"
fetch https://sources.phosh.mobi/releases/phoc/phoc-0.55.1.tar.xz "$SRC/g5/phoc-0.55.1.tar.xz"
fetch https://sources.phosh.mobi/releases/phoc/phoc-0.55.1.tar.xz.sha256sum "$SRC/g5/phoc-0.55.1.tar.xz.sha256sum"
fetch https://sources.phosh.mobi/releases/gmobile/gmobile-0.6.0.tar.xz "$SRC/g5/gmobile-0.6.0.tar.xz"
fetch https://sources.phosh.mobi/releases/gmobile/gmobile-0.6.0.tar.xz.sha256sum "$SRC/g5/gmobile-0.6.0.tar.xz.sha256sum"
( cd "$SRC/g5" && sha256sum -c phoc-0.55.1.tar.xz.sha256sum && sha256sum -c gmobile-0.6.0.tar.xz.sha256sum )

fetch_deb() {
	name=$1 path=$2
	fetch "https://deb.debian.org/debian/pool/main/$path" "$SRC/g5/$name"
	fetch "https://deb.debian.org/debian/pool/main/${path%.tar.*}.dsc" "$SRC/g5/${name}.dsc" || true
}

# dsc sits next to the orig tarball; the filename in the pool is the orig name.
fetch_orig() {
	orig=$1 pool=$2
	fetch "https://deb.debian.org/debian/pool/main/$pool/$orig" "$SRC/g5/$orig"
	dsc=${orig%.orig.tar.*}.dsc
	# versioned dsc names differ; caller passes the exact dsc.
}

fetch "https://deb.debian.org/debian/pool/main/w/wayland/wayland_1.26.0.orig.tar.xz" "$SRC/g5/wayland_1.26.0.orig.tar.xz"
fetch "https://deb.debian.org/debian/pool/main/w/wayland/wayland_1.26.0-1.dsc" "$SRC/g5/wayland.dsc"
fetch "https://deb.debian.org/debian/pool/main/p/pixman/pixman_0.46.4.orig.tar.gz" "$SRC/g5/pixman_0.46.4.orig.tar.gz"
fetch "https://deb.debian.org/debian/pool/main/p/pixman/pixman_0.46.4-1.dsc" "$SRC/g5/pixman.dsc"
fetch "https://deb.debian.org/debian/pool/main/libd/libdrm/libdrm_2.4.134.orig.tar.xz" "$SRC/g5/libdrm_2.4.134.orig.tar.xz"
fetch "https://deb.debian.org/debian/pool/main/libd/libdrm/libdrm_2.4.134-1.dsc" "$SRC/g5/libdrm.dsc"
fetch "https://deb.debian.org/debian/pool/main/libx/libxkbcommon/libxkbcommon_1.13.1.orig.tar.gz" "$SRC/g5/libxkbcommon_1.13.1.orig.tar.gz"
fetch "https://deb.debian.org/debian/pool/main/libx/libxkbcommon/libxkbcommon_1.13.1-1.dsc" "$SRC/g5/libxkbcommon.dsc"
fetch "https://deb.debian.org/debian/pool/main/w/wayland-protocols/wayland-protocols_1.49.orig.tar.xz" "$SRC/g5/wayland-protocols_1.49.orig.tar.xz"
fetch "https://deb.debian.org/debian/pool/main/w/wayland-protocols/wayland-protocols_1.49-1.dsc" "$SRC/g5/wayland-protocols.dsc"

python3 - <<'PY'
import hashlib, pathlib, sys
root = pathlib.Path("/opt/b8000h/g5/src/g5")
pairs = [
    ("wayland.dsc", "wayland_1.26.0.orig.tar.xz"),
    ("pixman.dsc", "pixman_0.46.4.orig.tar.gz"),
    ("libdrm.dsc", "libdrm_2.4.134.orig.tar.xz"),
    ("libxkbcommon.dsc", "libxkbcommon_1.13.1.orig.tar.gz"),
    ("wayland-protocols.dsc", "wayland-protocols_1.49.orig.tar.xz"),
]
for dsc_name, orig in pairs:
    text = (root / dsc_name).read_text(errors="replace")
    want = None
    on = False
    for line in text.splitlines():
        if line.startswith("Checksums-Sha256:"):
            on = True
            continue
        if on and line and not line.startswith(" "):
            on = False
        if on and orig in line.split():
            want = line.split()[0]
    if not want:
        sys.exit(f"no sha256 for {orig} in {dsc_name}")
    got = hashlib.sha256((root / orig).read_bytes()).hexdigest()
    if got != want:
        sys.exit(f"sha256 mismatch {orig}\n want {want}\n got  {got}")
    print("sha256 ok", orig)
PY

if [ ! -d "$SRC/libhybris/hybris" ]; then
	fetch "https://github.com/libhybris/libhybris/archive/7079712a42ea2754adf747e70c6cc75764c8596e.tar.gz" \
		"$G/libhybris-7079712.tar.gz"
	rm -rf "$G/libhybris-src"
	tar -C "$G" -xzf "$G/libhybris-7079712.tar.gz"
	mv "$G"/libhybris-7079712a42ea2754adf747e70c6cc75764c8596e "$SRC/libhybris"
fi
test -f "$SRC/libhybris/hybris/autogen.sh"
log "libhybris $(cat "$SRC/libhybris/hybris/../.git" 2>/dev/null || echo 7079712a42ea2754adf747e70c6cc75764c8596e)"

clone_aosp() {
	url=$1 dest=$2
	if [ -d "$dest/.git" ] || [ -f "$dest/MODULE_LICENSE_APACHE2" ] || [ -d "$dest/include" ]; then
		log "have aosp $(basename "$dest")"
		return 0
	fi
	rm -rf "$dest"
	mkdir -p "$(dirname "$dest")"
	log "clone $url"
	git clone --depth 1 --branch android-4.4.2_r1 "$url" "$dest"
}

clone_aosp https://android.googlesource.com/platform/hardware/libhardware "$AOSP/hardware/libhardware"
clone_aosp https://android.googlesource.com/platform/hardware/libhardware_legacy "$AOSP/hardware/libhardware_legacy"
clone_aosp https://android.googlesource.com/platform/system/core "$AOSP/system/core"
clone_aosp https://android.googlesource.com/platform/system/media "$AOSP/system/media"
clone_aosp https://android.googlesource.com/platform/bionic "$AOSP/bionic"
clone_aosp https://android.googlesource.com/platform/external/kernel-headers "$AOSP/external/kernel-headers"

if [ ! -f "$SRC/android-headers-19/cutils/native_handle.h" ]; then
	log "extract android 4.4.2 headers"
	rm -rf "$SRC/android-headers-19"
	sh "$SRC/libhybris/utils/extract-headers.sh" -v 4.4.2 "$AOSP" "$SRC/android-headers-19"
fi
test -f "$SRC/android-headers-19/hardware/gralloc.h"

log "debootstrap"
if [ ! -x "$ROOT/bin/bash" ]; then
	# A failed download leaves ROOT/debootstrap without /bin/bash. Continuing
	# that tree skips the missing packages, so start the foreign stage over.
	ok=0
	try=1
	while [ "$try" -le 4 ]; do
		log "debootstrap try $try"
		rm -rf "$ROOT"
		if debootstrap --arch=armhf --foreign --variant=minbase \
			trixie "$ROOT" https://deb.debian.org/debian; then
			ok=1
			break
		fi
		log "debootstrap try $try failed"
		try=$((try + 1))
		sleep 5
	done
	test "$ok" = 1
	if ! chroot "$ROOT" /bin/sh -c 'exit 0' 2>/dev/null; then
		log "register qemu-arm binfmt against this container"
		if [ -e /proc/sys/fs/binfmt_misc/qemu-arm ]; then
			echo -1 > /proc/sys/fs/binfmt_misc/qemu-arm || true
		fi
		echo ':qemu-arm:M::\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-arm:F' \
			> /proc/sys/fs/binfmt_misc/register
	fi
	chroot "$ROOT" /bin/sh -c 'exit 0'
	chroot "$ROOT" /debootstrap/debootstrap --second-stage
fi
test -x "$ROOT/bin/bash"
test -x "$ROOT/usr/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3" || \
	ls "$ROOT/usr/lib/arm-linux-gnueabihf" | head

printf '%s\n' \
	'deb https://deb.debian.org/debian trixie main' \
	'deb https://deb.debian.org/debian trixie-updates main' \
	'deb https://deb.debian.org/debian-security trixie-security main' \
	> "$ROOT/etc/apt/sources.list"
cp /etc/resolv.conf "$ROOT/etc/resolv.conf"

mkdir -p "$ROOT/root/src" "$ROOT/proc" "$ROOT/sys" "$ROOT/dev/pts" "$ROOT/tmp"
mountpoint -q "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
mountpoint -q "$ROOT/sys" || mount --rbind /sys "$ROOT/sys"
mountpoint -q "$ROOT/dev" || mount --rbind /dev "$ROOT/dev"
mountpoint -q "$ROOT/dev/pts" || mount --bind /dev/pts "$ROOT/dev/pts"
mountpoint -q "$ROOT/root/src" || mount --bind "$SRC" "$ROOT/root/src"

cleanup() {
	umount "$ROOT/root/src" "$ROOT/dev/pts" "$ROOT/dev" "$ROOT/proc" "$ROOT/sys" 2>/dev/null || true
}
trap cleanup EXIT

log "build libhybris and phoc inside the armhf chroot"
chroot "$ROOT" /usr/bin/env -i HOME=/root TERM=linux \
	PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
	LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive JOBS="$JOBS" \
	/bin/sh -eu -c '
		apt-get update
		apt-get install -y --no-install-recommends \
			build-essential autoconf automake libtool pkg-config \
			ca-certificates python3
		if [ ! -e /usr/local/hybris/lib/libhybris-common.so ]; then
			sh /root/src/build-hybris.sh
		fi
		gcc -O2 -shared -fPIC -o /root/src/libtlspad.so /root/src/tlspad.c
		if [ ! -x /usr/local/bin/phoc ]; then
			sh /root/src/build-phoc.sh
		fi
		readelf -lW /usr/local/bin/phoc
	'

# phoc must have an interpreter and no TLS segment of its own, or libtlspad
# cannot cover the bionic GL slots.
if readelf -lW "$ROOT/usr/local/bin/phoc" | awk '$1=="TLS"{found=1} END{exit !found}'; then
	log "FAIL phoc has its own TLS segment"
	exit 1
fi
test -f "$ROOT/usr/local/hybris/lib/libEGL.so.1" || test -f "$ROOT/usr/local/hybris/lib/libEGL.so"
test -f "$ROOT/usr/local/hybris/lib/libGLESv2.so" || test -f "$ROOT/usr/local/hybris/lib/libGLESv2.so.2"
test -f "$ROOT/usr/local/hybris/lib/libtlspad.so"
test -x "$ROOT/usr/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3"

rm -rf "$ROOT/var/cache/apt/archives/"*.deb
log "ISLAND_OK"
du -sh "$ROOT" || true

# --- trim: keep the packages phoc's libraries come from -------------------
log "trim"
inside() {
	chroot "$ROOT" /usr/bin/env -i HOME=/root TERM=linux \
		PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
		LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive "$@"
}
inside /usr/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3 \
	--library-path /usr/local/hybris/lib:/usr/local/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf \
	--list /usr/local/bin/phoc | awk '$3 ~ "^/usr/lib" {print $3}' >"$ROOT/tmp/phoc-libs"
test -s "$ROOT/tmp/phoc-libs"
inside /bin/sh -eu -c '
	apt-mark showmanual > /root/manual.orig
	apt-mark auto $(cat /root/manual.orig) >/dev/null
	apt-mark manual apt $(while read f; do dpkg -S "$f" | cut -d: -f1; done < /tmp/phoc-libs | sort -u) >/dev/null
	apt-get -y -o APT::AutoRemove::RecommendsImportant=false \
		-o APT::AutoRemove::SuggestsImportant=false autoremove --purge
	apt-get clean
	rm -rf /var/lib/apt/lists/* /tmp/phoc-libs
'
cleanup
trap - EXIT
rm -rf "$ROOT/root/src" "$ROOT/etc/resolv.conf"
mkdir -p "$ROOT/root/src"
# The trimmed island must still start phoc's dynamic linking.
inside /usr/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3 \
	--library-path /usr/local/hybris/lib:/usr/local/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf \
	--list /usr/local/bin/phoc | grep -q "not found" && { log "FAIL a library of phoc is gone"; exit 1; }

mkdir -p /opt/b8000h/dist
tar -C "$G" --numeric-owner -czf /opt/b8000h/dist/b8000h-island.tar.gz debian
sha256sum /opt/b8000h/dist/b8000h-island.tar.gz
du -sh "$ROOT"
log "ISLAND_PACKED"
