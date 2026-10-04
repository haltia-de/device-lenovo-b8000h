# Возврат на стоковый Android 4.4.2

Linux занимает только разделы BOOT и USRDATA; `system`, NVRAM и остальные разделы
он не меняет. Поэтому для возврата достаточно вернуть стоковые BOOT и RECOVERY и
стереть USRDATA.

Понадобится стоковая прошивка `B8000_A442_001_020_140620_WW_3GDATA`: из её папки
`target_bin` нужны `boot.img` и `recovery.img`. Сверьте их MD5 с `Checksum.ini` из той
же прошивки.

1. Fastboot: на выключенном планшете Vol− + Power → в меню fastboot.
   ```
   fastboot getvar product
   ```
   должно быть `BLADE10_ROW_3GDATA`.
2. Прошить стоковые образы:
   ```
   fastboot flash recovery recovery.img
   fastboot flash boot boot.img
   ```
3. Выключить (Power 15–20 с), затем Vol+ + Power → стоковый Recovery →
   **wipe data/factory reset** → **wipe cache partition** → reboot system now.
   Без wipe Android не загрузится: в USRDATA лежит файловая система Linux.
4. Первый запуск Android — до 5 минут.

Если `fastboot flash recovery` не проходит: SP Flash Tool → вкладка Download →
scatter стоковой прошивки → режим **Download Only** → отметить **только** BOOTIMG и
RECOVERY. Никогда не используйте Format All + Download — он сотрёт NVRAM с IMEI и
калибровками.
