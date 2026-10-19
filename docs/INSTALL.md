# Сборка и установка Linux на Lenovo YOGA Tablet 10 B8000-H

> Альфа. Установка **стирает все
> пользовательские данные Android** (раздел USRDATA). Только для **B8000-H** (60047 / Z0AJ).
> Всё — на свой риск. Готовые образы — в выпусках репозитория на GitHub; своя
> сборка (§0) нужна только для изменений.

## 0. Сборка

**Готовые образы** — в выпусках репозитория на GitHub (тег `vX.Y.Z`):
`pertin-haltia-lenovo-b8000h-<версия>-boot.img`, `-sd-installer.img.xz`, `-root.ext4.xz`,
`-kernel-<ядро>.tar.gz`, `SHA256SUMS` (до v2.0.0 — `b8000h-…`). Их собирает GitHub Actions
(workflow `image`, ADR 0003 — `docs/adr/0003-ci-image.md`) теми же сценариями
из `ci/`, что описаны ниже. Своя сборка: `docker build -t b8k-build ci`, затем
в привилегированном контейнере с репозиторием в `/src` и рабочим каталогом в
`/opt/b8000h` — `sh /src/ci/kernel.sh` и `sh /src/ci/image.sh` (остров —
`sh /src/ci/island.sh`, часы под qemu). Ниже — что делает каждый шаг.

1. **Окружение.** Docker-образ из `ci/Dockerfile` (Alpine 3.24 x86_64: clang/LLVM,
   `mkbootimg`, qemu-arm, e2fsprogs, debootstrap). Сценарии ждут репозиторий в
   `/src` и рабочий каталог `/opt/b8000h`; контейнер — привилегированный (chroot,
   binfmt).
2. **Ядро.** [TeamYogaBlade2/linux](https://github.com/TeamYogaBlade2/linux/tree/blade/v7.1)
   ветка `blade/v7.1`, коммит `7dd245445adf` (берёт `ci/sources.sh` вместе с ядерной
   частью PowerVR DDK и openpvrsgx); поверх — патчи `linux-port/patches/` по
   порядку номеров (`ci/apply-patches.sh`; несколько патчей — Python-скрипты);
   конфиг `lenovo-blade_defconfig` из патчей, `CONFIG_LOCALVERSION="-b8000h25"`. Номер сборки в
   версии ядра выбирает каталог модулей GPU (`/usr/lib/b8k/k25`), так что
   система загружается и со старым ядром, если его модули на месте. `ci/kernel.sh`
   делает шаги 2–4 разом и кладёт `boot.img` и архив ядра в `/opt/b8000h/dist`.
3. **boot.img с установщиком:**
   ```
   K=<дерево ядра> DTB_VARIANT=install2 IMAGE_SUFFIX=-my sh linux-port/build-safe-image.sh
   ```
   результат — `artifacts/linux-port/b8000h-linux-firstboot-my-safe.img` (ядро, DTB
   `install2` и initramfs из `linux-port/initramfs/init`).
4. **Внешние модули** — только GPU: `linux-port/drivers/pvrsrvkm/build.sh`,
   `dc_nohw/build-dc-nohw.sh`, `ion-legacy/build-ion-legacy.sh`; набор ложится в
   `/opt/b8000h/out/gpu-sets/kN`. Wi-Fi `mt6628`, звук и батарея собираются в
   самом ядре (патч 0033), исходники — в тех же каталогах. Модуль `wlan_mt6628` не
   объявляет шифр AES-CMAC: иначе NetworkManager выбирает WPA-PSK-SHA256,
   а чип его отвергает. Подробности — [wifi-mt6628.md](wifi-mt6628.md).
5. **wpa_supplicant для этой сети** — `sh linux-port/wifi/build-wpa-nm.sh`
   (официальный архив 2.11). Сборка выключает PMF/SAE/FILS/OWE и не
   объявляет capability `pmf`. Без этого NetworkManager не подключается.
6. **Среда Haltia** — оболочка, программы, библиотека `haltia-kit`, настройки
   Firefox `haltia-firefox` — отдельные репозитории организации
   [haltia-de](https://github.com/haltia-de) (ADR 0004); их выпуски — подписанные
   пакеты Alpine (ADR 0002) в [репозитории Haltia](https://haltia-de.github.io/packages/).
   Шаг 7 ставит метапакеты `haltia-desktop` и `haltia-desktop-ru`, а каждый пакет
   из `linux-port/apps/packages.lock` (файл, версия, sha256; обновляет его
   `linux-port/apps/update-lock.sh`) скачивает и сверяет с его sha256. Чего нет в lock-файле (или всё — с
   `B8K_FROM_SOURCE=1`), собирается из исходников по тегам из
   `linux-port/apps/sources`: репозиторий с `build.sh` (оболочка) или
   `vplay/build.sh` (Видео) сначала собирает свои C-части.
7. **Корневая система** — `UI_PROFILE=shell sh linux-port/build-rootfs.sh`: Alpine 3.24
   armv7. Нужна сеть: пакеты Haltia скрипт берёт из репозитория Haltia по
   lock-файлу (шаг 6). Сессия запускает оболочку Haltia — панель waybar слева
   на phoc ([shell.md](shell.md)). Скрипт ставит
   собранный wpa_supplicant, конфиг NetworkManager, повторную загрузку MT6628 после
   появления прошивки (с постоянным MAC из `/etc/machine-id`), службы
   `b8k-android-system` и `b8k-gpu`, модули GPU, обёртку `phoc-gpu`, среду Haltia,
   настройки Firefox `haltia-firefox` (uBlock Origin Lite вместо uBlock Origin,
   `/etc/firefox/policies/policies.json`), `iio-sensor-proxy` (поворот экрана),
   `feedbackd`, `earlyoom`, Aisleriot, заставку загрузки со знаком Haltia и
   исправление перезагрузки (`b8k-wdt-exrst`).
8. **Остров Debian** (`/opt/debian` в системе) — `sh /src/ci/island.sh`: Debian trixie
   armhf через debootstrap под qemu, libhybris (`linux-port/hybris/build-hybris.sh`),
   заголовки Android 4.4.2 из AOSP, phoc 0.55.1 с `wlroots-0.20-hybris.patch` и
   `phoc-0.55.1-touch-cancel.patch` (`build-phoc.sh`), затем урезание до библиотек phoc; результат —
   `/opt/b8000h/dist/b8000h-island.tar.gz`. Все источники закреплены коммитами и
   суммами. Сборка идёт часы, поэтому в CI остров выпускается отдельно (тег `island-*`),
   а образ берёт выпуск из `linux-port/hybris/island.lock`; без него система
   рисует процессором. GLES 2.0 использует только этот композитор; приложения
   остаются на процессоре. Как всё устроено — [g4-hybris.md](g4-hybris.md).
9. **Образ установщика для microSD** из ext4-образа системы и `boot.img`:
   ```
   python3 tools/mk-sd-installer.py root.ext4 boot.img b8000h-sd-installer.img
   ```
   Чистый образ (без личных данных и файлов Android) делает
   `linux-port/rootfs/make-release-rootfs.sh` — из tar работающего планшета или, в
   `ci/image.sh`, из только что собранной системы (шаги 5, 7, 9 разом).

## Что понадобится для установки

- Планшет B8000-H, заряд **не меньше 60%**.
- microSD на 4 ГБ и больше (содержимое карты будет стёрто).
- ПК с Windows или Linux:
  - `adb` и `fastboot` ([Android platform-tools](https://developer.android.com/tools/releases/platform-tools));
  - драйверы MediaTek USB VCOM и SP Flash Tool v5 — для резервной копии и возврата;
  - стоковая прошивка `B8000_A442_001_020_140620_WW_3GDATA` (Android 4.4.2, тема B8000
    на 4pda) — в ней scatter-файл и стоковые `boot.img`/`recovery.img`.
- Из выпуска: `pertin-haltia-lenovo-b8000h-<версия>-boot.img`, `pertin-haltia-lenovo-b8000h-<версия>-sd-installer.img.xz` и `SHA256SUMS`
  (проверка: `sha256sum -c --ignore-missing SHA256SUMS`) — или свои из §0.

## 1. Резервная копия уникальных разделов

В разделах NVRAM, PRO_INFO, PROTECT_F и PROTECT_S лежат IMEI, MAC-адреса и калибровки
радио — их нельзя взять из чужой прошивки. Linux их не трогает, но копия должна быть.

SP Flash Tool → Readback: загрузите scatter стоковой прошивки, добавьте эти разделы
(адреса и длины — из scatter; для этой прошивки готовые конфиги лежат в
`tools/readback-v5-*.xml`), выключите планшет и подключите его к ПК. Храните копию
у себя и никуда не выкладывайте.

## 2. Образ установщика на microSD

Запишите образ установщика на карту целиком: balenaEtcher принимает `.img.xz` как есть;
для Rufus (режим DD) или `dd` сначала распакуйте (`xz -d pertin-haltia-lenovo-b8000h-<версия>-sd-installer.img.xz`,
затем `dd if=pertin-haltia-lenovo-b8000h-<версия>-sd-installer.img of=/dev/sdX bs=4M conv=fsync` в Linux).
Разделы на карте создавать не нужно.

## 3. Ядро в раздел BOOT

1. Перевести планшет в fastboot: из Android `adb reboot bootloader` или на выключенном
   планшете Vol− + Power → в меню выбрать fastboot.
2. Проверить, что это нужная модель:
   ```
   fastboot getvar product
   ```
   ответ должен быть `BLADE10_ROW_3GDATA`.
3. Прошить ядро:
   ```
   fastboot flash boot pertin-haltia-lenovo-b8000h-<версия>-boot.img
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
  Журналы: `dmesg`, `/sys/fs/pstore/*`, `/var/log/session.log` (журнал сессии;
  в образах до 2.0.1 — `/var/log/phosh.log`).
- Обновления программ и оболочки: «Настройки → Обновления» или
  `doas haltia-update upgrade`. Перед обновлением делается снимок, при сбое
  обновление откатывается само; откатить вручную можно там же.

## Обновление ядра

Новый `boot.img` прошивается так же, `fastboot flash boot`. Система в USRDATA
сохраняется.

## Возврат на Android

[RETURN-TO-ANDROID.md](RETURN-TO-ANDROID.md).
