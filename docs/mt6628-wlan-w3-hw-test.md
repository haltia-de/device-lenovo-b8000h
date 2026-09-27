# MT6628 WLAN W3 — первый запуск на железе (2026-09-27)

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
