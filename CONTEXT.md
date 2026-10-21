# Context

The words of this device repository. The project-wide vocabulary (Haltia,
shell, program, kit, hook points, program id) lives in the root repository:
https://github.com/haltia-de/haltia/blob/main/CONTEXT.md

- **Устройство** (device; this repository, `device-lenovo-b8000h`): everything
  tied to the B8000-H — the kernel and its patches, the DTS, the drivers, the
  GPU island, the installer, the image build, and the device profile. It
  builds the Pertin Haltia image from pinned versions of the shell and the
  programs (`linux-port/apps/packages.lock`, `linux-port/apps/sources`).
- **Профиль устройства** (device profile): the hardware-specific helpers and
  numbers the shell plugs in — reboot (`b8k-wdt-exrst`), display tuning
  (`b8k-rdma-fifo`), the SD slot, the USB role, Wi-Fi watchdog and its
  reload around sleep (`b8k-suspend` as the shell's suspend hook), zram, the
  stock `system` mount, the speaker profile, the GPU stack (`b8k-gpu`,
  `/usr/lib/b8k/kN`).
- **Остров** (island): the armhf Debian chroot in `/opt/debian` where phoc
  renders through libhybris and the stock PowerVR blobs.
- **Набор GPU** (GPU set, `kN`): the out-of-tree GPU modules built for kernel
  build N; `b8k-gpu` picks the set matching the running kernel.
- **Установщик** (installer): the `B8KINST1` microSD image consumed by
  `initramfs/init`; it writes only USRDATA after its guards pass.
