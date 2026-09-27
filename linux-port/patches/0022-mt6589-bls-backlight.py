# Apply to /opt/b8000h/linux-drm: MT6589 BLS PWM backlight (DTS node + panel link, Kconfig/Makefile,
# defconfig HZ_1000 + BACKLIGHT_MT6589_BLS). Then copy
# linux-port/drivers/mt6589-bls-backlight/mt6589-bls-bl.c to drivers/video/backlight/.
import re
K='/opt/b8000h/linux-drm/'
p=K+'arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s=open(p).read()
if 'mt6589-bls-backlight' not in s:
    s=s.replace('''				compatible = "lenovo,b8000-panel";
				reg = <0>;
''','''				compatible = "lenovo,b8000-panel";
				reg = <0>;
				backlight = <&backlight>;
''',1)
    s=s.replace('''		disp_mutex: mutex@14011000 {''','''		/* LCD backlight: BLS PWM duty 0..255 (vendor cust_leds CUST_BLS_PWM). */
		backlight: backlight {
			compatible = "mediatek,mt6589-bls-backlight";
			mediatek,bls = <&disp_bls>;
		};

		disp_mutex: mutex@14011000 {''',1)
    open(p,'w').write(s)
k=K+'drivers/video/backlight/Kconfig'
s=open(k).read()
if 'BACKLIGHT_MT6589_BLS' not in s:
    s=s.replace('config BACKLIGHT_MT6370','''config BACKLIGHT_MT6589_BLS
	tristate "MediaTek MT6589 BLS PWM backlight"
	depends on ARCH_MEDIATEK || COMPILE_TEST
	depends on OF
	help
	  LCD backlight driven by the PWM of the MT6589 display BLS block
	  (Lenovo B8000-H).

config BACKLIGHT_MT6370''',1)
    open(k,'w').write(s)
m=K+'drivers/video/backlight/Makefile'
s=open(m).read()
if 'mt6589-bls-bl' not in s:
    s=s.replace('obj-$(CONFIG_BACKLIGHT_PWM)','obj-$(CONFIG_BACKLIGHT_MT6589_BLS)	+= mt6589-bls-bl.o\nobj-$(CONFIG_BACKLIGHT_PWM)',1)
    open(m,'w').write(s)
d=K+'arch/arm/configs/lenovo-blade_defconfig'
s=open(d).read()
s=s.replace('CONFIG_HZ_100=y\n','')
for l in ['CONFIG_HZ_1000=y','CONFIG_BACKLIGHT_MT6589_BLS=y']:
    if l not in s: s+=l+'\n'
open(d,'w').write(s)
c=K+'.config'
s=open(c).read()
if 'CONFIG_BACKLIGHT_MT6589_BLS=y' not in s: s+='CONFIG_BACKLIGHT_MT6589_BLS=y\n'
open(c,'w').write(s)

