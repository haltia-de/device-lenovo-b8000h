# Context

The words this project and its sibling repositories use for how they are
split. The decision behind them: docs/adr/0001-three-layers.md.

- **Устройство** (device; this repository, `lenovo-b8000h-linux`): everything
  tied to the B8000-H — the kernel and its patches, the DTS, the drivers, the
  installer, the image build, and the device profile. It builds the image from
  pinned versions of the shell and the programs.
- **Профиль устройства** (device profile): the hardware-specific helpers and
  numbers the shell plugs in — reboot (`b8k-wdt-exrst`), display tuning
  (`b8k-rdma-fifo`), the SD slot, the USB role, Wi-Fi watchdog and its
  reload around sleep, zram, the stock `system` mount, the touch thresholds,
  the speaker profile, the GPU stack.
- **Оболочка** (shell; repository `b8k-shell`): what runs programs on a
  tablet without a desktop — the panel, the home screen (the desktop launcher),
  the close button «✕», the start-up curtain, the panel tab, the shell's own
  questions (fuzzel), the touch filter with its buzz, the keyboard, waybar and
  the `b8k` GTK theme. It knows programs only through freedesktop standards.
- **Программа** (program; a repository of its own each): Видео, Музыка, MAX,
  Файлы/Галерея, Блокнот, Календарь, Раскраска, Погода, Новости. Built and
  installed on its own, at home in GNOME or KDE as in the shell.
- **Библиотека программ** (`b8k-compat`, Python module `b8k_compat`, GTK3):
  the code programs share — questions and menus as in-window GTK dialogs,
  settings, sound on libmpv, the photo viewer and small pictures, a fake
  player for tests. It knows nothing of the shell.
- **Id программы** (program id): one reverse-DNS name, `io.github.trudenboy.<Program>`,
  for the `.desktop` file, the GApplication and the Wayland window.
- **Стандарты связи** (integration standards): how the shell and programs meet —
  the `.desktop` file (with `X-B8K-…` keys for the curtain and the panel),
  `com.canonical.Unity.LauncherEntry` for counts on buttons, logind inhibit to
  stay awake, XDG autostart for background start.
