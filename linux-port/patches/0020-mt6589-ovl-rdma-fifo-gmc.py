K = '/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/'
def ed(p, old, new, marker):
    s = open(K + p).read()
    if marker in s:
        print('already', p); return
    assert s.count(old) == 1, (p, old[:60])
    open(K + p, 'w').write(s.replace(old, new)); print('patched', p)

ed('mtk_disp_ovl.c',
'''	mtk_ddp_write(cmdq_pkt, gmc_value,
		      &ovl->cmdq_reg, ovl->regs, DISP_REG_OVL_RDMA_GMC(idx));
''',
'''	/*
	 * MT6589 (values the bootloader uses): full per-layer RDMA FIFO and
	 * lower GMC thresholds; the MT2701 defaults underflow on this SoC.
	 */
	if (of_device_is_compatible(dev->of_node, "mediatek,mt6589-disp-ovl")) {
		mtk_ddp_write(cmdq_pkt, 0x03ff0001, &ovl->cmdq_reg, ovl->regs,
			      DISP_REG_OVL_RDMA_CTRL(idx));
		gmc_value = 0x10101010;
	}
	mtk_ddp_write(cmdq_pkt, gmc_value,
		      &ovl->cmdq_reg, ovl->regs, DISP_REG_OVL_RDMA_GMC(idx));
''', 'mediatek,mt6589-disp-ovl')

ed('mtk_disp_rdma.c',
'''	mtk_ddp_write(cmdq_pkt, reg, &rdma->cmdq_reg, rdma->regs, DISP_REG_RDMA_FIFO_CON);
}''',
'''	/*
	 * MT6589 FIFO is the 3840-byte line buffer LK programmed
	 * (pseudo-size 0xF0). A threshold of 256 bytes starts the line
	 * almost empty, so a scroll underruns and the panel shows a
	 * translucent horizontally shifted copy. 0x80f000e0 keeps that
	 * size, enables the underflow status bit, and waits until 3584
	 * bytes are queued.
	 */
	if (of_device_is_compatible(dev->of_node, "mediatek,mt6589-disp-rdma"))
		reg = 0x80f000e0;
	mtk_ddp_write(cmdq_pkt, reg, &rdma->cmdq_reg, rdma->regs, DISP_REG_RDMA_FIFO_CON);
}''', 'mediatek,mt6589-disp-rdma')
