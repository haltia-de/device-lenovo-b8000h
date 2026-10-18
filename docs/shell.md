# Оболочка

Оболочка планшета — отдельный репозиторий
[b8k-shell](https://github.com/trudenboy/b8k-shell); её полное описание —
[docs/shell.md](https://github.com/trudenboy/b8k-shell/blob/main/docs/shell.md)
там. Сборка образа (`linux-port/build-rootfs.sh`) берёт её версию из
`linux-port/apps/sources`, собирает её C-части и ставит. Почему так — ADR
0001 (`docs/adr/0001-three-layers.md`).
