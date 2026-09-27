K='/opt/b8000h/linux-drm/'
# DTS: no BQ27541 on B8000-H (MTK_BQ27541_SUPPORT=no); keep charger omit-battery-class
p=K+'arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s=open(p).read()
old='''	fuel-gauge@55 {
		compatible = "ti,bq27541";
		reg = <0x55>;
	};
'''
if old in s:
    s=s.replace('\n'+old,'\n',1).replace(old,'',1)
s=s.replace(''' * Battery: the vendor kernel registers the BQ27541 fuel gauge on I2C bus 6
 * (BQ24196_BUSNUM 6, 0xAA >> 1), next to the BQ24196 charger, not on i2c4 as
 * the shared blade description guesses.  Polled (EINT16 is an output here).
 * The charger's own "battery" (no capacity) made Phosh show 0 %: omit it.''',''' * Battery: B8000-H has no BQ27541 (vendor MTK_BQ27541_SUPPORT=no, nothing
 * answers at 0x55); the vendor computes the charge from MT6320 AUXADC
 * (docs/battery-mt6320-plan.md).  The charger's own capacity-less "battery"
 * made Phosh show 0 %: omit it.''')
open(p,'w').write(s)
# defconfig + .config: module unload for driver iteration without reboot
for f in ('arch/arm/configs/lenovo-blade_defconfig','.config'):
    q=K+f; t=open(q).read()
    t=t.replace('# CONFIG_MODULE_UNLOAD is not set\n','')
    for l in ('CONFIG_MODULE_UNLOAD=y','CONFIG_MODULE_FORCE_UNLOAD=y'):
        if l not in t: t+=l+'\n'
    open(q,'w').write(t)
print('ok')
