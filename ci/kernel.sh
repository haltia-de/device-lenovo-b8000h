#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build the kernel, its modules, the GPU module set and boot.img (ADR 0003),
# in the ci/Dockerfile environment, with this repository at /src and work
# space at /opt/b8000h. Output in /opt/b8000h/dist:
#   b8000h-boot.img          kernel + install2 DTB + the installer initramfs
#                            (fastboot flash boot)
#   b8000h-kernel-<rel>.tar.gz   zImage, the DTB, .config, /lib/modules/<rel>
#                            and usr/lib/b8k/kN (the GPU module set): a kernel
#                            update without reinstalling the system
# and in /opt/b8000h/out/gpu-sets/kN the set the root file system takes.
set -eu
K=/opt/b8000h/linux-drm
OUT=/opt/b8000h/out
DIST=/opt/b8000h/dist
J=${JOBS:-$(nproc)}
mkdir -p "$OUT" "$DIST"

sh /src/ci/sources.sh
sh /src/ci/apply-patches.sh

cd "$K"
make -j"$J" ARCH=arm LLVM=1 zImage dtbs modules
rel=$(make -s ARCH=arm LLVM=1 kernelrelease)
build=$(echo "$rel" | sed -n 's/.*-b8000h\([0-9][0-9]*\).*/\1/p')
[ -n "$build" ] || { echo "no -b8000hN in $rel" >&2; exit 1; }
echo "KERNEL $rel (GPU set k$build)"

# The external modules: PowerVR (pvrsrvkm), its display class (dc_nohw,
# against pvrsrvkm's symbols) and the legacy ion bridge.
D=/src/linux-port/drivers
KERNEL_DIR=$K sh "$D/pvrsrvkm/build.sh"
KERNEL_DIR=$K sh "$D/dc_nohw/build-dc-nohw.sh"
KERNEL_DIR=$K sh "$D/ion-legacy/build-ion-legacy.sh"
set_dir=$OUT/gpu-sets/k$build
rm -rf "$set_dir"
mkdir -p "$set_dir"
for m in pvrsrvkm dc_nohw ion_legacy; do
	f=$(find "$D" -name "$m.ko" -newer "$K/vmlinux" | head -1)
	[ -n "$f" ] || { echo "$m.ko not built" >&2; exit 1; }
	install -m 644 "$f" "$set_dir/$m.ko"
done

# The initramfs base: Alpine's static busybox for armv7.
I=/opt/b8000h/initramfs
if [ ! -x "$I/bin/busybox" ]; then
	tmp=$(mktemp -d)
	# Signed by Alpine: the index and the package are checked against
	# Alpine's armv7 keys.
	apk fetch -q --arch armv7 --repository https://dl-cdn.alpinelinux.org/alpine/v3.24/main \
		--keys-dir /usr/share/apk/keys/armv7 -o "$tmp" busybox-static
	apk verify --keys-dir /usr/share/apk/keys/armv7 "$tmp"/busybox-static-*.apk
	tar -xzf "$tmp"/busybox-static-*.apk -C "$tmp" bin/busybox.static 2>/dev/null
	install -D -m 755 "$tmp/bin/busybox.static" "$I/bin/busybox"
	rm -rf "$tmp"
fi

# boot.img: the installer's (install2 DTB: SD as mmcblk0, eMMC mmcblk1).
K=$K DTB_VARIANT=install2 IMAGE_SUFFIX=-ci sh /src/linux-port/build-safe-image.sh
cp "$OUT/b8000h-linux-firstboot-ci-safe.img" "$DIST/b8000h-boot.img"

# The kernel package: zImage, DTB, config, modules, GPU set.
P=/opt/b8000h/kernel-pkg
rm -rf "$P"
mkdir -p "$P/boot" "$P/usr/lib/b8k"
cp arch/arm/boot/zImage arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h-install2.dtb "$P/boot/"
cp .config "$P/boot/config-$rel"
make ARCH=arm LLVM=1 modules_install INSTALL_MOD_PATH="$P" >/dev/null
rm -f "$P/lib/modules/$rel/build" "$P/lib/modules/$rel/source"
cp -a "$set_dir" "$P/usr/lib/b8k/"
tar -C "$P" --owner=0 --group=0 -czf "$DIST/b8000h-kernel-$rel.tar.gz" .
echo "$rel" >"$DIST/kernel-release"
ls -l "$DIST"
