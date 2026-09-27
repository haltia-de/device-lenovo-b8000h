p='/opt/b8000h/linux-drm/arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s=open(p).read()
old_hdr='&uart3 {\n\tstatus = "okay";\n\tpinctrl-names = "default";\n\tpinctrl-0 = <&mt6628_uart3_pins>;\n'
assert old_hdr in s, 'uart3 block not found'
s=s.replace(old_hdr,'/*\n * The combo UART is vendor UART3 = /dev/ttyMT2 = 0x11008000, i.e. upstream\n * &uart2 (upstream labels count from 0).  Pins GPIO219/220 URXD3/UTXD3.\n */\n&uart2 {\n\tstatus = "okay";\n\tpinctrl-names = "default";\n\tpinctrl-0 = <&mt6628_uart3_pins>;\n',1)
# BGF EINT: our pinctrl resolves EINT8 to pin 90 (modem BPI bus) -> system hang. Leave it out.
s=s.replace('\t\tinterrupt-parent = <&pio>;\n\t\tinterrupts = <8 IRQ_TYPE_LEVEL_LOW>;\n','\t\t/* BGF EINT omitted: EINT8 resolves to pin 90 (modem BPI bus) and hangs the SoC. */\n',1)
open(p,'w').write(s)
print('ok')
# then: add '/delete-property/ dmas; /delete-property/ dma-names;' after pinctrl-0 in &uart2
