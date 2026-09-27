# MT6628 WLAN W3 — первый запуск на железе (2026-09-27)

## W3b: сверка HIF, исправления и тест ядра #18 (2026-09-27)

### Что сверено с вендором

- Direct-SDIO порт повторял логический порядок vendor `mtk_wcn_hif_sdio`:
  `sdio_enable_func` → block size 512 → claim IRQ → client probe → driver own
  (`WHLPCR`) → проверка `WCIR` → `WHIER` → FW download в polling mode.
- Существенное аппаратное отличие: на MT6589 вендорский `sdio_claim_irq()`
  маршрутизируется через отдельный level-low **EINT9/GPIO128**, а текущий upstream
  host использует in-band SDIO IRQ/DAT1.
- Ядро #18 подтвердило исправление DTS: `mmc2` работает в **4-bit**,
  SD high-speed, 25 МГц (actual 24.375 МГц). Гипотеза о реально однобитной
  шине для этого BOOT исключена.

### Исправления

- Коммит `5774dbb`: при ошибке probe после `sdio_claim_irq()` вызывается
  `sdio_release_irq()`. На железе повторный bind дошёл до FW download; ошибки
  `Set IRQ error` нет, в dmesg есть `probe cleanup: released SDIO IRQ`.
- `debug` по умолчанию теперь `47` (без TRACE). Явный setter обновляет
  `aucDebugModule[]`: запись `echo 31 > .../parameters/debug` вернула `rc=0`,
  чтение вернуло `31` (прежний EPERM устранён).
- Добавлена однократная диагностика первого scatter: параметры секции,
  заголовок CMD53, размер/режим, результат `sdio_writesb` и HIF CR.
- Коммит `c7dd831`: для upstream direct-SDIO claim DAT1 IRQ перенесён после
  успешного `wlanAdapterStart()`, чтобы FW download оставался polling-only.

### Результат первого теста (модуль из `5774dbb`)

SHA-256: `812fc3533c3fd0207bef09c9199fa9b3aba20fe8aa1a0d7d3460659e93d00caa`.

```text
FWDL first scatter dst=0x60000 len=2048 seq=1 crc=0xad0947b1 mode=0x80000001 txlen=2072
FWDL before TX WCIR=0x106628 WHLPCR=0x100 WHIER=0xffffff0f WHISR=0x10 WRPLR=0x0 WTSR0=0x0 WTSR1=0x0
FWDL first CMD53: port=0x28 requested=2072 transfer=2560 valid=44800 block=512 blocks=5
FWDL first CMD53 complete: ret=0
FWDL after TX WCIR=0x106628 WHLPCR=0x100 WHIER=0xffffff0f WHISR=0x0 WRPLR=0x0 WTSR0=0x0 WTSR1=0x0
response timeout port=0 polls=12035 elapsed=1001 WRPLR=0x0 WCIR=0x106628 WHLPCR=0x100 WHIER=0xffffff0f WHISR=0x0 WTSR0=0x0 WTSR1=0x0
```

Вывод: SDIO регистры доступны, chip ID верен, driver-own получен, CMD53
завершён без ошибки, но MT6628 не публикует ACK в `WRPLR`. Native DAT1
одновременно даёт шторм MSDC3 (20 000 IRQ примерно за 0.2 с).

### Точка остановки

Финальный модуль после `c7dd831` собран против `/opt/b8000h/linux-drm`,
передан в `/root/wlan_mt6628.ko`; SHA-256
`9ff0cbad7cf46d4cd921e2597747341baf9719d3875ae9578eb079269892ff92`.
После разрешённого `sync; reboot` COM10 не появился за 3 минуты. По условию
аппаратные тесты остановлены; планшету нужна ручная перезагрузка кнопкой.
Версия с отложенным IRQ ещё **не проверена**.

После возврата COM10: проверить `uname -v` (#18+), загрузить `/root/wmt.ko`,
затем `/root/wlan_mt6628.ko debug=47`. Сначала сравнить ACK/`WRPLR` и IRQ;
только при FW ready переходить к `iw dev wlan0 scan` и WPA2.

Состояние: wmt.ko полностью рабочий (патч E2, WIFI ON, SDIO 037a:6628 на 24.4 МГц).
wlan_mt6628.ko (порт gen2, коммит fe555de + незакоммиченные правки, собирается без ошибок)
грузится, probe доходит до wlanAdapterStart и падает на первой загрузке прошивки:
"Firmware scatter download failed!" (wlan_lib.c:1344). nicRxWaitResponse ~9000 опросов, u4PktLen=0 —
ответ от чипа не приходит. Одновременно MSDC3 показывает всплеск прерываний (B8K_IRQDBG calls=20000).

Найденные баги порта:
- После неудачного probe не освобождается SDIO IRQ: повторный bind падает "Set IRQ error"
  (нужен sdio_release_irq в пути ошибки / wlanRemove).
- debug по умолчанию 63 включает TRACE → dmesg забивается "RX TRACE" (использовать debug=47).
- Запись в /sys/module/wlan_mt6628/parameters/debug даёт EPERM.

Что проверить (гипотезы):
1. Шина MSDC3 1-битная (в DTS нет bus-width = <4>) — gen2 может ожидать 4 бита; SDIO IRQ по DAT1
   в 1-битном режиме/без cap-sdio-irq ведёт себя иначе.
2. Порядок до загрузки FW: driver own (LP_OWN/CHLPCR), проверка MCR_WCIR=0x6628, WHIER/WHLPCR;
   залогировать каждое чтение/запись MCR и результат sdio_writesb для первого пакета.
3. Шторм прерываний MSDC3: mtk_sdio_interrupt должен маскировать WHLPCR (INT_EN_CLR) и читать WHISR;
   проверить, что sdio_claim_irq не включён до готовности адаптера.
4. Сравнить с вендорским путём (mtk_wcn_hif_sdio_* через WMT HIF) — там 4-бит и EINT9 (GPIO128)
   как внешнее прерывание SDIO, а не DAT1.

Инструменты: tools/push-file-acm.ps1 (любые файлы на планшет по COM10, ~15 КБ/с),
перезапуск модуля только перезагрузкой (CONFIG_MODULE_UNLOAD выключен).

## Журнал (dmesg)
```
[   32.169931] wmt: loading out-of-tree module taints kernel.
[   32.171359] mt6628-wmt serial0-0: probe: UART3 open, bgf_irq=1 probe_only=0
[   32.440702] mt6628-wmt serial0-0: MT6628 hw=0x8b10 rom=0x8b10
[   32.458486] mt6628-wmt serial0-0: mt6628_patch_e2_0_hdr.bin: part 1/2 address 00 50 08 00
[   32.510895] mt6628-wmt serial0-0: downloaded mt6628_patch_e2_0_hdr.bin (5488 bytes)
[   32.514884] mt6628-wmt serial0-0: mt6628_patch_e2_1_hdr.bin: part 2/2 address 00 50 08 00
[   32.816599] mt6628-wmt serial0-0: downloaded mt6628_patch_e2_1_hdr.bin (34048 bytes)
[   32.846387] mt6628-wmt serial0-0: WMT patch loaded and Wi-Fi function enabled
[   32.846449] mt6628-wmt serial0-0: MT6628 WMT ready at 2000000 baud
[   33.070996] mtk-msdc 11260000.mmc: msdc_track_cmd_data: cmd=52 arg=00000C00; host->error=0x00000002
[   33.071511] mtk-msdc 11260000.mmc: msdc_track_cmd_data: cmd=52 arg=80000C08; host->error=0x00000002
[   33.097029] mmc2: new high speed SDIO card at address 0001
[   35.915724] wlan_mt6628: init: debug=0x2f
[   35.915774] wlan_mt6628: WMT Wi-Fi function ready
[   35.916047] wlan_mt6628 mmc2:0001:1: probe: SDIO 037a:6628 function 1
[   35.916140] wlan_mt6628 mmc2:0001:1: HIF init: SDIO function enabled
[   35.916199] wlan_mt6628 mmc2:0001:1: HIF init: 512-byte block size ready
[   35.916219] wlanNetCreate: (INIT INFO) 
[   35.916224] wireless_dev prWdev(0x7ea8d87a) allocated
[   35.916267] wlanNetCreate: (INIT INFO) 
[   35.916273] wiphy (0xd6cc4e11) allocated
[   35.916319] wlanNetCreate: (INIT INFO) 
[   35.916325] net_device prDev(0x849134d8) allocated
[   35.916386] [MT6628]: 0x220192ba(113328) allocated (wlan/common/wlan_lib.c:wlanAdapterCreate)
[   35.924566] wlan_mt6628 mmc2:0001:1: FW download: loaded WIFI_RAM_CODE_MT6628 (157344 bytes)
[   35.924678] nicAllocateAdapterMemory: (INIT INFO) 
[   35.924685] Allocating 4096 bytes for COMMON MGMT MEMORY POOL.
[   35.924741] [MT6628]: 0x92b908e2(4096) allocated (wlan/nic/nic.c:nicAllocateAdapterMemory)
[   35.924765] nicAllocateAdapterMemory: (INIT INFO) 
[   35.924770] Virtual Address = ef987000 for COMMON MGMT MEMORY POOL.
[   35.924791] nicAllocateAdapterMemory: (INIT INFO) 
[   35.924820] Allocating 11136 bytes for SW_RFB_T.
[   35.924855] [MT6628]: 0x54309385(11136) allocated (wlan/nic/nic.c:nicAllocateAdapterMemory)
[   35.924873] nicAllocateAdapterMemory: (INIT INFO) 
[   35.924878] Virtual Address = efa1b000 for SW_RFB_T.
[   35.924898] nicAllocateAdapterMemory: (INIT INFO) 
[   35.924902] Allocating 18432 bytes for MSDU_INFO_T.
[   35.924931] [MT6628]: 0x8d0190fa(18432) allocated (wlan/nic/nic.c:nicAllocateAdapterMemory)
[   35.924949] nicAllocateAdapterMemory: (INIT INFO) 
[   35.924954] Virtual Address = efa67000 for MSDU_INFO_T.
[   35.928734] Acquire: TC = 0 aucFreeBufferCount = 7
[   35.984967] B8K_REQDBG rq=5000 single=669 multi=4331 sbc=4330 last op=53 blocks=1 blksz=4
[   36.285261] mtk-msdc 11260000.mmc: B8K_IRQDBG calls=20000 empty=0 INT=00003040 INTEN=0000d080 b12=8501 b13=0 b8=7675 b3=4268 b6=0
[   36.413816] B8K_REQDBG rq=10000 single=5669 multi=4331 sbc=4330 last op=53 blocks=1 blksz=4
[   36.841420] B8K_REQDBG rq=15000 single=10669 multi=4331 sbc=4330 last op=53 blocks=1 blksz=4
[   36.929915] **Error[wlan/common/wlan_lib.c:1344]-
[   36.929942] wlanAdapterStart: (INIT ERROR) 
[   36.929966] Firmware scatter download failed!
[   36.929988] [MT6628]: 0x8d0190fa(18432) freed (wlan/nic/nic.c:nicReleaseAdapterMemory)
[   36.930021] [MT6628]: 0x54309385(11136) freed (wlan/nic/nic.c:nicReleaseAdapterMemory)
[   36.930043] [MT6628]: 0x92b908e2(4096) freed (wlan/nic/nic.c:nicReleaseAdapterMemory)
[   36.930092] wlanRemove: (INIT INFO) 
[   36.930098] Remove wlan!
[   36.930116] wlanRemove: (INIT INFO) 
[   36.930121] 0 == u4WlanDevNum
[   36.930143] wlan_mt6628 mmc2:0001:1: probe with driver wlan_mt6628 failed with error -1
```
