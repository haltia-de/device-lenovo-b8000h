# Индикатор заряда B8000-H: план (2026-09-27)

## Факты
- BQ27541 на B8000 **не установлен**: `MTK_BQ27541_SUPPORT=no` в
  `external/b8000-vendor-src/mediatek/config/lenovo89_tb_x10_jb2/ProjectConfig.mk` (и x8).
  Узел `fuel-gauge@55` на i2c6 (патч 0028) даёт ENXIO — удалить в следующем BOOT.
- Зарядник BQ24196 (`bq24190_charger`, i2c6 0x6b) работает: status Charging, health Good.
  `omit-battery-class` оставить (у него нет capacity). От порта ПК input_current_limit = 500 мА.
- Вендор считает заряд программно («MTK SW fuel gauge», `mt6320_fuel_gauge_bq24196.c`):
  - напряжение: `fgauge_read_voltage()` → `get_i_sense_volt(15)` — AUXADC MT6320, канал ISENSE
    (канал 1, `AUXADC_REF_CURRENT_CHANNEL`), усреднение 15;
  - пересчёт кода: `mV = code * r_val_temp(4) * 1200 / 1024` (см. `PMIC_IMM_GetOneChannelValue` в
    `platform/mt6589/kernel/drivers/power/pmic_mt6320.c`: `rg_auxadc_chsel`, `rg_avg_num=3`,
    `rg_vbuf_en`, `rg_auxadc_start` 0→1, ожидание ready, чтение out-регистра канала);
  - OCV→% : таблицы `battery_profile_t0..t3` / `r_profile_*` в
    `custom/lenovo89_tb_x10_jb2/kernel/battery/battery/cust_fuel_gauge.h` (25 °C = t2),
    `OCV_BOARD_COMPESATE`.

## Реализация под 7.1
1. IIO-драйвер AUXADC MT6320 (регистры через pwrap regmap родительского MFD mt6320;
   адреса полей `rg_auxadc_*` из вендорского `upmu_hw.h`/`pmic_mt6320.c`), каналы BATSNS/ISENSE/VCDT.
2. Маленький power_supply-драйвер «b8k-battery» (или `generic-adc-battery` + свой расчёт):
   voltage_now из IIO, capacity = `power_supply_batinfo_ocv2cap()` по `ocv-capacity-table-0`
   из `monitored-battery` (simple-battery в DTS, значения из battery_profile_t2), status берётся
   у bq24190 (supplied-from). Сглаживание: скользящее среднее, при зарядке учитывать подъём
   напряжения под током (вендор: R_FG_BOARD_*/компенсация).
3. Проверка: `/sys/class/power_supply/b8k-battery/{voltage_now,capacity}`; UPower/Phosh показывают %.
