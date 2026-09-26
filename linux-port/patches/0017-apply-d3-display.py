#!/usr/bin/env python3
"""D3: MT6589 support in mtk-mmsys, mtk-mutex, mediatek-drm, panel-simple + B8000-H DTS.

Run inside WSL on the worktree /opt/b8000h/linux-drm. Idempotent.
"""
import re
import sys

K = '/opt/b8000h/linux-drm/'


def edit(path, fn):
    p = K + path
    s = open(p).read()
    t = fn(s)
    if t != s:
        open(p, 'w').write(t)
        print('patched', path)
    else:
        print('unchanged', path)


def once(s, marker, anchor, text, after=True):
    if marker in s:
        return s
    assert s.count(anchor) == 1, (anchor[:60], s.count(anchor))
    return s.replace(anchor, anchor + text if after else text + anchor)


# --- mtk-mmsys ---------------------------------------------------------------
open(K + 'drivers/soc/mediatek/mt6589-mmsys.h', 'w').write('''/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_MEDIATEK_MT6589_MMSYS_H
#define __SOC_MEDIATEK_MT6589_MMSYS_H

/*
 * MT6589 DISPSYS config (vendor ddp_reg.h / ddp_path.c). The panel path set
 * up by the bootloader is OVL -> BLS -> RDMA0 -> DSI: RDMA0 sits between BLS
 * and DSI as a line buffer; BLS_SEL = 0 takes OVL, RDMA0_OUT_SEL = 0 is DSI.
 */
#define MT6589_DISP_REG_CONFIG_OVL_MOUT_EN	0x024
#define MT6589_DISP_REG_CONFIG_RDMA0_OUT_SEL	0x034
#define MT6589_DISP_REG_CONFIG_BLS_SEL		0x054

#define MT6589_OVL_MOUT_EN_MASK			GENMASK(2, 0)
#define MT6589_OVL_MOUT_EN_BLS			BIT(1)
#define MT6589_BLS_SEL_MASK			BIT(0)
#define MT6589_BLS_SEL_IN_OVL			0
#define MT6589_RDMA0_OUT_SEL_MASK		GENMASK(1, 0)
#define MT6589_RDMA0_OUT_SEL_DSI		0

static const struct mtk_mmsys_routes mt6589_mmsys_routing_table[] = {
	{
		DDP_COMPONENT_OVL0, DDP_COMPONENT_BLS,
		MT6589_DISP_REG_CONFIG_OVL_MOUT_EN, MT6589_OVL_MOUT_EN_MASK,
		MT6589_OVL_MOUT_EN_BLS
	}, {
		DDP_COMPONENT_OVL0, DDP_COMPONENT_BLS,
		MT6589_DISP_REG_CONFIG_BLS_SEL, MT6589_BLS_SEL_MASK,
		MT6589_BLS_SEL_IN_OVL
	}, {
		DDP_COMPONENT_RDMA0, DDP_COMPONENT_DSI0,
		MT6589_DISP_REG_CONFIG_RDMA0_OUT_SEL, MT6589_RDMA0_OUT_SEL_MASK,
		MT6589_RDMA0_OUT_SEL_DSI
	}
};

#endif /* __SOC_MEDIATEK_MT6589_MMSYS_H */
''')
print('wrote mt6589-mmsys.h')

edit('drivers/soc/mediatek/mtk-mmsys.c', lambda s: once(
    once(s, '"mt6589-mmsys.h"', '#include "mt8167-mmsys.h"\n',
         '#include "mt6589-mmsys.h"\n', after=False),
    'mt6589_mmsys_driver_data',
    'static const struct mtk_mmsys_driver_data mt2701_mmsys_driver_data = {',
    '''static const struct mtk_mmsys_driver_data mt6589_mmsys_driver_data = {
	.clk_driver = "clk-mt6589-disp",
	.routes = mt6589_mmsys_routing_table,
	.num_routes = ARRAY_SIZE(mt6589_mmsys_routing_table),
};

''', after=False))
edit('drivers/soc/mediatek/mtk-mmsys.c', lambda s: once(
    s, '"mediatek,mt6589-mmsys"',
    '\t{ .compatible = "mediatek,mt2701-mmsys", .data = &mt2701_mmsys_driver_data },\n',
    '\t{ .compatible = "mediatek,mt6589-mmsys", .data = &mt6589_mmsys_driver_data },\n',
    after=False))

# --- mtk-mutex ---------------------------------------------------------------
edit('drivers/soc/mediatek/mtk-mutex.c', lambda s: once(
    s, 'MT6589_MUTEX_MOD_DISP_OVL', '#define MT2701_MUTEX_MOD_DISP_OVL\t\t3\n',
    '''/* MT6589 (vendor ddp_path.c): mutex MOD bit per engine */
#define MT6589_MUTEX_MOD_DISP_OVL		2
#define MT6589_MUTEX_MOD_DISP_COLOR		3
#define MT6589_MUTEX_MOD_DISP_WDMA0		5
#define MT6589_MUTEX_MOD_DISP_RDMA0		7
#define MT6589_MUTEX_MOD_DISP_RDMA1		8
#define MT6589_MUTEX_MOD_DISP_BLS		9

''', after=False))
edit('drivers/soc/mediatek/mtk-mutex.c', lambda s: once(
    s, 'mt6589_mutex_mod[', 'static const u8 mt2701_mutex_mod[DDP_COMPONENT_ID_MAX] = {\n',
    '''static const u8 mt6589_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_BLS] = MT6589_MUTEX_MOD_DISP_BLS,
	[DDP_COMPONENT_COLOR0] = MT6589_MUTEX_MOD_DISP_COLOR,
	[DDP_COMPONENT_OVL0] = MT6589_MUTEX_MOD_DISP_OVL,
	[DDP_COMPONENT_RDMA0] = MT6589_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT6589_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_WDMA0] = MT6589_MUTEX_MOD_DISP_WDMA0,
};

''', after=False))
edit('drivers/soc/mediatek/mtk-mutex.c', lambda s: once(
    s, 'mt6589_mutex_driver_data =', 'static const struct mtk_mutex_data mt2701_mutex_driver_data = {\n',
    '''/* Same MOD/SOF layout as MT2701; no dedicated mutex clock gate. */
static const struct mtk_mutex_data mt6589_mutex_driver_data = {
	.mutex_mod = mt6589_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_mod1_reg = MT2701_MUTEX0_MOD1,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
	.no_clk = true,
};

''', after=False))
edit('drivers/soc/mediatek/mtk-mutex.c', lambda s: once(
    s, '"mediatek,mt6589-disp-mutex"',
    '\t{ .compatible = "mediatek,mt2701-disp-mutex", .data = &mt2701_mutex_driver_data },\n',
    '\t{ .compatible = "mediatek,mt6589-disp-mutex", .data = &mt6589_mutex_driver_data },\n',
    after=False))

# --- mediatek-drm ------------------------------------------------------------
def drm(s):
    s = once(s, 'mt6589_mtk_ddp_main', 'static const unsigned int mt2701_mtk_ddp_main[] = {\n',
             '''/* MT6589: RDMA0 is the line buffer after BLS, right before DSI. */
static const unsigned int mt6589_mtk_ddp_main[] = {
	DDP_COMPONENT_OVL0,
	DDP_COMPONENT_BLS,
	DDP_COMPONENT_RDMA0,
	DDP_COMPONENT_DSI0,
};

''', after=False)
    s = once(s, 'mt6589_mmsys_driver_data', 'static const struct mtk_mmsys_driver_data mt2701_mmsys_driver_data = {\n',
             '''static const struct mtk_mmsys_driver_data mt6589_mmsys_driver_data = {
	.main_path = mt6589_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt6589_mtk_ddp_main),
};

''', after=False)
    s = once(s, '"mediatek,mt6589-mmsys"', '\t{ .compatible = "mediatek,mt2701-mmsys",\n',
             '\t{ .compatible = "mediatek,mt6589-mmsys",\n\t  .data = &mt6589_mmsys_driver_data},\n',
             after=False)
    s = once(s, '"mediatek,mt6589-disp-mutex"', '\t{ .compatible = "mediatek,mt2701-disp-mutex",\n',
             '\t{ .compatible = "mediatek,mt6589-disp-mutex",\n\t  .data = (void *)MTK_DISP_MUTEX },\n',
             after=False)
    s = once(s, '"mediatek,mt6589-disp-bls"', '\t{ .compatible = "mediatek,mt2701-disp-pwm",\n',
             '\t{ .compatible = "mediatek,mt6589-disp-bls",\n\t  .data = (void *)MTK_DISP_BLS },\n',
             after=False)
    return s


edit('drivers/gpu/drm/mediatek/mtk_drm_drv.c', drm)

# --- panel-simple ------------------------------------------------------------
def panel(s):
    s = once(s, 'lenovo_b8000_mode', 'static const struct drm_display_mode auo_b080uan01_mode = {\n',
             '''/*
 * Lenovo YOGA Tablet 10 (B8000) 1280x800 DSI video panel "cm_hx8896a01".
 * Initialised by the bootloader; timings read back from the LK-programmed
 * MT6589 DSI registers (4 lanes, RGB888, sync-event, 60.09 Hz).
 */
static const struct drm_display_mode lenovo_b8000_mode = {
	.clock = 76130,
	.hdisplay = 1280,
	.hsync_start = 1280 + 186,
	.hsync_end = 1280 + 186 + 10,
	.htotal = 1280 + 186 + 10 + 45,
	.vdisplay = 800,
	.vsync_start = 800 + 21,
	.vsync_end = 800 + 21 + 2,
	.vtotal = 800 + 21 + 2 + 10,
};

static const struct panel_desc_dsi lenovo_b8000_panel = {
	.desc = {
		.modes = &lenovo_b8000_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 217,
			.height = 136,
		},
		.connector_type = DRM_MODE_CONNECTOR_DSI,
	},
	.flags = MIPI_DSI_MODE_VIDEO,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

''', after=False)
    s = once(s, '"lenovo,b8000-panel"', '\t\t.compatible = "auo,b080uan01",\n',
             '\t\t.compatible = "lenovo,b8000-panel",\n\t\t.data = &lenovo_b8000_panel,\n\t}, {\n',
             after=False)
    return s


edit('drivers/gpu/drm/panel/panel-simple.c', panel)

# --- DTS ---------------------------------------------------------------------
DTS = '''
/*
 * Display: mediatek-drm on the path the bootloader uses,
 * OVL -> BLS -> RDMA0 -> DSI (+ MIPI-TX), instead of simpledrm.
 */
&dispsys {
	compatible = "mediatek,mt6589-mmsys", "syscon";
	#clock-cells = <1>;
	power-domains = <&spm MT6589_POWER_DOMAIN_DIS>;
};

/ {
	soc {
		disp_ovl: ovl@14003000 {
			compatible = "mediatek,mt6589-disp-ovl", "mediatek,mt2701-disp-ovl";
			reg = <0x14003000 0x1000>;
			interrupts = <GIC_SPI 163 IRQ_TYPE_LEVEL_LOW>;
			clocks = <&dispsys CLK_DISP0_OVL_ENGINE>;
			power-domains = <&spm MT6589_POWER_DOMAIN_DIS>;
		};

		disp_rdma0: rdma@14006000 {
			compatible = "mediatek,mt6589-disp-rdma", "mediatek,mt2701-disp-rdma";
			reg = <0x14006000 0x1000>;
			interrupts = <GIC_SPI 166 IRQ_TYPE_LEVEL_LOW>;
			clocks = <&dispsys CLK_DISP0_RDMA0_ENGINE>;
			power-domains = <&spm MT6589_POWER_DOMAIN_DIS>;
		};

		disp_bls: bls@14008000 {
			compatible = "mediatek,mt6589-disp-bls";
			reg = <0x14008000 0x1000>;
			interrupts = <GIC_SPI 168 IRQ_TYPE_LEVEL_LOW>;
			clocks = <&dispsys CLK_DISP0_BLS>;
			power-domains = <&spm MT6589_POWER_DOMAIN_DIS>;
		};

		disp_mutex: mutex@14011000 {
			compatible = "mediatek,mt6589-disp-mutex";
			reg = <0x14011000 0x1000>;
			interrupts = <GIC_SPI 160 IRQ_TYPE_LEVEL_LOW>;
			power-domains = <&spm MT6589_POWER_DOMAIN_DIS>;
		};

		mipi_tx0: dsi-phy@10012000 {
			compatible = "mediatek,mt6589-mipi-tx", "mediatek,mt2701-mipi-tx";
			reg = <0x10012000 0x90>;
			clocks = <&clk26m>;
			clock-output-names = "mipi_tx0_pll";
			#clock-cells = <0>;
			#phy-cells = <0>;
		};

		dsi0: dsi@1400d000 {
			compatible = "mediatek,mt6589-dsi", "mediatek,mt2701-dsi";
			reg = <0x1400d000 0x1000>;
			interrupts = <GIC_SPI 172 IRQ_TYPE_LEVEL_LOW>;
			clocks = <&dispsys CLK_DISP1_DSI_ENGINE>,
				 <&dispsys CLK_DISP1_DSI_DIGITAL>,
				 <&mipi_tx0>;
			clock-names = "engine", "digital", "hs";
			phys = <&mipi_tx0>;
			phy-names = "dphy";
			power-domains = <&spm MT6589_POWER_DOMAIN_DIS>;
			#address-cells = <1>;
			#size-cells = <0>;

			panel@0 {
				compatible = "lenovo,b8000-panel";
				reg = <0>;
			};
		};
	};
};
'''


def dts(s):
    if 'mediatek,mt6589-mmsys' in s:
        return s
    return s + DTS


edit('arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts', dts)
