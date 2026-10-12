# Wi-Fi MT6628 в установленной системе

Phosh подключается к сети WPA2-PSK штатным NetworkManager. Пользователь
подтвердил, что Wi-Fi в шторке работает. В образ не записывается пароль
сети.

Драйвер `wlan_mt6628` в `mtk_cfg80211_connect` принимает только
`WLAN_AKM_SUITE_8021X` и `WLAN_AKM_SUITE_PSK`. SHA256 и SAE он отвергает
с `-EINVAL`.

Две правки не дают NetworkManager запросить неподдерживаемый AKM.

1. Список шифров станции больше не содержит `WLAN_CIPHER_SUITE_AES_CMAC`
   (`linux-port/drivers/mt6628/wlan/os/linux/gl_init.c`). Иначе
   wpa_supplicant предлагает WPA-PSK-SHA256.
2. В образ ставится wpa_supplicant 2.11 без PMF, SAE, FILS и OWE.
   NetworkManager 1.52 добавляет WPA-PSK-SHA256, только если в D-Bus
   Capabilities есть строка `pmf`. Upstream 2.11 объявляет её даже при
   выключенном `CONFIG_IEEE80211W`. Патч
   `linux-port/wifi/patches/0003-dbus-pmf-capability-follows-config.patch`
   прячет эту строку за `#ifdef CONFIG_IEEE80211W`. Сборка:
   `linux-port/wifi/build-wpa-nm.sh`.

`/etc/NetworkManager/conf.d/wifi-mt6628.conf` оставляет `wlan0` под
NetworkManager, отключает случайный MAC при сканировании и фиксирует
постоянный адрес интерфейса. Отдельные службы `wpa_supplicant` и
`wpa_cli` в runlevel не входят: supplicant запускает NetworkManager.

Прошивки чипа лежат в стоковом `/system` и видны как ссылки в
`/lib/firmware`. `mt6628-wmt` поднимается раньше, чем монтируется
`/system`, поэтому первая загрузка не находит `WMT.cfg`.
`/etc/local.d/00-b8k-modules.start` ждёт файл до 30 секунд и заново
загружает `wmt`, затем `wlan_mt6628`. `rmmod` у BusyBox принимает одно
имя модуля.
