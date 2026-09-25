# Lenovo B8000-H initial Linux bring-up

This directory contains the first device-specific work for the Lenovo YOGA
Tablet 10 B8000-H (machine type 60047, board `blade10_row_3gdata`). It targets
the TeamYogaBlade2 near-mainline kernel at commit
`7dd245445adf6eaa27018cf92fbd5a92ec0b118e`.

## Safety model

- The first test must use `fastboot boot`; never `fastboot flash`.
- `mt6589-lenovo-b8000-h-safe.dtb` disables both internal eMMC and removable
  SD controllers. The test kernel therefore cannot access Android storage.
- The initramfs does not mount any Android partition.
- The development H DTS contains the live B8000-H partition geometry, but all
  20 fixed partitions are marked `read-only`.
- Keep the verified stock/identity backup before any later write experiment.
- Do not test below 60% battery charge.

## Contents

- `patches/0001-lenovo-b8000-h-initial-dts.patch` adds the H and H-safe DTBs.
- `patches/0002-lenovo-blade-defconfig-appended-dtb.patch` enables
  `ARM_APPENDED_DTB`; LK 0.5 passes only ATAGs, so the DTB must be appended.
- `build-safe-image.sh` rebuilds the whole image inside WSL `B8000Build`.
- `initramfs/init` mounts only virtual filesystems and attempts to expose a USB
  ACM serial shell as `B8000H-LINUX`.
- `../tools/mkmtkimage.py` creates the legacy 512-byte MediaTek `KERNEL` and
  `ROOTFS` component headers required by the stock bootloader.

Generated build artifacts are under `artifacts/linux-port/` and are ignored by
Git. The first-boot image is `b8000h-linux-firstboot-safe.img`.

## Verified build

- Kernel branch: `blade/v7.1`
- Kernel commit: `7dd245445adf6eaa27018cf92fbd5a92ec0b118e`
- Compiler: Alpine Linux 3.24.2, LLVM/Clang 22
- Boot image size: 4,184,064 bytes
- v2 boot image SHA-256 (appended DTB, `second_addr` restored, not yet tested):
  `7EFBD36BD66B26CEB8CEC06B4C25898AC7A62393A31ABCD56DCEC5A0DA1F9ADA`
- v1 SHA-256 `B0BA8051E62355B9E90929588B43ABAF8375FF02667BB15CBD22A0B80C281F1B`
  failed: `Booting OKAY`, but LK stayed on its screen and in fastboot.
- Safe DTB SHA-256:
  `DD8E28E8AB668149265D8984716CA7B280D37F469C761AECFD53678BE0C82CD5`

The boot image was unpacked after creation. Its extracted kernel and ramdisk
matched the exact inputs byte-for-byte. The patch also passed `git apply
--check` against the clean pinned kernel commit.

## Stock boot header values retained

- page size: 2048
- kernel address: `0x10008000`
- ramdisk address: `0x11000000`
- second address: `0x10f00000`
- tags address: `0x10000100`
- board name: `39`

The kernel payload is `zImage + DTB`, wrapped with an MTK `KERNEL` header. The
compressed initramfs is wrapped with an MTK `ROOTFS` header.

## First test gate

Before executing the temporary boot:

1. Charge the tablet to at least 60% on a wall charger.
2. Confirm Android still boots and the verified backup is present.
3. Enter fastboot without flashing anything.
4. Recheck the boot image SHA-256.
5. Run only `fastboot boot b8000h-linux-firstboot-safe.img`.

If the kernel does not boot, hold Power to return to the untouched stock
Android boot image. A successful first milestone is framebuffer kernel output
or enumeration of the USB ACM console; it is not yet a full Linux tablet OS.
