#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

kernel_dir=${KERNEL_DIR:-/opt/b8000h/linux-drm}
module_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$module_dir/../../.." && pwd)
vendor_dir="$repo_root/external/mt6589-pvr-ddk-1.12/mediatek/platform/mt6589/kernel/drivers/gpu/pvr"
modern_dir="$repo_root/external/linux_openpvrsgx/drivers/gpu/drm/pvrsgx/generic"
stage_root=${MODULE_BUILD_ROOT:-/opt/b8000h/pvrsrvkm-kbuild}
stage_dir="$stage_root/drivers/gpu/pvr"

test -s "$kernel_dir/Module.symvers" || {
	echo "error: $kernel_dir/Module.symvers is missing or empty" >&2
	exit 1
}
test -f "$vendor_dir/include4/pvrversion.h" || {
	echo "error: exact MT6589 DDK 1.12 source is missing: $vendor_dir" >&2
	exit 1
}

case "$stage_root" in
	/opt/b8000h/pvrsrvkm-kbuild|/opt/b8000h/pvrsrvkm-kbuild-*) ;;
	*) echo "error: refusing to clean unexpected staging path: $stage_root" >&2; exit 1 ;;
esac

mkdir -p "$stage_dir"
find "$stage_root" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
mkdir -p "$stage_dir"
cp -a "$vendor_dir"/. "$stage_dir"/
# Reuse the upstream Linux-7.x port of version-neutral Services code.  The
# exact DDK 1.12 headers, bridge code and SGX implementation remain from MTK.
cp "$modern_dir/services4/srvkm/env/linux/osfunc.c" \
	"$stage_dir/services4/srvkm/env/linux/osfunc.c"
cp -a "$module_dir/overrides"/. "$stage_dir"/

make -C "$kernel_dir" \
	M="$stage_dir" \
	ARCH=arm LLVM=1 \
	CONFIG_PVR_BUILD_RELEASE_MT6589=y \
	CONFIG_PVR_SGX_MT6589=m \
	TARGET_BUILD_VARIANT=user \
	SUPPORT_ION=1 \
	MTK_PATH_PLATFORM="$stage_root" \
	KCFLAGS="-I$stage_dir/compat -include $stage_dir/compat/pvr_compat.h -Wno-declaration-after-statement" \
	modules

cp "$stage_dir/pvrsrvkm.ko" "$module_dir/pvrsrvkm.ko"
