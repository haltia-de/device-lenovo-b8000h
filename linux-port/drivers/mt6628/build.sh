#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

kernel_dir=${KERNEL_DIR:-/opt/b8000h/linux}
module_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

test -s "$kernel_dir/Module.symvers" || {
	echo "error: $kernel_dir/Module.symvers is missing or empty" >&2
	exit 1
}

exec make -C "$kernel_dir" M="$module_dir" ARCH=arm LLVM=1 modules

