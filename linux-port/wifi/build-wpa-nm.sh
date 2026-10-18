#!/bin/sh
# wpa_supplicant 2.11 for the MT6628, without PMF/SAE/FILS/OWE.
# NetworkManager 1.52 appends WPA-PSK-SHA256 when the supplicant's
# global D-Bus capabilities include "pmf". Upstream 2.11 advertises
# that string even with CONFIG_IEEE80211W disabled, and the MT6628
# connect path rejects the SHA256 AKM. This build turns the features
# off and stops advertising "pmf", so Phosh can connect with plain WPA-PSK.
#
# Place the official 2.11 tarball at $TARBALL first. The image build
# (linux-port/build-rootfs.sh, UI_PROFILE=phosh) installs the result
# over the Alpine binary.
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
S=${SYSROOT:-/opt/b8000h/sysroot-armv7}
OUT=${WPA_OUT:-/opt/b8000h/out/wpa-mt6628}
SRC=$OUT/wpa_supplicant-2.11
TARBALL=${TARBALL:-/opt/b8000h/out/wpa_supplicant-2.11.tar.gz}
PATCHES=$REPO/linux-port/wifi/patches
SUM=9a0a3a9d6fa2235903c40aa57b5955f0c9dd1dccfd0e3825a3b6f92b3e32db8d464b3ea0aef3285ba3ee109e7b190560cedd744902e954f0003cdba543e277b2

mkdir -p "$S/etc/apk/keys" "$OUT"
cp /usr/share/apk/keys/armv7/* "$S/etc/apk/keys/"
printf '%s\n' \
	https://dl-cdn.alpinelinux.org/alpine/v3.24/main \
	https://dl-cdn.alpinelinux.org/alpine/v3.24/community \
	> "$S/etc/apk/repositories"
if [ ! -e "$S/usr/include/openssl/ssl.h" ] || [ ! -e "$S/usr/lib/pkgconfig/libnl-3.0.pc" ] \
	|| [ ! -e "$S/usr/include/dbus-1.0/dbus/dbus.h" ]; then
	apk --root "$S" --arch armv7 --initdb --update-cache --no-scripts add \
		musl-dev linux-headers openssl-dev libnl3-dev dbus-dev pkgconf gcc libgcc
fi

echo "$SUM  $TARBALL" | sha512sum -c -
rm -rf "$SRC"
tar -C "$OUT" -xzf "$TARBALL"
for p in \
	0001-nl80211-add-extra-ies-only-if-allowed-by-driver.patch \
	0002-AP-guard-FT-SAE-code-with-CONFIG_IEEE80211R_AP.patch \
	wpa_supplicant-2.11-Revert-Mark-authorization-completed-on-driver-indica.patch \
	0003-dbus-pmf-capability-follows-config.patch
do
	patch -d "$SRC" -p1 < "$PATCHES/$p"
done

sed -e 's/^CONFIG_SAE=y/# CONFIG_SAE disabled: MT6628 rejects SAE/' \
	-e 's/^CONFIG_IEEE80211W=y/# CONFIG_IEEE80211W disabled: NM would add WPA-PSK-SHA256/' \
	-e 's/^CONFIG_OCV=y/# CONFIG_OCV disabled with IEEE80211W/' \
	-e 's/^CONFIG_FILS=y/# CONFIG_FILS disabled/' \
	-e 's/^CONFIG_FILS_SK_PFS=y/# CONFIG_FILS_SK_PFS disabled/' \
	-e 's/^CONFIG_OWE=y/# CONFIG_OWE disabled/' \
	-e 's/^CONFIG_SUITEB=y/# CONFIG_SUITEB disabled/' \
	-e 's/^CONFIG_SUITEB192=y/# CONFIG_SUITEB192 disabled/' \
	-e 's/^CONFIG_PCSC=y/# CONFIG_PCSC disabled/' \
	-e 's/^CONFIG_EAP_SIM=y/# CONFIG_EAP_SIM disabled/' \
	-e 's/^CONFIG_EAP_AKA=y/# CONFIG_EAP_AKA disabled/' \
	-e 's/^CONFIG_EAP_AKA_PRIME=y/# CONFIG_EAP_AKA_PRIME disabled/' \
	"$PATCHES/config" > "$SRC/wpa_supplicant/.config"
if grep -q '^CONFIG_IEEE80211W=' "$SRC/wpa_supplicant/.config"; then
	echo "error: CONFIG_IEEE80211W still enabled" >&2
	exit 1
fi
if grep -q '^CONFIG_SAE=' "$SRC/wpa_supplicant/.config"; then
	echo "error: CONFIG_SAE still enabled" >&2
	exit 1
fi
grep -q '^CONFIG_CTRL_IFACE_DBUS_NEW=y' "$SRC/wpa_supplicant/.config"
grep -q '#ifdef CONFIG_IEEE80211W' "$SRC/wpa_supplicant/dbus/dbus_new_handlers.c"

G=$(dirname "$(find "$S/usr/lib/gcc" -name crtbeginS.o | head -1)")
export PKG_CONFIG_SYSROOT_DIR="$S"
export PKG_CONFIG_LIBDIR="$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig"
export CC="clang --target=armv7-alpine-linux-musleabihf --sysroot=$S -B$G -L$G -fuse-ld=lld"
make -C "$SRC/wpa_supplicant" clean
make -C "$SRC/wpa_supplicant" -j"$(nproc)" LIBDIR=/lib BINDIR=/sbin wpa_supplicant wpa_cli wpa_passphrase

# 2.11 puts its objects under build/, next to wpa_supplicant/.
obj=$(find "$SRC" -name dbus_new_handlers.o | head -1)
[ -n "$obj" ] || { echo "error: dbus_new_handlers.o not built" >&2; exit 1; }
strings "$obj" | grep -qx pmf && {
	echo "error: dbus capability object still contains pmf" >&2
	exit 1
}

llvm-strip "$SRC/wpa_supplicant/wpa_supplicant" \
	"$SRC/wpa_supplicant/wpa_cli" \
	"$SRC/wpa_supplicant/wpa_passphrase"
install -m 755 "$SRC/wpa_supplicant/wpa_supplicant" "$OUT/wpa_supplicant"
install -m 755 "$SRC/wpa_supplicant/wpa_cli" "$OUT/wpa_cli"
install -m 755 "$SRC/wpa_supplicant/wpa_passphrase" "$OUT/wpa_passphrase"

if strings "$OUT/wpa_supplicant" | grep -qx pmf; then
	echo "error: stripped wpa_supplicant still contains the pmf capability" >&2
	exit 1
fi

qemu-arm -L "$S" "$OUT/wpa_supplicant" -v
echo WPA_BUILD_OK
