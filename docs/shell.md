# Оболочка

Оболочка планшета — оболочка среды Haltia, отдельный репозиторий
[haltia-de/shell](https://github.com/haltia-de/shell); её полное описание —
[docs/shell.md](https://github.com/haltia-de/shell/blob/main/docs/shell.md)
там. Сборка образа (`linux-port/build-rootfs.sh`) ставит её подписанный пакет
из `linux-port/apps/packages.lock` (без пакета или с `B8K_FROM_SOURCE=1` —
собирает из тега в `linux-port/apps/sources`). Почему так — ADR
0001 (`docs/adr/0001-three-layers.md`) и ADR 0004 (`docs/adr/0004-de-apart-from-device.md`).
