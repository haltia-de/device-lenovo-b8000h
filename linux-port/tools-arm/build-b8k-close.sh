#!/bin/sh
# Cross-build b8k-close (armv7 musl) in the b8000h-build:3.24 container, with
# the sysroot and the wlr-layer-shell protocol of the keyboard build
# (linux-port/wvkbd/build.sh, run it first; its sysroot has cairo and pango).
# Output: /opt/b8000h/tools/b8k-close, which build-rootfs.sh installs.
set -eu
S=/opt/b8000h/sysroot-wvkbd
PROTO=${PROTO:-/opt/b8000h/src/wvkbd/proto}
WORK=/tmp/b8k-close-build
if [ ! -e "$S/usr/include/pango-1.0/pango/pangocairo.h" ]; then
	echo "no $S: build the keyboard first (linux-port/wvkbd/build.sh)" >&2
	exit 1
fi
command -v wayland-scanner >/dev/null || apk add --no-cache wayland-dev
command -v ld.lld >/dev/null || apk add --no-cache lld
command -v pkg-config >/dev/null || apk add --no-cache pkgconf
rm -rf "$WORK"
mkdir -p "$WORK"
for p in wlr-layer-shell-unstable-v1 xdg-shell; do
	wayland-scanner client-header "$PROTO/$p.xml" "$WORK/$p-client-protocol.h"
	wayland-scanner private-code "$PROTO/$p.xml" "$WORK/$p-protocol.c"
done
export PKG_CONFIG_SYSROOT_DIR=$S
export PKG_CONFIG_LIBDIR=$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig
export PKG_CONFIG_PATH=
G=$(dirname "$(find "$S/usr/lib/gcc" -name crtbeginS.o | head -1)")
mkdir -p /opt/b8000h/tools
# shellcheck disable=SC2046 # pkg-config prints separate flags
clang --target=armv7-alpine-linux-musleabihf --sysroot="$S" -B"$G" -L"$G" \
	-fuse-ld=lld -O2 -Wall -Wextra -I"$WORK" -o /opt/b8000h/tools/b8k-close \
	$(pkg-config --cflags pangocairo wayland-client) \
	"$(dirname "$0")/b8k-close.c" "$WORK/wlr-layer-shell-unstable-v1-protocol.c" \
	"$WORK/xdg-shell-protocol.c" \
	$(pkg-config --libs pangocairo wayland-client) -ldl -lm
llvm-strip /opt/b8000h/tools/b8k-close
ls -l /opt/b8000h/tools/b8k-close
