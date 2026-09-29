#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Start phoc on the GPU (G5): the glibc build from the Debian chroot runs in
# the Alpine root through Debian's ld.so (a "glibc island"), with
# libtlspad.so preloaded for the bionic GL TLS slots (see tlspad.c).
# Nothing goes through the environment (no LD_*, EGL_PLATFORM): phoc's
# children (gnome-session, phosh) are musl programs and inherit it.
# The Alpine root carries two links for paths the glibc side opens by name:
#   /usr/lib/arm-linux-gnueabihf -> /opt/debian/usr/lib/arm-linux-gnueabihf
#   /usr/local/hybris            -> /opt/debian/usr/local/hybris
# Falls back to the packaged /usr/bin/phoc (pixman) when the GPU is not ready
# (b8k-gpu service) or after 3 starts in a row that did not last a minute.
# Installed as /usr/local/bin/phoc-gpu; phosh-session picks it up through
# /root/.phoshdebug.
D=/opt/debian
LDSO=$D/usr/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3
PHOC=$D/usr/local/bin/phoc
FAILS=/run/phoc-gpu.fails

fallback() {
	echo "phoc-gpu: $1, starting the stock phoc" > /dev/kmsg
	shift
	exec /usr/bin/phoc "$@"
}

[ -x "$PHOC" ] && [ -x "$LDSO" ] || fallback "no glibc phoc" "$@"
[ -e /run/b8k-gpu.ready ] && [ -e /dev/pvrsrvkm ] ||
	fallback "GPU not ready" "$@"
n=$(cat "$FAILS" 2>/dev/null || echo 0)
[ "$n" -lt 3 ] || fallback "$n failed starts" "$@"
echo $((n + 1)) > "$FAILS"

# Still alive a minute later (same PID after exec): a good start.  Double
# fork so the timer is not left as phoc's child.
pid=$$
( (sleep 60; kill -0 "$pid" 2>/dev/null && rm -f "$FAILS") & ) \
	> /dev/null 2>&1 < /dev/null

echo "phoc-gpu: starting glibc phoc (attempt $((n + 1)))" > /dev/kmsg
exec "$LDSO" --inhibit-cache \
	--library-path "$D/usr/local/hybris/lib:$D/usr/local/lib/arm-linux-gnueabihf:$D/usr/lib/arm-linux-gnueabihf" \
	--preload "$D/usr/local/hybris/lib/libtlspad.so" \
	"$PHOC" "$@"
