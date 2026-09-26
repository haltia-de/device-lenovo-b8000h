f = '/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/mtk_dsi.c'
s = open(f).read()
if 'mt6589_lk_timing' in s:
    print('already'); raise SystemExit
# 1) PHY timings: after the four TIMECON writes
old = '\twritel(timcon3, dsi->regs + DSI_PHY_TIMECON3);\n'
assert s.count(old) == 1
s = s.replace(old, old + '''
	/*
	 * B8000 / MT6589 bring-up: use the D-PHY timings the bootloader
	 * programs for this panel at 468 Mbps (read back from LK).
	 */
	if (of_device_is_compatible(dsi->dev->of_node, "mediatek,mt6589-dsi")) {
		writel(0x11090504, dsi->regs + DSI_PHY_TIMECON0);
		writel(0x08140610, dsi->regs + DSI_PHY_TIMECON1);
		writel(0x10140000, dsi->regs + DSI_PHY_TIMECON2);
		writel(0x00080e04, dsi->regs + DSI_PHY_TIMECON3);
	}
''')
# 2) horizontal word counts: after both vdo timing variants
for tag in ['per_frame_lp', 'per_line_lp']:
    pass
old2 = '\twritel(horizontal_frontporch_byte, dsi->regs + DSI_HFP_WC);\n'
assert s.count(old2) == 2
s = s.replace(old2, old2 + '''	mt6589_lk_timing(dsi);
''')
helper = '''/* B8000 / MT6589: horizontal word counts exactly as the bootloader sets them. */
static void mt6589_lk_timing(struct mtk_dsi *dsi)
{
	if (!of_device_is_compatible(dsi->dev->of_node, "mediatek,mt6589-dsi"))
		return;
	writel(0x500, dsi->regs + DSI_HSA_WC);
	writel(0x09c, dsi->regs + DSI_HBP_WC);
	writel(0x224, dsi->regs + DSI_HFP_WC);
}

'''
anchor = 'static void mtk_dsi_config_vdo_timing_per_frame_lp(struct mtk_dsi *dsi)\n'
assert s.count(anchor) == 1
s = s.replace(anchor, helper + anchor)
open(f, 'w').write(s)
print('ok')
