# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm: the vibration motor as a force-feedback
# input device. The motor hangs on the MT6320 VIBR LDO (stock LK buzzes it
# at power-on through the same LDO), so the generic regulator-haptic driver
# drives it: FF_RUMBLE switches the LDO on at 2.8 V.
K = '/opt/b8000h/linux-drm/'


def ed(p, old, new, marker):
    s = open(K + p).read()
    if marker in s:
        print('already', p)
        return
    assert s.count(old) == 1, (p, old[:60])
    open(K + p, 'w').write(s.replace(old, new))
    print('patched', p)


ed('arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts',
   '''/ {
	mt6628_sdio_vmmc: regulator-mt6628-sdio {''',
   '''/ {
	/* Vibration motor on the MT6320 VIBR LDO. */
	haptic {
		compatible = "regulator-haptic";
		haptic-supply = <&mt6320_ldo_vibr>;
		min-microvolt = <2800000>;
		max-microvolt = <2800000>;
	};

	mt6628_sdio_vmmc: regulator-mt6628-sdio {''',
   'compatible = "regulator-haptic"')

for cfg in ('arch/arm/configs/lenovo-blade_defconfig', '.config'):
    s = open(K + cfg).read()
    if 'CONFIG_INPUT_REGULATOR_HAPTIC=y' in s:
        print('already', cfg)
        continue
    s = s.replace('# CONFIG_INPUT_REGULATOR_HAPTIC is not set\n', '')
    s = s.replace('# CONFIG_INPUT_FF_MEMLESS is not set\n', '')
    s += '# Vibration motor (regulator-haptic on VIBR)\nCONFIG_INPUT_FF_MEMLESS=y\nCONFIG_INPUT_REGULATOR_HAPTIC=y\n'
    open(K + cfg, 'w').write(s)
    print('patched', cfg)
