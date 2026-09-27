K='/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/'
# --- mtk_drm_drv.c: cached (non-coherent) dumb buffers on MT6589
p=K+'mtk_drm_drv.c'; s=open(p).read()
if 'mtk_gem_create_object' not in s:
    s=s.replace('#include <drm/drm_gem_dma_helper.h>','#include <drm/drm_gem_dma_helper.h>\n#include <drm/drm_damage_helper.h>',1)
    old='\treturn drm_gem_fb_create(dev, file, info, cmd);'
    assert old in s
    s=s.replace(old,'\tif (of_machine_is_compatible("mediatek,mt6589"))\n\t\treturn drm_gem_fb_create_with_dirty(dev, file, info, cmd);\n\n'+old,1)
    anchor='DEFINE_DRM_GEM_FOPS(mtk_drm_fops);'
    s=s.replace(anchor,anchor+'''

/*
 * MT6589 has no GPU: phoc renders with pixman straight into the dumb
 * buffers.  Write-combined (uncached) buffers make every blend read go to
 * DRAM and pinned one Cortex-A7 core at 100%.  Use cached buffers and flush
 * the damaged area before scanout (mtk_plane_atomic_update()).
 */
static struct drm_gem_object *mtk_gem_create_object(struct drm_device *dev,
						    size_t size)
{
	struct drm_gem_dma_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);
	obj->map_noncoherent = of_machine_is_compatible("mediatek,mt6589");

	return &obj->base;
}''',1)
    s=s.replace('\tDRM_GEM_DMA_DRIVER_OPS,\n','\tDRM_GEM_DMA_DRIVER_OPS,\n\t.gem_create_object = mtk_gem_create_object,\n',1)
    open(p,'w').write(s); print('drv ok')
# --- mtk_plane.c: damage clips + cache flush before scanout
p=K+'mtk_plane.c'; s=open(p).read()
if 'drm_fb_dma_sync_non_coherent' not in s:
    s=s.replace('#include <drm/drm_atomic_helper.h>','#include <drm/drm_atomic_helper.h>\n#include <drm/drm_damage_helper.h>\n#include <drm/drm_fb_dma_helper.h>',1)
    old='''	mtk_plane_update_new_state(new_state, mtk_plane_state);
	wmb(); /* Make sure the above parameters are set before update */
	mtk_plane_state->pending.dirty = true;
}'''
    assert old in s
    new='''	/* No-op unless the buffer is non-coherent (MT6589). */
	drm_fb_dma_sync_non_coherent(plane->dev,
				     drm_atomic_get_old_plane_state(state, plane),
				     new_state);
	mtk_plane_update_new_state(new_state, mtk_plane_state);
	wmb(); /* Make sure the above parameters are set before update */
	mtk_plane_state->pending.dirty = true;
}'''
    s=s.replace(old,new,1)
    old='\tdrm_plane_helper_add(plane, &mtk_plane_helper_funcs);\n'
    s=s.replace(old,'\tif (of_machine_is_compatible("mediatek,mt6589"))\n\t\tdrm_plane_enable_fb_damage_clips(plane);\n\n'+old,1)
    open(p,'w').write(s); print('plane ok')
p='/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/mtk_plane.c'
s=open(p).read()
old='''	plane->state->dst.y1 = new_state->dst.y1;

	mtk_plane_update_new_state(new_state, new_plane_state);
	swap(plane->state->fb, new_state->fb);'''
new='''	plane->state->dst.y1 = new_state->dst.y1;

	/* Flush the damaged area of non-coherent buffers (MT6589). */
	drm_fb_dma_sync_non_coherent(plane->dev, plane->state, new_state);
	mtk_plane_update_new_state(new_state, new_plane_state);
	swap(plane->state->fb, new_state->fb);'''
if 'damaged area of non-coherent' not in s:
    assert old in s
    s=s.replace(old,new,1); open(p,'w').write(s)
print('ok')
p='/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/mtk_plane.c'
s=open(p).read()
old='''	/* No-op unless the buffer is non-coherent (MT6589). */
	drm_fb_dma_sync_non_coherent(plane->dev,
				     drm_atomic_get_old_plane_state(state, plane),
				     new_state);'''
new='''	/*
	 * No-op unless the buffer is non-coherent (MT6589).  FB_DAMAGE_CLIPS
	 * are relative to the previously shown fb, but a compositor using
	 * buffer age also repaints older damage into this fb: flush it all
	 * when the fb changes, only the clips when the same fb is updated.
	 */
	old_state = drm_atomic_get_old_plane_state(state, plane);
	if (!old_state || old_state->fb != new_state->fb)
		new_state->ignore_damage_clips = true;
	drm_fb_dma_sync_non_coherent(plane->dev, old_state, new_state);'''
if 'buffer age also repaints' not in s:
    assert old in s
    s=s.replace(old,new,1)
    s=s.replace('''static void mtk_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);''','''static void mtk_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state;
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);''',1)
    old2='''	/* Flush the damaged area of non-coherent buffers (MT6589). */
	drm_fb_dma_sync_non_coherent(plane->dev, plane->state, new_state);'''
    new2='''	/* Flush non-coherent buffers (MT6589); whole fb if it changed. */
	if (plane->state->fb != new_state->fb)
		new_state->ignore_damage_clips = true;
	drm_fb_dma_sync_non_coherent(plane->dev, plane->state, new_state);'''
    assert old2 in s
    s=s.replace(old2,new2,1)
    open(p,'w').write(s)
print('ok')
