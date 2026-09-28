# MT6589 / SGX544: аудит зависания при чтении `EUR_CR_CORE_ID`

Дата аудита: 2026-09-27. Область работы: только статический анализ исходников.
Планшет не подключался, регистры не читались и не записывались, ядро не собиралось.

## Краткий вывод

В найденном вендорском коде нет пропущенного обязательного «разрешения SGX» в
диапазоне `0x13xxxxxx`, MFG_APB, DEVAPC, SMI, M4U или EMI. До первого штатного
обращения к SGX вендор делает ту же последовательность MTCMOS, четырёх MFG clock
gates, двух reset и `TOPAXI_SI0_CTL.way_en`, которую воспроизводит `sgx_probe`.

Остаются два существенных пробела в доказательстве:

1. **Не проверен `hyd_sel`.** Вендорская DVFS-процедура
   `mt_gpu_clock_switch_initial()` всегда программирует **оба** селектора —
   `mfg_sel` в `CLK_CFG_0[18:16]` и `hyd_sel` в `CLK_CFG_4[2:0]`. Для 286 МГц
   оба должны быть `7` (`MMPLL_D5`), а `CLK_CFG_4[7]` должен быть 0. В текущем
   опыте подтверждены `mfg_sel`, четыре gate в `MFG_CG_CON` и MMPLL, но не
   источник/PDN `hyd_sel`. Открытый gate HYD при остановленном или неверном
   родителе не доказывает тактирование SGX front-end.
2. **Текущий `read_core=1` не доказывает, какой offset зависает.** Четыре
   `readl()` переданы аргументами одного `dev_info()`. Порядок вычисления
   аргументов в C не определён; первым может выполняться не чтение `+0x20`, а
   `+0x2c`, `+0x28` или `+0x24`. Следующий рискованный тест обязан делать ровно
   одно MMIO-чтение за загрузку и сохранять маркер до него.

Поэтому наиболее вероятная проверяемая причина сейчас — несовпадение
`hyd_sel` (и, вторично, родителя `smi_mfg_as_sel`) с состоянием, которое
вендорский userspace/DVFS устанавливает перед полноценной инициализацией.
Это ещё гипотеза, а не подтверждённая причина на железе. Следом по приоритету —
проверить второй power-status latch и корректно локализовать первый зависающий
offset. DEVAPC/SMI/M4U/EMI имеют низкий приоритет.

## Какие исходники проверены

- вендор: `external/b8000-vendor-src/mediatek/platform/mt6589/kernel/`:
  `core/mt_clkmgr.c`, `core/mt_spm_mtcmos.c`, `core/mt_gpufreq.c`,
  `core/mt_dcm.c`, `core/mt_pm_init.c`, `core/emi_bwl.c`, `core/mt_emifreq.c`,
  `drivers/devapc/`, `drivers/smi/`, `drivers/m4u/`, а также
  `drivers/gpu/pvr/services4/system/mt6589/` и путь инициализации Services;
- вендорское ядро 3.4 и его `.config`;
- текущее ядро WSL `/opt/b8000h/linux-drm`: MT6589 clk/genpd/DTS;
- `linux-port/drivers/sgx-probe/sgx_probe.c`;
- доступные bootloader-каталоги
  `external/b8000-vendor-src/bootable/bootloader/uboot` и
  `external/b8000-vendor-src/mediatek/custom/common/lk`.

Полного platform LK/preloader для MT6589 в распакованном дереве нет:
`mediatek/platform/mt6589/` содержит только `kernel`, а `custom/common/lk` —
общие файлы. Поиск по доступному U-Boot/LK не нашёл MT6589 MFG/GPU/MMPLL init.
Поэтому начальные значения mux, оставленные preloader/LK, из этих исходников
восстановить нельзя.

## Физические адреса

Вендорские `0xF...` — статические kernel virtual addresses. Для этого SoC
`IO_VIRT_TO_PHYS(v) = 0x10000000 | (v & 0x0fffffff)`.

| Узел | Вендорское имя | Физический адрес |
|---|---|---:|
| SGX register aperture | `SYS_MTK_SGX_REGS_SYS_PHYS_BASE` | `0x13000000` |
| `EUR_CR_CORE_ID` SGX544 | offset `0x20` | `0x13000020` |
| MFG clock/reset/DCM | `MFG_CG_CON` | `0x10206000` |
| SPM MFG power | `SPM_MFG_PWR_CON` | `0x10006214` |
| top clock muxes | `CLK_CFG_0/4/8` | `0x10000140/150/164` |
| MMPLL | `MMPLL_CON0` | `0x10209240` |
| TOPAXI MFG way | `TOPAXI_SI0_CTL` | `0x10001200` |
| legacy MFG AXI block | `MFG_AXI_BASE` | `0x12060000` |
| legacy MFG APB block | `MFG_APB_BASE` | `0x120c4000` |

`MFG_APB_BASE` получается из `0xF20C4000` как **`0x120c4000`**, не
`0x13fff000`. Макрос нигде в исследованном MT6589 kernel-коде не используется.
Адрес `0x13fff000` не найден. Единственный осмысленный GPU-блок в диапазоне
`0x13xxxxxx`, найденный в исходниках, — aperture SGX с базой `0x13000000`.
Комментарий `// MFG_AXI_BASE` рядом с этой базой в `sysconfig.h` не совпадает с
`MFG_AXI_BASE` из `mt_reg_base.h` и не является доказательством второго config
block.

## Полный путь вендора до первого SGX MMIO

### 1. Ранняя power/clock инициализация

`mt_power_management_init()` (`arch_initcall`) вызывает:

1. `spm_module_init()`;
2. `mt_clkmgr_init()`;
3. `mt_dcm_init()`;
4. `mt_idle_init()`.

Нормальный `mt_clkmgr_init()` выполняет `mt_plls_init()` →
`mt_subsys_init()` → `mt_muxs_init()` → `mt_clks_init()`. Функция
`cg_all_force_on()` вызывается только из отдельного bring-up init и **не входит**
в штатный путь.

- `mt_plls_init()` лишь считывает состояние PLL. При последующем enable MMPLL
  `lc_pll_enable_op()` ставит `en_mask=0xf0000001`, ждёт 20 мкс и ставит
  `RST_BAR`, bit 27. Текущий `mtk_pll_prepare()` совпадает с этим.
- `mt_subsys_init()` вызывает `test_spm_gpu_power_on()`. Тест пять раз включает
  только `PWR_ON/PWR_ON_S` и проверяет оба status-latch; если это работает,
  источник считается VCORE. Затем MFG переводится в default-off. На B8000-H
  VCORE уже установлен отдельной проверкой; VRF18_2 не нужен.
- `mt_muxs_init()` ставит PDN для MFG, HYD и SMI_MFG_AS, но **не выбирает их
  parent**. Оно лишь читает `CLK_CFG_8[17:16]`, чтобы построить программную
  зависимость: `2=mfg_sel`, `3=hyd_sel`, остальные значения дают `parent=NULL`.
  Таким образом, штатное ядро тоже зависит от оставленного bootloader значения,
  пока DVFS/userspace его явно не изменит.
- `mt_gpufreq_init()` только читает `CLK_CFG_0[18:16]` и заполняет
  `g_cur_freq`; аппаратных записей при init нет.

`mt_dcm_init()` вызывает `dcm_enable(ALL_DCM)`. Ветка `BUS_DCM` внутри этой
функции скомпилирована под `#if 0`. Отдельный `bus_dcm_enable()` используется
idle-кодом и пишет `DCM_CFG=BIT(7)|0xf`; состояние bit 7 поэтому динамическое.
Ветка `MFG_DCM` пишет `MFG_DCM_CON0/1` только если MFG уже включён. Поскольку
`mt_subsys_init()` перед этим оставляет MFG выключенным, эти MFG DCM записи в
обычном boot path до запуска PVR не выполняются. `EnableSGXClocks()` их также не
вызывает.

### 2. DEVAPC, SMI/M4U и EMI

DEVAPC init очищает старые violation/debug данные, снимает bit 2 в `APC_CON`,
настраивает IRQ и запрещает выбранные устройства для MD1/MD2. Он не выдаёт
специальное AP-разрешение для SGX; в таблицах вообще нет именованного GPU/MFG
entry. Поэтому отсутствие этого init не объясняет, почему AP CPU read получает
вечный AXI wait вместо обычного permission fault. DEVAPC полезен как индикатор:
следует сравнить violation/debug до и после отдельной попытки, не меняя policy.

SMI настраивает larb0..4 для video/display/camera и общие arbitration значения.
В enum 54 портов M4U перечислены VENC/VDEC/DISP/ISP/GDMA/AUDIO, но GPU/MFG порта
нет. SGX использует собственный BIF/MMU. Эти настройки нужны GPU позже, когда он
сам становится bus master и обращается к RAM, но не лежат на пути CPU →
`0x13000020`.

EMI init программирует DRAM arbitration (`EMI_ARB*`, `EMI_CON*`, `EMI_TESTB`).
Это глобальная настройка пропускной способности и также не является decode/
clock enable для CPU-чтения SGX. Менять EMI в опытах с CORE_ID не следует.

### 3. Services / PVR

`SysInitialise()` делает:

1. общую программную инициализацию;
2. `SysLocateDevices()` — объявляет SGX base `0x13000000`, size `0xffff`, IRQ 220;
3. runtime-PM registration;
4. `PVRSRVRegisterDevice()`;
5. `EnableSystemClocks()` (для MT6589 аппаратных GPU-записей здесь нет);
6. при APM — `EnableSGXClocks()`;
7. `PVRSRVInitialiseDevice()`, который на этом этапе вызывает
   `DevInitSGXPart1`: выделение памяти и MMU structures, без SGX MMIO;
8. clocks снова выключаются при APM.

Фактическое отображение aperture и аппаратная `SGXInitialise()` происходят в
`DevInitSGXPart2KM`, вызываемом userspace. Тогда init script, reset и BIF впервые
обращаются к SGX. Закомментированная `SysCreateVersionString()` с чтением
`EUR_CR_CORE_REVISION` не входит в сборку.

В том же userspace bridge существует `MTKSetFreqInfo(freq, tbltype)`. Его порядок
относительно `DevInitSGXPart2KM` задаёт закрытый userspace blob, а не kernel init.
При вызове `MTKSetFreqInfo()`:

- `mt_gpufreq_set_initial()` программирует `mfg_sel` **и** `hyd_sel`; для
  `GPU_MMPLL_D5` это selector 7 в обоих регистрах;
- если `freq > GPU_DVFS_F5` (286 МГц), дополнительно
  `CLK_CFG_8[17:16]=3`, то есть `smi_mfg_as_sel <- hyd_sel`;
- ровно при 286 МГц эта дополнительная запись `CLK_CFG_8` не выполняется;
- `MtkSetKeepFreq()` в `EnableSGXClocks()` только переключает software DVFS
  policy и сам регистры не пишет;
- `mt_gpufreq_gpu_clock_ratio(ON)` — только учёт wall time, аппаратного
  `clock_ratio` регистра нет.

`EnableSGXClocks()` затем включает HYD/G3D/MEM/AXI и выполняет точную
последовательность:

1. очистить `TOPAXI_SI0_CTL[10]`;
2. в `SPM_MFG_PWR_CON`: assert reset, `CLK_DIS=1`, короткий ISO pulse, затем
   `ISO=0`, `CLK_DIS=0`, `RST_B=1`;
3. `MFG_RESET=1`, `MFG_CG_CLR=0xf`, 1 мкс, `MFG_CG_SET=0xf`, 1 мкс,
   `MFG_RESET=0`, `MFG_CG_CLR=0xf`, 1 мкс;
4. поставить `TOPAXI_SI0_CTL[10]`.

`SGX_OCP_REGS_ENABLED` в `syslocal.h` закомментирован, поэтому OCP-регистры до
SGX MMIO не программируются.

### 4. Сопоставление с нашим clk/genpd/probe

Текущий genpd:

- делает SPM unlock `0x0b160001`;
- ждёт bit 4 и в `PWR_STATUS`, и в `PWR_STATUS_2ND`;
- включает все SRAM banks `[11:8]` и ждёт ack bit 12;
- снимает TOPAXI protection bit 5 и включает SI0 way bit 10.

CCF описывает правильные mux и parents. Probe дополнительно включает
`infra_mfgaxi` и `mfg_hyd`, затем почти дословно повторяет вендорский reset.
Единственное прямое отличие reset sequence: последняя запись probe в
`MFG_CG_CLR` равна `0x1f`, тогда как вендор всегда пишет `0x0f`. Это лишняя запись
неизвестного bit 4, а не пропущенный enable; её следует убрать в будущем
диагностическом probe, но `MFG_CG_CON=0` показывает, что известные четыре gates
открыты.

## Регистры, которые вендор может писать, а текущий GPU path — нет

Таблица ограничена регистрами из запрошенных подсистем, которые могут иметь
отношение к доступу SGX. Массовые SoC-wide clock/EMI arbitration записи приведены
группами: они не являются частью `EnableSGXClocks()` и не должны слепо
копироваться.

| Адрес / биты | Вендорская запись | Где | Есть у нас | Риск / вывод |
|---|---|---|---|---|
| `0x10000150[2:0]` (`hyd_sel`) | selector, для 286 МГц `7` | `mt_gpu_clock_switch_initial()` через `MTKSetFreqInfo()` | CCF умеет, probe parent не задаёт и значение не логирует | **Высокий. Проверить первым.** |
| `0x10000150[7]` | 0 при enable HYD mux | `enable_clock(MT_CG_MFG_HYD)` через mux dependency | должен очищаться CCF при enable, но не измерен | Высокий как часть того же чтения. |
| `0x10000140[18:16]` | selector 7 для 286 МГц | `mt_gpu_clock_switch_initial()` | подтверждён selector/rate mfg | Низкий. |
| `0x10000164[17:16]` | `3` только если `freq > 286 МГц`; иначе boot value | `MTKSetFreqInfo()` | CCF умеет, probe не задаёт parent | Средний: допустимы только 2 или 3; проверить. |
| `0x10000164[23]` | 0 при enable MEM mux | `enable_clock(MT_CG_MFG_MEM)` | должен очищаться CCF, не зафиксирован raw | Средний. |
| `0x10000104` (`DCM_CFG`) | idle: `0x8f`; выход: clear bit 7, обычно `0x0f` | `bus_dcm_enable/disable()` | отдельного MT6589 bus-DCM driver нет | Низкий: vendor сам работает с обоими состояниями bit 7; только сравнить чтением. |
| `0x10206010`, `0x10206014` | `0xc03fc03f`, `0x0000c03f` только если MFG on во время `dcm_enable(MFG_DCM)` | `mt_dcm.c` | probe только читает dump, не пишет | Низкий: штатный boot обычно пропускает эти записи, так как MFG off. Не копировать первым. |
| `0x10010090..0x10010490`, `0x10207090..0x10207490`, bit 2 | clear bit 2 | `init_devpac()` | отдельного DEVAPC port нет | Низкий; global APC enable, не SGX grant. |
| `0x10207040..0x10207440` | `0x80000000` clear debug | `init_devpac()` | нет | Диагностический side effect; перед опытом не очищать, сначала читать. |
| `0x10207084` | W1C `0x7f`; mask `0x10207080=0x00ff00f0` | `init_devpac()` | нет | Низкий; IRQ/status, не datapath enable. |
| `0x10001040[6]` | odd RMW clear в `init_devpac()` | `devapc.c` | CCF управляет infra gates иначе | Низкий; не повторять raw без datasheet. |
| SMI common/larb0..4 | scenario arbitration (`REG_SMI_L1ARB0..4`, larb `+0x14/+0x18`) | `smi_common.c` | не GPU-specific | Не на CPU→SGX slave path; не экспериментировать. |
| M4U | M4U/SMI port setup для перечисленных media ports | `drivers/m4u/` | нет GPU port | Не относится к SGX aperture. |
| EMI `EMI_CONB/C/D/E/G/H`, `EMI_SLCT`, `EMI_ARBI/J/K` и `*_2ND`, `EMI_TESTB`, `EMI_ARBA/C/D/E/F/G` | DRAM-type/scenario-specific constants | `emi_bwl.c` | mainline DRAM остаётся от boot firmware | Глобально опасно и не требуется для CORE_ID. Не писать. |
| `0x120c4000` (`MFG_APB_BASE`) | **записей не найдено** | только macro в `mt_reg_base.h` | не описан | Не является найденным пропуском; неизвестное чтение само может повесить AXI. |
| `0x13fff000` | адрес не найден | — | — | Не использовать. |
| `0x13000000 + ...` OCP | код условный, но feature выключен | `sysconfig.c`, `syslocal.h` | нет | Не требуется в данной vendor-конфигурации. |

Все реально обязательные MFG записи — `SPM_MFG_PWR_CON`, status/ack ожидания,
TOPAXI bit 5/way bit 10, `MFG_CG_*`, `MFG_RESET`, MMPLL enable — уже
представлены в genpd/CCF/probe и подтверждены, кроме raw-проверки второго status
latch и mux HYD/SMI_MFG_AS.

## Упорядоченный план безопасных экспериментов

Ниже только план; в рамках этого аудита ничего не запускалось. Каждый этап —
отдельная загрузка с рабочими watchdog/pstore и известным способом long-press
recovery. Нельзя переходить к следующему этапу, если предыдущий дал неожиданный
результат.

`linux-port/tools-arm/mmcwp.c` фактически ожидает третий аргумент: чтение имеет
форму `/usr/local/bin/mmcwp m <addr> -`. Если установленная копия принимает
двухаргументную форму, можно использовать её, но приведённые команды соответствуют
исходнику в репозитории.

### Этап A — только безопасные чтения, MFG удерживается включённым

Предусловие: отдельный безопасный harness удерживает runtime-PM reference после
полного `read_core=0` bring-up. Текущий probe сразу делает cleanup, поэтому нельзя
считать результаты, снятые после его выгрузки, состоянием включённого домена.
На этом этапе **не читать никакой адрес `0x13xxxxxx` и `0x120c4000`**.

```sh
/usr/local/bin/mmcwp m 0x10006214 -   # MFG_PWR_CON: ожидается 0x0000000d
/usr/local/bin/mmcwp m 0x1000660c -   # PWR_STATUS: bit4=1
/usr/local/bin/mmcwp m 0x10006610 -   # PWR_STATUS_2ND: bit4=1, обязательная новая проверка
/usr/local/bin/mmcwp m 0x10000140 -   # CLK_CFG_0: [18:16]=7, [23]=0
/usr/local/bin/mmcwp m 0x10000150 -   # CLK_CFG_4: [2:0]=7, [7]=0
/usr/local/bin/mmcwp m 0x10000164 -   # CLK_CFG_8: [17:16]=2 или 3, [23]=0
/usr/local/bin/mmcwp m 0x10209240 -   # MMPLL_CON0: enable-mask/RST_BAR active
/usr/local/bin/mmcwp m 0x10206000 -   # MFG_CG_CON: bits[3:0]=0
/usr/local/bin/mmcwp m 0x1020600c -   # MFG reset released: 0
/usr/local/bin/mmcwp m 0x10206010 -   # MFG_DCM_CON0, только записать значение
/usr/local/bin/mmcwp m 0x10206014 -   # MFG_DCM_CON1, только записать значение
/usr/local/bin/mmcwp m 0x10001048 -   # INFRA_PDN_STA: infra_mfgaxi bit должен быть 0
/usr/local/bin/mmcwp m 0x10001200 -   # SI0_CTL: bit10=1
/usr/local/bin/mmcwp m 0x10001220 -   # TOPAXI_PROT_EN: bit5=0
/usr/local/bin/mmcwp m 0x10001228 -   # PROT_STA1: bit5=0
/usr/local/bin/mmcwp m 0x10000104 -   # DCM_CFG: зафиксировать, не менять
```

Ожидаемый решающий результат: если `CLK_CFG_4[2:0] != 7` либо bit 7 равен 1,
получена конкретная недостающая часть clock tree. Если `CLK_CFG_8[17:16]` не 2/3,
`mfg_mem` формально открыт, но не имеет валидного GPU clock parent.

### Этап B — DEVAPC только как осциллограф ошибки

До любых SGX-чтений сохранить, не очищая:

```sh
/usr/local/bin/mmcwp m 0x10207084 -   # DEVAPC0_DXS_VIO_STA
/usr/local/bin/mmcwp m 0x10207040 -   # DEVAPC0_VIO_DBG0
/usr/local/bin/mmcwp m 0x10207044 -   # DEVAPC0_VIO_DBG1/address
/usr/local/bin/mmcwp m 0x102070b4 -   # DEC_ERR_CON
/usr/local/bin/mmcwp m 0x102070b8 -   # DEC_ERR_ADDR
/usr/local/bin/mmcwp m 0x102070bc -   # DEC_ERR_ID
```

Если после отдельной неудачной попытки эти значения нельзя прочесть из-за полного
AXI lock, pstore-маркер всё равно различит timeout. Не менять APC policy: vendor
не содержит SGX-specific permission write, которое можно было бы безопасно
скопировать.

### Этап C — одна минимальная clock-запись за опыт

Только если этап A нашёл расхождение. Делать RMW через небольшой диагностический
драйвер/CCF, а не полным raw value, и логировать old/new/readback до SGX MMIO.

1. Если HYD неверен: оставить остальные биты `CLK_CFG_4`, установить
   `[2:0]=7`, очистить `[7]`; ожидается HYD от MMPLL_D5, 286 МГц.
2. Повторить только safe register dump — без SGX read. Значения и clocks должны
   остаться стабильными.
3. Если `SMI_MFG_AS` невалиден: оставить остальные биты `CLK_CFG_8`, установить
   `[17:16]=3`, очистить `[23]`, как vendor OD path. Альтернативный валидный
   selector 2 (`mfg_sel`) проверять только отдельной загрузкой.

Не менять DEVAPC, EMI, M4U, MFG DCM и `MFG_APB_BASE`: исходники не дают основания
смешивать эти изменения с clock experiment.

### Этап D — локализация зависающего SGX offset

Только после совпадения всех значений этапа A. Исправить методику probe так, чтобы
один запуск делал ровно одно volatile `readl()` и печатал/сохранял отдельный
pre-read marker. Начать именно с `0x13000020`; не передавать несколько `readl()`
аргументами одной функции.

Порядок отдельных загрузок:

1. `0x13000020` (`EUR_CR_CORE_ID`);
2. лишь если он прочитан — `0x13000024` (`CORE_REVISION`);
3. затем отдельно `0x13000028` и `0x1300002c`.

Ожидаемые варианты:

- после фикса HYD чтение отвечает — причина локализована в HYD clock path;
- `+0x20` всё ещё вешает, DEVAPC без violation — проблема ниже policy layer:
  clock/reset/decode либо неописанная boot-firmware настройка;
- `+0x20` отвечает, а другой offset вешает — прежняя атрибуция CORE_ID была
  следствием неопределённого порядка аргументов;
- появляется DEVAPC violation с адресом `0x13000020` — только тогда исследовать
  точный APC slot/domain, не писать blanket L0 permissions.

### Этап E — последние, более рискованные развилки

Только если D не дал ответа:

1. сравнить полный register snapshot с тем же B8000-H под штатным Android ровно
   в момент активного PVR (особенно `CLK_CFG_4`, `CLK_CFG_8`, `DCM_CFG`,
   `MFG_DCM_CON0/1`);
2. отдельно проверить вариант MFG DCM vendor-значений, только если Android
   snapshot докажет, что они действительно присутствуют до CORE_ID;
3. не читать `0x120c4000` без такого же доказательства: macro без пользователя не
   делает адрес безопасным;
4. поиск/получение настоящих MT6589 preloader/LK platform sources остаётся
   единственным способом закрыть boot-firmware gap статически.

## Итоговый приоритет

1. raw `CLK_CFG_4` + `CLK_CFG_8` и `PWR_STATUS_2ND` при удерживаемом MFG;
2. исправление методики «одно SGX-чтение на загрузку»;
3. минимальный HYD selector experiment, только если dump показывает отличие;
4. DEVAPC как диагностика, без policy writes;
5. Android register snapshot / отсутствующие preloader-LK sources;
6. SMI/M4U/EMI/MFG_APB не трогать без новых доказательств.

## Сравнение со стоковым ядром 3.4.67 (2026-09-28, на железе)

Прошит временный BOOT `b8000h-stock-kernel-gpu-probe.img` (стоковое ядро 3.4.67
из бэкапа `boot.img` + наш мини-initramfs с USB-ACM; eMMC не монтируется). У
стокового ядра нет `/dev/mem`, но его `/proc/clkmgr/{pll,mux,subsys}_test`
читает и пишет те же регистры через код вендора. MFG включали/выключали
штатно: `echo enable 4 > /proc/clkmgr/subsys_test`.

Стоковые значения при включённом MFG (домен на, `SYS_MFG=0x0000000d state(1)`,
`SPM_PWR_STATUS=0x0006605c`):

| Регистр | Сток (MFG on) | Наш probe #20 | Совпадает |
|---|---|---|---|
| `MFG_PWR_CON` (SYS_MFG) | `0x0000000d` | `0x0000000d` | да |
| `CLK_CFG_0` mfg_sel[18:16] | `0x82870305` → 7 | `0x02070305` → 7 | да |
| `CLK_CFG_4` hyd_sel[2:0] | `0x01848687` → 7 | `0x01040607` → 7 | да |
| `CLK_CFG_8` smi_mfg_as[17:16] | `0x00818505` → **1** | `0x00018501` → **1** | да |
| `MFG_CG_CON` | `0x00000010` | `0x00000010` (step3b) | да |
| `SPM_PWR_STATUS` bit4 | 1 | 1 | да |

**Выводы.**
1. Питание MFG, все clock mux (mfg_sel/hyd_sel/smi_mfg_as_sel) и MFG_CG у нашего
   ядра **в точности совпадают** со стоковым при включённом GPU-домене. Зависание
   чтения `0x13000020` **не** объясняется пробелом в тактировании/питании — эта
   гипотеза из аудита закрыта.
2. Прежний эксперимент с `CLK_CFG_8[17:16]=3` был ошибкой: значение `0x30000`
   вендор пишет только в OD-режиме (`MTKSetFreqInfo`, `freq>GPU_DVFS_F5`). В
   штатном пути smi_mfg_as_sel остаётся **1** — то, что у нас уже было. Запись 3
   и повесила шину.
3. Значит, чтение aperture SGX открывает не bring-up тактов, а полная
   инициализация интерфейса SGX (`DevInitSGXPart2KM`), которую у вендора
   запускает userspace PVR blob уже после EnableSGXClocks. Проверить чтение
   `0x13000020` на стоковом ядре нельзя: нет `/dev/mem` и нет Android userspace.

**Следующий шаг по GPU:** не искать больше clock/power-пробел. Либо (а) собрать
loadable-модуль под стоковое 3.4.67 (дерево `external/b8000-vendor-src/kernel`)
и проверить, читается ли `0x13000020` при живом PVR; либо (б) на нашем ядрe 7.1
портировать openpvrsgx и довести инициализацию до DevInitSGXPart2 — чтение
регистров в bare-probe до полной init всё равно будет висеть, это ожидаемо.
