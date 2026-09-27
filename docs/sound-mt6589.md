# Звук MT6589/MT6320 на Lenovo B8000-H

Дата проверки: 2026-09-28. Рабочее ядро планшета во время теста:
`7.1.0-g7dd245445adf-dirty #19`, ARMv7, `CONFIG_MODULE_UNLOAD=y`.

## Результат

Сделан минимальный out-of-tree ALSA-драйвер воспроизведения
`linux-port/drivers/mt6589-audio/mt6589_audio.c`. Он создаёт карту
`Lenovo B8000-H` и PCM `MT6589 DL1`, поддерживает только 48 кГц, S16_LE,
stereo и включает SN2145B через GPIO73 лишь на время активного PCM.

На подключённом планшете подтверждены:

- загрузка `soundcore`, `snd`, `snd_timer`, `snd_pcm` и `mt6589_audio` с
  vermagic `7.1.0-g7dd245445adf-dirty SMP mod_unload ARMv7 thumb2 p2v8`;
- регистрация `/dev/snd/controlC0` и `/dev/snd/pcmC0D0p`;
- аппаратный mixer `Speaker Playback Volume` 0..7, тест выполнялся на 0;
- `speaker-test -D hw:0,0 -c2 -r48000 -F S16_LE -t sine -f 220` принял
  формат, отработал Front Left/Front Right и закрыл PCM без новой ошибки ядра;
- после теста GPIO73 был `speaker-enable out lo`, все звуковые модули штатно
  выгружены; Wi-Fi и драйвер батареи продолжили работать.

COM11 не даёт акустической обратной связи, поэтому слышимость тона человеком
не подтверждена. Повторный тест с повышением громкости не выполнялся: батарея
показывала 1%, `Charging`, 2.973 В до теста и 2.956 В после. Таким образом,
цифровой ALSA/runtime-путь проверен на железе, но критерий S1 «тон слышен из
динамиков» остаётся открытым. S2 (ACCDET/наушники и PulseAudio/PipeWire) не
начинался до закрытия этого критерия.

Это намеренно прямой ALSA-прототип, а не окончательный ASoC machine/codec/platform
драйвер: у запущенного ядра #19 было `CONFIG_SOUND=n`. Прототип позволил проверить
реальный DL1/MT6320/GPIO путь загружаемыми модулями без прошивки нового BOOT.
Следующая версия должна перенести проверенную последовательность в ASoC.

## Что установлено по исходникам

- Точная база AFE MT6589 — `0x12070000`, не предполагавшаяся `0x11140000`;
  внутренний audio SRAM начинается с `0x12008000`.
- DL1 использует `AFE_DL1_BASE/CUR/END`; соединения I05→O03 и I06→O04 —
  `AFE_CONN1[21]` и `AFE_CONN2[6]`; код частоты 48 кГц — 10.
- MT6320 доступен через regmap устройства `1000f000.pwrap`, как и в
  `b8k-battery`.
- Для штатного SN2145B используется GPIO73 и задержка прогрева 40 мс.
  `GPIO_EXTERNAL_AMPLIFIER_PIN` GPIO178 относится к альтернативному
  `amp_yad_2_spk`, а не к выбранному B8000-H SN2145B path.
- Цифровая и аналоговая последовательности MT6320 восстановлены из
  `libaudio.primary.default.so` стокового
  `B8000_A442_001_020_140620_WW_3GDATA` и сверены с vendor `AudDrv_Afe.h`,
  `AudDrv_Ana.h`, `AudDrv_Clk.c`.
- В качестве открытого образца использован MediaTek ASoC v1 из
  `tpruvot/android_kernel_alcatel_mt6582m` @
  `86812531d3a95f385d59dc55efb6683b40b6c9bb` (`external/`, не в git).
  Дополнительно зафиксирован `tpruvot/MT6592-KK-KERNEL` @
  `9cd49c406ac8cbd341498bddf619d605abe0f472`.

## Реализация и патчи ядра

Драйвер самостоятельно отображает AFE и infracfg, использует DMA-буфер ALSA,
опрос `AFE_DL1_CUR` hrtimer-ом и PMIC regmap. GPIO получен descriptor API через
lookup `pinctrl_paris`, hardware offset 73: глобальные legacy GPIO numbers в
ядре 7.1 динамические, поэтому первоначальный `gpio_request(73)` корректно
возвращал `-EPROBE_DEFER`.

- `0030-lenovo-blade-enable-alsa-modules.patch` включает базовые ALSA-модули.
  `SND_DUMMY=m` нужен, чтобы Kconfig сохранил скрытые `SND_PCM/SND_TIMER`.
- `0031-alsa-control-avoid-xa-store-range.patch` заменяет новый неэкспортируемый
  для ядра #19 `xa_store_range()` на цикл `xa_store()`. После этого новый
  `snd.ko` загрузился поверх работающего ядра.

Сборка модуля:

```sh
wsl.exe -d B8000Build -- bash -lc \
  "cd '/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/drivers/mt6589-audio' && ./build.sh"
```

Финальный тестовый комплект находится вне git в
`artifacts/linux-port/audio-test/`:

- `mt6589_audio.ko` — SHA-256
  `e42fbd3a49400aa19b52c239a668d6737c51a64e38e1d061c57164efad27e663`;
- `soundcore.ko` — `01899a951fa02f355a927a10127b3eebabde2c1b094bd5fffc335f1a83abaea4`;
- `snd.ko` — `fde3da0cf7ecf3dd268356ce994b99880364502d0bcd2a1767da2c1818879f46`;
- `snd-timer.ko` — `c656717662e7b848c9893eeaf9e9d6f526e3de5d9aba5ce4fe7ef088ae05ed92`;
- `snd-pcm.ko` — `c1ac1f9485a1ae2097422e9691ff8ec6a40ecb09ef70e806d5d9e2b2f970776b`.

## BOOT с ALSA

Собран с `K=/opt/b8000h/linux-drm DTB_VARIANT=install2 IMAGE_SUFFIX=-drm` и
проверен распаковкой `unpack_bootimg`/`cmp`. Ничего не прошивалось.

- `artifacts/linux-port/b8000h-linux-firstboot-drm-safe.img` —
  `184e8b40a46e053e30c060293edf9f431feb8654e05b66d6730f216220ef13f3`;
- `artifacts/linux-port/b8000h-linux-recovery-drm-safe.img` —
  `d1d743721581f6cf82c5776e18e565bc715284ad68e2be1bc3352c6b90b7be8b`;
- `mt6589-lenovo-b8000-h-install2.dtb` —
  `64c8fe249cb546ad50a17c3f955edf7a73796f61cb3007bfa5ede2ede98fce90`.

Для постоянной работы нужен новый BOOT, но его нельзя прошивать без отдельного
явного согласия пользователя и нормального заряда батареи. Сам внешний модуль
также требует сначала загрузить четыре ALSA-модуля из тестового комплекта.

## Следующий безопасный тест

1. Зарядить планшет минимум до установленного проектом порога 60%.
2. Загрузить ALSA-модули и `mt6589_audio.ko`, убедиться, что карта появилась.
3. Установить mixer в 0 и выполнить один короткий `speaker-test`; пользователь
   должен подтвердить физически слышимый тихий тон.
4. Если тон отсутствует, снять во время PCM `AFE_DL1_CUR`, AFE connection/DAC
   registers и MT6320 registers, не повышая громкость вслепую.
5. После подтверждения перенести path в ASoC, затем добавить ACCDET/headphones и
   проверить обнаружение карты PulseAudio/PipeWire.

Открытые референсы: [Alcatel MT6582M kernel](https://github.com/tpruvot/android_kernel_alcatel_mt6582m),
[MT6592 KK kernel](https://github.com/tpruvot/MT6592-KK-KERNEL).
