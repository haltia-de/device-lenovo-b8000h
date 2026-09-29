#!/bin/sh
# Проверяет, что каждый undefined-символ модуля разрешается ядром или pvrsrvkm.
set -eu

module=${1:-/opt/b8000h/dc-nohw-kbuild/drivers/gpu/pvr/dc_nohw.ko}
kernel_symvers=${KERNEL_SYMVERS:-/opt/b8000h/linux-drm/Module.symvers}
pvr_symvers=${PVR_SYMVERS:-/opt/b8000h/pvrsrvkm-kbuild/drivers/gpu/pvr/Module.symvers}
tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT HUP INT TERM

test -s "$module"
test -s "$kernel_symvers"
test -s "$pvr_symvers"

llvm-nm -u "$module" | awk '{print $2}' | sort -u > "$tmp_dir/undefined"
awk '{print $2}' "$kernel_symvers" "$pvr_symvers" | sort -u > "$tmp_dir/available"
comm -23 "$tmp_dir/undefined" "$tmp_dir/available" > "$tmp_dir/missing"

if test -s "$tmp_dir/missing"; then
	echo "error: unresolved module symbols:" >&2
	cat "$tmp_dir/missing" >&2
	exit 1
fi

echo "ok: all undefined symbols are present in kernel + pvrsrvkm Module.symvers"
