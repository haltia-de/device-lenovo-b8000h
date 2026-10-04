# Lenovo Yoga Tablet 10 B8000-H (60047): recovery and Linux port

This workspace is for one confirmed **Lenovo Yoga Tablet 10 B8000-H**, machine
type **60047 / Z0AJ**, with MediaTek MT8389 and 3G data modem.

## Evidence collected

- Windows saw the tablet as `MT65xx Preloader`, USB `VID_0E8D&PID_2000`.
- The device appeared for about eight seconds and disconnected normally for
  preloader mode, but Windows had no matching driver installed.
- Last reported behaviour: intermittent boot followed by stripes on screen.
  This can be corrupted firmware, but it can also be battery/power, display
  path, RAM, or failing eMMC. Preloader enumeration only proves that the early
  MediaTek boot path is alive.
- The matching stock image is
  `B8000_A442_001_020_140620_WW_3GDATA` (Android 4.4.2). Its published archive
  contains `MT6589_Android_scatter_emmc.txt` and
  `preloader_blade10_row_3gdata.bin`.
- A Microsoft Update Catalog driver CAB for `VID_0E8D&PID_2000` is staged in
  `artifacts/drivers/`. Its SHA-256 matches the catalog metadata and its
  `cdc-acm.cat` signature validates. Installing it requires an elevated shell;
  it has not been installed automatically.

## Safety gate before writing flash

1. Install/bind a suitable MTK preloader/VCOM driver and confirm stable
   detection.
2. Read the device information and eMMC geometry.
3. Make readbacks of the complete addressable eMMC and, at minimum, all
   identity/calibration partitions (NVRAM/protect/secro) before any write.
4. Validate that the readback is repeatable with matching hashes. A changing
   or unreadable dump points to hardware trouble, not a ROM-selection problem.
5. Use **Download Only** for the first recovery attempt. Do not use
   **Format All + Download**. Do not write the package preloader unless the
   hardware identity and preloader image have both been verified and there is
   no safer path.

`backups/` is deliberately ignored by Git because it will contain unique device
data such as radio calibration and identifiers. Do not upload it.

## Recovery stages

- **R0 — electrical/visual check:** known-good cable and charger, inspect USB
  socket, battery swelling/voltage, display connector, and reaction with the
  battery disconnected/reconnected according to the service manual.
- **R1 — read-only MTK session:** establish communication and preserve flash.
- **R2 — storage diagnosis:** compare repeated reads and inspect the partition
  table. If writes appear successful but old data remains, suspect read-only or
  worn eMMC.
- **R3 — minimal stock recovery:** write only the necessary boot/system images,
  keeping the original identity partitions and preferably the original
  preloader.
- **R4 — full stock verification:** boot, recovery, touch, display, Wi-Fi,
  charging, audio, and 3G data.

## Full Linux port

The current TeamYogaBlade2 work is a near-mainline Linux 7.1 development tree
for MT6589/MT8125/MT8389. Its postmarketOS package currently selects the
`mt6589-lenovo-b8000-f` device tree (the Wi-Fi/MT8125 model). This tablet is the
**B8000-H/MT8389** variant, so the existing image must not simply be flashed.

The first Linux milestone is a **non-destructive boot test** with serial/pstore
logging and the stock bootloader-provided framebuffer. A B8000-H device tree
must then be derived from the shared `lenovo-blade`/B8000 description using the
stock `blade10_row_3gdata` configuration. Expected later work includes modem,
Wi-Fi/Bluetooth, sound, charging/battery, touch, suspend, thermal control, and
PowerVR SGX544 acceleration.

Imported reference trees live under `external/`. Firmware archives, extracted
images, builds, and unique tablet dumps stay outside Git.
