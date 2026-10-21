# Камера: стоковый HAL через libhybris или свой V4L2

Исследование 21 октября 2026 года (задача #14, шаг 6 плана звонков в
haltia-de/haltia, `docs/plans/calls.md`). Фронтальный датчик уже включается и
отвечает (#10, `docs/camera-front.md`): OV9760 на I2C 1, адрес 0x10.

Закрытые библиотеки читались с `/system` планшета и в git не попадают.
Источники ядра — GPL-дерево bq/aquaris-5HD @ 1b08ad7 (MT6589, ядро 3.4), как в
CI. Стоковое ядро — `kernel_blade10_row_3gdata.bin` прошивки
`B8000_A442_001_020_140620_WW_3GDATA` (3.4.67, символы восстановлены
`vmlinux-to-elf`).

## Коротко

| Путь | Оценка | Риск |
|---|---|---|
| (а) стоковый HAL через libhybris | 9–15 недель | высокий |
| (б) V4L2: драйвер OV9760 + минимальный SENINF/CSI-2 + программный ISP libcamera | 6–10 недель | средний |

Главное препятствие (а) — не драйверы ядра, а Android-окружение: стоковый
`libbinder` говорит протоколом binder 7 (32-битный ABI), а в основном ядре
остался только протокол 8; нужны ещё ashmem, ION с M4U и сервисы Android.
Самое трудное в (б) — последовательность регистров SENINF/MIPI-RX/TG/IMGO, но
её можно снять с той же закрытой `libcamdrv.so`, которую пришлось бы
запускать в (а). Рекомендация — (б), с оговорками в конце.

## Что открывает стоковый стек камеры

Android 4.4.2 (SDK 19), HAL камеры версии 1 (`camera.default.so`,
`NSCam::CamDeviceManager`). Цепочка: `camera.default.so` → `libcam.device1` →
`libcam.camadapter`/`libcam.client` → `libcamdrv` (`ImgSensorDrv`,
`IspDrvImp`, `SeninfDrvImp`, `IMemDrvImp`, `ResMgrDrvImp`) → ядро; 3A —
`lib3a`, `libfeatureio`, `libcamalgo` (2,1 МБ, алгоритмы; ему нужны EGL, GLESv2,
libgui), настройки датчиков — `libcameracustom` (3,9 МБ).

Узлы устройств (строки и импорт `open`/`ioctl`/`mmap`):

| Узел | Кто открывает | ioctl (номера сняты дизассемблером с вызовов `ioctl`) |
|---|---|---|
| `/dev/kd_camera_hw` | libcamdrv `ImgSensorDrv` | тип `'i'`: 0 open, 5 getInfo (24 Б), 10 getResolution (84 Б), 15 featureControl (16 Б), 20 setScenario (16 Б), 25 close, 30/35 поиск датчика, 40 позиция сокета |
| `/dev/camera-isp` | libcamdrv `IspDrvImp`, `IMemDrvImp`, `SeninfDrvImp` | тип `'k'`: 0 reset, 1 resetBuf, 2/3 чтение/запись регистров, 5 holdReg, 6 waitIrq, 7 readIrq, 8 clearIrq, 9 dumpReg, 11 буферы (ring), 12 счётчик ссылок; плюс `mmap` окон регистров |
| `/dev/camera-pipemgr` | libcamdrv `ResMgrDrvImp`, libcam.campipe | тип `'p'`: 0, 1, 3, 4, 6 (lock, set/get mode, …) |
| `/dev/devmap` | libcamdrv `SeninfDrvImp::init` | `0x80046401`, затем `mmap` |
| `/dev/hdmitx` | libcamdrv `ResMgrDrvImp::CloseHdmi` | тип `'H'` 14, 15 |
| `/proc/M4U_device` | libm4u | тип `'g'`: 25 команд (alloc/dealloc MVA, config port, TLB, cache sync, …) |
| `/dev/ion` | libion, libcamdrv | `ION_IOC_CUSTOM` (`0xc0084906`) и обычные ION 4.x |
| `/dev/kd_camera_flashlight` | libfeatureio `FlashlightDrv` | тип `'S'`, 19 команд |
| объектив | libfeatureio `LensDrv` | тип `'A'` 0–3 (у фронтальной камеры фокуса нет) |
| `/dev/binder` | libbinder | его линкуют `libcam.client`, `libcam.camadapter`, `libcamalgo`, `libcamera_client` |

Строк `getService`/`media.camera` в библиотеках самого HAL нет: HAL можно
загрузить в одном процессе (`hw_get_module("camera")`) без mediaserver.
Сервисы ищет `libcam.utils.sensorlistener` (SensorService); как он ведёт себя
без сервиса — не проверено.

Важно: драйвер ISP ядра тонкий. `ISP_mmap` отдаёт процессу окна регистров
(IMGSYS `0x15000000`, SENINF `0x15008000`, MIPI-RX `0x1500C000` и аналоговую
часть `0x10012000`, TOPCKGEN `0x10000000`, GPIO `0x10005000`), а
`libcamdrv.so` программирует SENINF, CSI-2, TG, IMGO и MCLK сама. Ядро делает
часы, прерывание, кольцо буферов IMGO/IMG2O и сигнал процессу. В (а) закрытая
библиотека будет писать в TOPCKGEN и GPIO в обход наших драйверов часов и
выводов.

## Драйверы ядра в дереве bq

| Драйвер | Путь (`mediatek/…`), строк | Узел |
|---|---|---|
| ISP | `platform/mt6589/kernel/core/camera_isp.c` 4612 | `/dev/camera-isp` |
| Pipe manager | `core/camera_pipe_mgr.c` 1140 | `/dev/camera-pipemgr` |
| SYSRAM | `core/camera_sysram.c` 1439 | `/dev/camera-sysram` |
| Список датчиков | `custom/mt6589/kernel/imgsensor/src/kd_sensorlist.c` 2522 + заголовки ~1,9 тыс. | `/dev/kd_camera_hw`, `_bus2` |
| Питание | `custom/mt6589/kernel/camera/camera/kd_camera_hw.c` 284 (плата bq; у Lenovo своя, её последовательность снята в #10) | — |
| OV9760 | `custom/common/kernel/imgsensor/ov9760_mipi_raw/` (Sensor.c 1468) | — |
| OV9726, A5142 | там же | — |
| M4U (IOMMU) | `platform/mt6589/kernel/drivers/m4u/m4u.c` 6278 + ~1,6 тыс. | `/dev/M4U_device` |
| SMI | `drivers/smi/smi_common.c` 969 + ~0,9 тыс. | `/dev/MTK_SMI` |
| ION | `kernel/drivers/ion_drv/ion_drv.c` 582 + `ion_mm_heap.c` 797 | `/dev/ion` |
| Вспышка | `custom/common/kernel/flashlight/src/kd_flashlightlist.c` 478 | `/dev/kd_camera_flashlight` |

EIS и FDVT для MT6589 в дереве нет (закомментированы в Makefile). В
стоковом ядре ровно три драйвера датчиков: a5142, ov9726, ov9760.

Перенос на 7.1: ушли earlysuspend, `wake_lock`, `create_proc_entry`,
`hwPowerOn`, `mt_set_gpio`, `enable_clock` (наши `clk-mt6589-img` дают те же
вентили), статические `IO_VIRT`, pmem, ION 3.4, MTK-флаги I2C. ioctl уже
`unlocked_ioctl`, на 32-битном ядре compat не нужен. Побитово сохранить нужно
структуры ISP и кольца буферов, смещения `mmap`, структуры `ACDK_SENSOR_*` и
идентификаторы FeatureControl, структуры M4U, `ION_IOC_CUSTOM` (наш
`linux-port/drivers/ion-legacy` уже отвечает на них, но без MVA), SMI BWC,
SYSRAM, pipemgr — около 60 ioctl. Своих драйверов M4U/SMI для MT6589 в
основном ядре нет (`mtk_iommu_v1`, `mtk-smi` — без MT6589).

## Binder, ashmem, ION

- **Binder.** Стоковый `libbinder.so` собран под протокол 7 (32-битный ABI,
  `BINDER_IPC_32BIT`), он проверяет версию («Binder driver protocol does not
  match user space protocol!»). Основное ядро после удаления
  `ANDROID_BINDER_IPC_32BIT` (коммит 1190b4e38f97, 2018) говорит только
  протоколом 8, в том числе на 32-битном ARM. В нашем `.config` binder
  выключен. Выходы: пересобрать libbinder из AOSP 4.4 под 64-битный заголовок
  (как в Android 5) с сохранением C++-ABI для закрытых библиотек; вернуть
  32-битный ABI в драйвер 7.1 (тяжело, драйвер переписан); или обойтись без
  межпроцессного binder — HAL в одном процессе.
- **Ashmem** убран из ядра (2022); `MemoryHeapBase` и `libmtkjpeg` им
  пользуются. Замена — libcutils на memfd или внешний модуль ashmem.
- **ION** убран из основного ядра (замена — dma-buf heaps); наш `ion-legacy`
  закрывает ABI ION 4.x, но не M4U.
- Halium, Ubuntu Touch и Sailfish (droidmedia, gst-droid) запускают камеру
  при полном Android-окружении (servicemanager, mediaserver) на ядре
  производителя. Запуска MediaTek KitKat HAL камеры на основном ядре мы не
  нашли.
- В нашем острове libhybris собрана с обёрткой `libcamera` (совместимый слой
  Ubuntu Touch), но ей нужен `libcamera_compat_layer` из дерева Android, а он
  ходит в CameraService через binder.

## Путь (а): стоковый HAL, 9–15 недель

1. Драйверы-прокладки ядра: isp, sysram, pipemgr, sensorlist с OV9760, SMI,
   devmap, вспышка — ~4 недели.
2. M4U: эмуляция (MVA = физический адрес, порты larb в обход, CMA) или перенос
   6 тыс. строк — 2–3 недели.
3. Binder: 64-битный libbinder или загрузка HAL в одном процессе, заглушки
   servicemanager/SensorService — 2–4 недели.
4. Ashmem на memfd, ion-legacy с MVA — 1 неделя.
5. Остров: свойства, файлы калибровки nvram, окно предпросмотра на gralloc,
   обёртка для GStreamer/мессенджера — 2–3 недели.

Риски: ABI binder и возможные добавки MediaTek в libbinder; закрытая
библиотека пишет в TOPCKGEN и GPIO в обход ядра; поведение M4U; умолчания 3A
и nvram; процесс уровня mediaserver на 1 ГБ памяти; отладка без исходников;
~60 ioctl с побитовым ABI.

Плюс пути: стоковое качество картинки (3A, ISP, шумоподавление) и обе камеры
сразу.

## Путь (б): V4L2, 6–10 недель

1. Драйвер OV9760 (V4L2 subdev) из таблиц GPL-драйвера MTK по образцу
   `ov9734.c`: 1 линия MIPI, RAW, MCLK 24 МГц, 1470×1100 и 1280×720, ~30 к/с,
   pixel clock 60 МГц — 1–1,5 недели.
2. Последовательность SENINF/MIPI-RX/TG/IMGO снять с `libcamdrv.so`
   (`configTg1CSI2`: задержки D-PHY, settle, число линий) и, при
   необходимости, трассой регистров под стоковым Android — 2–3 недели.
3. Драйвер захвата: media controller, vb2-dma-contig, прерывание, часы
   imgsys, M4U в обход (CMA, ~2 МБ на кадр) — 2–3 недели. Образец устройства
   — серия SENINF/CAMSV для MT8365 (2025, не в нашем дереве).
4. libcamera: простой pipeline handler, программный ISP на процессоре,
   настройка, приложение или GStreamer — 1–2 недели.

Риски: недокументированные тайминги D-PHY; работает ли IMGO с выключенным
M4U; скорость дебайеризации на Cortex-A7 (GPU-дебайер libcamera нам
недоступен: SGX только через hybris в phoc; оценка 5–15 к/с при 720p, нужен
ранний замер); без настройки AE/AWB картинка хуже стоковой; задняя камера
A5142 — отдельный драйвер позже.

Для звонка достаточно 320×240–640×480 при 10–15 к/с (план звонков), это
снимает остроту вопроса скорости программного ISP.

## Рекомендация

(б): меньше и предсказуемее, всё остаётся в открытом ядре и стандартных
интерфейсах (V4L2/libcamera), которыми камера сможет пользоваться любая
программа (ADR 0004), и не требует воскрешать binder 7 и ashmem. Шаг 2 пути (б)
— тот же разбор `libcamdrv.so`, что понадобился бы для отладки пути (а), так
что первые недели не пропадут при любом решении.

Не проверено: точные версии ядра, где убраны 32-битный binder (вероятно 4.17),
ashmem (5.18) и ION (5.11); поведение SensorListener без сервиса; скорость
программного ISP на A7; порядок Bayer у OV9760 (`RAW_B`).
