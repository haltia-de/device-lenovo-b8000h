// SPDX-License-Identifier: GPL-2.0
/*
 * MSDC3 bring-up helper for the MT6628 SDIO function (test only).
 * echo 1 > /sys/module/msdc3_tool/parameters/rescan
 * sets ocr_avail and MSDC_IOCON (holding the host runtime-resumed so the
 * value is not restored), then rescans the non-removable host.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

static char *host = "11260000.mmc";
module_param(host, charp, 0444);
static uint ocr = 0x001f0000;
module_param(ocr, uint, 0644);
/* MSDC_IOCON: BIT(1) RSPL, BIT(2) DSPL, BIT(8) W_DSPL; -1 = leave */
static int iocon = -1;
module_param(iocon, int, 0644);

/* MSDC source is really ~4x what CCF reports (msdcpll_208m bug): scale. */
static uint f_init;
module_param(f_init, uint, 0644);
static uint f_max;
module_param(f_max, uint, 0644);

static struct device *hostdev;

static int match_mmc_host(struct device *dev, const void *data)
{
	return !strncmp(dev_name(dev), "mmc", 3);
}

static int do_rescan(const char *val, const struct kernel_param *kp)
{
	struct device *cdev;
	struct mmc_host *mmc;

	if (iocon >= 0) {
		void __iomem *reg = ioremap(0x11260004, 4);

		if (reg) {
			writel(iocon, reg);
			pr_info("msdc3_tool: IOCON=%#x\n", readl(reg));
			iounmap(reg);
		}
	}
	cdev = device_find_child(hostdev, NULL, match_mmc_host);
	if (!cdev)
		return -ENODEV;
	mmc = container_of(cdev, struct mmc_host, class_dev);
	pr_info("msdc3_tool: %s ocr %#x, rescan\n", dev_name(cdev), ocr);
	mmc->ocr_avail = ocr;
	if (f_init)
		mmc->f_init = f_init;
	if (f_max)
		mmc->f_max = f_max;
	if (f_init)
		mmc->f_min = min(mmc->f_min, f_init);
	mmc->rescan_entered = 0;
	mmc_detect_change(mmc, 0);
	put_device(cdev);
	return 0;
}

static const struct kernel_param_ops rescan_ops = { .set = do_rescan };
module_param_cb(rescan, &rescan_ops, NULL, 0200);

static int __init msdc3_tool_init(void)
{
	hostdev = bus_find_device_by_name(&platform_bus_type, NULL, host);
	if (!hostdev)
		return -ENODEV;
	pm_runtime_get_sync(hostdev);
	return 0;
}
module_init(msdc3_tool_init);

MODULE_DESCRIPTION("MSDC3 bring-up helper for MT6628");
MODULE_LICENSE("GPL");
