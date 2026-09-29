# dc_nohw для MT6589 PowerVR DDK 1.12

Скрипт переносит Linux-7-совместимый IMG `dc_nohw` из локального
`external/linux_openpvrsgx`, но компилирует его с точными заголовками и
`config_kernel_user.h` MTK DDK `1.12@2701748`. Исходное внешнее дерево не
изменяется; адаптации хранятся в `patches/`.

Сборка запускается только из PowerShell:

```powershell
wsl.exe -d B8000Build -- sh "/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/drivers/pvrsrvkm/build.sh"
wsl.exe -d B8000Build -- sh "/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/drivers/dc_nohw/build-dc-nohw.sh"
```

Первый шаг обязателен: `dc_nohw.ko` разрешает
`PVRGetDisplayClassJTable` через `Module.symvers` именно этой сборки
`pvrsrvkm.ko`.
