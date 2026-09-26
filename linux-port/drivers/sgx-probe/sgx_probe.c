// SPDX-License-Identifier: GPL-2.0
/*
 * sgx_probe: power up the MT6589 MFG domain and clocks, read the
 * PowerVR SGX core ID/revision registers and print them. Bring-up aid
 * for the SGX544 port (docs/wifi-gpu-port-plan.md, stage G1).
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

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

	/* Each step is logged and followed by a pause so pstore keeps the last one. */
	dev_info(dev, "step1 probe entered (MFG domain attached)\n");
	mdelay(200);
	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	nclks = devm_clk_bulk_get_all(dev, &clks);
	if (nclks < 0)
		return dev_err_probe(dev, nclks, "clocks\n");

	/*
	 * The SGX core rail is MT6320 VRF18_2 (vendor get_gpu_power_src()==0);
	 * it is off after boot. No DT supply yet: look it up by regulator name.
	 */
	{
		struct regulator *vgpu = devm_regulator_get(dev, "vrf18_2");

		if (IS_ERR(vgpu))
			return dev_err_probe(dev, PTR_ERR(vgpu), "vrf18_2\n");
		ret = regulator_enable(vgpu);
		dev_info(dev, "step1b vrf18_2 enable=%d, %d uV\n", ret,
			 regulator_get_voltage(vgpu));
		if (ret)
			return ret;
		mdelay(200);
	}
	dev_info(dev, "step2 %d clocks, runtime resume\n", nclks);
	mdelay(200);
	/*
	 * genpd powered MFG on at attach, before the rail was up: cycle the
	 * domain now that VRF18_2 is on.
	 */
	pm_runtime_enable(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_put_sync(dev);
	{
		void __iomem *spm = ioremap(0x10006000, 0x1000);

		dev_info(dev, "step2b after put: MFG_PWR_CON=%08x PWR_STATUS=%08x\n",
			 readl(spm + 0x214), readl(spm + 0x60c));
		mdelay(100);
		ret = pm_runtime_resume_and_get(dev);
		dev_info(dev, "step2c after get(%d): MFG_PWR_CON=%08x PWR_STATUS=%08x\n",
			 ret, readl(spm + 0x214), readl(spm + 0x60c));
		/* vendor clears SRAM_PDN = 0xf << 8, mainline only BIT(8) */
		writel(readl(spm + 0x214) & ~0xf00, spm + 0x214);
		udelay(50);
		dev_info(dev, "step2d SRAM on: MFG_PWR_CON=%08x\n", readl(spm + 0x214));
		iounmap(spm);
	}
	if (ret)
		return dev_err_probe(dev, ret, "MFG power domain\n");

	dev_info(dev, "step3 domain on, enabling clocks\n");
	mdelay(200);
	ret = clk_bulk_prepare_enable(nclks, clks);
	if (ret) {
		pm_runtime_put(dev);
		return dev_err_probe(dev, ret, "enable clocks\n");
	}

	/*
	 * Vendor MFG bring-up (pvr/services4/system/mt6589/sysutils_linux.c):
	 * all four MFG CGs incl. HYD on, pulse the SGX544 reset in mfgsys,
	 * then open the MFG "way_en" path in infracfg.
	 */
	{
		void __iomem *mfg = ioremap(0x10206000, 0x100);
		void __iomem *infra = ioremap(0x10001200, 4);

		dev_info(dev, "step3b mfg cg=%08x rst=%08x way=%08x\n", readl(mfg),
			 readl(mfg + 0xc), readl(infra));
		writel(0x1, mfg + 0xc);		/* assert SGX544 reset */
		writel(0xf, mfg + 0x8);		/* MFG clocks on (CG_CLR) */
		udelay(10);
		writel(0xf, mfg + 0x4);		/* MFG clocks off (CG_SET) */
		writel(0x0, mfg + 0xc);		/* release reset */
		writel(0xf, mfg + 0x8);		/* MFG clocks on */
		writel(readl(infra) | 0x400, infra);
		dev_info(dev, "step3c mfg cg=%08x rst=%08x way=%08x\n", readl(mfg),
			 readl(mfg + 0xc), readl(infra));
		{
			void __iomem *ic = ioremap(0x10001000, 0x300);

			dev_info(dev, "step3d TOPAXI_PROT_EN=%08x STA0=%08x STA1=%08x SI0=%08x\n",
				 readl(ic + 0x220), readl(ic + 0x224),
				 readl(ic + 0x228), readl(ic + 0x200));
			iounmap(ic);
		}
		mdelay(200);
		iounmap(infra);
		iounmap(mfg);
	}
	dev_info(dev, "step4 clocks on, reading SGX registers\n");
	mdelay(200);
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
