// SPDX-License-Identifier: GPL-2.0
/*
 * Test: keep MSDC3 runtime-active with a free-running SDIO clock
 * (MSDC_CFG.CKPDN), like the vendor host does while the SDIO IRQ is claimed.
 * Hypothesis: MT6628 moves HIF FIFO data to its CPU only while SDIO CLK runs.
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define MSDC_CFG_CKPDN	BIT(1)

static struct device *hostdev;

static int __init ckon_init(void)
{
	void __iomem *base;
	u32 v;

	hostdev = bus_find_device_by_name(&platform_bus_type, NULL, "11260000.mmc");
	if (!hostdev)
		return -ENODEV;
	pm_runtime_get_sync(hostdev);
	base = ioremap(0x11260000, 4);
	if (!base) {
		pm_runtime_put(hostdev);
		put_device(hostdev);
		return -ENOMEM;
	}
	v = readl(base);
	writel(v | MSDC_CFG_CKPDN, base);
	pr_info("msdc3_ckon: MSDC_CFG %#x -> %#x\n", v, readl(base));
	iounmap(base);
	return 0;
}
module_init(ckon_init);

static void __exit ckon_exit(void)
{
	pm_runtime_put(hostdev);
	put_device(hostdev);
}
module_exit(ckon_exit);
MODULE_LICENSE("GPL");
