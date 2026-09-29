#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build phoc 0.55.1 with its bundled wlroots 0.20 inside the Debian trixie
# armhf chroot (run through debian-enter.sh), against the libhybris
# EGL/GLESv2 in /usr/local/hybris (G5: phoc on the SGX544).
#
# trixie is too old for wlroots 0.20 (libdrm >= 2.4.129, xkbcommon >= 1.8,
# wayland-protocols >= 1.47): these are built into /usr/local first, so every
# pkg-config lookup (phoc, wlroots, gmobile) sees one consistent version.
#
# Sources in /root/src/g5, downloaded on the PC and verified there:
#   phoc-0.55.1.tar.xz, gmobile-0.6.0.tar.xz  (sources.phosh.mobi, .sha256sum)
#   libdrm_2.4.134.orig.tar.xz, libxkbcommon_1.13.1.orig.tar.gz,
#   wayland-protocols_1.49.orig.tar.xz        (Debian pool, sha256 from .dsc)
# Result: /usr/local/bin/phoc; wlroots/gmobile are linked in statically.
# Usage: build-phoc.sh [deps|phoc]   (default: both)
set -eu
S=/root/src/g5
B=$S/build
J=${JOBS:-2}
export DEBIAN_FRONTEND=noninteractive
# libhybris egl.pc/glesv2.pc first; /usr/local (new deps) is on pkgconf's
# default search path in Debian.
export PKG_CONFIG_PATH=/usr/local/hybris/lib/pkgconfig
what=${1:-all}

unpack() {
	mkdir -p "$S/src"
	[ -d "$S/src/$2" ] || tar -xf "$S/$1" -C "$S/src"
	test -d "$S/src/$2"
}

mbuild() {
	name=$1 dir=$2
	shift 2
	if [ ! -f "$B/$name/build.ninja" ]; then
		rm -rf "$B/$name"	# left over from a failed setup
		meson setup "$B/$name" "$S/src/$dir" --prefix=/usr/local \
			--buildtype=release "$@"
	fi
	ninja -C "$B/$name" -j"$J"
	ninja -C "$B/$name" install
}

if [ "$what" != phoc ]; then
	apt-get update
	apt-get install -y --no-install-recommends \
		meson ninja-build pkgconf bison gettext desktop-file-utils \
		libglib2.0-dev libglib2.0-dev-bin libgnome-desktop-3-dev \
		gsettings-desktop-schemas-dev libinput-dev libudev-dev \
		libpixman-1-dev libwayland-dev libwayland-bin wayland-protocols \
		libseat-dev libdisplay-info-dev hwdata libgbm-dev \
		libjson-glib-dev

	unpack libdrm_2.4.134.orig.tar.xz libdrm-2.4.134
	mbuild libdrm libdrm-2.4.134 -Dauto_features=disabled -Dtests=false \
		-Dman-pages=disabled -Dvalgrind=disabled
	# GitHub-style orig tarball: top directory named after the commit.
	unpack libxkbcommon_1.13.1.orig.tar.gz xkbcommon-libxkbcommon-920ea79
	mbuild xkbcommon xkbcommon-libxkbcommon-920ea79 -Denable-tools=false \
		-Denable-x11=false -Denable-docs=false -Denable-xkbregistry=false \
		-Denable-bash-completion=false \
		-Dxkb-config-root=/usr/share/X11/xkb \
		-Dx-locale-root=/usr/share/X11/locale
	unpack wayland-protocols_1.49.orig.tar.xz wayland-protocols-1.49
	# 1.49 marks interfaces "frozen", which trixie's wayland-scanner 1.23
	# only knows as a DTD warning; --strict makes that fatal for the
	# generated enum headers.
	sed -i "/'--strict',/d" \
		"$S/src/wayland-protocols-1.49/include/wayland-protocols/meson.build"
	mbuild wayland-protocols wayland-protocols-1.49 -Dtests=false
	ldconfig
fi

if [ "$what" != deps ]; then
	unpack phoc-0.55.1.tar.xz phoc-0.55.1
	# gmobile.wrap is a git wrap (gitlab.gnome.org is unreachable from
	# here); a directory of that name makes meson use it as is.
	if [ ! -d "$S/src/phoc-0.55.1/subprojects/gmobile" ]; then
		tar -xf "$S/gmobile-0.6.0.tar.xz" -C "$S/src/phoc-0.55.1/subprojects"
		mv "$S/src/phoc-0.55.1/subprojects/gmobile-0.6.0" \
			"$S/src/phoc-0.55.1/subprojects/gmobile"
	fi
	mbuild phoc phoc-0.55.1 --wrap-mode=nodownload \
		-Ddefault_library=static \
		-Dxwayland=disabled -Dembed-wlroots=enabled -Dtests=false \
		-Dman=false -Dgtk_doc=false \
		-Dwlroots:renderers=gles2 -Dwlroots:backends=drm,libinput \
		-Dwlroots:xwayland=disabled -Dwlroots:examples=false \
		-Dwlroots:session=enabled -Dwlroots:libliftoff=disabled \
		-Dwlroots:color-management=disabled -Dwlroots:xcb-errors=disabled
	install -m 0755 /root/src/libtlspad.so /usr/local/hybris/lib/libtlspad.so
	ls -l /usr/local/bin/phoc
	readelf -lW /usr/local/bin/phoc | grep -E "INTERP|TLS" || true
fi
echo "build-phoc: done ($what)"
