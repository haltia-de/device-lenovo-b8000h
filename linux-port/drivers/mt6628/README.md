# MT6628 out-of-tree modules for Lenovo B8000-H

This directory contains the W2 WMT/STP port and the buildable W3 SDIO/cfg80211
front end for the MT6628 combo chip. It replaces the vendor Android integration
points with Linux APIs:

- `wmt.ko`: serdev on UART3, mandatory/full STP framing, DT GPIO/IRQ resources,
  vendor power timing, chip/revision query, 115200 -> 2 Mbaud transition,
  multi-part E1/E2 patch download, WMT Wi-Fi function-on and MSDC3 rescan;
- `wlan_mt6628.ko`: direct SDIO binding for `037a:6628` function 1, 512-byte
  blocks, firmware_class loading, native SDIO IRQ and a cfg80211/nl80211
  station interface named `wlan%d`;
- no WEXT private ioctls, procfs HAL ABI, Android wakelocks, early-suspend,
  BT, FM or GPS drivers.

The hardware constants were checked against the Lenovo GPL sources, not taken
on trust from `docs/mt6628-vendor-map.md`: GPIO14 PMU_EN, GPIO225 RST,
GPIO26/EINT8 BGF (level low), GPIO128/EINT9 Wi-Fi (level low), UART3, MSDC3,
SDIO ID `037a:6628`, function 1 and block size 512. Default delays are RTC
100 ms, off 10 ms, reset 30 ms and on 100 ms.

## Build

From PowerShell (not Git Bash):

```powershell
wsl.exe -d B8000Build -- sh "/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/drivers/mt6628/build.sh"
```

Equivalent inside `B8000Build`, from this directory:

```sh
./build.sh
```

The script refuses to build if `/opt/b8000h/linux/Module.symvers` is absent or
empty. Because Kbuild cannot accept the Windows checkout path containing
spaces as `M=`, the script stages sources in `/opt/b8000h/mt6628-kbuild` and
copies the two `.ko` files back. Build products are ignored by `.gitignore`.

Verified on 2026-09-26 with clang/LLVM 22: both modules build without warnings.

After a successful build, inspect metadata with the target tool (or host
`modinfo` when it understands the ARM module format):

```sh
modinfo ./wmt.ko
modinfo ./wlan_mt6628.ko
```

Verified metadata:

- `wmt.ko`: `mt6628_patch_e1_hdr.bin`, `mt6628_patch_e2_0_hdr.bin`,
  `mt6628_patch_e2_1_hdr.bin`, `WMT.cfg`;
- `wlan_mt6628.ko`: `WIFI_RAM_CODE_MT6628`.

## Firmware

Copy these stock files to `/lib/firmware` on the target before loading WMT:

- `mt6628_patch_e1_hdr.bin`;
- `mt6628_patch_e2_0_hdr.bin` and `mt6628_patch_e2_1_hdr.bin`;
- `WMT.cfg` and `mt6628_ant_m1.cfg`;
- `WIFI_RAM_CODE_MT6628` for the WLAN driver.

The E2 download order and addresses reproduce the stock launcher records:
part `_1` at bytes `21 00 0e f0`, then part `_0` at `22 00 06 00`, with WMT
reset between parts. Firmware copies extracted from the stock system image are
kept under ignored `artifacts/firmware/mt6628/`.

## Hardware test (do not flash from this task)

Use the BOOT containing patch `0016-b8000-h-mt6628-wmt-uart3.patch`, place the
firmware files above, then run:

```sh
insmod /path/to/wmt.ko
dmesg | tail -n 100
find /sys/bus/sdio/devices -maxdepth 2 -type f -name vendor -o -name device
cat /sys/bus/sdio/devices/*/vendor /sys/bus/sdio/devices/*/device
insmod /path/to/wlan_mt6628.ko
ip link show wlan0
```

Expected W2 messages are the detected `hw`/`rom` revision, two successful E2
patch downloads (or one E1 download), `WMT patch loaded and Wi-Fi function
enabled`, followed by an SDIO function with vendor `0x037a`, device `0x6628`.
If initialization fails, record the exact first timeout/unexpected opcode and
all UART/MSDC3 messages; do not repeatedly power-cycle or alter fastboot.

## Current status and hard boundary

W2 is implemented and build-verified but has not yet been run on the tablet.
The WLAN module is still only a W3 front end: after enumeration it can register
`wlan0`, but scan/connect return `-EOPNOTSUPP` and TX is dropped because the
vendor wlan_gen2 firmware loader and NIC command/event engine are not ported.

Consequently the following remain before hardware acceptance:

1. Hardware-test W2 and confirm that MSDC3 enumerates `037a:6628` after WMT
   function-on; adjust only from captured UART/dmesg evidence.
2. Port the GPL wlan_gen2 NIC/mgmt command engine and firmware-section loader,
   then connect it to the direct `sdio_readsb()`/`sdio_writesb()` transport.
3. Validate BGF EINT8 polarity/pin mapping and suspend/resume. The MT6320 RTC
   32 kHz output is already enabled but is not represented by a CCF provider.
4. After W3 is real, verify firmware boot, IRQ polarity, `wlan0`,
   scan, WPA2 association, suspend/resume and unload/reload.  No such test was
   performed by this task.

The kernel DTS change is exported as patch `0016` and is included in the
build-verified `install2` BOOT artifact.
