# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An experimental port of Linux 7.1 + Alpine 3.24 armv7 to the Lenovo YOGA Tablet 10 **B8000-H** (MT8389/MT6589, 3G only, board `BLADE10_ROW_3GDATA`, stock firmware `B8000_A442_001_020_140620_WW_3GDATA`). Alpha quality; the kernel randomly crashes every 10–40 minutes (memory corruption, cause not found), and work is paused. User-facing docs (`README.md`, `docs/*.md`) are written in **Russian**. Code comments and commit messages are in English: full-sentence subjects for fixes ("Pin the RDMA FIFO threshold that stops the Firefox scroll copies."), with `G5 step N:` / `kernel NN:` / `docs:` / `build-phoc:` prefixes for milestone work.

There is no test suite, no linter and no CI. A change is verified by building the images and running them on the tablet. The tablet gives you `dmesg` (often flooded by MSDC debug lines), `/sys/fs/pstore/*` after a crash or reboot, and `/var/log/phosh.log` (the session log; the name is left over from Phosh).

## Working on a connected tablet

- **SSH:** `root@<ip>` with a key in `/root/.ssh/authorized_keys`. The tablet's IP comes from DHCP, so scan for an `OpenSSH_10.3` banner on the LAN to find it.
- **USB serial (development rootfs only):** `/dev/ttyACM0` on the host is a raw root shell (`acm-shell` on `ttyGS0`). It has no prompt or echo, so wrap each command between marker lines. The host user needs `dialout` group membership or an ACL. `make-release-rootfs.sh` removes this shell.
- **Running things in the session:** run them as `user` with the environment of the `waybar` process (`/proc/<pid>/environ`: `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `DBUS_*`). `grim` takes screenshots. `b8k-launch start <id>` opens programs.
- **Power:** a failed `reboot` used to leave the tablet off; that is fixed by `b8k-wdt-exrst`. After `poweroff` with USB attached, LK shows the charging animation and stays off. Either way the user must press Power to bring it back, so ask before you power the tablet off.

## Build pipeline

The build is not fully automated. `docs/INSTALL.md` §0 is the authoritative step list. The scripts hardcode work directories under `/opt/b8000h/` (`linux`, `rootfs`, `out`, `initramfs`, `release-root`). They were written for a WSL Alpine distro named `B8000Build` and run as root. Some scripts also copy output to `/mnt/c/Users/Public/MTK_B8000H/`. Build outputs go to `artifacts/linux-port/`, which is git-ignored, as are `backups/`, `build/` and `external/`.

1. **Kernel tree**: [TeamYogaBlade2/linux](https://github.com/TeamYogaBlade2/linux/tree/blade/v7.1) `blade/v7.1` @ `7dd245445adf`. Apply `linux-port/patches/` in numeric order. The patches come in three formats:
   - `*.patch`: apply with `git apply`.
   - `*.py`: idempotent string-replace scripts run against the kernel tree. They hardcode `K = '/opt/b8000h/linux-drm/...'`, assert that each anchor occurs exactly once, and print `already` when the change is present.
   - `*.txt`: whole DTS files or snapshots, e.g. `0011-b8000-h-install2-dts.txt` is the `install2` DTS. `*.patch.current.txt` holds the current state of an evolving patch.

   Config: `lenovo-blade_defconfig` (from the patches) with `CONFIG_LOCALVERSION="-b8000h23"` (patch 0038 enables zram). The build number in the release selects the GPU module directory (`/usr/lib/b8k/kN`, parsed by `b8k-gpu` from `-b8000hN-`), so bump it whenever the kernel config changes, and install the new modules next to the old ones so that the previous `boot.img` still boots. The release must end in `-g<sha>-dirty`: in Docker, run `git config --global --add safe.directory <tree>` first, or `setlocalversion` drops the suffix. `CONFIG_ARM_APPENDED_DTB=y` is mandatory, because LK 0.5 passes only ATAGs.
2. **boot.img**:
   ```
   K=<kernel tree> DTB_VARIANT=install2 IMAGE_SUFFIX=-my sh linux-port/build-safe-image.sh
   ```
   The script builds `zImage + dtbs` with `ARCH=arm LLVM=1` and packs the initramfs from `linux-port/initramfs/init` plus the `splash.py` boot picture. It wraps the kernel and the ramdisk in 512-byte MTK headers (`tools/mkmtkimage.py`; the ramdisk label is `ROOTFS` for BOOT and `RECOVERY` for RECOVERY). It then runs `mkbootimg` with the stock header values: base `0x10000000`, pagesize 2048, board `39`. `tools/fix-legacy-boot-header.py` restores `second_addr=0x10f00000`, and a round-trip `unpack_bootimg` + `cmp` checks the result. `DTB_VARIANT` picks `mt6589-lenovo-b8000-h-<variant>.dtb`:
   - `safe`: no eMMC and no SD.
   - `sd`: rescue root on microSD.
   - `install`/`install2`: SD is `mmcblk0`, eMMC is `mmcblk1`.
   - `emmc`: root is USRDATA `/dev/mmcblk0p20`.
3. **External modules**: `build.sh` in each `linux-port/drivers/*` directory. Without the WSL distro, use the `b8000h-build:3.24` Docker image: `docker run --rm -v <kernel tree>:/k:ro -v <copy of the module dir>:/m b8000h-build:3.24 make -C /k M=/m ARCH=arm LLVM=1 modules`. Build in a copy, so that `.o`/`.ko` files stay out of the repo. These are PowerVR `pvrsrvkm` (DDK 1.12, sources in `external/mt6589-pvr-ddk-1.12`), `dc_nohw`, `ion-legacy`, `mt6628` Wi-Fi, `mt6589-audio`, `b8k-battery` and others.
4. **wpa_supplicant**: `sh linux-port/wifi/build-wpa-nm.sh`. This build drops PMF/SAE/FILS/OWE support; without it NetworkManager cannot connect on the MT6628. See `docs/wifi-mt6628.md`.
5. **Root filesystem**: `UI_PROFILE=phosh sh linux-port/build-rootfs.sh`. The other profile, `labwc`, is the known-good rescue desktop. The script bootstraps Alpine through qemu-arm binfmt into `/opt/b8000h/rootfs` and installs the files from `linux-port/rootfs/` (`b8k-*` helpers, waybar, Firefox files, icons, the galculator GTK theme) and `linux-port/gpu/` (the `b8k-gpu` service). It also installs `/opt/b8000h/out/wvkbd-b8k` if present; build that first with `linux-port/wvkbd/build.sh`.
   - Despite its name, the `phosh` profile neither runs nor installs Phosh. See "Tablet shell" below. The audio stack (PulseAudio, ALSA tools, UCM) and gnome-settings-daemon used to come in as Phosh dependencies; they are now listed explicitly.
   - `00-b8k-modules.start` exists in two copies, in `build-rootfs.sh` and in `make-release-rootfs.sh`. Change both together.
   - `build-emmc-root-image.sh` packs that tree as a sparse ext4 image of at most 780 MB, because LK reports userdata as 800 MiB.
   - `linux-port/rootfs/make-release-rootfs.sh TAR BOOT OUTDIR [AUDIT]` takes a tar of a working tablet and builds the public image. It strips personal data and every Android binary, then fails if any audit pattern still occurs in the result.
   - `tools/mk-sd-installer.py root.ext4 boot.img out.img` builds the microSD installer image (`B8KINST1` format, consumed by `initramfs/init`).
6. **GPU stack (built on the tablet itself)**: an armhf Debian "island" chroot in `/opt/debian` (`linux-port/hybris/debian-enter.sh`). In it, `build-hybris.sh` builds libhybris for the Android 4.4.2 / API 19 blobs and `build-phoc.sh` builds phoc 0.55.1 with `wlroots-0.20-hybris.patch`. Only phoc renders through GLES 2.0 on the SGX544. GTK4 needs ES 3.0, so apps stay on the CPU. Design notes: `docs/g4-hybris.md`.

## Tablet shell

The session in `/etc/local.d/desktop.start` runs `phoc-gpu ... -E b8k-launcher` in a restart loop. `b8k-launcher` starts the wallpaper, the keyboard (`b8k-keyboard` → `wvkbd-b8k`), `b8k-focus watch` and finally `exec waybar`. If waybar dies, the whole session restarts. `docs/shell.md` (Russian) describes the shell in full. The parts that span files:

- **Buttons and state.**
  - Program button ids (`firefox calc weather news max term settings`) are repeated in `waybar/config`, `b8k-launch` (`label_of`/`url_of`/`bin_of`/`aliases_of`), `b8k-focus` (`BUTTONS` with signal numbers) and `b8k-ask` (`NAMES`/`ALIASES`). Change all of them together.
  - `b8k-focus watch` writes `$XDG_RUNTIME_DIR/b8k-launch/state` and signals waybar with `SIGRTMIN+N`. It signals only when `/proc/<pid>/status` `SigCgt` shows that waybar catches that signal. An early signal killed waybar and caused the boot flashing.
- **Web-app windows.**
  - `firefox/b8k-webapp.js` runs in Firefox through autoconfig. It picks up URLs that `b8k-launch` writes to `$XDG_RUNTIME_DIR/b8k-webapp.d/` and opens them in chromeless windows.
  - Its `ALLOW` keys must match `url_of` in `b8k-launch` exactly. `HOSTS` limits each window to its own site; foreign pages move to a normal window.
- **Runtime files** go to `$XDG_RUNTIME_DIR`, which `pam_elogind` sets up through `b8k-session`. Never use `/tmp`.
- **Power.**
  - `b8k-ask power` calls `doas -n /usr/local/sbin/b8k-poweroff off|reboot`. The doas rule is written by `make-release-rootfs.sh`.
  - The power key only toggles the backlight (`b8k-sleep.py`), because suspend does not resume the panel or Wi-Fi.

## Architecture constraints that span files

- **No Android blobs in the repo or in images.** At boot, `linux-port/rootfs/b8k-android-system.initd` mounts the tablet's own stock `system` partition read-only. The PowerVR, gralloc and Wi-Fi firmware blobs are loaded from there. Never add proprietary binaries to the tree or to `make-release-rootfs.sh` output.
- **Storage safety.** The installer (`initramfs/init`, `install-root-to-emmc.sh`, `tools/*-acm.ps1`) writes only USRDATA (`mmcblk1p20` under the install DTB), and only after checking the DTB, the SD root device and the exact size of 27589888 sectors. Never write BOOT, RECOVERY, NVRAM, PRO_INFO, PROTECT_F/S or the partition table. Partitions in the development DTS are marked `read-only`. Keep these guards when you edit installer code.
- **Display path.** The display uses mediatek-drm over DSI and takes over the LK-initialized pipeline (patches 0017–0036: OVL/RDMA FIFO, BLS backlight, CRTC kept on across DPMS, cached dumb buffers). Runtime tuning such as the RDMA FIFO threshold lives in `linux-port/rootfs/b8k-rdma-fifo.py`. `docs/scroll-tear.md` explains why.
- **Stability workaround.** The CPU is pinned to 806 MHz by `/etc/local.d/00-b8k-stability.start` on the device.
- **Reboot.** `mtk_wdt_restart` resets through TOPRGU (`0x10000000`), leaving `WDT_MODE` as LK set it (`0x79`). Without `EXRST_EN` (bit 2) the preloader keeps the tablet off. `rootfs/b8k-wdt-exrst` sets the bit at boot, and a successful reboot shows `boot_reason=4` in `/proc/cmdline`. Access `/dev/mem` there with 32-bit reads and writes only: a byte store raises a bus error.
- **Wi-Fi hangs.** `wlan_mt6628` sometimes stops passing traffic during large transfers (an scp of a few MB) while the link stays up. `rootfs/b8k-netwatch` pings the gateway and reloads the driver once; the panel shows `wifi-x.svg`. Copy big files to the tablet over SSH in pieces of about 128 KB with pauses. Pushing base64 through the ACM shell lost data.
- **Wi-Fi MAC.** Without Android NVRAM, `wlan_mt6628` builds its MAC from the timer, and the MAC cannot be changed after the module loads. The module takes a `mac=` parameter, which the boot script derives from `/etc/machine-id`.
- GPU module sets are keyed by kernel build number (`/root/kN`, e.g. `K=k22` in `make-release-rootfs.sh`). `g3-run.sh` picks the set that matches the running kernel.

## Windows tooling

`tools/*.ps1` and the SP Flash Tool XML configs are for Windows. They handle readback and flashing over the MediaTek preloader and stream images through the recovery USB ACM shell (`acm-shell.ps1`, `stream-emmc-acm.ps1`). Multiple versions of SP Flash Tool are kept under `external/spft-*`.

## Line endings

`.gitattributes` forces LF for `*.sh`, `*.patch`, `*.py`, `initramfs/init`, `Makefile` and `Kbuild`. Keep LF when you create files of these types, because the repo is also edited from Windows.
