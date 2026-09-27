p='/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/mtk_crtc.c'
s=open(p).read()
if 'hw_kept_on' not in s:
    # 1. state flag in struct mtk_crtc
    anchor='struct mtk_crtc {\n'
    i=s.index(anchor)+len(anchor)
    s=s[:i]+'''	/*
	 * MT6589: DSI runs in the bootloader's video mode setup, which this
	 * driver cannot re-program; tearing the OVL/RDMA path down on DPMS off
	 * starves DSI and it never resyncs.  Keep the path running and only
	 * drop the planes (the panel turns the backlight off).
	 */
	bool				hw_kept_on;
'''+s[i:]
    # 2. enable: skip re-init when the path is still running
    old='''	drm_dbg_driver(dev, "%s %d\\n", __func__, crtc->base.id);

	ret = mtk_ddp_comp_power_on(comp);'''
    new='''	drm_dbg_driver(dev, "%s %d\\n", __func__, crtc->base.id);

	if (mtk_crtc->hw_kept_on) {
		drm_crtc_vblank_on(crtc);
		mtk_crtc->enabled = true;
		return;
	}

	ret = mtk_ddp_comp_power_on(comp);'''
    assert old in s, 'enable'
    s=s.replace(old,new,1)
    # 3. disable: keep the hardware running on MT6589
    old='''	drm_crtc_vblank_off(crtc);
	mtk_crtc_ddp_hw_fini(mtk_crtc);
	mtk_ddp_comp_power_off(comp);

	mtk_crtc->enabled = false;
}'''
    new='''	drm_crtc_vblank_off(crtc);
	if (of_machine_is_compatible("mediatek,mt6589")) {
		mtk_crtc->hw_kept_on = true;
		mtk_crtc->enabled = false;
		return;
	}
	mtk_crtc_ddp_hw_fini(mtk_crtc);
	mtk_ddp_comp_power_off(comp);

	mtk_crtc->enabled = false;
}'''
    assert old in s, 'disable'
    s=s.replace(old,new,1)
    open(p,'w').write(s)
print('ok')
