# MT6628 out-of-tree modules for Lenovo B8000-H

This directory is the W2/W3 buildable port skeleton for the MT6628 combo
chip.  It replaces the vendor Android integration points with Linux APIs:

- `wmt.ko`: serdev on UART3, DT GPIO/IRQ/clock resources, vendor power timing,
  `request_firmware()`, and exported reference-counted Wi-Fi power control;
- `wlan_mt6628.ko`: direct SDIO binding for `037a:6628` function 1, 512-byte
  blocks, firmware_class loading, native SDIO IRQ and a cfg80211/nl80211
  station interface named `wlan%d`;
- no WEXT private ioctls, procfs HAL ABI, Android wakelocks, early-suspend,
  BT, FM or GPS drivers.

The hardware constants were checked against the Lenovo GPL sources, not taken
on trust from `docs/mt6628-vendor-map.md`: GPIO14 PMU_EN, GPIO225 RST,
GPIO26/EINT8 BGF (level low), GPIO128/EINT9 Wi-Fi (level low), UART3, MSDC3,
SDIO ID `037a:6628`, function 1 and block size 512.  Default delays are the
vendor values: LDO 100 ms, RTC 100 ms, off 10 ms, reset 30 ms, on 30 ms.

## Build

From PowerShell (not Git Bash):

```powershell
wsl.exe -d B8000Build -- sh "/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/drivers/mt6628/build.sh"
```

Equivalent inside `B8000Build`:

```sh
make -C /opt/b8000h/linux M="$(pwd)" ARCH=arm LLVM=1 modules
```

The script refuses to build if `/opt/b8000h/linux/Module.symvers` is absent or
empty.  Build products are ignored by the local `.gitignore`.

The 2026-09-26 session could not execute this verification: every
`wsl.exe -d B8000Build -- ...` invocation was rejected by the host before the
guest command ran with `Wsl/Service/E_ACCESSDENIED`.  Therefore no successful
build, warning list, `vermagic`, or `modinfo` result is claimed here.

After a successful build, inspect metadata with the target tool (or host
`modinfo` when it understands the ARM module format):

```sh
modinfo ./wmt.ko
modinfo ./wlan_mt6628.ko
```

Expected firmware metadata:

- `wmt.ko`: `mt6628_patch_e1_hdr.bin`, `mt6628_patch_e2_0_hdr.bin`,
  `mt6628_patch_e2_1_hdr.bin`, `WMT.cfg`;
- `wlan_mt6628.ko`: `WIFI_RAM_CODE_MT6628`.

## Current status and hard boundary

The modern bus/platform layer is present, but this is **not yet a functional
Wi-Fi port**.  In particular, the vendor wlan_gen2 command/event engine and
the WMT/STP patch-download framing have not yet been connected to these bus
front ends.  `wlan0` can be registered after SDIO enumeration, but scan and
connect return `-EOPNOTSUPP` and TX is dropped.  This limitation is deliberate:
sending guessed WMT frames to untested hardware would make a clean module
build look more complete than it is.

Consequently the following remain before hardware acceptance:

1. Port the GPL wlan_gen2 NIC/mgmt command engine and firmware-section loader,
   then connect it to the direct `sdio_readsb()`/`sdio_writesb()` transport.
2. Port and validate the MT6628 WMT/STP framing, chip-revision query, UART baud
   transition and E1/E2 patch selection over serdev.
3. Resolve single ownership/order for PMU_EN/RST between WMT and mmc-pwrseq;
   validate the MT6320 RTC 32 kHz provider and BGF EINT pin mapping.
4. On hardware, verify SDIO enumeration, firmware boot, IRQ polarity, `wlan0`,
   scan, WPA2 association, suspend/resume and unload/reload.  No such test was
   performed by this task.

`dt-binding-example.txt` is documentation only.  The kernel DTS is intentionally
unchanged.
