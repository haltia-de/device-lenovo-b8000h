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
# Reuse the upstream Linux-7.x port of the version-neutral Linux environment
# as one coherent unit.  In particular osfunc.c, mm.[ch], mmap.[ch] and
# module.c share private types and helpers and cannot be mixed individually.
# The exact DDK 1.12 headers, bridge code, SGX implementation and MT6589
# system layer remain from the MTK source copied above.
cp -a "$modern_dir/services4/srvkm/env/linux"/. \
	"$stage_dir/services4/srvkm/env/linux"/
# The MTK build predates SUPPORT_ION_HEADER and its bridge only needs the
# stable ION function declarations.  Keep that exact header; ion.c is supplied
# by overrides below because the in-kernel ION allocator no longer exists.
cp "$vendor_dir/services4/srvkm/env/linux/ion.h" \
	"$stage_dir/services4/srvkm/env/linux/ion.h"
# This file is part of the userspace ioctl ABI.  The generic port targets a
# different PVRSRV_BRIDGE_PACKAGE layout, so retain the exact MTK dispatcher.
cp "$vendor_dir/services4/srvkm/env/linux/pvr_bridge_k.c" \
	"$stage_dir/services4/srvkm/env/linux/pvr_bridge_k.c"
cp -a "$module_dir/overrides"/. "$stage_dir"/
# The imported openpvrsgx sources use CRLF; normalize only files touched by
# our small source patches so POSIX patch can match their context reliably.
sed -i 's/\r$//' \
	"$stage_dir/services4/srvkm/env/linux/module.c" \
	"$stage_dir/services4/srvkm/env/linux/mm.c"
# Linux 7 no longer exposes the compiler's freestanding stdarg.h to kernel
# modules.  The DDK only uses va_list, so use the kernel wrapper everywhere.
find "$stage_dir" -type f \( -name '*.c' -o -name '*.h' \) \
	-exec sed -i 's|#include <stdarg.h>|#include <linux/stdarg.h>|' {} +
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
	KCFLAGS="-I$stage_dir/compat -include $stage_dir/compat/pvr_compat.h -DPVR_LDM_DEVICE_TREE -DPVR_LINUX_DYNAMIC_SGX_RESOURCE_INFO -Wno-declaration-after-statement" \
	modules

cp "$stage_dir/pvrsrvkm.ko" "$module_dir/pvrsrvkm.ko"
