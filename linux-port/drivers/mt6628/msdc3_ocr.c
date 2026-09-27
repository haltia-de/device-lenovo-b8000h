// SPDX-License-Identifier: GPL-2.0
/*
 * Test helper: MSDC3 has no vmmc (the MT6628 is powered by WMT), so its
 * ocr_avail is 0 and SDIO init fails with "no support for card's volts".
 * Give the live host a 2.7-3.6 V OCR and rescan it.  The permanent fix is a
 * fixed vmmc regulator in the DTS.
 */

#include <linux/device.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

static char *host = "11260000.mmc";
static uint ocr = 0x00ff8000;
module_param(ocr, uint, 0444);
module_param(host, charp, 0444);
/* MSDC_IOCON: BIT(1) RSPL, BIT(2) DSPL, BIT(8) W_DSPL; vendor SDIO = falling. */
static int iocon = -1;
module_param(iocon, int, 0444);
static ulong iocon_phys = 0x11260004;
module_param(iocon_phys, ulong, 0444);

static int match_mmc_host(struct device *dev, const void *data)
{
	return !strncmp(dev_name(dev), "mmc", 3);
}

static int __init msdc3_ocr_init(void)
{
	struct device *pdev, *cdev;
	struct mmc_host *mmc;

	pdev = bus_find_device_by_name(&platform_bus_type, NULL, host);
	if (!pdev)
		return -ENODEV;
	if (iocon >= 0) {
		void __iomem *reg = ioremap(iocon_phys, 4);

		/* keep the host resumed so IOCON is not restored/gated */
		pm_runtime_get_sync(pdev);
		if (reg) {
			writel(iocon, reg);
			pr_info("msdc3_ocr: IOCON=%#x\n", readl(reg));
			iounmap(reg);
		}
	}
	cdev = device_find_child(pdev, NULL, match_mmc_host);
	put_device(pdev);
	if (!cdev)
		return -ENODEV;
	mmc = container_of(cdev, struct mmc_host, class_dev);

	pr_info("msdc3_ocr: %s ocr_avail %#x -> %#x\n", dev_name(cdev),
		mmc->ocr_avail, ocr);
	mmc->ocr_avail = ocr;
	mmc->rescan_entered = 0;
	mmc_detect_change(mmc, 0);
	put_device(cdev);
	return 0;
}
module_init(msdc3_ocr_init);

MODULE_DESCRIPTION("MSDC3 OCR test helper for MT6628");
MODULE_LICENSE("GPL");
