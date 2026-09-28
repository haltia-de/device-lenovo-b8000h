# G2: `pvrsrvkm.ko` DDK 1.12 для MT6589 и ядра #20

Дата сборки: 2026-09-28. Этап G2 завершён как **сборка и статическая
проверка**. Планшет в ходе работы не подключался; `insmod` и инициализация
userspace на железе не проверялись.

## Результат

- Модуль: `artifacts/linux-port/pvrsrvkm/pvrsrvkm.ko`
- Размер: `2047488` байт.
- SHA-256: `16c8c0a4a02425b608a92d93d133c41cf7cb9ee13e0c42b5b27d283824e856f7`.
- Исходный DDK: `SGX_DDK_Android sgxddk 1.12@2701748`.
- Сборка: внешний модуль против `/opt/b8000h/linux-drm`; дерево ядра не
  изменялось и не пересобиралось.
- `build.sh` каждый раз удаляет только разрешённый staging
  `/opt/b8000h/pvrsrvkm-kbuild`, заново копирует исходники и выполняет
  `make -C /opt/b8000h/linux-drm M=... ARCH=arm LLVM=1 modules`.

Финальная чистая сборка завершилась с `exit=0`, включая `MODPOST` и
`LD [M] pvrsrvkm.ko`. Вывод `modinfo`:

```text
filename:       .../artifacts/linux-port/pvrsrvkm/pvrsrvkm.ko
author:         Imagination Technologies Ltd. <gpl-support@imgtec.com>
description:    PowerVR SGX544 Services kernel module for MT6589
license:        Dual MIT/GPL
parm:           gPVRDebugLevel:Sets the level of debug output (default 0x7)
alias:          of:N*T*Cmediatek,mt6589-gpu
alias:          of:N*T*Cmediatek,mt6589-gpuC*
depends:
vermagic:       7.1.0-b8000h20-g7dd245445adf-dirty SMP mod_unload ARMv7 thumb2 p2v8
```

`include/config/kernel.release` содержит
`7.1.0-b8000h20-g7dd245445adf-dirty`: release в `vermagic` совпадает.
`llvm-nm -u` нашёл 142 внешних символа; все 142 присутствуют в
`/opt/b8000h/linux-drm/Module.symvers`, отсутствующих символов — 0.

## ABI и build options

Итоговый модуль принудительно включает точный MTK
`include4/config_kernel_user.h`. Makefile/config из generic openpvrsgx не
используется для выбора ABI: там возможны `SUPPORT_DRI_DRM` и два MP-ядра,
что несовместимо со стоковым Android UM B8000-H.

| Опция | MTK user | Generic openpvrsgx | Итог | Причина/влияние |
|---|---:|---:|---:|---|
| DDK | `1.12@2701748` | код той же версии | `1.12@2701748` | Точная версия bridge и структур. |
| `SGXCORE`, `SGX544`, `SUPPORT_SGX544`, `SUPPORT_SGX` | `544`, да | зависит от target | без изменений | Выбран SGX544. |
| `SGX_CORE_REV` | `115` | зависит от target | `115` | Точная ревизия MT6589. |
| `SGX_FEATURE_MP` | да | для SGX544 возможно да | да | Входит в SGX ABI/options. |
| `SGX_FEATURE_MP_CORE_COUNT` | `1` | generic SGX544 задаёт `2` | `1` | Generic-значение отвергнуто; у MT6589 одно ядро. |
| `SGX_FEATURE_SYSTEM_CACHE` | да | зависит от target | да | Входит в `SGX_BUILD_OPTIONS`. |
| `TRANSFER_QUEUE` | да | да | да | Состав SGX bridge/очередей совпадает. |
| `PVR_SECURE_HANDLES` | да | да | да | Тип и проверка bridge handles совпадают. |
| `SUPPORT_LISR_MISR_SYNC` | да | не является общей гарантией generic target | да | Сохранён MTK interrupt ABI. |
| `SUPPORT_PERCONTEXT_PB` | да | да | да | Входит в `SGX_BUILD_OPTIONS`. |
| `SUPPORT_HW_RECOVERY` | да | да | да | Входит в `SGX_BUILD_OPTIONS`. |
| `SUPPORT_ACTIVE_POWER_MANAGEMENT` | да | да | да | Входит в options и включает MTK power callbacks. |
| `SUPPORT_SGX_HWPERF` | да | да | да | Входит в options и init-структуры. |
| `SUPPORT_SGX_LOW_LATENCY_SCHEDULING` | да | да | да | Входит в options. |
| `SUPPORT_SGX_PRIORITY_SCHEDULING` | косвенно | зависит от target | да, косвенно | Автоматически определяется `sgxfeaturedefs.h` из low-latency и даёт bit20. |
| `SUPPORT_MEMINFO_IDS` | да | generic kernel Makefile не включает | да | Сохранена MTK структура meminfo. |
| `SUPPORT_SGX_NEW_STATUS_VALS` | да | да | да | Совпадает набор status values. |
| `SUPPORT_DBGDRV_EVENT_OBJECTS` | да | зависит от target | да | Сохранён MTK bridge ABI. |
| `SYS_USING_INTERRUPTS` | да | да | да | IRQ берётся из DT. |
| `PVRSRV_NEW_PVR_DPF`, `PVRSRV_NEED_PVR_DPF` | да | частично | да | Формат KM logging как у MTK. |
| `SUPPORT_ION` | да | Android-target: да | да | Bridge ABI сохранён; реализация импорта временно возвращает `NOT_SUPPORTED`. |
| `SUPPORT_PVRSRV_DEVICE_CLASS` | да | да | да | Device-class ABI сохранён. |
| `SGX_DYNAMIC_TIMING_INFO` | да | generic kernel Makefile выключает | да | Возвращается MT6589 286 МГц. |
| `SYS_CUSTOM_POWERLOCK_WRAP` | да | да | да | Перенесён на современный mutex/in_interrupt. |
| `PVR_LINUX_USING_WORKQUEUES` | да | да | да | Без расхождения. |
| `PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE` | да | да | да | Без расхождения. |
| `PVR_LINUX_TIMERS_USING_WORKQUEUES` | да | да | да | Без расхождения. |
| `PVR_LINUX_MEM_AREA_POOL_MAX_PAGES` | `5400` | target-зависимо | `5400` | Точное MTK значение. |
| `PVR_LINUX_MEM_AREA_USE_VMAP` | да | target-зависимо | да | Точный MTK memory path. |
| `PVR_LINUX_MEM_AREA_POOL_ALLOW_SHRINK` | да | target-зависимо | да | Точный MTK path, API shrinker портирован. |
| `SUPPORT_LARGE_GENERAL_HEAP` | да | target-зависимо | да | Сохраняет MTK layout heaps. |
| `ANDROID` | да | generic kernel Makefile выключает | да | Нужен стоковому Android UM. |
| `SUPPORT_PVRSRV_ANDROID_SYSTRACE` | да | generic kernel Makefile выключает | да | ABI сохранён; backend заменён безопасной заглушкой. |
| `PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC` | да | Android-target: да | да | Структуры bridge сохранены; legacy sync backend пока заглушен. |
| `USE_RGBA_8888_FB` | да | target-зависимо | да | Точный MTK pixel format. |
| `SUPPORT_LINUX_X86_WRITECOMBINE`, `SUPPORT_LINUX_X86_PAT` | да | да | да | Нерелевантны ARM, оставлены ради точного MTK config. |
| `RELEASE`, `PVR_BUILD_TYPE` | да, `release` | release | без изменений | Использован production/user config, не eng/debug. |
| `SUPPORT_DRI_DRM` | **нет** | generic может включать | **нет** | Обязателен char-device bridge `/dev/pvrsrvkm`. |
| `PVR_LDM_DEVICE_TREE` | нет в старом MTK | да | да | Единственное интеграционное добавление: bind к DT. Bridge ABI не меняет. |
| `PVR_LINUX_DYNAMIC_SGX_RESOURCE_INFO` | нет в MTK config | доступно | да | MMIO/IRQ берутся из DT, а не из legacy констант. ABI не меняет. |
| `MTK_DEBUG_LFB`, `MTK_DEBUG_TIMER_MONITOR`, `MTK_HAL_MM_STATISTIC`, `MTK_FREQ_OD_INIT` | да | нет | defines сохранены | Старые debug/DVFS внешние API заменены локальными/no-op реализациями; bridge layout не меняется. |

Фактическая маска итогового KM — **`SGX_BUILD_OPTIONS = 0x0036a418`**.
Она подтверждена дизассемблированием `SGXDevInitPart2BW`: `movw #0xa418`,
`movt #0x36`. Установлены HW recovery (bit3), secure handles (bit4), MP
(bit10), system cache (bit13), APM (bit15), per-context PB (bit17), HWPerf
(bit18), priority scheduling (bit20) и low-latency scheduling (bit21).
`MP_CORE_COUNT=1` кодируется нулём в high byte.

В стоковом `/system/vendor/lib/libsrv_um.so` найдены строки:

```text
/dev/pvrsrvkm
Build options %s: client/UM 0x%x, driver/KM 0x%x
SGXConnectionCheck: Build option mismatch, driver contains extra options: %x...
SGXConnectionCheck: Build option mismatch, client contains extra options: %x...
```

В стоковом `pvrsrvctl` также есть `pvrsrvkm` и
`/system/lib/modules/pvrsrvkm.ko`. Числовая UM-маска в stripped `.so` не
найдена как явная строка/32-битный литерал, поэтому независимое сравнение её
числа отложено до реального `SGXConnectionCheck`. Конфигурация KM взята из
точного MTK user header, а не реконструирована по generic target.

## Платформенный слой MT6589

- platform driver привязывается к `compatible = "mediatek,mt6589-gpu"`;
  MMIO resource и IRQ получает через DT (`platform_get_resource`,
  `platform_get_irq`; ожидается GIC IRQ 220 / SPI 188);
- включает `infra_mfgaxi`, clocks DT и отдельный `mfg_hyd`;
- использует runtime PM домена MFG, очищает четыре SRAM_PDN bit и выполняет
  проверенную последовательность MFGReset: SPM RST/ISO/CLK_DIS, mfgsys reset,
  `MFG_CG_CLR = 0xf`, `way_en`;
- сообщает 286 МГц; legacy DVFS отключён;
- не вызывает `enable_clock`, `mt65xx_*`, legacy `mach/*` и не использует
  старые виртуальные адреса `0xF...`;
- **не пишет `CLK_CFG_8`**: `smi_mfg_as_sel` остаётся равным 1.

Путь `probe` совпадает с вендорской двухчастной моделью:
`SysInitialise -> PVRSRVInitialiseDevice -> DevInitSGXPart1`. Part1 выделяет
служебную память/heaps и BIF reset page directory, но не вызывает
`OSReadHWReg`/`OSWriteHWReg` по SGX aperture. `DevInitSGXPart2KM` доступен
только через userspace bridge. Следовательно, `insmod` не должен читать
`0x13000020` и другие SGX-регистры.

## Изменения относительно MTK DDK

1. Linux env (`osfunc`, `mm`, `mmap`, `module` и связанные файлы) взят единым
   набором из Linux-7-порта openpvrsgx. Точные MTK bridge code,
   `pvr_bridge_k.c`, SGX device code, headers и `ion.h` сохранены.
2. Добавлены compat headers для удалённых kernel API и новые module/class,
   shrinker, uaccess/proc API. В `module.c` добавлен MT6589 DT match.
3. MTK `sysutils_linux.c` заменён CCF/runtime-PM реализацией на основе
   проверенного `sgx_probe`, без чтения SGX на probe.
4. Legacy MTK DVFS заменён фиксированными 286 МГц; telemetry callbacks
   локализованы, так как provider MTK в ядре #20 отсутствует.
5. На Linux 7 не экспортирован `__vmalloc_node_range_noprof`; fallback
   использует экспортированный `__vmalloc`.
6. Legacy ION и Android sync timeline удалены из Linux 7. Bridge ABI и
   структуры сохранены, но ION import и fence-backed операции пока возвращают
   `PVRSRV_ERROR_NOT_SUPPORTED`.
7. Отдельный legacy `mtklfb.ko` сделан опциональным и в G2 не собирается: он
   зависит от старых Android `xlog`/fbdev API и не входит в требуемый
   `pvrsrvkm.ko`.
8. Добавлено только описание модуля; warning modpost устранён.

## Риски первого запуска и нерешённые вопросы

- Модуль **не проверен на планшете**. Успешная сборка не доказывает успешный
  `probe`, создание `/dev/pvrsrvkm` или работу SGX.
- На `insmod` возможна ошибка DT resource/IRQ, clock provider, runtime PM или
  выделения Part1. Если после последнего сообщения до возврата команды система
  зависнет, нельзя переходить к userspace init; нужен анализ pstore.
- `__vmalloc` на Linux 7 не принимает старый `pgprot_t`. Это может дать
  cache-aliasing для редкого vmalloc fallback; нужно наблюдать Part1/Part2 и
  при необходимости заменить его полноценным современным allocator port.
- ION import и Android native fences пока не работают. Простая Services init и
  pbuffer без внешнего ION могут пройти, но gralloc/window render, импорт
  буферов и синхронизация могут вернуть `NOT_SUPPORTED`.
- `mtklfb` отсутствует. `/dev/pvrsrvkm` и offscreen init от него не зависят,
  но старый Android framebuffer display-class потребует отдельного порта либо
  иного display path.
- Первая реальная проверка `SGX_BUILD_OPTIONS` произойдёт в стоковом UM. Любой
  mismatch надо зафиксировать целиком; маску не следует обходить вручную.
- На `pvrsrvctl --start --no-module` Part2 впервые обращается к SGX MMIO и
  загружает microkernel. Возможны AXI hang, несовпадение firmware/структур,
  cache/MMU fault или IRQ timeout. Нужны последовательный лог и pstore; голые
  чтения SGX до Part2 запрещены.
- Стоковый `pvrsrvctl` требует подготовленную bionic/property среду, описанную
  в `docs/gpu-sgx-hang-audit.md`; без неё его падение не является результатом
  теста KM.

Рекомендуемый следующий этап: сначала только контролируемый `insmod` с
проверкой bind, `/dev/pvrsrvkm`, IRQ/clocks/runtime-PM и отсутствия SGX MMIO;
затем отдельной загрузкой — `pvrsrvctl --start --no-module` или native G2b
bridge trigger. В этой работе ни одна из этих команд на планшете не выполнялась.
