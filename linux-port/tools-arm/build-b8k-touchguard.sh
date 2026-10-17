#!/bin/sh
# Cross-build b8k-touchguard (armv7 musl, no libraries) in the
# b8000h-build:3.24 container, with the keyboard build's sysroot
# (linux-port/wvkbd/build.sh, run it first). Output:
# /opt/b8000h/tools/b8k-touchguard, which build-rootfs.sh installs.
set -eu
S=/opt/b8000h/sysroot-wvkbd
[ -e "$S/usr/include/linux/uinput.h" ] || { echo "no $S: build the keyboard first" >&2; exit 1; }
command -v ld.lld >/dev/null || apk add --no-cache lld
G=$(dirname "$(find "$S/usr/lib/gcc" -name crtbeginS.o | head -1)")
mkdir -p /opt/b8000h/tools
clang --target=armv7-alpine-linux-musleabihf --sysroot="$S" -B"$G" -L"$G" \
	-fuse-ld=lld -O2 -Wall -Wextra -o /opt/b8000h/tools/b8k-touchguard \
	"$(dirname "$0")/b8k-touchguard.c"
llvm-strip /opt/b8000h/tools/b8k-touchguard
ls -l /opt/b8000h/tools/b8k-touchguard
