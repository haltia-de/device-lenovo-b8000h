# G3a: фиктивный PowerVR display class `dc_nohw`

Дата: 2026-09-29. Цель — дать стоковому IMG gralloc устройство класса
`DISPLAY` без порта legacy `mtklfb` и без управления реальным дисплеем.

## Результат

Собран отдельный внешний модуль `dc_nohw.ko` для ядра #20 и точного MTK DDK
`1.12@2701748`. Он регистрирует один дисплей `DC_NOHW`, один swapchain и три
буфера в системной памяти. Планшет в рамках G3a не подключался, модуль на нём
не загружался и прошивка не менялась.

Артефакты находятся в `artifacts/linux-port/pvrsrvkm/` (каталог исключён из
git):

| Файл | Размер | SHA-256 |
|---|---:|---|
| `dc_nohw.ko` | 148020 | `5eabbaafbf590b891c04f12d8a6207eef6262d8a1f74689e44ae072fa7b5c27a` |
| `dc_nohw-stripped.ko` | 8544 | `5567fc6a08d547921d701a2e15c1dc5f45678fa06ed3588f6afbac8ab582c99e` |
| `dc_nohw.ko.gz` | 67958 | `81f8ac2cc69a28d7e509462ba8f1ea9863ed83979ec44183bab74aa6c1de8582` |
| `dc_nohw-stripped.ko.gz` | 4061 | `a23635be420ee5df5d13e60abea68dc1e44ae6b9f15d48e8da9af1bc9f881556` |
| `pvrsrvkm.ko` с экспортом display class | 2050232 | `d3d4c8a2d34a556aacdce79775c5fe0bebd8b6ad5ab0add65270aca2e2b4d429` |
| `pvrsrvkm-stripped.ko` | 311716 | `70caedd2bebb6953b42954530cf8d9ab2945c3f21eb3c37efae8f395d8ae06c2` |

Сжатые варианты `pvrsrvkm.ko.gz` и `pvrsrvkm-stripped.ko.gz` также созданы;
их SHA-256 соответственно `4a0d891c3e7f98330a2e0aca2f7b2debdc41ca871717f5049d0dabd905ed9b48`
и `2d4262f06116ec08ab9cb9d98e67c3d7f710fe50b3394a60cddfacffaafca9ef`.

`modinfo` полного и stripped `dc_nohw` совпадает:

```text
author:      Imagination Technologies Ltd. <gpl-support@imgtec.com>
description: PowerVR no-hardware display class for MT6589
license:     Dual MIT/GPL
depends:     pvrsrvkm
vermagic:    7.1.0-b8000h20-g7dd245445adf-dirty SMP mod_unload ARMv7 thumb2 p2v8
```

## Display class ABI

Модуль берёт реализацию `dc_nohw` из generic Linux-7-порта, но при сборке
использует только MTK-заголовки и `config_kernel_user.h` точной версии
`1.12@2701748`. Это принципиально: одноимённый upstream IMG 1.12 не совпадает
с MTK-вариантом той же версии.

- В MTK `PVRSRV_DC_SRV2DISP_KMJTABLE` заканчивается `pfnSetDCState`.
  В upstream 1.12 после него есть дополнительный `pfnQuerySwapCommandID`.
- В MTK `PVRSRV_DC_DISP2SRV_KMJTABLE` после
  `pfnPVRSRVRegisterPowerDevice` сразу идут четыре `pfnPVRSRVDCMemInfo*`.
  В upstream 1.12 между ними добавлен `pfnPVRSRVFreeCmdCompletePacket`.
- Generic/1.13/1.14 имеют нужный MTK порядок обоих jtable, но generic
  `DISPLAYCLASS_FLIP_COMMAND{,2}` отличается порядком части полей. Поэтому
  generic `kerneldisplay.h` не копируется: `dc_nohw_displayclass.c`
  компилируется с точным MTK header, и обращения по именам получают MTK
  offsets. Для command2 поле на месте `hExtBuffer` является `hUnused` и
  инициализируется Services в NULL; no-hardware `Flip()` буфер не разыменовывает.
- Сигнатура `pfnGetBufferAddr` сохранена MTK: возвращаются массив
  `IMG_SYS_PHYADDR` по одной записи на страницу, размер, CPU virtual address,
  `hOSMapInfo = NULL` и `pbIsContiguous = IMG_FALSE`.

Существующий modern env `pvrsrvkm` экспортировал
`PVRGetDisplayClassJTable` только для Linux до 5.19. Патч
`0009-export-display-class-jtable.patch` снимает это ошибочное ограничение.
Новая сборка `pvrsrvkm` создаёт `Module.symvers` с
`PVRGetDisplayClassJTable` и `PVRGetBufferClassJTable`; `dc_nohw` собирается с
`KBUILD_EXTRA_SYMBOLS` именно из этого файла. Проверка `verify-symbols.sh`
подтвердила, что все undefined-символы присутствуют в объединении
`Module.symvers` ядра и `pvrsrvkm`.

## Параметры и память

| Параметр | Значение |
|---|---|
| Размер | 1280 x 800 |
| Формат | `PVRSRV_PIXEL_FORMAT_ARGB8888`, 32 bpp |
| Stride | 5120 байт |
| Размер одного буфера | 4096000 байт, 1000 страниц |
| Число буферов | 3 |
| Swapchains | 1 |
| Swap interval | 0..1 |

Каждый буфер состоит из order-0 страниц, выделенных `alloc_page`; CPU mapping
создаётся `vmap(..., VM_MAP | VM_MAP_PUT_PAGES,
pgprot_noncached(PAGE_KERNEL))`. `pfnGetBufferAddr` сообщает Services массив
физических адресов и признак `IMG_FALSE` для contiguous. Освобождение идёт
через `vfree`, которое при `VM_MAP_PUT_PAGES` освобождает также страницы и их
массив. В исходниках модуля нет `ioremap`, `readl`, `writel`, запроса MMIO или
DMA к display controller; `SwapToDCBuffer` и аппаратный `Flip` — no-op.

## Сборка

Запускать из PowerShell, а не из Git Bash:

```powershell
wsl.exe -d B8000Build -- sh "/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/drivers/pvrsrvkm/build.sh"
wsl.exe -d B8000Build -- sh "/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/drivers/dc_nohw/build-dc-nohw.sh"
```

Второй скрипт делает внешнюю сборку:

```text
make -C /opt/b8000h/linux-drm M=/opt/b8000h/dc-nohw-kbuild/drivers/gpu/pvr \
  ARCH=arm LLVM=1 KBUILD_EXTRA_SYMBOLS=<pvrsrvkm/Module.symvers> modules
```

Используются те же MTK user config, `SUPPORT_*`, `KCFLAGS` и compat include,
что у `pvrsrvkm`. `/opt/b8000h/linux-drm` не изменяется.

## Регистрация и будущая проверка на планшете

`module_init(DC_NOHW_Init)` синхронно вызывает `Init()`, получает таблицу через
прямую зависимость `PVRGetDisplayClassJTable`, затем вызывает
`pfnPVRSRVRegisterDCDevice` и `pfnPVRSRVRegisterCmdProcList`. Userspace для
регистрации не требуется. После успеха в kmsg ожидается:

```text
dc_nohw: registered DISPLAY class: 1280x800 ARGB8888, stride 5120, buffers 3
```

Порядок будущего теста (не выполнялся в G3a):

```sh
insmod /root/pvrsrvkm.ko
insmod /root/dc_nohw.ko
pvrsrvctl --start --no-module
/root/gles2_pbuffer
```

Проверять нужно в одном чистом boot/module state. Успешная локальная сборка и
регистрация по коду ещё не доказывают прохождение стокового gralloc/EGL или
реальный GLES render на планшете.
