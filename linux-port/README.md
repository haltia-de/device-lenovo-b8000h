# linux-port

Device work for the Lenovo YOGA Tablet 10 B8000-H (machine type 60047, board
`blade10_row_3gdata`) on the TeamYogaBlade2 kernel `blade/v7.1` at
`7dd245445adf6eaa27018cf92fbd5a92ec0b118e`. The build and install steps are in
[../docs/INSTALL.md](../docs/INSTALL.md) (Russian); this file maps the directory.

| Path | Contents |
|---|---|
| `patches/` | Kernel patches, applied in numeric order: `*.patch` with `git apply`, `*.py` are idempotent edit scripts run against the kernel tree, `*.txt` are whole DTS files |
| `build-safe-image.sh` | `boot.img`: zImage + DTB, initramfs, MTK headers, stock boot header values. `DTB_VARIANT` = `safe`, `sd`, `install`, `install2` or `emmc` |
| `initramfs/` | `init` (mounts, microSD installer, boot splash with `splash=1`) and `splash.py` |
| `drivers/` | Out-of-tree modules: PowerVR `pvrsrvkm` (DDK 1.12), `dc_nohw`, `ion-legacy`, MT6628 Wi-Fi, audio, battery |
| `wifi/` | wpa_supplicant 2.11 without PMF/SAE, NetworkManager config |
| `gpu/`, `hybris/` | PowerVR services, libhybris and the phoc/wlroots GLES2 build in the Debian armhf chroot |
| `build-rootfs.sh` | Alpine 3.24 armv7 root file system; `UI_PROFILE=shell` installs the tablet shell (Haltia) (see [../docs/shell.md](../docs/shell.md)), `labwc` a rescue desktop |
| `rootfs/` | Shell scripts (`b8k-*`), waybar, Firefox theme and web-app script, galculator theme, icons; `make-release-rootfs.sh` builds the public image from a tar of a working tablet |
| `wvkbd/` | On-screen keyboard: patches and layout for wvkbd 0.21, `build.sh` |
| `build-emmc-root-image.sh`, `install-root-to-emmc.sh` | eMMC root image and the guarded USRDATA writer |

## Safety

- Only USRDATA is ever written, after checking the installer DTB, the SD root
  device and the exact partition size (27589888 sectors). BOOT is flashed
  separately with `fastboot flash boot`. NVRAM, PRO_INFO, PROTECT_F/S and the
  partition table are never touched; back them up first.
- Android blobs are not shipped. The tablet's own stock `system` partition is
  mounted read-only at boot (`rootfs/b8k-android-system.initd`).
- Do not test below 60% battery charge.

## Stock boot header values

page size 2048, kernel `0x10008000`, ramdisk `0x11000000`, second
`0x10f00000`, tags `0x10000100`, board `39`. The kernel payload is
`zImage + DTB` (LK 0.5 passes only ATAGs, so `ARM_APPENDED_DTB` is required),
wrapped with an MTK `KERNEL` header; the ramdisk has an MTK `ROOTFS` (BOOT) or
`RECOVERY` header.
