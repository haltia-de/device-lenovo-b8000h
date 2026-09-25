#!/bin/sh
# Rebuild the RAM-only first-boot image inside the WSL distro B8000Build.
# Run from Windows:
#   wsl.exe -d B8000Build -- sh "/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/build-safe-image.sh"
# Output: artifacts/linux-port/b8000h-linux-firstboot-safe.img (+ SHA256 printed)
set -eu

REPO="/mnt/c/Users/<user>/src/lenovo-b8000h-linux"
K=/opt/b8000h/linux
OUT=/opt/b8000h/out
DST="$REPO/artifacts/linux-port"
DTB=arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h-safe.dtb

mkdir -p "$OUT" "$DST"
cd "$K"

# Kernel tree must contain patches 0001 (DTS) and 0002 (appended DTB).
grep -q '^CONFIG_ARM_APPENDED_DTB=y' .config || {
	echo "CONFIG_ARM_APPENDED_DTB missing: apply patch 0002 and run lenovo-blade_defconfig" >&2
	exit 1
}

make -j4 ARCH=arm LLVM=1 zImage dtbs >/tmp/build-safe.log

# initramfs: busybox-static + linux-port/initramfs/init
cp "$REPO/linux-port/initramfs/init" /opt/b8000h/initramfs/init
chmod 755 /opt/b8000h/initramfs/init
(cd /opt/b8000h/initramfs && find . -print0 | sort -z |
	cpio --null -o --format=newc --owner=0:0 2>/dev/null | gzip -9) \
	> /opt/b8000h/b8000h-safe-initramfs.cpio.gz

cat arch/arm/boot/zImage "$DTB" > "$OUT/kernel-safe-zimage-dtb.bin"
python3 "$REPO/tools/mkmtkimage.py" "$OUT/kernel-safe-zimage-dtb.bin" "$OUT/kernel-safe-mtk.bin" KERNEL

# Two variants: the stock BOOT image labels its ramdisk ROOTFS, the stock
# RECOVERY image labels it RECOVERY (LK checks the label per partition).
for variant in firstboot:ROOTFS recovery:RECOVERY; do
	name=${variant%%:*}
	label=${variant##*:}
	RD="$OUT/ramdisk-$name-mtk.bin"
	python3 "$REPO/tools/mkmtkimage.py" /opt/b8000h/b8000h-safe-initramfs.cpio.gz "$RD" "$label"

	IMG="$OUT/b8000h-linux-$name-safe.img"
	mkbootimg --kernel "$OUT/kernel-safe-mtk.bin" --ramdisk "$RD" \
		--base 0x10000000 --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 \
		--second_offset 0x00f00000 --tags_offset 0x00000100 --pagesize 2048 \
		--board 39 --header_version 0 --output "$IMG"
	# Modern mkbootimg zeroes second_addr when there is no second stage; stock keeps it.
	python3 "$REPO/tools/fix-legacy-boot-header.py" "$IMG" --second-addr 0x10f00000

	# Round-trip check: unpacked payloads must equal the inputs.
	rm -rf /opt/b8000h/unpacked-safe && mkdir /opt/b8000h/unpacked-safe
	unpack_bootimg --boot_img "$IMG" --out /opt/b8000h/unpacked-safe >/dev/null
	cmp "$OUT/kernel-safe-mtk.bin" /opt/b8000h/unpacked-safe/kernel
	cmp "$RD" /opt/b8000h/unpacked-safe/ramdisk
	cp "$IMG" "$DST/"
	sha256sum "$IMG"
done

cp .config "$DST/.config"
cp arch/arm/boot/zImage "$DTB" "$DST/"
cp /opt/b8000h/b8000h-safe-initramfs.cpio.gz "$DST/"
git rev-parse HEAD > "$DST/kernel-commit.txt"
sha256sum "$DTB"
