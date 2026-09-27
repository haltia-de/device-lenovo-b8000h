# Apply to /opt/b8000h/linux-drm after 0023-mt6589-rdma-vblank.patch: define mtk_ddp_comp_get_type().
p='/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/mtk_ddp_comp.c'
s=open(p).read()
if 'mtk_ddp_comp_get_type(unsigned' not in s:
    s=s.replace('''int mtk_ddp_comp_get_id(struct device_node *node,''','''enum mtk_ddp_comp_type mtk_ddp_comp_get_type(unsigned int comp_id)
{
	if (comp_id >= ARRAY_SIZE(mtk_ddp_matches))
		return MTK_DDP_COMP_TYPE_MAX;

	return mtk_ddp_matches[comp_id].type;
}

int mtk_ddp_comp_get_id(struct device_node *node,''',1)
    open(p,'w').write(s)

