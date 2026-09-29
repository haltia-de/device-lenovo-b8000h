# G4 — libhybris: стоковый PowerVR из Linux-процесса

Цель G4 — чтобы Linux-интерфейс (Phosh) и приложения рисовались SGX544. Драйвер GPU у нас —
стоковые Android 4.4 (bionic) библиотеки; мост в Linux-процесс — libhybris.

## Шаг 1 (2026-09-29): мост работает — `RENDER OK`

- В Alpine 3.24 libhybris нет (и он написан под glibc), поэтому стенд — **Debian trixie armhf
  в chroot** `/opt/debian` на планшете (debootstrap из Alpine, подписи проверены; вход —
  `linux-port/hybris/debian-enter.sh`: bind `/dev`,`/proc`,`/sys`, стоковый `/system`,
  `/vendor -> /system/vendor`, `/dev/pvr_sync -> /dev/null`).
- libhybris upstream `7079712` собран `linux-port/hybris/build-hybris.sh` в
  `/usr/local/hybris` (jb-линкер, EGL-платформа по умолчанию `null`) против заголовков
  AOSP `android-4.4.2_r1` (`external/aosp-4.4.2`, `utils/extract-headers.sh` →
  `external/android-headers-19`, kernel-headers из вендорских исходников). Правка: в
  `cutils/native_handle.h` 4.4 нет `native_handle_clone()` (есть в libhybris) — добавлен
  прототип.
- Тест `linux-port/gpu/g3-render/gles2_pbuffer.c`, собранный Debian gcc против libhybris
  `libEGL/libGLESv2` (Services запущены стоковым `pvrsrvctl`, модули #22):
  `renderer "PowerVR SGX 544MP"`, `inside=(255,0,0,255) corner=(0,0,255,255)`, **RENDER OK**.

## Главная ловушка: TLS на ARM

Без обхода — SIGSEGV в `glCompileShader`: `libglslcompiler` зовёт `sprintf("%f")`, glibc
берёт десятичную точку из локали потока, а указатель на локаль испорчен. Причина: bionic и
glibc оба держат указатель потока в `TPIDRURO`; Android-код EGL/GLES пишет слоты
`TLS_SLOT_OPENGL`/`_API` (tp+12…tp+19) inline-ассемблером (перехват `__get_tls` libhybris
их не ловит) прямо поверх статического TLS glibc. Статический TLS исполняемого файла на ARM
(вариант I) идёт сразу после 8-байтового TCB, поэтому `__thread void *pad[8]` в программе
(`-DHYBRIS_TLS_PAD`) принимает эти записи — и всё работает.

Для произвольных программ (phoc, GTK-приложения) нужен общий обход: glibc с резервом
слотов после TCB (как в Mer/Sailfish) или аналог в загрузчике.

## Дальше

1. Общий TLS-обход (патч glibc Debian armhf или иной механизм).
2. Вывод на экран: EGL-платформа hybris (`fbdev`/`hwcomposer`, у нас HWC 1.x) или свой путь
   через KMS mediatek-drm + dma-buf; затем композитор (phoc/wlroots хочет GBM/DRM EGL).
3. Решить: Phosh на glibc-rootfs (Debian) или порт libhybris на musl (Alpine).
