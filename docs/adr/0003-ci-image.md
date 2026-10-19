# 0003. Сборка ядра и образа в CI

Дата: 18 октября 2026. Статус: принято.

## Контекст

Ядро, модули, boot.img, корневая система и образ установщика собирались
вручную в WSL или Docker на одной машине. Часть шагов жила вне репозитория
(наложение патчей ядра, сборка острова Debian, упаковка установщика), а
остров для phoc на GPU собирали на планшете или в chroot на ПК. После ADR
0002 программы и оболочка приходят готовыми пакетами — осталось
автоматизировать то, что относится к самому устройству.

## Решение

Всё собирается на GitHub-раннерах из публичных источников, в среде из
`ci/Dockerfile` (Alpine 3.24 x86_64, clang/LLVM, qemu-arm), с этим
репозиторием в `/src` и рабочим каталогом `/opt/b8000h`.

**Ядро** (`ci/kernel.sh`): `ci/sources.sh` берёт TeamYogaBlade2/linux
`blade/v7.1` @ `7dd245445adf`, ядерную часть PowerVR DDK 1.12 MT6589
(bq/aquaris-5HD @ `1b08ad7`, только её каталог pvr) и openpvrsgx @ `c017d06`;
`ci/apply-patches.sh` накладывает `linux-port/patches/` и делает конфиг.
Затем zImage, DTB, модули, внешние модули GPU (`pvrsrvkm`, `dc_nohw`,
`ion-legacy`) в набор `kN` и boot.img установщика (`build-safe-image.sh`,
DTB `install2`, initramfs со статическим busybox Alpine armv7).

**Остров** (`ci/island.sh`, workflow `island`): Debian trixie armhf через
debootstrap под qemu, libhybris @ `7079712`, заголовки Android 4.4.2 из AOSP
`android-4.4.2_r1`, phoc 0.55.1 с wlroots 0.20, `wlroots-0.20-hybris.patch` и `phoc-0.55.1-touch-cancel.patch`,
новые wayland/pixman/libdrm/xkbcommon/wayland-protocols из пула Debian
(sha256 из `.dsc`), затем урезание до библиотек phoc. Он собирается часами,
поэтому выпускается отдельно (тег `island-*` или запуск вручную), а сборка
образа берёт выпуск, указанный в `linux-port/hybris/island.lock` (URL и
sha256).

**Образ** (`ci/image.sh`): wpa_supplicant без PMF (официальный архив, sha512),
`build-rootfs.sh` (Alpine armv7, пакеты из `linux-port/apps/packages.lock`,
модули ядра, набор GPU, остров), затем `make-release-rootfs.sh` — тот же
выпускной путь, что и для образа с планшета: чистка, блокировка root,
аудит, ext4 и установщик microSD.

**Выпуск** — по тегу `vX.Y.Z` репозитория устройства (workflow `image`, SemVer
по ADR 0002): `b8000h-boot.img`, `b8000h-sd-installer.img.xz`,
`b8000h-root.ext4.xz`, `b8000h-kernel-<версия>.tar.gz` (zImage, DTB, конфиг,
модули, набор GPU — обновить ядро без переустановки) и `SHA256SUMS`. Запуск
вручную без тега оставляет те же файлы артефактом на неделю. С v2.0.0
(ADR 0004) выпуск называется Pertin Haltia, а файлы —
`pertin-haltia-lenovo-b8000h-<версия>-{boot.img,sd-installer.img.xz,root.ext4.xz,kernel-<ядро>.tar.gz}`.

**Шаги и переиспользование.** Workflow `image` — три задачи: `kernel`
(результат кэшируется по хешу всего, что читает `ci/kernel.sh`: патчи,
драйверы, initramfs, сценарии; правка в другом месте сборку ядра
пропускает), `rootfs` (берёт артефакт `kernel` — tar ядра с модулями и
набором GPU и boot.img — и собирает образы) и `release` (только по тегу).
«Re-run failed jobs» переиспользует артефакты прошедших задач. Остров
собирается в два этапа: база (Debian, зависимости сборки, libhybris,
исходники) кэшируется по хешу того, что её делает (`ci/island.sh` до строки
`log "build libhybris…"`, `build-hybris.sh`, `tlspad.c`, `ci/Dockerfile`), а phoc и
урезание идут поверх неё: файлы, нужные только phoc, копируются после этой строки,
и правка патча phoc пересобирает только phoc. Образ среды из `ci/Dockerfile` тоже хранится в кэше
(`.github/actions/build-env`).

## Последствия

- Образ можно собрать без этой машины и без планшета; версии всех внешних
  источников закреплены в `ci/sources.sh`, `ci/island.sh` и lock-файлах.
- Блобов Android в выпуске по-прежнему нет: их даёт раздел `system`
  планшета; аудит `make-release-rootfs.sh` это проверяет.
- Ручная сборка остаётся: те же сценарии запускаются в контейнере из
  `ci/Dockerfile` локально.
