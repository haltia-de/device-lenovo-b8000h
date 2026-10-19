#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build the root file system and the images ready to flash (ADR 0003),
# from ci/kernel.sh's output in /opt/b8000h/dist (b8000h-boot.img and the
# kernel tarball), in the ci/Dockerfile environment (privileged: chroot
# through qemu-arm), with this repository at /src. Output in
# /opt/b8000h/dist:
#   b8000h-sd-installer.img.xz   the microSD installer (B8KINST1): write it to
#                                a card, boot the tablet with it, and it
#                                installs the system into USRDATA
#   b8000h-root.ext4.xz          the root file system alone
# The island (phoc on the GPU) comes from the release named in
# linux-port/hybris/island.lock, checked against its sha256.
set -eu
OUT=/opt/b8000h/out
DIST=/opt/b8000h/dist
mkdir -p "$OUT" "$DIST"

# wpa_supplicant 2.11 without PMF (the official tarball, sha512 in the script).
[ -f "$OUT/wpa_supplicant-2.11.tar.gz" ] ||
	wget -q -O "$OUT/wpa_supplicant-2.11.tar.gz" https://w1.fi/releases/wpa_supplicant-2.11.tar.gz
sh /src/linux-port/wifi/build-wpa-nm.sh

# The island, as pinned. A release needs it (B8K_REQUIRE_ISLAND=1); a test
# build without one gets a session that draws on the CPU.
island=$(grep -v '^#' /src/linux-port/hybris/island.lock | grep . | head -1 || true)
rm -f "$OUT/island.tgz.pinned"
if [ -n "$island" ]; then
	island_url=${island%% *}
	island_sum=${island##* }
	[ -f "$OUT/island.tgz" ] && echo "$island_sum  $OUT/island.tgz" | sha256sum -c - >/dev/null 2>&1 ||
		wget -q -O "$OUT/island.tgz" "$island_url"
	echo "$island_sum  $OUT/island.tgz" | sha256sum -c -
else
	[ -z "${B8K_REQUIRE_ISLAND:-}" ] || { echo "no island in linux-port/hybris/island.lock" >&2; exit 1; }
	echo "warning: no island pinned, the image draws on the CPU" >&2
	rm -f "$OUT/island.tgz"
fi

# qemu-arm for the chroot steps (the host registered binfmt with the F flag).
mountpoint -q /proc/sys/fs/binfmt_misc || mount -t binfmt_misc none /proc/sys/fs/binfmt_misc
[ -e /proc/sys/fs/binfmt_misc/qemu-arm ] || {
	echo ':qemu-arm:M::\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-arm:F' \
		>/proc/sys/fs/binfmt_misc/register
}

# The root file system: Alpine armv7, the packages of the shell and the
# programs (apps/packages.lock), the kernel's modules, the GPU set, the island.
# The kernel comes as the tarball of ci/kernel.sh (an earlier CI job).
KP=/opt/b8000h/kernel-pkg
rm -rf "$KP"
mkdir -p "$KP"
tar -xzf "$DIST"/b8000h-kernel-*.tar.gz -C "$KP"
KERNEL_PKG=$KP ISLAND=$OUT/island.tgz \
	UI_PROFILE=shell sh /src/linux-port/build-rootfs.sh

# The release: make-release-rootfs.sh takes a tar of a working system; here
# the freshly built one. It leaves no development access, locks root and
# audits the result, then packs the ext4 image and the installer.
tar -C /opt/b8000h/rootfs --numeric-owner -czf "$OUT/rootfs.tar.gz" .
sh /src/linux-port/rootfs/make-release-rootfs.sh "$OUT/rootfs.tar.gz" "$DIST/b8000h-boot.img" \
	"$OUT/release"
rm -f "$OUT/rootfs.tar.gz"
xz -T0 -6 -c "$OUT/release/b8000h-sd-installer.img" >"$DIST/b8000h-sd-installer.img.xz"
xz -T0 -6 -c "$OUT/release/b8000h-root.ext4" >"$DIST/b8000h-root.ext4.xz"
ls -l "$DIST"
