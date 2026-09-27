#!/bin/sh
# Cross-build b8k-session (armv7 musl + libpam) inside WSL B8000Build.
# Output: /opt/b8000h/tools/b8k-session (used by build-rootfs.sh).
set -eu
S=/opt/b8000h/sysroot-armv7
if [ ! -e $S/usr/include/security/pam_appl.h ]; then
	mkdir -p $S/etc/apk/keys
	cp /opt/b8000h/rootfs/etc/apk/repositories $S/etc/apk/
	cp /usr/share/apk/keys/armv7/* $S/etc/apk/keys/
	apk --root $S --arch armv7 --initdb --update-cache --no-scripts \
		add musl-dev linux-pam-dev gcc libgcc >/dev/null
fi
G=$(dirname "$(find $S/usr/lib/gcc -name crtbeginS.o | head -1)")
mkdir -p /opt/b8000h/tools
clang --target=armv7-alpine-linux-musleabihf --sysroot=$S -B"$G" -L"$G" \
	-fuse-ld=lld -O2 -Wall -o /opt/b8000h/tools/b8k-session \
	"$(dirname "$0")/b8k-session.c" -lpam
llvm-strip /opt/b8000h/tools/b8k-session
ls -l /opt/b8000h/tools/b8k-session
