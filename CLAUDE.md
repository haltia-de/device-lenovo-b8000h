# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An experimental port of Linux 7.1 + Alpine 3.24 armv7 to the Lenovo YOGA Tablet 10 **B8000-H** (MT8389/MT6589, 3G only, board `BLADE10_ROW_3GDATA`, stock firmware `B8000_A442_001_020_140620_WW_3GDATA`). Alpha quality. Until early October (kernel 22 era) the kernel crashed every 10–40 minutes with memory corruption; kernel 23 passed a one-hour full-load soak at 1.2 GHz with a 150 MB memory-pattern check and no errors. User-facing docs (`README.md`, `docs/*.md`) are written in **Russian**. Code comments and commit messages are in English: full-sentence subjects for fixes ("Pin the RDMA FIFO threshold that stops the Firefox scroll copies."), with `G5 step N:` / `kernel NN:` / `docs:` / `build-phoc:` prefixes for milestone work.

There is no test suite, no linter and no CI. A change is verified by building the images and running them on the tablet. The tablet gives you `dmesg` (often flooded by MSDC debug lines), `/sys/fs/pstore/*` after a crash or reboot, and `/var/log/phosh.log` (the session log; the name is left over from Phosh).

## Working on a connected tablet

- **SSH:** `root@<ip>` with a key in `/root/.ssh/authorized_keys`. The tablet's IP comes from DHCP, so scan for an `OpenSSH_10.3` banner on the LAN to find it.
- **USB serial (development rootfs only):** `/dev/ttyACM0` on the host is a raw root shell (`acm-shell` on `ttyGS0`). It has no prompt or echo, so wrap each command between marker lines. The host user needs `dialout` group membership or an ACL. `make-release-rootfs.sh` removes this shell.
- **Running things in the session:** run them as `user` with the environment of the `waybar` process (`/proc/<pid>/environ`: `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `DBUS_*`). `grim` takes screenshots. `b8k-launch start <id>` opens programs.
- **Power:** a failed `reboot` used to leave the tablet off; that is fixed by `b8k-wdt-exrst`. After `poweroff` with USB attached, LK shows the charging animation and stays off. Either way the user must press Power to bring it back, so ask before you power the tablet off.
- **Gotchas:**
  - The Python helpers run as `python3 /usr/local/bin/<name>`, so `pkill -x <name>` and `killall <name>` miss them. Find the pid with `ps -o pid,args`.
  - `b8k-focus watch` reads `apps.conf` only at start. After you change it, restart the watcher, or a new program never shows its state and never asks to close. Its `ps` line starts with `{b8k-focus}`, so the script path is the fourth field, not the third.
  - Firefox reads `apps.conf` (for the web apps) only when it starts.
  - `pkill -f <pattern>` run over SSH also matches the remote shell that runs it, and kills your own command.
  - Killing `waybar` restarts the whole session and closes all programs. `kill -USR2 $(pidof waybar)` reloads its config and CSS in place.
  - Children of `b8k-launcher` that exit become zombies, because it `exec`s into waybar, which does not reap them. `pidof` still lists them.
  - Only one `wvkbd` may run: a second input-method client makes phoc misbehave.
  - The USB ACM console does not come back after s2idle, and the host ACL on `/dev/ttyACM0` is lost when the device re-enumerates.

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

The session in `/etc/local.d/desktop.start` runs `phoc-gpu -C /usr/local/share/b8k/phoc.ini -E b8k-launcher` in a restart loop. `b8k-launcher` sets `NO_AT_BRIDGE`/`GTK_A11Y`, restores the brightness (`b8k-brightness --restore`), and starts the volume keys (`b8k-volume`), the keyboard (`b8k-keyboard` → `wvkbd-b8k`) and `b8k-focus watch`. It ends with `exec waybar`. If waybar dies, the whole session restarts. The panel is 120px wide on the left edge. `docs/shell.md` (Russian) describes the shell in full, including the palette. The parts that span files:

- **Buttons and state.**
  - Every program is one section of `rootfs/apps.conf` (installed to `/usr/local/share/b8k/apps.conf`): `label`, panel `signal` (0 = no button), window `app_ids`, `exec` (and `env`), or `url` and `host` for a web app. `b8k-launch` reads it with awk on every call, while `b8k-focus`, `b8k-ask` and `firefox/b8k-webapp.js` read it once at start. A program with a panel button also needs its module in `waybar/config` and `style.css`, with the same signal.
  - The terminal has no panel button. It sits in the power menu (`b8k-ask power`).
  - `b8k-launch` tests for a running Firefox with `alive`, not `pidof`. Programs the panel started become zombies when they exit, and `pidof` lists the zombies too.
  - `b8k-focus watch` writes `$XDG_RUNTIME_DIR/b8k-launch/state` and signals waybar with `SIGRTMIN+N`. It signals only when `/proc/<pid>/status` `SigCgt` shows that waybar catches that signal. An early signal killed waybar and caused the boot flashing.
- **Web-app windows.**
  - `firefox/b8k-webapp.js` runs in Firefox through autoconfig. It picks up URLs that `b8k-launch` writes to `$XDG_RUNTIME_DIR/b8k-webapp.d/` and opens them in chromeless windows.
  - It builds `ALLOW` (page → title `U+2060 + label`) and `HOSTS` from the `url` and `host` of `apps.conf`. `HOSTS` limits each window to its own site; foreign pages move to a normal window.
- **Runtime files** go to `$XDG_RUNTIME_DIR`, which `pam_elogind` sets up through `b8k-session`. Never use `/tmp`.
- **Layers.**
  - waybar stays on the `top` layer. The keyboard runs on `bottom` (`--wayland-layer bottom`). Then phoc arranges the panel first, and the keyboard gets the width right of it.
  - With the keyboard on `top`, it squeezed the left panel to 440px. waybar asked for 800px again, and phoc looped (`Timeout ... configures pending` in the log), so the keyboard never appeared.
  - Moving waybar to `overlay` blanked the screen.
- **Dialogs** (`b8k-ask`, `b8k-brightness`) are undecorated dark windows.
  - Once mapped, each asks `b8k-focus activate <app_id>` to raise it. A second launch, which finds the lock taken, raises the open one, because a dialog opened during boot could end up under Firefox.
  - Python GTK3 programs must call `gi.require_version("Gdk", "3.0")` as well as `Gtk`: GTK4 is installed too, and gi otherwise loads Gdk 4.
- **Power key** (`b8k-sleep.py`, root, nice -10).
  - A short press toggles the backlight on release. A one-second hold opens `b8k-ask power`. Ten minutes without input from any device (read ungrabbed) turn the screen off the same way (`IDLE_OFF`). 30 s after the screen goes off, `b8k-suspend` runs s2idle.
  - BLS latches a new PWM duty only at the start of a frame, so after each change the handler runs `grim` into `/dev/null` to make phoc draw one.
  - Programs for the session start with the uid and an allowlisted environment of the real `/usr/bin/waybar` process. That uid must belong to `/etc/b8k/session-user`.
  - `b8k-ask power` calls `doas -n /usr/local/sbin/b8k-poweroff off|reboot`. The doas rule is written by `make-release-rootfs.sh`.
- **Brightness and volume.**
  - `b8k-brightness` sets the level through logind `Session.SetBrightness` and keeps it in `~/.config/b8k/brightness`.
  - `b8k-volume` steps the PulseAudio sink by 5% (at most 100%) and signals the panel's volume module with `SIGRTMIN+8`.
  - The hardware `Speaker` gain is pinned at step 7 by a PulseAudio profile set (`rootfs/pulse/`, selected by a udev rule), because steps 0–2 crackle.
- **Tap feedback.**
  - Every panel `on-click` starts with `b8k-press <id> <signal>` (`tools-arm/b8k-press.c`, built by `build-b8k-press.sh`). It marks `$XDG_RUNTIME_DIR/b8k-press/<id>` for 300 ms, which `b8k-launch status` and `b8k-meter` turn into the `pressed` class, and buzzes the motor for 70 ms.
  - waybar ignores `:active` on touch. Panel signals: programs 1–7, volume 8, brightness 9, Wi-Fi 10, battery 11.
  - The motor is the `regulator-haptic` input device on the MT6320 VIBR LDO (patch 0039). Pulses shorter than about 50 ms are not felt.
  - The keyboard buzzes on every key press too (`wvkbd/0005-haptic.patch`, same 70 ms).
- **Hiding the panel.** The "‹" button under the clock runs `b8k-tab` (`tools-arm/b8k-tab.c`, built by `build-b8k-tab.sh` against the keyboard's sysroot and wvkbd's `proto/`). It sends waybar `SIGUSR1` (invisible mode, exclusive zone 0) and puts a 120×44 "›" tab on the overlay layer exactly where the button was. A tap on the tab sends `SIGUSR1` again and exits. Keep the `#custom-hide` size (40 px + 2 px margins, 30 px icon) in step with `W`/`H` and the arrow in `b8k-tab.c`. `SIGUSR1` toggles, so never send it to waybar by hand while a tab is up.
- **Palette.** The frame is dark and the content light, in GNOME/libadwaita colours with the accent `#3584e4`. The palette table in `docs/shell.md` lists every file that repeats it. The keyboard colours are compiled into `wvkbd/config.b8k.h`.

## Architecture constraints that span files

- **No Android blobs in the repo or in images.** At boot, `linux-port/rootfs/b8k-android-system.initd` mounts the tablet's own stock `system` partition read-only. The PowerVR, gralloc and Wi-Fi firmware blobs are loaded from there. Never add proprietary binaries to the tree or to `make-release-rootfs.sh` output.
- **Storage safety.** The installer (`initramfs/init`, `install-root-to-emmc.sh`, `tools/*-acm.ps1`) writes only USRDATA (`mmcblk1p20` under the install DTB), and only after checking the DTB, the SD root device and the exact size of 27589888 sectors. Never write BOOT, RECOVERY, NVRAM, PRO_INFO, PROTECT_F/S or the partition table. Partitions in the development DTS are marked `read-only`. Keep these guards when you edit installer code.
- **Display path.** The display uses mediatek-drm over DSI and takes over the LK-initialized pipeline (patches 0017–0036: OVL/RDMA FIFO, BLS backlight, CRTC kept on across DPMS, cached dumb buffers). Runtime tuning such as the RDMA FIFO threshold lives in `linux-port/rootfs/b8k-rdma-fifo.py`. `docs/scroll-tear.md` explains why.
- **CPU frequency.** The CPU runs at up to 1209 MHz (vproc 1.2 V). An old 806 MHz cap (`00-b8k-stability.start`) was never part of the build and is gone. There is no thermal zone and no thermal throttling, so watch for heat under long load.
- **Reboot.** `mtk_wdt_restart` resets through TOPRGU (`0x10000000`), leaving `WDT_MODE` as LK set it (`0x79`). Without `EXRST_EN` (bit 2) the preloader keeps the tablet off. `rootfs/b8k-wdt-exrst` sets the bit at boot, and a successful reboot shows `boot_reason=4` in `/proc/cmdline`. Access `/dev/mem` there with 32-bit reads and writes only: a byte store raises a bus error.
- **Wi-Fi hangs.** `wlan_mt6628` sometimes stops passing traffic during large transfers (an scp of a few MB) while the link stays up. `rootfs/b8k-netwatch` pings the gateway and reloads the driver once; the panel shows `wifi-x.svg`. It also reloads once when the link has been gone for a minute with Wi-Fi switched on. Copy big files to the tablet over SSH in pieces of about 128 KB with pauses. Pushing base64 through the ACM shell lost data.
- **Sleep.** Only s2idle exists (there is no SPM code for MT6589, so no `deep`). The screen and touch survive it. The power key's PMIC IRQ wakes the tablet (`wakeup-source` in `mt6320.dtsi`). `wlan_mt6628` does not survive it, so `b8k-suspend` unloads `wlan_mt6628` and `wmt` before the sleep and reloads them after. bq24190 logs harmless "Transfer while suspended" warnings.
- **Power use.** The empty microSD slot (MSDC1, `11240000.mmc`) has no card detect and was polled every second (20 IRQ/s, an error line each time); `b8k-sdslot` unbinds it 20 s after boot when no card is in. `wlan_mt6628` takes `debug=3` (errors and warnings): its default logged every packet. phoc runs without `-v`, which logged every frame into `phosh.log`. Polling loops read sysfs with the `read` builtin rather than `cat`, and the panel polls Wi-Fi, the drawer handle and the battery every 30 s. The kernel still ticks at `HZ=1000` on all four cores through the GPT broadcast (about 3000 wakeups a second); see `docs/power.md`.
- **Memory.** Kernel 23 has zram. `b8k-zram` makes 512 MB of lz4 swap at boot, with `swappiness` 100.
- **Wi-Fi MAC.** Without Android NVRAM, `wlan_mt6628` builds its MAC from the timer, and the MAC cannot be changed after the module loads. The module takes a `mac=` parameter, which the boot script derives from `/etc/machine-id`.
- GPU module sets are keyed by kernel build number: `/usr/lib/b8k/kN` (the old development tablet used `/root/kN`). `b8k-gpu` and `g3-run.sh` pick the set that matches the running kernel, and `make-release-rootfs.sh` keeps every set it finds.

## Windows tooling

`tools/*.ps1` and the SP Flash Tool XML configs are for Windows. They handle readback and flashing over the MediaTek preloader and stream images through the recovery USB ACM shell (`acm-shell.ps1`, `stream-emmc-acm.ps1`). Multiple versions of SP Flash Tool are kept under `external/spft-*`.

## Line endings

`.gitattributes` forces LF for `*.sh`, `*.patch`, `*.py`, `initramfs/init`, `Makefile` and `Kbuild`. Keep LF when you create files of these types, because the repo is also edited from Windows.
