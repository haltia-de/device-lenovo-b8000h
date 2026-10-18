#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Fetch what the kernel build needs from outside this repository, at the
# commits it is pinned to (ADR 0003):
#   /opt/b8000h/linux-drm             TeamYogaBlade2/linux blade/v7.1 @ 7dd245445adf
#   /src/external/mt6589-pvr-ddk-1.12 the MT6589 PowerVR DDK 1.12 kernel side
#                                     (bq/aquaris-5HD @ 1b08ad7, only its pvr tree)
#   /src/external/linux_openpvrsgx    openpvrsgx @ c017d06 (only drivers/gpu/drm/pvrsgx)
# Shallow, sparse fetches of exactly those commits.
set -eu
K=/opt/b8000h/linux-drm
E=/src/external
KERNEL_URL=https://github.com/TeamYogaBlade2/linux.git
KERNEL_COMMIT=7dd245445adf6eaa27018cf92fbd5a92ec0b118e
DDK_URL=https://github.com/bq/aquaris-5HD.git
DDK_COMMIT=1b08ad7
DDK_PATH=mediatek/platform/mt6589/kernel/drivers/gpu/pvr
SGX_URL=https://github.com/openpvrsgx-devgroup/linux_openpvrsgx.git
SGX_COMMIT=c017d06
SGX_PATH=drivers/gpu/drm/pvrsgx

fetch() { # url commit dir [sparse path]
	url=$1 commit=$2 dir=$3 path=${4:-}
	if [ -d "$dir/.git" ] && git -C "$dir" rev-parse --verify -q "$commit^{commit}" >/dev/null; then
		echo "have $dir"
		return 0
	fi
	rm -rf "$dir"
	mkdir -p "$dir"
	git -C "$dir" init -q
	git -C "$dir" remote add origin "$url"
	if [ -n "$path" ]; then
		git -C "$dir" config core.sparseCheckout true
		echo "$path" >"$dir/.git/info/sparse-checkout"
	fi
	# A short commit cannot be fetched by name: fetch the history it lies
	# in without blobs, then check it out (blobs come for the sparse tree).
	if [ ${#commit} -lt 40 ]; then
		git -C "$dir" fetch -q --filter=blob:none origin
	else
		git -C "$dir" fetch -q --depth 1 origin "$commit"
	fi
	git -C "$dir" checkout -q "$commit"
	echo "fetched $dir at $(git -C "$dir" rev-parse --short HEAD)"
}

fetch "$KERNEL_URL" "$KERNEL_COMMIT" "$K"
fetch "$DDK_URL" "$DDK_COMMIT" "$E/mt6589-pvr-ddk-1.12" "$DDK_PATH"
fetch "$SGX_URL" "$SGX_COMMIT" "$E/linux_openpvrsgx" "$SGX_PATH"
test -f "$E/mt6589-pvr-ddk-1.12/$DDK_PATH/include4/pvrversion.h"
test -d "$E/linux_openpvrsgx/$SGX_PATH/generic"
