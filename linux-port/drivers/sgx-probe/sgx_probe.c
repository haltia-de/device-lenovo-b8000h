// SPDX-License-Identifier: GPL-2.0
/*
 * sgx_probe: power up the MT6589 MFG domain and clocks, read the
 * PowerVR SGX core ID/revision registers and print them. Bring-up aid
 * for the SGX544 port (docs/wifi-gpu-port-plan.md, stage G1).
 */
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define EUR_CR_CORE_ID		0x0020
#define EUR_CR_CORE_REVISION	0x0024
#define EUR_CR_DESIGNER_REV_FIELD1 0x0028
#define EUR_CR_DESIGNER_REV_FIELD2 0x002c

static int sgx_probe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_bulk_data *clks;
	void __iomem *base;
	int nclks, ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	nclks = devm_clk_bulk_get_all(dev, &clks);
	if (nclks < 0)
		return dev_err_probe(dev, nclks, "clocks\n");

	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return dev_err_probe(dev, ret, "MFG power domain\n");

	ret = clk_bulk_prepare_enable(nclks, clks);
	if (ret) {
		pm_runtime_put(dev);
		return dev_err_probe(dev, ret, "enable clocks\n");
	}

	dev_info(dev, "SGX core_id=%08x core_rev=%08x designer=%08x/%08x (%d clocks)\n",
		 readl(base + EUR_CR_CORE_ID), readl(base + EUR_CR_CORE_REVISION),
		 readl(base + EUR_CR_DESIGNER_REV_FIELD1),
		 readl(base + EUR_CR_DESIGNER_REV_FIELD2), nclks);

	clk_bulk_disable_unprepare(nclks, clks);
	pm_runtime_put(dev);
	return 0;
}

static void sgx_probe_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id sgx_probe_of[] = {
	{ .compatible = "mediatek,mt6589-gpu" },
	{ }
};
MODULE_DEVICE_TABLE(of, sgx_probe_of);

static struct platform_driver sgx_probe_driver = {
	.probe = sgx_probe_probe,
	.remove = sgx_probe_remove,
	.driver = {
		.name = "sgx-probe",
		.of_match_table = sgx_probe_of,
	},
};
module_platform_driver(sgx_probe_driver);
MODULE_DESCRIPTION("MT6589 SGX544 bring-up register probe");
MODULE_LICENSE("GPL");
