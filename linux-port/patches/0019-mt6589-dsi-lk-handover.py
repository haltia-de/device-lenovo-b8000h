K = '/opt/b8000h/linux-drm/'
def ed(p, old, new, marker):
    s = open(K + p).read()
    if marker in s:
        return
    assert s.count(old) == 1, (p, old[:50])
    open(K + p, 'w').write(s.replace(old, new))

# DIS domain always on: keep the bootloader's display state (BLS/DSI) intact.
ed('drivers/pmdomain/mediatek/mt6589-pm-domains.h',
   '\t\t.ctl_offs = SPM_DIS_PWR_CON,\n\t\t.sram_pdn_bits = GENMASK(11, 8),\n\t\t.sram_pdn_ack_bits = GENMASK(15, 12),\n',
   '\t\t.ctl_offs = SPM_DIS_PWR_CON,\n\t\t.sram_pdn_bits = GENMASK(11, 8),\n\t\t.sram_pdn_ack_bits = GENMASK(15, 12),\n'
   '\t\t/* B8000: keep the display that LK set up (DSI/BLS) alive */\n\t\t.caps = MTK_SCPD_ALWAYS_ON,\n',
   'keep the display that LK')

p = 'drivers/gpu/drm/mediatek/mtk_dsi.c'
ed(p, '\tint refcount;\n\tbool enabled;\n',
   '\tint refcount;\n\tbool enabled;\n\tbool lk_checked;\n\tbool lk_handover;\n', 'lk_handover;')
ed(p, '''	if (++dsi->refcount != 1)
		return 0;

	ret = mipi_dsi_pixel_format_to_bpp(dsi->format);''',
   '''	if (++dsi->refcount != 1)
		return 0;

	/*
	 * MT6589 (B8000): the D-PHY/DSI programming that works for this panel
	 * is only known from the bootloader. If LK left DSI running in video
	 * mode, take it over as is and never reset or power it down.
	 */
	if (!dsi->lk_checked) {
		dsi->lk_checked = true;
		if (of_device_is_compatible(dev->of_node, "mediatek,mt6589-dsi") &&
		    (readl(dsi->regs + DSI_START) & 1)) {
			dsi->lk_handover = true;
			dev_info(dev, "keeping bootloader DSI video mode setup\n");
		}
	}
	if (dsi->lk_handover)
		return 0;

	ret = mipi_dsi_pixel_format_to_bpp(dsi->format);''', 'keeping bootloader DSI')
ed(p, '''	if (--dsi->refcount != 0)
		return;
''', '''	if (--dsi->refcount != 0)
		return;

	if (dsi->lk_handover)
		return;
''', 'if (dsi->lk_handover)\n\t\treturn;\n')
ed(p, '''	if (dsi->enabled)
		return;

	mtk_dsi_set_mode(dsi);''', '''	if (dsi->enabled)
		return;

	if (dsi->lk_handover) {
		dsi->enabled = true;
		return;
	}

	mtk_dsi_set_mode(dsi);''', 'if (dsi->lk_handover) {\n\t\tdsi->enabled = true;')
print('ok')
