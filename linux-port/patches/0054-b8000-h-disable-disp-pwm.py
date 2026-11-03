# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm. The upstream mt6589-disp-pwm driver and
# mt6589-bls-backlight both map the BLS block at 14008000: the blade.dtsi
# enables &bls, and the backlight class then wrote a PWM duty that never
# reached the LED. The B8000-H drives its backlight through the BLS
# registers only (vendor cust_leds CUST_BLS_PWM), so keep the disp-pwm
# driver away from the block.
K = '/opt/b8000h/linux-drm/'
p = K + 'arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s = open(p).read()
if 'B8000-H: keep the disp-pwm driver away' in s:
    print('already')
else:
    s += """
/*
 * B8000-H: keep the disp-pwm driver away from 14008000. The blade dtsi
 * enables &bls, but the backlight is driven through the BLS registers by
 * mt6589-bls-backlight; with both bound the PWM duty never reached the LED.
 */
&bls {
	status = "disabled";
};
"""
    open(p, 'w').write(s)
    print('patched')
