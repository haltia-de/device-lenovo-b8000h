# Сборка и установка Linux на Lenovo YOGA Tablet 10 B8000-H

> Альфа. Ядро случайно падает раз в 10–40 минут. Установка **стирает все
> пользовательские данные Android** (раздел USRDATA). Только для **B8000-H** (60047 / Z0AJ).
> Всё — на свой риск. Готовых образов нет: только сборка из исходников, нужен опыт
> сборки ядра и работы с Linux.

## 0. Сборка

Сборка целиком ещё не автоматизирована: часть шагов ручная, подробности по каждому —
в `docs/` и в комментариях скриптов.

1. **Окружение.** Linux или WSL с Alpine 3.24 (clang/LLVM, `mkbootimg`, `python3`).
2. **Ядро.** [TeamYogaBlade2/linux](https://github.com/TeamYogaBlade2/linux/tree/blade/v7.1)
   ветка `blade/v7.1`, коммит `7dd245445adf`; поверх — патчи `linux-port/patches/` по
   порядку номеров (несколько патчей — Python-скрипты, их запускают в дереве ядра);
   конфиг `lenovo-blade_defconfig` из патчей, `CONFIG_LOCALVERSION="-b8000h22"`.
3. **boot.img с установщиком:**
   ```
   K=<дерево ядра> DTB_VARIANT=install2 IMAGE_SUFFIX=-my sh linux-port/build-safe-image.sh
   ```
   результат — `artifacts/linux-port/b8000h-linux-firstboot-my-safe.img` (ядро, DTB
   `install2` и initramfs из `linux-port/initramfs/init`).
4. **Внешние модули** — `build.sh` в `linux-port/drivers/` (PowerVR `pvrsrvkm`,
   `dc_nohw`, `ion-legacy`, Wi-Fi `mt6628`, звук).
5. **Корневая система** — `UI_PROFILE=phosh sh linux-port/build-rootfs.sh`: Alpine 3.24
   armv7 с Phosh (рисование процессором).
6. **GPU-стек** собирается на самом планшете: Debian armhf в `/opt/debian`
   (debootstrap), `linux-port/hybris/build-hybris.sh` (libhybris), `build-phoc.sh` (phoc
   0.55.1 с `wlroots-0.20-hybris.patch`), службы `b8k-android-system` и `b8k-gpu`,
   обёртка `phoc-gpu`, `b8k-session`, `desktop.start`. Как всё устроено —
   [g4-hybris.md](g4-hybris.md).
7. **Образ установщика для microSD** из ext4-образа системы и `boot.img`:
   ```
   python3 tools/mk-sd-installer.py root.ext4 boot.img b8000h-sd-installer.img
   ```
   Из уже работающего планшета чистый образ (без личных данных и файлов Android)
   делает `linux-port/rootfs/make-release-rootfs.sh`.

## Что понадобится для установки

- Планшет B8000-H, заряд **не меньше 60%**.
- microSD на 4 ГБ и больше (содержимое карты будет стёрто).
- ПК с Windows или Linux:
  - `adb` и `fastboot` ([Android platform-tools](https://developer.android.com/tools/releases/platform-tools));
  - драйверы MediaTek USB VCOM и SP Flash Tool v5 — для резервной копии и возврата;
  - стоковая прошивка `B8000_A442_001_020_140620_WW_3GDATA` (Android 4.4.2, тема B8000
    на 4pda) — в ней scatter-файл и стоковые `boot.img`/`recovery.img`.
- Собранные `boot.img` (ядро с установщиком) и образ установщика для microSD (шаг 0).

## 1. Резервная копия уникальных разделов

В разделах NVRAM, PRO_INFO, PROTECT_F и PROTECT_S лежат IMEI, MAC-адреса и калибровки
радио — их нельзя взять из чужой прошивки. Linux их не трогает, но копия должна быть.

SP Flash Tool → Readback: загрузите scatter стоковой прошивки, добавьте эти разделы
(адреса и длины — из scatter; для этой прошивки готовые конфиги лежат в
`tools/readback-v5-*.xml`), выключите планшет и подключите его к ПК. Храните копию
у себя и никуда не выкладывайте.

## 2. Образ установщика на microSD

Запишите образ установщика на карту целиком (balenaEtcher, Rufus в режиме DD или
`dd if=… of=/dev/sdX bs=4M conv=fsync` в Linux). Разделы на карте создавать не нужно.

## 3. Ядро в раздел BOOT

1. Перевести планшет в fastboot: из Android `adb reboot bootloader` или на выключенном
   планшете Vol− + Power → в меню выбрать fastboot.
2. Проверить, что это нужная модель:
   ```
   fastboot getvar product
   ```
   ответ должен быть `BLADE10_ROW_3GDATA`.
3. Прошить собранное ядро:
   ```
   fastboot flash boot boot.img
   ```
   (`fastboot boot` — загрузка без прошивки — загрузчик этого планшета не умеет.)

## 4. Первый запуск

Выключите планшет (удерживать Power 15–20 с), вставьте microSD и включите. Ядро не
найдёт системы в USRDATA, найдёт карту установщика (сигнатура `B8KINST1`), сверит
контрольную сумму и распакует систему в USRDATA (прогресс на экране; карта читается медленно, большой образ ставится десятки минут).
Затем раздел расширится на весь объём и загрузится Phosh. Карту после этого можно
вынуть.

## 5. После установки

- Пользователь `user`, пароль пустой: экран блокировки снимается свайпом и «ОК».
  Задайте PIN в Консоли: `passwd`.
- Wi-Fi — в верхней шторке или в «Настройках».
- Права администратора — `doas <команда>`: спросит пароль пользователя `user`, поэтому
  сначала задайте его (`passwd`). Вход под root закрыт. В «Настройках»
  административные действия спрашивают тот же пароль.
- SSH включён, но вход по паролю запрещён: положите свой ключ в
  `~/.ssh/authorized_keys`.
- GPU-композитор включается сам. Если интерфейс не поднимается три раза подряд, система
  вернётся к рисованию процессором; отключить GPU насовсем: `touch ~/.phoc-gpu-disabled`.
- Драйверы GPU и прошивки Wi-Fi система берёт из стокового раздела Android `system`
  вашего планшета: он подключается только для чтения в `/system`. Не стирайте и не
  перепрошивайте его, пока пользуетесь Linux.
- Частота процессора закреплена на 806 МГц — так заметно реже падения ядра. Вернуть
  до 1.2 ГГц: `doas rm /etc/local.d/00-b8k-stability.start` и перезагрузить.
- После падения ядра планшет перезагрузится сам (или удерживайте Power 15–20 с).
  Журналы: `dmesg`, `/sys/fs/pstore/*`, `/var/log/phosh.log`.

## Обновление ядра

Новый `boot.img` прошивается так же, `fastboot flash boot`. Система в USRDATA
сохраняется.

## Возврат на Android

[RETURN-TO-ANDROID.md](RETURN-TO-ANDROID.md).
