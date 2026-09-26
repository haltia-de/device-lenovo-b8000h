# План: Wi-Fi (MT6628) и GPU (SGX544) для B8000-H на ядре 7.1

Исследование 2026-09-26. Исходники вендора (Lenovo 3.4, TeamYogaBlade):
`external/b8000-vendor-kernel` (sparse: `mediatek/kernel/drivers/combo`,
`mediatek/platform/mt6589/kernel/drivers/gpu/pvr`, `mediatek/custom/lenovo89_tb_x10_jb2`).
Проект вендора: `lenovo89_tb_x10_jb2` (`mediatek/config/.../ProjectConfig.mk`).

## Факты железа (из вендорского DCT / board.c)

| Что | Значение |
|---|---|
| Комбо-чип | MT6628 (`MTK_COMBO_CHIP=MT6628`): Wi-Fi + BT + GPS + FM |
| Wi-Fi шина | SDIO на **MSDC3** (`CONFIG_MTK_WCN_CMB_SDIO_SLOT 3`), mainline `mmc3: mmc@11260000`, SPI 42 |
| WMT/STP шина | UART3: GPIO219 URXD3 / GPIO220 UTXD3 (mode 1), mainline `uart3: serial@11009000`, SPI 54, без DMA |
| PMU_EN | GPIO14 (out, active high) |
| RST | GPIO225 (out, active high = работа) |
| WIFI EINT | GPIO128 = EINT9, level low |
| BGF EINT | GPIO26 / EINT8 (в mainline-таблице EINT8 = пин 127 "EINT8", проверить), level low |
| 32 кГц | `rtc_gpio_enable_32k(RTC_GPIO_USER_GPS)` — выход 32K из RTC PMIC MT6320 |
| Тайминги питания | `WMT_SOC.cfg`/`WMT.cfg` (`pwr_on_*_slot`), файл прошивки `system/etc/firmware/WMT.cfg` |
| Прошивки | `WIFI_RAM_CODE_MT6628`, `mt6628_patch_e1_hdr.bin`, `mt6628_patch_e2_{0,1}_hdr.bin`, `WMT.cfg`, `mt6628_ant_m1.cfg` (в stock `system.img`) |
| GPU | `MTK_GPU_CHIP=SGX544_115` (ревизия 115), DDK стока **1.12@2701748** (Android) |
| GPU в DTS | mainline `gpu@13000000`, SPI 188, клоки MFG G3D/MEM/AXI, домен MFG есть |
| PMIC EINT | `CUST_EINT_MT6320_PMIC_NUM 3`, level high, пин 37 (EINT3). Исправлено в H DTS. |

**Опасность:** в `mt6589-lenovo-blade.dtsi` у `&mmc3` стоят `vmmc/vqmmc = vemc` —
это питание eMMC. Для SDIO MT6628 регуляторы НЕ указывать (или фиксированные),
иначе mmc3 может выключить питание внутренней памяти.

## Wi-Fi: этапы

W1. Аппаратный подъём (на железе): DTS `mmc3` (4 бит, cap-sdio-irq,
non-removable, keep-power, max 50 МГц для начала, без vemc), `mmc-pwrseq-simple`
(reset-gpios = PMU_EN + RST, clocks = 32K PMIC), `uart3` с пинами. Критерий: SDIO-карта
видна (`/sys/bus/sdio/devices/*/vendor` = `0x037a`, device `0x6628`).
Нужен драйвер выхода 32K из MT6320 RTC (в mainline нет) — маленький патч.

W2. Порт WMT/STP: `combo/common` → модуль для 7.1. STP через **serdev**
на uart3 вместо line discipline + userspace `6620_launcher`. Платформа (GPIO/EINT/32K)
из DT. Загрузка патча `mt6628_patch_*` через `request_firmware`. Критерий: WMT
включает Wi-Fi функцию (`WMT_FUNC_ON wifi`).

W3. Порт wlan: `combo/drv_wlan/mt6628/wlan` (cfg80211, SDIO) на 7.1.
Ориентир по API — автономный порт gen2 для MT6625L (github abbradar/mt6625l-wlan-gen2,
frank-w BPI-R2 5.x). Критерий: `wlan0`, скан, WPA2 через NetworkManager.

## GPU: этапы

G1. (на железе) Включить домен MFG + клоки, прочитать `EUR_CR_CORE_REVISION`
(base+0x14) и `EUR_CR_CORE_ID`. Критерий: ревизия 115, регистры читаются.

G2. Ядро: openpvrsgx `pvrsrvkm` 1.17.4948957 (Letux, есть A83T sgx544-115
binding, доведён до 6.3) → 7.1 + MT6589 glue (домен MFG, клоки, IRQ, DVFS нет).

G3. Userspace — главный риск: Linux-библиотек DDK 1.17 для **rev 115** в открытом
доступе не найдено (TI — rev 116, glibc). Варианты: TI 1.17 blobs с проверкой/патчем
ревизии (риск зависаний из-за разных BRN), Debian armhf chroot/rootfs (glibc).
Сток 1.12 — только Android bionic (libhybris, не совместим с wlroots/phoc).
До G3 — ускорять программную отрисовку (llvmpipe/pixman, cpufreq).
