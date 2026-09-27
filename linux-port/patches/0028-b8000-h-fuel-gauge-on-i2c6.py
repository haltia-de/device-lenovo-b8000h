p='/opt/b8000h/linux-drm/arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s=open(p).read()
old_start=s.index('/*\n * The fuel gauge IRQ line (EINT16/GPIO16')
old_end=s.index('};\n', s.index('&bq27541 {', old_start))+3
new='''/*
 * Battery: the vendor kernel registers the BQ27541 fuel gauge on I2C bus 6
 * (BQ24196_BUSNUM 6, 0xAA >> 1), next to the BQ24196 charger, not on i2c4 as
 * the shared blade description guesses.  Polled (EINT16 is an output here).
 * The charger's own "battery" (no capacity) made Phosh show 0 %: omit it.
 */
&i2c4 {
	/delete-node/ fuel-gauge@55;
};

&i2c6 {
	charger@6b {
		omit-battery-class;
	};

	fuel-gauge@55 {
		compatible = "ti,bq27541";
		reg = <0x55>;
	};
};
'''
s=s[:old_start]+new+s[old_end:]
open(p,'w').write(s)
print(s[old_start-40:old_start+len(new)+60])
