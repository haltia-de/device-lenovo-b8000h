import shutil
K = '/opt/b8000h/linux-drm/'
shutil.copy('/mnt/c/Users/<user>/src/lenovo-b8000h-linux/linux-port/drivers/mt6589-mipi-tx/phy-mtk-mipi-dsi-mt6589.c',
            K + 'drivers/phy/mediatek/phy-mtk-mipi-dsi-mt6589.c')
def ed(p, old, new):
    s = open(K + p).read()
    if new in s:
        return
    assert s.count(old) == 1, (p, old)
    open(K + p, 'w').write(s.replace(old, new))
ed('drivers/phy/mediatek/Makefile', 'phy-mtk-mipi-dsi-drv-y\t\t\t+= phy-mtk-mipi-dsi-mt8173.o\n',
   'phy-mtk-mipi-dsi-drv-y\t\t\t+= phy-mtk-mipi-dsi-mt8173.o\nphy-mtk-mipi-dsi-drv-y\t\t\t+= phy-mtk-mipi-dsi-mt6589.o\n')
ed('drivers/phy/mediatek/phy-mtk-mipi-dsi.h', 'extern const struct mtk_mipitx_data mt2701_mipitx_data;\n',
   'extern const struct mtk_mipitx_data mt2701_mipitx_data;\nextern const struct mtk_mipitx_data mt6589_mipitx_data;\n')
ed('drivers/phy/mediatek/phy-mtk-mipi-dsi.c', '\t{ .compatible = "mediatek,mt2701-mipi-tx", .data = &mt2701_mipitx_data },\n',
   '\t{ .compatible = "mediatek,mt2701-mipi-tx", .data = &mt2701_mipitx_data },\n\t{ .compatible = "mediatek,mt6589-mipi-tx", .data = &mt6589_mipitx_data },\n')
ed('drivers/gpu/drm/panel/panel-simple.c', '\t.flags = MIPI_DSI_MODE_VIDEO,\n\t.format = MIPI_DSI_FMT_RGB888,\n\t.lanes = 4,\n};\n\nstatic const struct drm_display_mode auo_b080uan01_mode',
   '\t/* LK: TXRX_CTRL HSTX_CKLP_EN set, i.e. non-continuous HS clock */\n\t.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,\n\t.format = MIPI_DSI_FMT_RGB888,\n\t.lanes = 4,\n};\n\nstatic const struct drm_display_mode auo_b080uan01_mode')
print('ok')
