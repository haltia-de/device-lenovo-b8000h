#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

kernel_dir=${KERNEL_DIR:-/opt/b8000h/linux}
module_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Kbuild treats an M= path containing spaces as several external modules.
# The Windows checkout has "Lenovo Yoga Tablet" in its path, so build through
# a stable WSL-side staging directory and copy the resulting modules back.
case "$module_dir" in
	*" "*)
		source_dir=$module_dir
		module_dir=${MODULE_BUILD_DIR:-/opt/b8000h/mt6628-kbuild}
		mkdir -p "$module_dir"
		cp "$source_dir"/Makefile "$source_dir"/*.c \
			"$source_dir"/*.h "$module_dir"/
		;;
esac

test -s "$kernel_dir/Module.symvers" || {
	echo "error: $kernel_dir/Module.symvers is missing or empty" >&2
	exit 1
}

make -C "$kernel_dir" M="$module_dir" ARCH=arm LLVM=1 modules

if [ "${source_dir:-}" ]; then
	cp "$module_dir"/*.ko "$source_dir"/
fi
