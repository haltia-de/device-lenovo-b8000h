#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

kernel_dir=${KERNEL_DIR:-/opt/b8000h/linux-drm}
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module_dir=${MODULE_BUILD_DIR:-/opt/b8000h/mt6589-audio-kbuild}

mkdir -p "$module_dir"
find "$module_dir" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
cp -a "$source_dir"/. "$module_dir"/
find "$module_dir" \( -name '*.o' -o -name '*.ko' -o -name '*.mod' \
	-o -name '*.mod.c' -o -name '.*.cmd' -o -name Module.symvers \
	-o -name modules.order \) -delete

test -s "$kernel_dir/Module.symvers"
make -C "$kernel_dir" M="$module_dir" ARCH=arm LLVM=1 modules
cp "$module_dir"/*.ko "$source_dir"/
