# План: программы Haltia в Flatpak и deb

Составлен 19 октября 2026. Цель — чтобы программы Haltia ставились на
распространённые дистрибутивы Linux независимо от оболочки и образа:
сначала Flatpak (любой дистрибутив) и deb (Debian, Ubuntu и их потомки).
Проверка начинается на ПК разработчика: elementary OS 8 (Ubuntu 24.04
noble), x86_64, Pantheon на X11.

## Что упаковываем

| Программа | Репозиторий | Зависимости кроме PyGObject и GTK 3 | Сложность |
|---|---|---|---|
| Календарь | calendar | — | лёгкая |
| Новости | news | — | лёгкая |
| Погода | weather | — | лёгкая |
| Блокнот | text | haltia-kit | лёгкая |
| Файлы и Галерея | files | haltia-kit (mpv, mutagen, ffmpeg) | средняя |
| Раскраска | paint | haltia-kit, Pillow, numpy, scipy, pycairo | средняя |
| Музыка | music | haltia-kit, python-mpv/libmpv, aiohttp, aiofiles, yarl, requests, qrcode | средняя |
| Сообщения (MAX) | messenger | haltia-kit, python-mpv/libmpv, aiohttp, pydantic, aiofiles, aiosqlite, msgpack, python-socks, qrcode, websockets, zstandard | средняя |
| Видео | video | haltia-kit, Pillow, qrcode, yt-dlp, quickjs; плеер `vplay` на C (libmpv, Wayland, cairo, pango) | сложная |
| библиотека | kit | PyGObject, python-mpv/libmpv, mutagen, ffmpeg | — |

Не упаковываем: **оболочку** (это рабочий стол со своим сеансом, её путь —
apk и postmarketOS) и **haltia-firefox** (настройки системного Firefox под
планшет).

Вне оболочки программы уже работают: команды оболочки (`haltia-focus`,
вибро) они ищут и без них обходятся. Это надо проверить на каждой.

## Flatpak

- **Runtime:** `org.gnome.Platform` (в нём GTK 3, PyGObject, Python 3).
  Версия — текущая стабильная на Flathub на момент начала работ.
- **Python-зависимости** — модулями манифеста, сгенерированными
  `flatpak-pip-generator` из закреплённых версий; numpy и scipy — готовыми
  колёсами.
- **libmpv** — модулем из flathub/shared-modules. **ffmpeg** — расширение
  `org.freedesktop.Platform.ffmpeg-full`. **yt-dlp** и **quickjs** — модулями
  со своей сборкой; yt-dlp часто обновляется, его версия в манифесте
  поднимается отдельно.
- **haltia-kit** входит модулем в каждую программу, которой он нужен (у
  Flatpak нет общих библиотек между приложениями).
- **Права** — минимальные. Сеть — только Новостям, Погоде, Музыке, Видео и
  Сообщениям. Доступ к файлам: Файлам — домашний каталог, Блокноту и
  Раскраске — через портал выбора файлов. Звук — Музыке, Видео, Сообщениям,
  Файлам. Wayland и X11 — всем.
- **Идентификаторы** уже годятся для Flatpak и Flathub:
  `io.github.haltia_de.<Программа>` (ADR 0004).
- **Где публиковать**, по шагам:
  1. Сначала свой репозиторий OSTree на GitHub Pages
     (`haltia-de/flatpak`), подписанный GPG, и `.flatpakref`/`.flatpakrepo`
     для установки одной командой.
  2. Затем Flathub — по одной программе, начиная с тех, что не завязаны на
     чужие сервисы.

  Клиенты MAX и Яндекс Музыки на Flathub могут не пропустить: это
  неофициальные клиенты чужих сервисов, с их товарными знаками.

## deb

- **Пакеты:** `haltia-<программа>` (Architecture: all) и
  `python3-haltia-kit`; у Видео — `haltia-video` (all) и `haltia-vplay`
  (any: amd64, arm64, armhf).
- **Сборка** — `debian/` в каждом репозитории (debhelper, pybuild для kit),
  в контейнерах `debian:trixie` и `ubuntu:noble`.
- **Зависимости** берутся из дистрибутива, где они есть (python3-gi,
  gir1.2-gtk-3.0, python3-mpv, python3-aiohttp, python3-pil, python3-numpy,
  python3-scipy, python3-mutagen, ffmpeg и т. д.). Чего нет или что слишком
  старое — вендорится в пакет программы: pydantic нужной версии, свежий
  yt-dlp. Таблицу «что есть в trixie и noble» — составить первым шагом.
- **Где публиковать** — свой apt-репозиторий на GitHub Pages
  (`haltia-de/apt`, reprepro или aptly), подписанный отдельным ключом GPG.
  Ключ — секрет организации, рядом с ключом apk.
- **Цели:** Ubuntu 24.04 (и elementary OS 8, Linux Mint 22, Pop!_OS — те же
  пакеты) и Debian 13 trixie. Debian 12 bookworm — если обойдётся без
  отдельных сборок.

## Сборка в CI

`haltia-de/ci` получает два переиспользуемых workflow рядом с `apk.yml`:

- `flatpak.yml`: `flatpak-builder` в контейнере
  `ghcr.io/flathub-infra/flatpak-github-actions` → бандл `.flatpak` в выпуск
  тега и коммит в репозиторий OSTree;
- `deb.yml`: `dpkg-buildpackage` в контейнерах trixie и noble → `.deb` в
  выпуск тега, затем `haltia-de/apt` собирает индекс.

Тег `vX.Y.Z` по-прежнему один на все форматы (ADR 0002). Версия в
`debian/changelog` и в metainfo совпадает с тегом, проверка — в ci.

## Порядок

1. **ПК:** `flatpak-builder` и SDK из Flathub (`--user`, без sudo), сборка
   deb — в Docker.
2. **Пилот на Календаре** (без зависимостей): манифест Flatpak и `debian/`,
   локальная сборка, установка на ПК, запуск. Проверить: ярлык в меню, иконка,
   окно, тема, настройки в своём каталоге, работу без оболочки.
3. **Блокнот и haltia-kit:** второй уровень — библиотека модулем в Flatpak
   и пакетом `python3-haltia-kit` в deb.
4. **Новости и Погода** (сеть), затем **Файлы** (ffmpeg, mpv) и **Раскраска**
   (numpy, scipy).
5. **Музыка и Сообщения:** библиотеки, которых нет в дистрибутивах.
6. **Видео:** плеер на C, yt-dlp, quickjs.
7. **CI и репозитории:** workflow в `haltia-de/ci`, `haltia-de/flatpak`,
   `haltia-de/apt`, инструкции в профиле организации.
8. **Flathub:** заявки по одной программе.

Пилот (шаги 1–2) — мой, в репозиториях моих программ. Шаги 5–6 — сеансы
Музыки, Сообщений и Видео, по образцу пилота.

## Решения (за автором)

1. Flathub — сразу или после своего репозитория?
2. Клиенты MAX и Яндекс Музыки — публиковать ли их вообще вне планшета
   (это неофициальные клиенты чужих сервисов)?
3. Debian 12 bookworm — поддерживать или только trixie и noble?
4. Ключ GPG для apt и Flatpak — новый ключ организации.
