# B8000-H stock firmware validation

Package: `B8000_A442_001_020_140620_WW_3GDATA.rar`

- Downloaded size: `690819995` bytes
- SHA-256: `BBDB37EFAF67451BA961EDA613A701BEA5DEC554A167EF56AEF4791E6F40C0BA`
- Container: RAR4, 48 files / 4 folders, unpacked size 1,081,524,056 bytes
- 7-Zip archive test: `Everything is Ok`
- Board markers: `blade10_row_3gdata`
- Scatter: `target_bin/MT6589_Android_scatter_emmc.txt`

Important image SHA-256 values:

| Image | SHA-256 |
|---|---|
| `boot.img` | `5000FE8EFAF8A90FAF9FB7EA993E96352341C7687A1582BFD35B4C0EEBC700F9` |
| `fac.img` | `B4C681F5FC7AFD8E4A25D52B4EDD9CF4292FC0B47A5AF00A15E579D4B2E7564D` |
| `kernel_blade10_row_3gdata.bin` | `04B5F4BA12E078566B2E6A5C19FB8DF3B82D714C7EB4185B35F2C2D62A9459BF` |
| `lk.bin` | `33B6140DD98C9DBF081D30DA8A40D386446D489BACD5FCDD1F11C498D54995B0` |
| `preloader_blade10_row_3gdata.bin` | `1311DC9B46B30DB3B4D94F11383060EBD740AF47C223EE915A9764F549026ACD` |
| `recovery.img` | `38B049AFCA4A6A16E270C29C112621E6B276C8CB964016764351D290D058441E` |
| `secro.img` | `ACAF48DE55477D23437A81E27241EC4CFB59186369919FDD942EAE41BC262FB6` |
| `system.img` | `900F892385418ABDDDFAF2D8FCAFFDDB9B15BD926D2568D8B066E193093FBE5D` |

## Scatter-derived readback regions

These boundaries come from this exact package. `USRDATA` extends to a
device-dependent end and must not be guessed before reading eMMC geometry.
`__NODL_*` means the vendor scatter deliberately excludes the region from a
normal download operation.

| Region | Start | Length |
|---|---:|---:|
| `PRELOADER` | `0x00000000` | `0x00600000` |
| `MBR` | `0x00600000` | `0x00080000` |
| `EBR1` | `0x00680000` | `0x00080000` |
| `__NODL_PMT` | `0x00700000` | `0x00400000` |
| `__NODL_PRO_INFO` | `0x00B00000` | `0x00300000` |
| `__NODL_NVRAM` | `0x00E00000` | `0x00500000` |
| `__NODL_PROTECT_F` | `0x01300000` | `0x00A00000` |
| `__NODL_PROTECT_S` | `0x01D00000` | `0x00A00000` |
| `__NODL_SECCFG` | `0x02700000` | `0x00020000` |
| `UBOOT` | `0x02720000` | `0x00060000` |
| `BOOTIMG` | `0x02780000` | `0x00600000` |
| `RECOVERY` | `0x02D80000` | `0x00600000` |
| `SEC_RO` | `0x03380000` | `0x00600000` |
| `__NODL_MISC` | `0x03980000` | `0x00080000` |
| `LOGO` | `0x03A00000` | `0x00300000` |
| `EBR2` | `0x03D00000` | `0x00080000` |
| `__NODL_EXPDB` | `0x03D80000` | `0x00A00000` |
| `FAC` | `0x04780000` | `0x0C800000` |
| `ANDROID` | `0x10F80000` | `0x40000000` |
| `CACHE` | `0x50F80000` | `0x07E00000` |
| `USRDATA` | `0x58D80000` | determine from live eMMC geometry |

Before flashing, preserve at least the full range through `SECCFG`, preferably
the complete eMMC. Do not substitute a different model's scatter.
