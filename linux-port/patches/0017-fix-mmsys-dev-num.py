f = '/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/mtk_drm_drv.c'
s = open(f).read()
old = '''	.main_path = mt6589_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt6589_mtk_ddp_main),
};'''
new = '''	.main_path = mt6589_mtk_ddp_main,
	.main_len = ARRAY_SIZE(mt6589_mtk_ddp_main),
	.shadow_register = true,
	.mmsys_dev_num = 1,
};'''
if 'mmsys_dev_num = 1,\n};' not in s.split('mt6589_mmsys_driver_data = {')[1][:300]:
    assert s.count(old) == 1
    s = s.replace(old, new)
    open(f, 'w').write(s)
print('ok')
