/* SPDX-License-Identifier: MIT OR GPL-2.0 */
#ifndef __PVR_UACCESS_H__
#define __PVR_UACCESS_H__

#include <linux/uaccess.h>

static inline unsigned long pvr_copy_to_user(void __user *to,
					      const void *from,
					      unsigned long bytes)
{
	if (!access_ok(to, bytes))
		return bytes;
	return __copy_to_user(to, from, bytes);
}

static inline unsigned long pvr_copy_from_user(void *to,
						const void __user *from,
						unsigned long bytes)
{
	if (!access_ok(from, bytes))
		return bytes;
	return __copy_from_user(to, from, bytes);
}

#define pvr_put_user put_user
#define pvr_get_user get_user

#endif

