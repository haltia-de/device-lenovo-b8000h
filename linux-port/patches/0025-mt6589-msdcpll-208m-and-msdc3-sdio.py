K='/opt/b8000h/linux-drm/'
# 1. clock: msdcpll_208m is VCO/8 (before the /4 post divider) = msdcpll/2
c=K+'drivers/clk/mediatek/clk-mt6589-apmixedsys.c'
s=open(c).read()
old='\tFACTOR(CLK_APMIXED_MSDCPLL_208M, "msdcpll_208m", "msdcpll", 1, 8),'
new=('\t/*\n\t * The MSDC source tap is VCO/8 (vendor MSDC_CLKSRC_200MHZ), taken\n'
     '\t * before the /4 post divider that "msdcpll" (390 MHz) includes.\n'
     '\t * With 1, 8 every MSDC bus clock ran 4x faster than requested.\n\t */\n'
     '\tFACTOR(CLK_APMIXED_MSDCPLL_208M, "msdcpll_208m", "msdcpll", 1, 2),')
if old in s: s=s.replace(old,new); open(c,'w').write(s); print('clk fixed')
# 2. DTS: MSDC3 pins with pull-ups, fixed vmmc
d=K+'arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s=open(d).read()
if 'mt6628_msdc3_pins' not in s:
    s=s.replace('\tmt6628_uart3_pins: mt6628-uart3-pins {','''\t/* MSDC3 (MT6628 SDIO): vendor pulls CMD/DAT up, CLK down (PUPD R1=1). */
\tmt6628_msdc3_pins: mt6628-msdc3-pins {
\t\tpins-cmd-dat {
\t\t\tpinmux = <MT6589_GPIO226__FUNC_MSDC3_DAT2>,
\t\t\t\t <MT6589_GPIO227__FUNC_MSDC3_DAT3>,
\t\t\t\t <MT6589_GPIO228__FUNC_MSDC3_CMD>,
\t\t\t\t <MT6589_GPIO230__FUNC_MSDC3_DAT1>,
\t\t\t\t <MT6589_GPIO231__FUNC_MSDC3_DAT0>;
\t\t\tinput-enable;
\t\t\tbias-pull-up = <MTK_PUPD_SET_R1R0_10>;
\t\t};

\t\tpins-clk {
\t\t\tpinmux = <MT6589_GPIO229__FUNC_MSDC3_CLK>;
\t\t\tbias-pull-down = <MTK_PUPD_SET_R1R0_10>;
\t\t};
\t};

\tmt6628_uart3_pins: mt6628-uart3-pins {''',1)
    s=s.replace('\t/delete-property/ vmmc-supply;\n\t/delete-property/ mmc-pwrseq;\n','''\t/delete-property/ mmc-pwrseq;
\t/* Chip power is owned by WMT; this only gives the host a 3.3 V OCR. */
\tvmmc-supply = <&mt6628_sdio_vmmc>;
\tpinctrl-names = "default";
\tpinctrl-0 = <&mt6628_msdc3_pins>;
''',1)
    s=s.replace('&uart2 {','''/ {
\tmt6628_sdio_vmmc: regulator-mt6628-sdio {
\t\tcompatible = "regulator-fixed";
\t\tregulator-name = "mt6628-sdio";
\t\tregulator-min-microvolt = <3300000>;
\t\tregulator-max-microvolt = <3300000>;
\t\tregulator-always-on;
\t};
};

&uart2 {''',1)
    open(d,'w').write(s); print('dts fixed')
if '#include <dt-bindings/pinctrl/mt65xx.h>' not in s:
    print('NEED mt65xx.h include')
p='/opt/b8000h/linux-drm/arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s=open(p).read()
old='\tpinctrl-names = "default";\n\tpinctrl-0 = <&mt6628_msdc3_pins>;\n'
new='\t/* mtk-sd requires both states */\n\tpinctrl-names = "default", "state_uhs";\n\tpinctrl-0 = <&mt6628_msdc3_pins>;\n\tpinctrl-1 = <&mt6628_msdc3_pins>;\n'
assert old in s
s=s.replace(old,new,1); open(p,'w').write(s); print('ok')
