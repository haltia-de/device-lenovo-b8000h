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

## Шаг 2 (2026-09-29): GPU рисует прямо в буфер экрана — 60 fps, без копий

`linux-port/gpu/g3-render/gles2_kmsdemo.c` + `linux-port/gpu/kms-demo.sh`:
- два буфера 1280x800 BGRA из стокового gralloc (ION → `ion_legacy`), обёрнутые в
  `ANativeWindowBuffer` → `eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)` → FBO;
- тот же dma-buf (fd из IMG-хэндла) → `DRM_IOCTL_PRIME_FD_TO_HANDLE` → `ADDFB2
  ARGB8888` → `SETCRTC`/`PAGE_FLIP` с событием (сырые uapi ioctl, без libdrm);
- `ion_legacy` теперь выделяет буферы от `contig_min` (256 КБ) одним блоком порядка
  ≤ 10 (`alloc_pages` + `split_page`): mediatek-drm без IOMMU принимает только непрерывные
  dma-buf (GEM DMA `drm_prime_get_contiguous_size`).
- Замеры: `light` (плоский градиент) — 60.0 fps, рендер 10 мс; `heavy` (3×sin на пиксель) —
  12 fps (упор в fill rate SGX544); `scene` (плазма по вершинам сетки 64x40 + 9 вращающихся
  треугольников) — **60.0 fps, рендер GPU 4.3 мс**, остальное — ожидание vblank.

## Шаг 3 (2026-09-29): TLS решён без патча glibc; что умеет драйвер

- `linux-port/hybris/tlspad.c` → `libtlspad.so`: `__thread void *pad[8]` в `.tdata`. Статический
  TLS glibc раскладывается по порядку загрузки: исполняемый файл, `LD_PRELOAD`, затем
  `DT_NEEDED`. Если у программы нет своего `PT_TLS`, pad встаёт на `tp+8` и принимает запись
  GL-слотов bionic. Проверка: `egl_info` без pad — SIGSEGV в `glCompileShader`, с
  `LD_PRELOAD=libtlspad.so` — `shader compile: OK`, `tlspad: ok, pad at tp+8`. Конструктор
  предупреждает, если у исполняемого файла свой TLS (тогда pad не спасает).
  Без переменных окружения (чтобы не наследовали musl-потомки): `ld.so --preload` или
  `libtlspad.so` первым в `DT_NEEDED`.
- `linux-port/hybris/egl_info.c` (EGL 1.4 «Android META-EGL», GLES 2.0 build 1.12@2701748):
  - есть: `EGL_KHR_image_base`, `EGL_ANDROID_image_native_buffer`, `EGL_KHR_fence_sync`,
    `EGL_KHR_wait_sync`, `EGL_KHR_gl_renderbuffer_image`; `GL_OES_EGL_image`,
    `GL_OES_EGL_image_external`, `GL_EXT_texture_format_BGRA8888`, `GL_IMG_read_format`,
    `GL_OES_surfaceless_context`, `GL_OES_vertex_array_object`, `GL_EXT_discard_framebuffer`,
    `GL_KHR_debug`; `eglMakeCurrent` без поверхности работает (хотя
    `EGL_KHR_surfaceless_context` не объявлен); текстуры до 4096.
  - нет: клиентских расширений EGL (`EGL_EXT_platform_base`, GBM/device-платформы),
    `EGL_EXT_image_dma_buf_import`, `EGL_KHR_no_config_context`,
    `EGL_ANDROID_native_fence_sync`, `GL_EXT_unpack_subimage`, `GL_EXT_read_format_bgra`.

## План G5: phoc рисует на GPU

Цель — композитор (phoc 0.55.1 + его wlroots) на SGX544, вывод на mediatek-drm тем же путём,
что в `gles2_kmsdemo`. Клиенты (phosh, приложения — Alpine/musl) пока рисуют сами (wl_shm);
композиция, анимации и переходы — на GPU. GPU-клиенты — отдельный этап G6.

Устройство: phoc собирается в Debian-chroot (glibc), но работает в корне Alpine как
«glibc-островок» — обёртка `/usr/local/bin/phoc-gpu`:
`exec /opt/debian/usr/lib/ld-linux-armhf.so.3 --inhibit-cache --preload <libtlspad.so>
/opt/debian/usr/local/bin/phoc "$@"`, плюс симлинки в корне Alpine
`/usr/lib/arm-linux-gnueabihf -> /opt/debian/usr/lib/arm-linux-gnueabihf` и
`/usr/local/hybris -> /opt/debian/usr/local/hybris` (стандартные пути поиска glibc для armhf
идут раньше `/usr/lib` с musl-библиотеками; GIO-модуль dconf, gconv и плагины libhybris
находятся по своим штатным путям). Ни одна переменная окружения не нужна, поэтому потомок
`-E gnome-session` (musl) запускается как обычно. Подключение без правки пакетов Alpine:
`phosh-session` читает `$HOME/.phoshdebug` → там `COMPOSITOR=/usr/local/bin/phoc-gpu` и
`WLR_RENDERER=gles2`.

Шаги:
1. **Автозапуск GPU**: OpenRC-служба `b8k-gpu` (модули из `/root/kN` + `pvrsrvctl --start` как в
   `g3-run.sh`, `/dev/__properties__`, `/dev/pvr_sync -> /dev/null`); `desktop.start` ждёт её.
   Откат: после 3 неудачных стартов phoc-gpu подряд — сеанс на штатном phoc + pixman.
2. **phoc 0.55.1 из исходников в chroot без изменений** (тот же релиз, что phosh 0.55 в Alpine,
   — протокол phosh-private совпадёт), запуск через `phoc-gpu` с `WLR_RENDERER=pixman`: должен
   вести себя как пакетный. Проверить `readelf -l phoc | grep TLS` (своего TLS быть не должно).
3. **Патч wlroots (подпроект phoc) «hybris»** — опция сборки, по умолчанию выключена:
   - `render/egl.c`: при hybris `eglGetDisplay(EGL_DEFAULT_DISPLAY)`, без требований
     client-расширений/платформ/`no_config_context` (контекст с конфигом RGBA8888 ES2),
     surfaceless make-current; наборы dmabuf-форматов для рендера — фиктивные
     `ARGB8888`/`XRGB8888` × {`LINEAR`, `INVALID`}; текстурных dmabuf-форматов нет → phoc не
     объявит linux-dmabuf, клиенты останутся на wl_shm (как сейчас с pixman).
   - новый аллокатор `render/allocator/gralloc.c`: `hw_get_module(gralloc)` через libhybris,
     буферы `HAL_PIXEL_FORMAT_BGRA_8888` с usage `HW_RENDER|HW_TEXTURE|HW_COMPOSER` (для XRGB
     тоже), `get_dmabuf` → fd из IMG-хэндла (как в демо), stride в байтах, modifier `LINEAR`;
     DRM-бэкенд wlroots импортирует его через PRIME + ADDFB2 (проверено демо).
   - `render/gles2/renderer.c`: цель рендера из gralloc-буфера →
     `eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)` из `ANativeWindowBuffer` →
     `glEGLImageTargetRenderbufferStorageOES` (как в демо).
   - `render/gles2/texture.c`: без `GL_EXT_unpack_subimage` — загрузка полосы строк при
     `stride == width*bpp`, иначе перепаковка во временный буфер.
4. **Замеры и оптимизация**: стоимость `glTex(Sub)Image2D` полноэкранного shm-буфера на SGX
   (твиддлинг на CPU); если дорого — текстуры клиентов через линейные gralloc-буферы
   (memcpy повреждённых строк + EGLImage, забор через `EGL_KHR_fence_sync`).

Риски: непрерывные 4 МБ-буферы (`contig_min`) при фрагментации памяти — swapchain выделять один
раз; стабильность (#22, порча памяти) под постоянной нагрузкой GPU; зависимости phoc 0.55.1
могут быть новее trixie (тогда собрать недостающие, например wayland-protocols, в
`/usr/local`, а не обновлять chroot).

## Шаг 4 (2026-09-30): G5 шаги 1–2 — GPU при загрузке, glibc-phoc в сеансе

- **Служба `b8k-gpu`** (`linux-port/gpu/b8k-gpu.initd` → `/etc/init.d/b8k-gpu`, runlevel default,
  `before local`): модули из `/root/kN`, `pvrsrvctl --start` через `pvr-run.sh` (логи
  `/root/pvrlog/boot-gpu`), флаг `/run/b8k-gpu.ready`. Повторный старт — `PVRSRVInitSrvConnect
  failed (129)` в log-main, считается «уже запущено». Проверено при реальных загрузках.
- **phoc 0.55.1 + wlroots 0.20.0** собраны в chroot (`linux-port/hybris/build-phoc.sh`,
  `-Ddefault_library=static`, renderers=gles2 против EGL/GLESv2 libhybris, backends drm+libinput,
  без Xwayland). trixie не хватило, собраны в `/usr/local` chroot из пула Debian (sha256 сверены
  с `.dsc`): wayland 1.26.0 (wlroots ≥ 1.24), pixman 0.46.4 (≥ 0.46), libdrm 2.4.134 (≥ 2.4.129),
  libxkbcommon 1.13.1 (≥ 1.8, `xkb-config-root=/usr/share/X11/xkb`), wayland-protocols 1.49
  (≥ 1.47; без `--strict`, т.к. при первой сборке сканер был 1.23); gmobile 0.6.0 и phoc — с
  sources.phosh.mobi (gitlab.gnome.org с планшета недоступен). Пакеты: см. apt-get в скрипте
  (json-glib для gmobile, заголовки xcb — их тянет glib-mkenums phoc даже без Xwayland).
- **Ловушка**: в релизном тарболе phoc подпроект `wlroots-0.20.x` лежит без патча из
  `subprojects/packagefiles/wlroots/` (meson накладывает его только при скачивании). Без него
  phoc отвечает phosh протокольной ошибкой `height 0 requested without setting top and bottom
  anchors` — phosh падает через ~8 с, сеанс перезапускается. Скрипт накладывает патч сам.
- **Запуск**: `/usr/local/bin/phoc-gpu` (`linux-port/hybris/phoc-gpu.sh`) — ld.so Debian
  `--inhibit-cache --library-path hybris:/usr/local/lib/…:/usr/lib/… --preload libtlspad.so`;
  симлинки в корне Alpine `/usr/lib/arm-linux-gnueabihf` и `/usr/local/hybris` → `/opt/debian/…`;
  `/root/.phoshdebug` (`linux-port/hybris/phoshdebug`) → `COMPOSITOR=/usr/local/bin/phoc-gpu`.
  Проверено: `LD_DEBUG=libs` — 86 библиотек, все из Debian; EGL/GLES — из libhybris, libdrm/
  wayland/xkbcommon — из `/usr/local`; в `/proc/PID/maps` нет musl; `PT_TLS` у phoc нет; RSS 29 МБ.
  Сеанс Phosh работает на glibc-phoc с pixman.
- **Откат проверен вживую**: три коротких запуска (из-за ошибки выше) → `phoc-gpu: 3 failed
  starts, starting the stock phoc`. Счётчик `/run/phoc-gpu.fails` сбрасывается через 60 с
  работы. Отладка: `touch /run/phoc-gpu.wldebug` → `WAYLAND_DEBUG=server` в лог сеанса.
  Отключить совсем: `touch /root/.phoc-gpu-disabled`.
- Мелочи: glibc-libinput 1.28 не читает quirks из `/usr/share/libinput` (файлы от libinput
  Alpine) — предупреждение, на сенсор не влияет; busybox `pkill -x phoc` не находит процессы
  (без `-x` или по PID — работает); phoc на первый SIGTERM иногда не выходит.

## Шаг 5 (2026-09-30): G5 шаг 3 — phoc рисует на SGX544

`linux-port/hybris/wlroots-0.20-hybris.patch` (накладывается `build-phoc.sh` после патча phoc):
- `render/egl.c`: если `eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)` пуст, а
  `eglHybrisCreateNativeBuffer` есть — это libhybris: `eglGetDisplay(EGL_DEFAULT_DISPLAY)`,
  конфиг RGBA8888/ES2, контекст без поверхности; фиктивные render-форматы XRGB/ARGB8888 ×
  {INVALID, LINEAR}. Никаких изменений сборки — только EGL API.
- `render/allocator/hybris.c` (новый): буферы через `EGL_HYBRIS_native_buffer`
  (`HAL_PIXEL_FORMAT_BGRA_8888`, usage `HW_TEXTURE|HW_RENDER|HW_COMPOSER`), DMA-BUF = тот fd
  gralloc-хэндла, что указывает на `dmabuf` (`/proc/self/fd`), stride × 4, modifier INVALID;
  `wlr_allocator_autocreate` выбирает его первым при hybris-EGL.
- `render/gles2`: цель рендера — `eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)`;
  **SGX отвечает `GL_INVALID_OPERATION` на `glEGLImageTargetRenderbufferStorageOES`**, поэтому
  FBO собирается через текстуру (`glEGLImageTargetTexture2DOES`); DMA-BUF-текстур нет (phoc не
  объявляет linux-dmabuf, клиенты на wl_shm); `glFinish()` перед сдачей кадра (у gralloc-буферов
  нет неявных fence); загрузка текстур без `GL_EXT_unpack_subimage` — полоса целых строк при
  плотном stride, иначе перепаковка; `GL_KHR_debug` выключен (драйвер пишет high-severity
  сообщение на каждый `eglMakeCurrent`, тысячи строк в минуту).
- `phoc-gpu` ставит `WLR_RENDERER=gles2` (`/root/.phoc-gpu-pixman` — вернуть pixman).

Итог на планшете: `Using libhybris EGL 1.4 (Android gralloc buffers)`, `GL renderer: PowerVR
SGX 544MP`; сеанс Phosh держится, в покое phoc ~0–3% CPU, RSS ~82–93 МБ (блобы драйвера).
Клиенты (phosh, приложения) по-прежнему рисуют сами (wl_shm), phoc загружает их кадры в
текстуры (`glTex(Sub)Image2D`, твиддлинг SGX на CPU) — следующий шаг: замерить и при
необходимости перейти на линейные gralloc-текстуры (шаг 4 плана G5).
