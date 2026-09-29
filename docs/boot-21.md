# BOOT #21 — возврат дисплея mediatek-drm и подсветки

Ядро #20 собрано с конфигом, перегенерированным из `lenovo-blade_defconfig`
(2026-09-27 18:31), куда никогда не записывали дисплей: `DRM_MEDIATEK` и
`BACKLIGHT_CLASS_DEVICE` выключены, работал только simpledrm поверх конвейера LK.
Следствия: подсветка не гаснет на блокировке; любой драйвер, который включит и
выключит общий такт (так `pvrsrvkm` погасил `smi_sel`), гасит экран.

## Что изменено (патч `linux-port/patches/0035-...`)

- `CONFIG_DRM_MEDIATEK=y`, `CONFIG_DRM_PANEL_SIMPLE=y` (`lenovo,b8000-panel`),
  `CONFIG_PHY_MTK_MIPI_DSI=y` (наш MT6589 MIPI TX, патч 0018 — в 7.1 DRM его больше не
  `select`-ит), `CONFIG_BACKLIGHT_CLASS_DEVICE=y` (`BACKLIGHT_MT6589_BLS=y` уже был);
  `# CONFIG_DRM_SIMPLEDRM is not set`; `LOCALVERSION="-b8000h21"`.
- Всё остальное — как в #20: итоговый `.config` отличается только этим и зависимостями
  (`DRM_GEM_DMA_HELPER`, `DRM_MIPI_DSI`, `VIDEOMODE_HELPERS`, HDMI/CEC-помощники;
  ушли simpledrm/`APERTURE_HELPERS`/`SYSFB`). defconfig проверенно пересоздаёт этот
  `.config` один в один.
- Отладочные `DEBUG_LIST`/`DEBUG_ATOMIC_SLEEP` не включены (требуют `DEBUG_KERNEL`),
  это отдельная сборка для поиска порчи памяти.

## Образ

- `artifacts/linux-port/k21/boot-21.img`, копия `C:\Users\Public\B8000H-flash-kit\
  boot-21.img` + `flash-21.cmd`; SHA-256
  `cd9015ce50804804cc1018c9434d9f28890df6e9dfd64bd99b25bea8bc4302a6`.
- Сборка как у #20: `zImage` + `mt6589-lenovo-b8000-h-install2.dtb` (тот же, что в #20
  побайтно) → `mkmtkimage KERNEL`; ramdisk — ровно рамдиск #20; `mkbootimg` с теми же
  адресами и `--board 39`; заголовок отличается от #20 только `kernel_size`.
- Модули: `modules-21.tar.gz` (в `/lib/modules/7.1.0-b8000h21-g7dd245445adf-dirty/`),
  GPU-модули под #21 — `artifacts/linux-port/k21/{pvrsrvkm,dc_nohw,ion_legacy}.ko`
  (на планшете в `/root/k21/`, `g3-run.sh` выбирает по `uname -r`).

## Установка и откат

1. На работающем #20: распаковать `modules-21.tar.gz` в `/`, GPU-модули в `/root/k21/`.
2. Выключить (Power 15–20 с), Vol-Down + Power → fastboot, `flash-21.cmd`.
3. Проверить: `/sys/class/drm/card*-DSI-1`, `/sys/class/backlight/mt6589-bls`, Phosh на
   DSI-1, яркость, гашение при блокировке, G3 (`g3-run.sh`).
4. Откат: `flash-boot.cmd` (#20 `a895ff8c…`) или `flash-prev.cmd` (#19).
