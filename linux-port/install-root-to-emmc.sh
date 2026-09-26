#!/bin/sh
# Copy the running Phosh SD root to the existing B8000-H USRDATA partition.
# This intentionally does not touch BOOT, RECOVERY, NVRAM, the partition table,
# or any other eMMC region.  Run only from the dedicated install DTB, where the
# Live probing on B8000-H shows microSD as mmcblk0 and eMMC as mmcblk1
# while both controllers are enabled by the installer DTB.
set -eu

TOKEN=--erase-userdata-and-install
TARGET=/dev/mmcblk1p20
SOURCE=/dev/mmcblk0p1
MOUNT=/mnt/b8000-emmc-root

if [ "${1:-}" != "$TOKEN" ]; then
	echo "Refusing to continue without exact token: $TOKEN" >&2
	exit 2
fi

model=$(tr -d '\000' </proc/device-tree/model 2>/dev/null || true)
case "$model" in
	*"B8000-H (SD to eMMC installer)"*) ;;
	*) echo "Wrong DTB/model: $model" >&2; exit 3 ;;
esac

root_source=$(awk '$2 == "/" { print $1; exit }' /proc/mounts)
[ "$root_source" = "$SOURCE" ] || {
	echo "Root is $root_source, expected $SOURCE; refusing." >&2
	exit 4
}
[ -b "$TARGET" ] || { echo "$TARGET is absent" >&2; exit 5; }

# The factory USRDATA partition is about 13.1 GiB.  Reject a different layout
# so a changed device number cannot erase a small identity/boot partition.
sectors=$(cat /sys/class/block/mmcblk1p20/size)
case "$sectors" in ''|*[!0-9]*) echo "Invalid target size" >&2; exit 6;; esac
bytes=$((sectors * 512))
[ "$sectors" = 27589888 ] && [ "$bytes" = 14126022656 ] || {
	echo "Unexpected $TARGET size: $bytes bytes" >&2
	exit 7
}

grep -q "^$TARGET " /proc/mounts && {
	echo "$TARGET is already mounted; refusing." >&2
	exit 8
}

echo "Formatting only $TARGET ($bytes bytes) as ext4..."
mkfs.ext4 -F -L B8000ROOT "$TARGET"
mkdir -p "$MOUNT"
mount -t ext4 -o rw,noatime "$TARGET" "$MOUNT"

echo "Copying the running SD root to eMMC..."
tar -C / --one-file-system \
	--exclude=./dev --exclude=./proc --exclude=./sys --exclude=./run \
	--exclude=./tmp --exclude=./mnt -cpf - . | tar -C "$MOUNT" -xpf -
mkdir -p "$MOUNT/dev" "$MOUNT/proc" "$MOUNT/sys" "$MOUNT/run" "$MOUNT/tmp" "$MOUNT/mnt"
chmod 1777 "$MOUNT/tmp"
sync

echo "Verifying copied root..."
test -x "$MOUNT/sbin/init"
test -x "$MOUNT/usr/bin/phosh-session"
test -x "$MOUNT/usr/bin/phoc"
du -sh "$MOUNT"
umount "$MOUNT"
echo "PASS: Phosh root installed to $TARGET; BOOT was not modified."
