#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

kernel_dir=${KERNEL_DIR:-/opt/b8000h/linux-drm}
module_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$module_dir/../../.." && pwd)
vendor_dir="$repo_root/external/mt6589-pvr-ddk-1.12/mediatek/platform/mt6589/kernel/drivers/gpu/pvr"
modern_dc_dir="$repo_root/external/linux_openpvrsgx/drivers/gpu/drm/pvrsgx/generic/services4/3rdparty/dc_nohw"
pvr_module_dir="$repo_root/linux-port/drivers/pvrsrvkm"
pvr_symvers=${PVR_SYMVERS:-/opt/b8000h/pvrsrvkm-kbuild/drivers/gpu/pvr/Module.symvers}
stage_root=${MODULE_BUILD_ROOT:-/opt/b8000h/dc-nohw-kbuild}
stage_dir="$stage_root/drivers/gpu/pvr"
artifact_dir="$repo_root/artifacts/linux-port/pvrsrvkm"

test -s "$kernel_dir/Module.symvers" || {
	echo "error: $kernel_dir/Module.symvers is missing or empty" >&2
	exit 1
}
test -f "$vendor_dir/include4/pvrversion.h" || {
	echo "error: exact MT6589 DDK 1.12 source is missing: $vendor_dir" >&2
	exit 1
}
test -f "$modern_dc_dir/dc_nohw_displayclass.c" || {
	echo "error: Linux 7 dc_nohw source is missing: $modern_dc_dir" >&2
	exit 1
}
test -s "$pvr_symvers" || {
	echo "error: pvrsrvkm Module.symvers is missing or empty; build pvrsrvkm first" >&2
	exit 1
}
grep -q '[[:space:]]PVRGetDisplayClassJTable[[:space:]]' "$pvr_symvers" || {
	echo "error: pvrsrvkm does not export PVRGetDisplayClassJTable" >&2
	exit 1
}

case "$stage_root" in
	/opt/b8000h/dc-nohw-kbuild|/opt/b8000h/dc-nohw-kbuild-*) ;;
	*) echo "error: refusing to clean unexpected staging path: $stage_root" >&2; exit 1 ;;
esac

mkdir -p "$stage_dir"
find "$stage_root" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
mkdir -p "$stage_dir/services4/3rdparty"
cp -a "$vendor_dir"/. "$stage_dir"/
rm -rf "$stage_dir/services4/3rdparty/dc_nohw"
cp -a "$modern_dc_dir" "$stage_dir/services4/3rdparty/dc_nohw"
cp "$module_dir/Makefile" "$stage_dir/Makefile"
cp -a "$pvr_module_dir/overrides/compat" "$stage_dir/compat"

find "$stage_dir/services4/3rdparty/dc_nohw" -type f \
	\( -name '*.c' -o -name '*.h' \) -exec sed -i 's/\r$//' {} +
for patch_file in "$module_dir"/patches/*.patch; do
	[ -e "$patch_file" ] || continue
	patch -d "$stage_dir" -p1 --forward --batch < "$patch_file"
done

make -C "$kernel_dir" \
	M="$stage_dir" \
	ARCH=arm LLVM=1 \
	CONFIG_PVR_BUILD_RELEASE_MT6589=y \
	CONFIG_PVR_SGX_MT6589=m \
	TARGET_BUILD_VARIANT=user \
	SUPPORT_ION=1 \
	MTK_PATH_PLATFORM="$stage_root" \
	KBUILD_EXTRA_SYMBOLS="$pvr_symvers" \
	KCFLAGS="-I$stage_dir/compat -include $stage_dir/compat/pvr_compat.h -DPVR_LDM_DEVICE_TREE -DPVR_LINUX_DYNAMIC_SGX_RESOURCE_INFO -Wno-declaration-after-statement" \
	modules

KERNEL_SYMVERS="$kernel_dir/Module.symvers" PVR_SYMVERS="$pvr_symvers" \
	sh "$module_dir/verify-symbols.sh" "$stage_dir/dc_nohw.ko"

cp "$stage_dir/dc_nohw.ko" "$module_dir/dc_nohw.ko"

mkdir -p "$artifact_dir"
cp "$pvr_module_dir/pvrsrvkm.ko" "$artifact_dir/pvrsrvkm.ko"
cp "$module_dir/dc_nohw.ko" "$artifact_dir/dc_nohw.ko"
cp "$artifact_dir/pvrsrvkm.ko" "$artifact_dir/pvrsrvkm-stripped.ko"
cp "$artifact_dir/dc_nohw.ko" "$artifact_dir/dc_nohw-stripped.ko"
llvm-strip --strip-debug "$artifact_dir/pvrsrvkm-stripped.ko"
llvm-strip --strip-debug "$artifact_dir/dc_nohw-stripped.ko"
gzip -9 -n -c "$artifact_dir/pvrsrvkm.ko" > "$artifact_dir/pvrsrvkm.ko.gz"
gzip -9 -n -c "$artifact_dir/pvrsrvkm-stripped.ko" > "$artifact_dir/pvrsrvkm-stripped.ko.gz"
gzip -9 -n -c "$artifact_dir/dc_nohw.ko" > "$artifact_dir/dc_nohw.ko.gz"
gzip -9 -n -c "$artifact_dir/dc_nohw-stripped.ko" > "$artifact_dir/dc_nohw-stripped.ko.gz"
