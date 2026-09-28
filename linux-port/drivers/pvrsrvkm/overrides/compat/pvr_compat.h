/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PVR_LINUX_7_COMPAT_H
#define PVR_LINUX_7_COMPAT_H

#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/file.h>

/* in_irq() was removed after callers had migrated to the explicit helpers. */
#undef in_irq
#define in_irq in_hardirq

/* Android 3.4 helper renamed to the flags-taking API. */
#define get_unused_fd() get_unused_fd_flags(0)

#endif
