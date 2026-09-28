/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PVR_LINUX_7_COMPAT_H
#define PVR_LINUX_7_COMPAT_H

#include <linux/interrupt.h>

/* in_irq() was removed after callers had migrated to the explicit helpers. */
#undef in_irq
#define in_irq in_hardirq

#endif
