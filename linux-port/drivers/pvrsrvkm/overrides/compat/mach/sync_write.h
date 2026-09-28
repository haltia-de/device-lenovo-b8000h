/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PVR_COMPAT_MACH_SYNC_WRITE_H
#define PVR_COMPAT_MACH_SYNC_WRITE_H

#include <linux/io.h>

static inline void mt65xx_reg_sync_writel(unsigned int value, void __iomem *addr)
{
	writel(value, addr);
	mb();
}

#endif

