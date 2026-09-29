#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

kernel_dir=${KERNEL_DIR:-/opt/b8000h/linux-drm}
module_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$module_dir/../../.." && pwd)
stage_root=${MODULE_BUILD_ROOT:-/opt/b8000h/ion-legacy-kbuild}
artifact_dir="$repo_root/artifacts/linux-port/pvrsrvkm"

test -s "$kernel_dir/Module.symvers" || {
	echo "error: $kernel_dir/Module.symvers is missing or empty" >&2
	exit 1
}

case "$stage_root" in
	/opt/b8000h/ion-legacy-kbuild|/opt/b8000h/ion-legacy-kbuild-*) ;;
	*) echo "error: refusing to clean unexpected staging path: $stage_root" >&2; exit 1 ;;
esac

mkdir -p "$stage_root"
find "$stage_root" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
cp "$module_dir/Makefile" "$module_dir/ion_legacy.c" "$stage_root/"

make -C "$kernel_dir" M="$stage_root" ARCH=arm LLVM=1 modules

tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT HUP INT TERM
llvm-nm -u "$stage_root/ion_legacy.ko" | awk '{print $2}' | sort -u > "$tmp_dir/undefined"
awk '{print $2}' "$kernel_dir/Module.symvers" | sort -u > "$tmp_dir/available"
comm -23 "$tmp_dir/undefined" "$tmp_dir/available" > "$tmp_dir/missing"
if test -s "$tmp_dir/missing"; then
	echo "error: unresolved ion_legacy symbols:" >&2
	cat "$tmp_dir/missing" >&2
	exit 1
fi

cp "$stage_root/ion_legacy.ko" "$module_dir/ion_legacy.ko"
mkdir -p "$artifact_dir"
cp "$stage_root/ion_legacy.ko" "$artifact_dir/ion_legacy.ko"
cp "$stage_root/ion_legacy.ko" "$artifact_dir/ion_legacy-stripped.ko"
llvm-strip --strip-debug "$artifact_dir/ion_legacy-stripped.ko"
gzip -9 -n -c "$artifact_dir/ion_legacy.ko" > "$artifact_dir/ion_legacy.ko.gz"
gzip -9 -n -c "$artifact_dir/ion_legacy-stripped.ko" > "$artifact_dir/ion_legacy-stripped.ko.gz"

echo "ok: all ion_legacy undefined symbols are present in kernel Module.symvers"

