/* SPDX-License-Identifier: MIT OR GPL-2.0 */
#ifndef PVR_COMPAT_SW_SYNC_H
#define PVR_COMPAT_SW_SYNC_H

#include <linux/slab.h>

/*
 * Minimal lifetime-compatible stand-in for Android JB's removed sync API.
 * It lets the original bridge layout remain enabled, but does not provide
 * real cross-driver fence signalling.  Fence-backed ioctls remain unsupported.
 */
struct sync_timeline { int unused; };
struct sw_sync_timeline { struct sync_timeline base; };
struct sync_pt { int unused; };
struct sync_fence { int unused; };

static inline struct sw_sync_timeline *sw_sync_timeline_create(const char *name)
{
	(void)name;
	return kzalloc(sizeof(struct sw_sync_timeline), GFP_KERNEL);
}

static inline struct sync_pt *sw_sync_pt_create(
	struct sw_sync_timeline *timeline, unsigned int value)
{
	(void)timeline;
	(void)value;
	return kzalloc(sizeof(struct sync_pt), GFP_KERNEL);
}

static inline struct sync_fence *sync_fence_create(
	const char *name, struct sync_pt *point)
{
	struct sync_fence *fence;

	(void)name;
	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	kfree(point);
	return fence;
}

static inline void sync_pt_free(struct sync_pt *point)
{
	kfree(point);
}

static inline void sync_fence_put(struct sync_fence *fence)
{
	kfree(fence);
}

static inline void sync_fence_install(struct sync_fence *fence, int fd)
{
	(void)fence;
	(void)fd;
}

static inline void sw_sync_timeline_inc(
	struct sw_sync_timeline *timeline, unsigned int value)
{
	(void)timeline;
	(void)value;
}

static inline void sync_timeline_destroy(struct sync_timeline *timeline)
{
	kfree(timeline);
}

#endif
