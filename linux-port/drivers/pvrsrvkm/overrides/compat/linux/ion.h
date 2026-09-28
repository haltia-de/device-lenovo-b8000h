/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PVR_COMPAT_LINUX_ION_H
#define PVR_COMPAT_LINUX_ION_H

#include <linux/slab.h>

#define ION_CLIENT_NAME_SIZE 64

struct ion_device { int unused; };
struct ion_client { int unused; };

static inline struct ion_client *ion_client_create(struct ion_device *dev,
						    const char *name)
{
	return kzalloc(sizeof(struct ion_client), GFP_KERNEL);
}

static inline void ion_client_destroy(struct ion_client *client)
{
	kfree(client);
}

#endif
