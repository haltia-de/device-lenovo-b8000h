#!/bin/sh
# Package the already-built /opt/b8000h/rootfs as a compact Android sparse
# ext4 image for the existing USRDATA partition. Run inside WSL B8000Build.
set -eu

REPO="/mnt/c/Users/<user>/src/lenovo-b8000h-linux"
ROOT=/opt/b8000h/rootfs
OUT=/opt/b8000h/out
RAW="$OUT/b8000h-phosh-emmc-root.ext4"
SPARSE="$OUT/b8000h-phosh-emmc-root.sparse.img"
ROUNDTRIP="$OUT/b8000h-phosh-emmc-root.roundtrip.ext4"
SIZE_MB=1024

test -x "$ROOT/sbin/init"
test -x "$ROOT/usr/bin/phosh-session"
test -x "$ROOT/usr/local/sbin/install-root-to-emmc.sh"

apk add --no-cache e2fsprogs android-tools-img2simg android-tools-simg2img >/dev/null
mkdir -p "$OUT" "$REPO/artifacts/linux-port" /mnt/c/Users/Public/MTK_B8000H/flash-userdata
rm -f "$RAW" "$SPARSE" "$ROUNDTRIP"
truncate -s ${SIZE_MB}M "$RAW"
mkfs.ext4 -q -F -L B8000ROOT -d "$ROOT" "$RAW"
e2fsck -fn "$RAW"
img2simg "$RAW" "$SPARSE"
simg2img "$SPARSE" "$ROUNDTRIP"
cmp "$RAW" "$ROUNDTRIP"
e2fsck -fn "$ROUNDTRIP"

cp "$SPARSE" "$REPO/artifacts/linux-port/"
cp "$SPARSE" /mnt/c/Users/Public/MTK_B8000H/flash-userdata/userdata-linux.img
cp "$RAW" /mnt/c/Users/Public/MTK_B8000H/flash-userdata/b8000h-phosh-emmc-root.ext4
sha256sum "$SPARSE"
sha256sum "$RAW"
ls -lh "$RAW" "$SPARSE"
