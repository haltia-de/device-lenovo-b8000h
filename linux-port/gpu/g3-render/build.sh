#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build the G3 render test with the official Android NDK r25c (Windows host,
# run from Git Bash).  API 19 = the tablet's stock Android 4.4.2 bionic.
set -eu

src_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$src_dir/../../.." && pwd)
ndk=${ANDROID_NDK:-$repo_root/artifacts/android-ndk-r25c}
out_dir=$repo_root/artifacts/linux-port/g3-render
clang=$ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe

test -x "$clang" || { echo "error: NDK clang not found: $clang" >&2; exit 1; }
mkdir -p "$out_dir"

"$clang" --target=armv7a-linux-androideabi19 \
	-O2 -Wall -Wextra -Werror -fPIE -pie \
	-o "$out_dir/gles2_pbuffer" "$src_dir/gles2_pbuffer.c" \
	-lEGL -lGLESv2

sha256sum "$out_dir/gles2_pbuffer"
