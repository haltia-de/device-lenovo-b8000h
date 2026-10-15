# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm: stop the OVL frame-complete interrupt that
# LK leaves enabled. On MT6589 the CRTC takes its vblank from RDMA0 (patch
# 0023), so nothing ever calls mtk_ovl_enable/disable_vblank, and the OVL
# keeps the INTEN bits the bootloader set: about 60 interrupts a second,
# each running the vblank callback that every DDP component registers, on
# top of the real one from RDMA. The handler now masks the OVL interrupt
# on MT6589 the first time it fires; the registers are reachable then,
# since the interrupt just came from them.
K = '/opt/b8000h/linux-drm/'


def ed(p, old, new, marker):
    s = open(K + p).read()
    if marker in s:
        print('already', p)
        return
    assert s.count(old) == 1, (p, old[:60])
    open(K + p, 'w').write(s.replace(old, new))
    print('patched', p)


P = 'drivers/gpu/drm/mediatek/mtk_disp_ovl.c'
ed(P, '''	void				(*vblank_cb)(void *data);
	void				*vblank_cb_data;
};

static irqreturn_t mtk_disp_ovl_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_ovl *priv = dev_id;

	/* Clear frame completion interrupt */
	writel(0x0, priv->regs + DISP_REG_OVL_INTSTA);
''', '''	void				(*vblank_cb)(void *data);
	void				*vblank_cb_data;
	/* MT6589: vblank comes from RDMA; the OVL interrupt is LK's leftover */
	bool				lk_irq_only;
};

static irqreturn_t mtk_disp_ovl_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_ovl *priv = dev_id;

	/* Clear frame completion interrupt */
	writel(0x0, priv->regs + DISP_REG_OVL_INTSTA);

	if (priv->lk_irq_only) {
		writel(0x0, priv->regs + DISP_REG_OVL_INTEN);
		return IRQ_HANDLED;
	}
''', 'lk_irq_only')
ed(P, '''	priv->data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, priv);

	ret = devm_request_irq(dev, irq, mtk_disp_ovl_irq_handler,''', '''	priv->data = of_device_get_match_data(dev);
	priv->lk_irq_only = of_machine_is_compatible("mediatek,mt6589");
	platform_set_drvdata(pdev, priv);

	ret = devm_request_irq(dev, irq, mtk_disp_ovl_irq_handler,''', 'priv->lk_irq_only = ')
