# MT6589: patch 0029 keeps the OVL/RDMA/DSI path running on DPMS off by
# returning from mtk_crtc_atomic_disable() before mtk_crtc_ddp_hw_fini().
# hw_fini also completes the disabling commit's vblank event; skipping it
# left the event pending ("WARNING ... drm_atomic_helper_commit_hw_done",
# then "flip_done timed out"), so phoc blocked and the screen never came
# back after lock.  Send the event in the early-return path as hw_fini does.
p = '/opt/b8000h/linux-drm/drivers/gpu/drm/mediatek/mtk_crtc.c'
s = open(p).read()
if 'hw_kept_on event' not in s:
    old = '''	drm_crtc_vblank_off(crtc);
	if (of_machine_is_compatible("mediatek,mt6589")) {
		mtk_crtc->hw_kept_on = true;
		mtk_crtc->enabled = false;
		return;
	}'''
    new = '''	drm_crtc_vblank_off(crtc);
	if (of_machine_is_compatible("mediatek,mt6589")) {
		/* hw_kept_on event: complete it like mtk_crtc_ddp_hw_fini() */
		if (crtc->state->event && !crtc->state->active) {
			unsigned long flags;

			spin_lock_irqsave(&crtc->dev->event_lock, flags);
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
			crtc->state->event = NULL;
			spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		}
		mtk_crtc->hw_kept_on = true;
		mtk_crtc->enabled = false;
		return;
	}'''
    assert s.count(old) == 1, 'disable'
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
print('ok')
