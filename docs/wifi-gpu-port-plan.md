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

**G1 ход (2026-09-26, `linux-port/drivers/sgx-probe`):**
- SPM на MT6589 заперт: без `SPM_POWERON_CONFIG_SET (0x10006000) = 0x0b160001`
  (`SPM_PROJECT_CODE 0xb16`) записи в `*_PWR_CON` игнорируются → genpd тихо не может
  включить MFG → `-EPROBE_DEFER` без сообщения. **Надо в mtk-pm-domains для mt6589.**
- mainline MFG `sram_pdn_bits = BIT(8)`, вендор снимает `0xf << 8` → после включения
  `MFG_PWR_CON=0x0e0d` (SRAM 9–11 выключены). Исправить на `GENMASK(11,8)`.
- Питание ядра GPU = MT6320 **VRF18_2** (после загрузки выключен) → нужен `*-supply` в узле gpu.
- Вендор (`pvr/services4/system/mt6589/sysutils_linux.c`): 4 клока (HYD тоже!),
  сброс SGX `mfgsys+0xC` 1→0 с CG_SET/CLR, `way_en` infracfg `0x10001200 |= 0x400`.
- С ключом SPM, SRAM 0xf, VRF18_2, клоками, сбросом, PROT_EN=0 — **чтение
  `0x13000000+0x20` всё ещё вешает шину** (watchdog-ребут, pstore сохраняет лог).
  Дальше проверить: родительский PLL `mfg_sel`/`smi_mfg_as_sel` (CLK_CFG_8, вендорский
  `mtk_sysfreq.c` пишет `0x30000`), порядок rail→MTCMOS, HYD-клок в DTS, SYSRAM bank.

- 13:30–13:55 исключено: MMPLL (mfg_sel на univpll1_d4 — висит), шлюз `infra_mfgaxi`
  и клок HYD (включены — висит), питание (MTCMOS подтверждается без VRF18_2 → источник
  VCORE, как у вендорского `test_spm_gpu_power_on`), дословный вендорский MFGReset
  (SPM RST/ISO/CLK_DIS + mfgsys reset + way_en) — висит на первом `readl(0x13000020)`.
- 14:00 исключено ещё: DEVAPC (все D0_APC групп 0–4 = 0, LOCK=0 — доступ открыт),
  MFG CG бит 4 (открыт, `cg=0`). VRF18_2 на этой плате = RF (1.825 В), GPU от VCORE
  1.05 В — VRF18_2 для GPU не включать. mfgsys+0x10/0x14 = `c03fc03f`/`0000c03f` (DCM?).
  Без доступа к регистрам SGX порт DDK бессмыслен — G1 заморожен до новой идеи
  (сравнить с состоянием под стоковым ядром невозможно: Android снесён).
  Прежние гипотезы: DEVAPC (доступ к MFG из non-secure), порядок относительно
  SMI/larb, чтение другого смещения (0x0, 0x14) и проверка через bus-timeout, если он есть.

G2. Ядро: openpvrsgx `pvrsrvkm` 1.17.4948957 (Letux, есть A83T sgx544-115
binding, доведён до 6.3) → 7.1 + MT6589 glue (домен MFG, клоки, IRQ, DVFS нет).

G3. Userspace — главный риск: Linux-библиотек DDK 1.17 для **rev 115** в открытом
доступе не найдено (TI — rev 116, glibc). Варианты: TI 1.17 blobs с проверкой/патчем
ревизии (риск зависаний из-за разных BRN), Debian armhf chroot/rootfs (glibc).
Сток 1.12 — только Android bionic (libhybris, не совместим с wlroots/phoc).
До G3 — ускорять программную отрисовку (llvmpipe/pixman, cpufreq).

## Дисплей: mediatek-drm вместо simpledrm (приоритет после Wi-Fi, SGX заморожен)

Зачем: DPMS (выключение экрана/подсветки), vsync/page flip, имя выхода DSI-1 (без
`fake-builtin`), аппаратные слои OVL. В mainline для MT6589 нет ничего, кроме syscon
`dispsys@14000000`; `DRM_MEDIATEK` выключен. Ближайший поддерживаемый родственник —
**MT2701** (OVL, RDMA, COLOR, BLS, DSI, MIPI-TX).

Исходники вендора (sparse): `mediatek/platform/mt6589/kernel/drivers/dispsys/`
(ddp_ovl/rdma/color/bls/path/reg.h), `.../drivers/video/dsi_drv.c`,
`mediatek/kernel/drivers/video/{mtkfb,disp_drv}.c`.
Панель `cm_hx8896a01_dsi_vdo` (из cmdline LK) в релизе Lenovo отсутствует (конфиг
указывает шаблонную NT35521) → панель не инициализируем, её включает LK;
тайминги/линии снять живьём из регистров DSI после загрузки.

D1. Дамп регистров DSI/OVL/RDMA/MUTEX/MMSYS-путей, настроенных LK →
    тайминги, число линий, формат, маршрут OVL→RDMA→COLOR→BLS→DSI.
D2. Сравнить карты регистров вендора MT6589 с mainline MT2701 (ovl/rdma/color/dsi/
    mipi-tx/mutex/mmsys routing) — объёмная сверка, кандидат на отдельную задачу.
D3. DTS-узлы + данные mt6589 в mtk-mmsys/mtk-mutex/mediatek-drm, панель
    как `panel-simple-dsi` с таймингами из D1 (без init-последовательности).
D4. Подсветка: BLS PWM (ddp_bls) → pwm-backlight.

## eMMC: медленно из-за шторма прерываний MSDC0 (2026-09-26 14:20)

- Чтение 1,5–2,9 МБ/с, запись 0,64 МБ/с при 24 МГц/8 бит (ожидается 15–20 МБ/с).
- **~67–104 тыс. IRQ MSDC0 на 16 МБ** (~2000 на запрос 512 КБ). DMA включён (PIO нет).
- Однажды в покое: `MSDC_INT=0x3040`, `MSDC_INTEN=0xd000` → XFER_COMPL висел разрешённым,
  7600 IRQ/с; ручной W1C `0x1000` → 0, шторм прекратился. После I/O INTEN=0, в покое 0.
- Следующее: отладочный счётчик битов `events & event_mask` в `msdc_irq()`
  (drivers/mmc/host/mtk-sd.c), сверить с вендорским msdc (mt6589) порядок
  очистки XFER_COMPL/DXFER_DONE и остановки DMA.
- HIGH_RES_TIMERS=y собрано, но не активируется: локальные таймеры CPU = dummy_timer
  (`Could not switch to high resolution mode`). Разрешение таймеров остаётся 10 мс.

### D1: регистры DSI, настроенные LK (снято 2026-09-26 14:15 живьём, mmcwp)

Раскладка DSI MT6589 (`dsi_reg.h`) = смещениям mainline `mtk_dsi.c` (как MT2701/8173).

| Рег | Значение | Смысл |
|---|---|---|
| 0x00 START | 1 | видеорежим запущен |
| 0x14 MODE_CTRL | 2 | sync event video mode |
| 0x18 TXRX_CTRL | 0x1003c | LANE_NUM=0xf → **4 линии** |
| 0x1c PSCTRL | 0x30f00 | RGB888 packed, WC=3840 → **1280 px** |
| 0x20/24/28/2c | 2 / 10 / 21 / 800 | VSA / VBP / VFP / **VACT=800** |
| 0x50/54/58 | 0x500 / 0x9c / 0x224 | HSA_WC 1280, HBP_WC 156, HFP_WC 548 (байты, вывести px по формуле вендора `dsi_drv.c`) |

Панель: **1280×800**, 4 lane, RGB888, video sync-event. MIPI-TX (0x10012000):
`00000003 00000821 00000401 00000101 / 00000101 00000101 ... / +0x40: 00000082 88492483 / +0x50: ac415423 00000501`
(PLL/скорость линий — расшифровать по вендорскому `dsi_drv.c` DSI_PHY/MIPITX).
