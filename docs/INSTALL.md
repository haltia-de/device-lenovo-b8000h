# Сборка и установка Linux на Lenovo YOGA Tablet 10 B8000-H

> Альфа. Установка **стирает все
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
   конфиг `lenovo-blade_defconfig` из патчей, `CONFIG_LOCALVERSION="-b8000h23"` (патч 0038: zram). Номер сборки в
   версии ядра выбирает каталог модулей GPU (`/usr/lib/b8k/k23`), так что
   система загружается и со старым ядром, если его модули на месте.
3. **boot.img с установщиком:**
   ```
   K=<дерево ядра> DTB_VARIANT=install2 IMAGE_SUFFIX=-my sh linux-port/build-safe-image.sh
   ```
   результат — `artifacts/linux-port/b8000h-linux-firstboot-my-safe.img` (ядро, DTB
   `install2` и initramfs из `linux-port/initramfs/init`).
4. **Внешние модули** — `build.sh` в `linux-port/drivers/` (PowerVR `pvrsrvkm`,
   `dc_nohw`, `ion-legacy`, Wi-Fi `mt6628`, звук). Модуль `wlan_mt6628` не
   объявляет шифр AES-CMAC: иначе NetworkManager выбирает WPA-PSK-SHA256,
   а чип его отвергает. Подробности — [wifi-mt6628.md](wifi-mt6628.md).
5. **wpa_supplicant для этой сети** — `sh linux-port/wifi/build-wpa-nm.sh`
   (официальный архив 2.11). Сборка выключает PMF/SAE/FILS/OWE и не
   объявляет capability `pmf`. Без этого NetworkManager не подключается.
6. **Оболочка, программы, библиотека `b8k-compat`, настройки Firefox
   `b8k-firefox`** — отдельные репозитории; их выпуски — подписанные пакеты
   Alpine в GitHub Releases (ADR 0002, `docs/adr/0002-versions-and-releases.md`).
   Шаг 7 ставит их по `linux-port/apps/packages.lock` (файл, версия, sha256;
   обновляет его `linux-port/apps/update-lock.sh`), проверяя подпись ключом
   из `linux-port/apk-keys/`. Чего нет в lock-файле (или всё — с
   `B8K_FROM_SOURCE=1`), собирается из исходников по тегам из
   `linux-port/apps/sources`: репозиторий с `build.sh` (оболочка) или
   `vplay/build.sh` (Видео) сначала собирает свои C-части в контейнере
   `b8000h-build:3.24`.
7. **Корневая система** — `UI_PROFILE=phosh sh linux-port/build-rootfs.sh`: Alpine 3.24
   armv7. Нужна сеть: пакеты программ и оболочки скрипт скачивает из GitHub
   Releases по lock-файлу (шаг 6). Профиль называется `phosh` по старой памяти, но сессия запускает свою
   оболочку b8k-shell — панель waybar слева на phoc ([shell.md](shell.md)). Скрипт ставит
   собранный wpa_supplicant, конфиг NetworkManager, повторную загрузку MT6628 после
   появления прошивки (с постоянным MAC из `/etc/machine-id`), службы
   `b8k-android-system` и `b8k-gpu`, модули GPU, обёртку `phoc-gpu`, оболочку
   b8k-shell, настройки Firefox b8k-firefox (uBlock Origin Lite вместо uBlock
   Origin, `/etc/firefox/policies/policies.json`), `earlyoom`, Aisleriot и исправление
   перезагрузки (`b8k-wdt-exrst`).
8. **Остров Debian** собирается на самом планшете: armhf в `/opt/debian`
   (debootstrap), `linux-port/hybris/build-hybris.sh` (libhybris), `build-phoc.sh`
   (phoc 0.55.1 с `wlroots-0.20-hybris.patch`). GLES 2.0 использует только этот
   композитор; приложения остаются на процессоре. Как всё устроено —
   [g4-hybris.md](g4-hybris.md).

   После сборки компиляторы, `-dev`-пакеты, perl и python острову не нужны
   (около 320 МБ). Оставить только то, что использует phoc:
   ```
   D=/opt/debian; L=$D/usr/lib/arm-linux-gnueabihf
   $L/ld-linux-armhf.so.3 --library-path $D/usr/local/hybris/lib:$D/usr/local/lib/arm-linux-gnueabihf:$L \
       --list $D/usr/local/bin/phoc | awk '$3 ~ "^/opt/debian/usr/lib" {sub("/opt/debian","",$3); print $3}' > $D/tmp/phoc-libs
   sh linux-port/hybris/debian-enter.sh sh -c '
       apt-mark showmanual > /tmp/manual.orig
       apt-mark auto $(cat /tmp/manual.orig)
       apt-mark manual apt $(while read f; do dpkg -S "$f" | cut -d: -f1; done < /tmp/phoc-libs | sort -u)
       mkdir -p /var/cache/apt/archives/partial /var/log/apt
       apt-get -y -o APT::AutoRemove::RecommendsImportant=false \
           -o APT::AutoRemove::SuggestsImportant=false autoremove --purge'
   ```
   Сохраните `/opt/debian/tmp/manual.orig`: чтобы снова собирать, верните
   пакеты командой `apt-get install $(cat /tmp/manual.orig)` внутри острова.
9. **Образ установщика для microSD** из ext4-образа системы и `boot.img`:
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
Затем раздел расширится на весь объём и загрузится рабочий стол. Карту после этого можно
вынуть.

## 5. После установки

- Пользователь `user`, пароль пустой, экрана блокировки нет. Как устроена оболочка —
  [shell.md](shell.md).
- Wi-Fi — нажатие на значок Wi-Fi в панели открывает «Настройки». Сеть — обычный
  WPA2-PSK. PMF и SAE этот чип в Linux не использует. Пароль в образ не
  зашивается. MAC-адрес постоянный (выводится из `/etc/machine-id`), поэтому
  роутер выдаёт планшету один и тот же адрес.
- Кнопка Power гасит и зажигает экран, через 30 с с погашенным экраном планшет
  засыпает; удержание Power 1 с открывает меню выключения и перезагрузки (в нём
  же терминал).
- Яркость — солнце на панели в группе под «ползунками» (уровень сохраняется),
  громкость — кнопки громкости,
  нажатие на значок динамика выключает звук.
- Права администратора — `doas <команда>`: спросит пароль пользователя `user`, поэтому
  сначала задайте его (`passwd`). Вход под root закрыт. Терминал — кнопка «Терминал» на панели. В «Настройках»
  административные действия спрашивают тот же пароль.
- SSH включён, но вход по паролю запрещён: положите свой ключ в
  `~/.ssh/authorized_keys`.
- GPU-композитор включается сам. Если интерфейс не поднимается три раза подряд, система
  вернётся к рисованию процессором; отключить GPU насовсем: `touch ~/.phoc-gpu-disabled`.
- Драйверы GPU и прошивки Wi-Fi система берёт из стокового раздела Android `system`
  вашего планшета: он подключается только для чтения в `/system`. Не стирайте и не
  перепрошивайте его, пока пользуетесь Linux.
- Если ядро упадёт, планшет перезагрузится сам (или удерживайте Power 15–20 с).
  Журналы: `dmesg`, `/sys/fs/pstore/*`, `/var/log/phosh.log` (журнал сессии, имя
  осталось от Phosh).

## Обновление ядра

Новый `boot.img` прошивается так же, `fastboot flash boot`. Система в USRDATA
сохраняется.

## Возврат на Android

[RETURN-TO-ANDROID.md](RETURN-TO-ANDROID.md).
