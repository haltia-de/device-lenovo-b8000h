# План: программы Haltia в Flatpak и deb

Составлен 19 октября 2026. Цель — чтобы программы Haltia ставились на
распространённые дистрибутивы Linux независимо от оболочки и образа:
сначала Flatpak (любой дистрибутив) и deb (Debian, Ubuntu и их потомки).
Проверка начинается на ПК разработчика: elementary OS 8 (Ubuntu 24.04
noble), x86_64, Pantheon на X11.

## Что упаковываем

| Программа | Репозиторий | Зависимости кроме PyGObject и GTK 3 | Сложность |
|---|---|---|---|
| Новости | news | — | лёгкая |
| Погода | weather | — | лёгкая |
| Раскраска | paint | haltia-kit, Pillow, numpy, scipy, pycairo | средняя |
| Музыка | music | haltia-kit, python-mpv/libmpv, aiohttp, aiofiles, yarl, requests, qrcode | средняя |
| Сообщения (MAX) | messenger | haltia-kit, python-mpv/libmpv, aiohttp, pydantic, aiofiles, aiosqlite, msgpack, python-socks, qrcode, websockets, zstandard | средняя |
| Видео | video | haltia-kit, Pillow, qrcode, yt-dlp, quickjs, mpv (вне планшета — вместо своего `vplay`) | средняя |
| библиотека | kit | PyGObject, python-mpv/libmpv, mutagen, ffmpeg | — |

Не упаковываем:
- **оболочку** — это рабочий стол со своим сеансом, её путь — apk и
  postmarketOS;
- **haltia-firefox** — настройки системного Firefox под планшет;
- **Календарь, Блокнот, Файлы и Галерею** — на обычном компьютере у людей
  уже есть свои (решение автора, 19 октября 2026). Они остаются в Haltia.

Вне оболочки программы уже работают: команды оболочки (`haltia-focus`,
вибро) они ищут и без них обходятся. Это надо проверить на каждой.

## Стандартные механизмы вместо своих

Проверено по коду программ 19 октября 2026. Что уже стандартное:
- `.desktop`, metainfo, иконки по id;
- уведомления через `Gio.Notification` (Сообщения);
- MPRIS (Музыка, Сообщения);
- ссылки через `Gtk.show_uri` (Новости, Сообщения);
- каталоги XDG (`GLib.get_user_special_dir`, `get_user_*_dir`);
- XDG autostart (Сообщения);
- счётчик через Unity LauncherEntry (понимают Plank, Dash to Dock, KDE).

Это работает и в Flatpak через порталы. Что своё и чем заменить:

| # | Сейчас | Стандарт | Кто | Зачем |
|---|---|---|---|---|
| 1 | Запрет сна прямым вызовом logind по D-Bus (`haltia_kit.Awake`) | `Gtk.Application.inhibit()`: в песочнице — портал Inhibit, без неё — сессия или logind | kit (Музыка, Сообщения, Видео) | из Flatpak logind недоступен |
| 2 | Токены и cookies в JSON-файлах: x_token и refresh Яндекса, токен VK, cookies YouTube | Secret Service через libsecret (в песочнице — портал Secret); файл — запасной путь, если службы ключей нет (на планшете её нет) | Музыка, Видео | пароли не лежат открытым текстом |
| 3 | `Gtk.FileChooserDialog` («Сохранить как», «Папка для файлов») | `Gtk.FileChooserNative`: в песочнице и в KDE — портал, иначе тот же диалог | Сообщения | из Flatpak обычный диалог видит только песочницу |
| 4 | Поиск картинок обходом `/media`, `/run/media` и каталогов XDG | выбор файла через `FileChooserNative` (портал), доступ на чтение к «Изображениям» | Раскраска | обход дисков в песочнице невозможен и лишний |
| 5 | Фоновый запуск через `/etc/xdg/autostart` | deb — так же; Flatpak — портал Background (`RequestBackground` с autostart) | Сообщения | у Flatpak нет общесистемного autostart |
| 6 | Свой плеер `vplay` на C (Wayland, вывод на панель оболочки) | вне планшета — обычный `mpv` отдельным процессом (он уже запасной путь), в Flatpak — `mpv` внутри пакета; окно поднимает сам композитор | Видео | убирает сборку на C и привязку к оболочке; `vplay` остаётся для планшета |
| 7 | Вход в YouTube: человек сам выгружает cookies.txt расширением браузера | deb: `cookies-from-browser` у yt-dlp (берёт cookies из Firefox или Chromium сам); Flatpak — оставить файл: песочница не видит профиль браузера | Видео | на обычном компьютере проще |
| 8 | Свой разбор RSS и Atom на `xml.etree` | `feedparser` (есть в Debian и Ubuntu): RSS 0.9–2.0, RDF, Atom, кодировки, кривые ленты | Новости | меньше своего кода, больше лент открывается |

Оставить как есть:
- **настройки в ini** (`~/.config/haltia/*.conf`): это XDG, а в Flatpak они
  сами окажутся в своём каталоге программы. GSettings потребовал бы схем, а
  пользы не добавил бы;
- **сеть через `urllib`**;
- **данные Gismeteo**: стандартного источника погоды нет. Определение
  места через портал геолокации — возможная доработка, не обязательная;
- **сеанс MAX** (база `web.db`): это не пароль, а данные клиента; в Flatpak
  они и так закрыты в каталоге программы;
- **numpy и scipy в Раскраске**: без них не сделать картинку по номерам;
  в Flatpak это готовые колёса, в deb — пакеты дистрибутива.

Пункты 1–8 выгодны и на планшете, кроме 6 и 7: там остаются `vplay` и файл
cookies. Делаются до упаковки, каждый в своём репозитории.

## Flatpak

- **Runtime:** `org.gnome.Platform` 51 (в нём GTK 3, PyGObject, Python 3).
- **Python-зависимости** — модулями манифеста, сгенерированными
  `flatpak-pip-generator` из закреплённых версий; numpy и scipy — готовыми
  колёсами.
- **libmpv** — своим модулем (в flathub/shared-modules его нет): у Музыки
  только звук, у Видео — mpv по рецепту `io.mpv.Mpv`. **ffmpeg** — из
  runtime GNOME 51; H.264 и AAC даёт его расширение `codecs-extra`
  (`ffmpeg-full` для 26.08 на Flathub нет). **yt-dlp** и **quickjs** — модулями
  со своей сборкой; yt-dlp часто обновляется, его версия в манифесте
  поднимается отдельно.
- **haltia-kit** входит модулем в каждую программу, которой он нужен (у
  Flatpak нет общих библиотек между приложениями).
- **Права** — минимальные. Сеть — всем шести. Файлы — только через портал
  выбора; Раскраске ещё чтение «Изображений». Звук — Музыке, Видео,
  Сообщениям. Wayland и X11 — всем.
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
  `python3-haltia-kit`. Видео в deb — без `vplay`, с зависимостью от `mpv`.
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
2. **Стандартные механизмы** (таблица выше): kit — п. 1; Новости — п. 8;
   Раскраска — п. 4; Сообщения — п. 3, 5; Музыка — п. 2; Видео — п. 2, 6, 7.
3. **Пилот на Погоде** (без зависимостей, с сетью): манифест Flatpak и
   `debian/`, локальная сборка, установка на ПК, запуск. Проверить: ярлык в
   меню, иконка, окно, тема, настройки в своём каталоге, работу без
   оболочки.
4. **Новости** (feedparser), затем **haltia-kit** — модулем в Flatpak и
   пакетом `python3-haltia-kit` в deb — и **Раскраска** (numpy, scipy).
5. **Музыка и Сообщения:** библиотеки, которых нет в дистрибутивах.
6. **Видео:** yt-dlp, quickjs, mpv вместо `vplay`.
7. **CI и репозитории:** workflow в `haltia-de/ci`, `haltia-de/flatpak`,
   `haltia-de/apt`, инструкции в профиле организации.
8. **Flathub:** заявки по одной программе.

Шаги 1–4 — мои (Погода, Новости, kit); Раскраска — сеанс Раскраски. Шаги 5–6 — сеансы
Музыки, Сообщений и Видео, по образцу пилота.

## Ход работ

19 октября 2026:
- **Стандартные механизмы:** kit 2.1.0 (п. 1), Новости 2.1.1 (п. 8),
  Раскраска 4.1.0 (п. 4), Сообщения 2.2.0 (п. 3, 5), Музыка 2.2.0 (п. 2).
  Видео (п. 2, 6, 7) — в работе.
- **Правило путей:** в песочнице `~` — настоящий домашний каталог, но
  закрытый; каталоги XDG ведут в `~/.var/app/<id>/`. Поэтому пути только
  через `GLib.get_user_config_dir()`, `get_user_data_dir()`,
  `get_user_cache_dir()`, `get_user_special_dir()`, никогда через
  `os.path.expanduser("~/...")`. На планшете и в deb пути те же, переносить
  ничего не нужно.
- **feedparser** получает ленту только потоком (`io.BytesIO`): байты или
  строку он принимает за имя файла или адрес.
- **Пилот — Погода 2.1.0:** `packaging/flatpak/` и `debian/` в репозитории.
  Flatpak собран `flatpak-builder` (Flatpak `org.flatpak.Builder`, `--user`)
  и установлен на ПК: работает в Pantheon, настройки и кэш — в
  `~/.var/app/io.github.haltia_de.Weather/`. deb собран в контейнерах noble
  и trixie, lintian — только «нет man-страницы».
- **Новости:** манифест с feedparser и sgmllib3k модулем (колесо и sdist с
  PyPI по sha256), Flatpak работает на ПК; deb зависит от
  `python3-feedparser` дистрибутива (6.0.10 в noble, 6.0.11 в trixie).
- **haltia-kit:** deb `python3-haltia-kit` (pybuild) для noble и trixie,
  ставится вместе с `python3-mpv` и `python3-mutagen`. Для Flatpak —
  модуль `packaging/flatpak/haltia-kit.yml` (kit по тегу, python-mpv,
  mutagen), программа подключает его в свой манифест. libmpv и ffmpeg в
  модуль не входят: их добавляет программа, которой нужен звук.
- **Сборка Flatpak на ПК:** `flatpak-builder` из Flatpak не видит `/tmp`;
  манифесты собирать из каталога под `/mnt/data` или `$HOME`.
- **CI:** `haltia-de/ci` v1 — `deb.yml` (noble и trixie, lintian) и
  `flatpak.yml` (образ Flathub GNOME 51, x86_64). Файлы и
  `SHA256SUMS.deb`/`SHA256SUMS.flatpak` — в выпуск тега, каждый файл
  проверяется по sha256 (`digest` выпуска). GitHub в именах файлов меняет
  `~` на точку и отвечает ложным 404 на часть загрузок. Удалённый и
  заново запушенный тег делает выпуск черновиком — так не делать.
  Выпуски с deb и Flatpak: Погода 2.1.1, Новости 2.2.0, kit 2.1.1
  (`python3-haltia-kit`).
- **Сеансы:** Музыка, Раскраска, Видео собрали Flatpak и deb и проверили
  на ПК. Видео в deb — `amd64`: QuickJS в noble слишком старый, свой
  собирается в `/usr/lib/haltia-video/vendor`.
- **Репозитории** (19 октября 2026) — в `haltia-de/packages`, на том же
  сайте, что apk: `https://haltia-de.github.io/packages/apt` (noble и
  trixie, amd64 и arm64) и `…/packages/flatpak` (`haltia.flatpakrepo`,
  `.flatpakref` на каждую программу, runtime с Flathub). Подпись — ключ
  GPG организации «Haltia packages», RSA 4096 до 2031 года, отпечаток
  `13461D3E807729260DD4A61461FCD08BBFE7462F`, секрет `HALTIA_GPG_PRIVKEY`,
  копия в `/mnt/data/b8000h-build/keys/gpg`. Кто что вправе публиковать —
  `publishers.conf`: репозиторий, его имена deb и id Flatpak; каждый файл
  сверяется с `digest` выпуска. Обновляется вместе с apk, раз в 30 минут.
  Все шесть программ вышли с deb и Flatpak и ставятся из репозиториев.
- **Папки пользователя на планшете** — русские (`user-dirs.dirs`), GTK
  переведён (`gtk+3.0-lang`).

## Решения

Приняты автором 19 октября 2026:
1. Flathub — после своего репозитория.
2. Клиенты MAX и Яндекс Музыки — только в своих репозиториях, не на Flathub.
3. Только Debian 13 trixie и Ubuntu 24.04 noble, без bookworm.
4. Новый ключ GPG организации для apt и Flatpak.
5. Календарь, Блокнот, Файлы — не упаковываем.
