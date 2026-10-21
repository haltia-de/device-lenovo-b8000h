// SPDX-License-Identifier: GPL-2.0
/*
 * B8000-H front camera power probe (test tool, not part of the image).
 *
 * Loading it powers the front sensor the way the stock kernel does
 * (kdCISModulePowerOn, sub sensor), gives it a 24 MHz MCLK from SENINF TG1
 * the way the stock libcamdrv does (SeninfDrvImp::setTg1PhaseCounter) and
 * reads the OmniVision chip ID on I2C bus 1, address 0x10.  The sensor stays
 * powered while the module is loaded, so /dev/i2c-1 can talk to it;
 * unloading switches everything off again.  See docs/camera-front.md.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define DRV_NAME		"b8k-cam-probe"

/* MT6320 LDOs (pwrap regmap): enable bit 15, voltage select. */
#define VCAMA_EN		0x0408
#define VCAMA_VOSEL		0x0414	/* [7:6]: 3 = 2.8 V */
#define VGP2_EN			0x042c
#define VGP2_VOSEL		0x0452	/* [7:5]: 3 = 1.8 V */
#define LDO_EN			BIT(15)

/* SENINF (0x15008000) TG1 sensor clock. */
#define SENINF_PHYS		0x15008000
#define SENINF_TOP		0x000
#define TG1_PH_CNT		0x300
#define TG1_SEN_CK		0x304
#define PH_TGCLK_SEL		0x1	/* [1:0] */
#define PH_CLKFL_POL		BIT(2)
#define PH_ADCLK_EN		BIT(29)	/* MCLK out (kernel ISP_MCLK1_EN) */
#define PH_PCEN			BIT(31)

/* include/dt-bindings/clock/mediatek,mt6589-clk.h, imgsys */
#define CLK_IMAGE_CAM_CAM	4
#define CLK_IMAGE_SEN_TG	5
#define CLK_IMAGE_SEN_CAM	6

#define SENSOR_BUS		1
#define SENSOR_ADDR		0x10

static unsigned int clkcnt = 1;	/* MCLK = 48 MHz / (clkcnt + 1) */
module_param(clkcnt, uint, 0444);
MODULE_PARM_DESC(clkcnt, "MCLK divider: 48 MHz / (clkcnt + 1), default 24 MHz");

static struct gpiod_lookup_table b8k_cam_gpios = {
	.dev_id = DRV_NAME,
	.table = {
		/* Stock pin set for the sub sensor: CMPDN GPIO31, CMRST GPIO32. */
		GPIO_LOOKUP("pinctrl_paris", 31, "pdn", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("pinctrl_paris", 32, "rst", GPIO_ACTIVE_HIGH),
		{ }
	},
};

struct b8k_cam {
	struct regmap *pmic;
	struct gpio_desc *pdn, *rst;
	struct clk *clk[3];
	void __iomem *seninf;
	u32 top_old, ph_old, ck_old;
};

static struct platform_device *b8k_cam_pdev;

static struct clk *img_clk(struct device_node *np, int id)
{
	struct of_phandle_args a = { .np = np, .args_count = 1, .args = { id } };

	return of_clk_get_from_provider(&a);
}

static int b8k_cam_mclk_on(struct b8k_cam *c)
{
	static const int ids[] = { CLK_IMAGE_CAM_CAM, CLK_IMAGE_SEN_TG, CLK_IMAGE_SEN_CAM };
	struct device_node *np;
	int i, ret;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mt6589-imgsys");
	if (!np)
		return -ENODEV;
	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		c->clk[i] = img_clk(np, ids[i]);
		if (IS_ERR(c->clk[i])) {
			ret = PTR_ERR(c->clk[i]);
			c->clk[i] = NULL;
			goto err;
		}
		ret = clk_prepare_enable(c->clk[i]);
		if (ret) {
			clk_put(c->clk[i]);
			c->clk[i] = NULL;
			goto err;
		}
	}
	of_node_put(np);

	c->seninf = ioremap(SENINF_PHYS, 0x1000);
	if (!c->seninf)
		return -ENOMEM;
	c->top_old = readl(c->seninf + SENINF_TOP);
	c->ph_old = readl(c->seninf + TG1_PH_CNT);
	c->ck_old = readl(c->seninf + TG1_SEN_CK);
	writel(c->top_old | 0xc00, c->seninf + SENINF_TOP);
	/* CLKRS [13:8] 0, CLKFL [5:0] (cnt + 1) / 2, CLKCNT [21:16] cnt. */
	writel((((clkcnt + 1) / 2) & 0x3f) | ((clkcnt & 0x3f) << 16),
	       c->seninf + TG1_SEN_CK);
	writel((c->ph_old & ~(0x3 | PH_CLKFL_POL)) | PH_TGCLK_SEL |
	       ((clkcnt & 1) ? 0 : PH_CLKFL_POL) | PH_ADCLK_EN | PH_PCEN,
	       c->seninf + TG1_PH_CNT);
	pr_info(DRV_NAME ": MCLK %lu Hz (TG1_PH_CNT %08x, TG1_SEN_CK %08x)\n",
		clk_get_rate(c->clk[1]) / (clkcnt + 1),
		readl(c->seninf + TG1_PH_CNT), readl(c->seninf + TG1_SEN_CK));
	return 0;
err:
	of_node_put(np);
	while (--i >= 0) {
		clk_disable_unprepare(c->clk[i]);
		clk_put(c->clk[i]);
		c->clk[i] = NULL;
	}
	return ret;
}

static void b8k_cam_mclk_off(struct b8k_cam *c)
{
	int i;

	if (c->seninf) {
		writel(c->ph_old, c->seninf + TG1_PH_CNT);
		writel(c->ck_old, c->seninf + TG1_SEN_CK);
		writel(c->top_old, c->seninf + SENINF_TOP);
		iounmap(c->seninf);
		c->seninf = NULL;
	}
	for (i = ARRAY_SIZE(c->clk) - 1; i >= 0; i--) {
		if (!c->clk[i])
			continue;
		clk_disable_unprepare(c->clk[i]);
		clk_put(c->clk[i]);
		c->clk[i] = NULL;
	}
}

/*
 * kdCISModulePowerOn, sub sensor (ov9726/ov9760 branch): PDN and RST low,
 * VCAMA 2.8 V, VGP2 1.8 V, VGP5 1.2 V (VGP5 already feeds the touch panel at
 * 2.8 V and the stock hwPowerOn leaves a shared LDO's voltage alone), then
 * RST low, PDN low, RST high, PDN high.  PDN high is "sensor on": with PDN
 * low the sensor does not answer.
 */
static void b8k_cam_power_on(struct b8k_cam *c)
{
	gpiod_set_value(c->pdn, 0);
	mdelay(1);
	gpiod_set_value(c->rst, 0);
	mdelay(3);
	regmap_update_bits(c->pmic, VCAMA_VOSEL, 0x3 << 6, 3 << 6);
	regmap_update_bits(c->pmic, VCAMA_EN, LDO_EN, LDO_EN);
	regmap_update_bits(c->pmic, VGP2_VOSEL, 0x7 << 5, 3 << 5);
	regmap_update_bits(c->pmic, VGP2_EN, LDO_EN, LDO_EN);
	msleep(1);
	gpiod_set_value(c->rst, 0);
	mdelay(3);
	gpiod_set_value(c->pdn, 0);
	msleep(3);
	gpiod_set_value(c->rst, 1);
	msleep(1);
	gpiod_set_value(c->pdn, 1);
	mdelay(1);
}

static void b8k_cam_power_off(struct b8k_cam *c)
{
	gpiod_set_value(c->rst, 0);
	gpiod_set_value(c->pdn, 0);
	regmap_update_bits(c->pmic, VGP2_EN, LDO_EN, 0);
	regmap_update_bits(c->pmic, VCAMA_EN, LDO_EN, 0);
}

static int sccb_read(struct i2c_adapter *ad, u16 reg, u8 *val)
{
	u8 w[2] = { reg >> 8, reg & 0xff };
	struct i2c_msg m[2] = {
		{ .addr = SENSOR_ADDR, .flags = 0, .len = 2, .buf = w },
		{ .addr = SENSOR_ADDR, .flags = I2C_M_RD, .len = 1, .buf = val },
	};

	return i2c_transfer(ad, m, 2) == 2 ? 0 : -EIO;
}

static void b8k_cam_read_id(void)
{
	struct i2c_adapter *ad = i2c_get_adapter(SENSOR_BUS);
	u8 hi, lo;

	if (!ad) {
		pr_warn(DRV_NAME ": no I2C bus %d\n", SENSOR_BUS);
		return;
	}
	if (sccb_read(ad, 0x300a, &hi) || sccb_read(ad, 0x300b, &lo))
		pr_warn(DRV_NAME ": no answer at %d-%04x\n", SENSOR_BUS, SENSOR_ADDR);
	else
		pr_info(DRV_NAME ": sensor at %d-%04x, chip ID %02x%02x\n",
			SENSOR_BUS, SENSOR_ADDR, hi, lo);
	i2c_put_adapter(ad);
}

static int b8k_cam_probe(struct platform_device *pdev)
{
	struct device *pwrap;
	struct b8k_cam *c;
	int ret;

	c = devm_kzalloc(&pdev->dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	pwrap = bus_find_device_by_name(&platform_bus_type, NULL, "1000f000.pwrap");
	if (!pwrap)
		return -EPROBE_DEFER;
	c->pmic = dev_get_regmap(pwrap, NULL);
	put_device(pwrap);
	if (!c->pmic)
		return -EPROBE_DEFER;
	c->pdn = devm_gpiod_get(&pdev->dev, "pdn", GPIOD_OUT_LOW);
	if (IS_ERR(c->pdn))
		return PTR_ERR(c->pdn);
	c->rst = devm_gpiod_get(&pdev->dev, "rst", GPIOD_OUT_LOW);
	if (IS_ERR(c->rst))
		return PTR_ERR(c->rst);

	ret = b8k_cam_mclk_on(c);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "MCLK\n");
	b8k_cam_power_on(c);
	b8k_cam_read_id();
	platform_set_drvdata(pdev, c);
	return 0;
}

static void b8k_cam_remove(struct platform_device *pdev)
{
	struct b8k_cam *c = platform_get_drvdata(pdev);

	b8k_cam_power_off(c);
	b8k_cam_mclk_off(c);
}

static struct platform_driver b8k_cam_driver = {
	.probe = b8k_cam_probe,
	.remove = b8k_cam_remove,
	.driver = { .name = DRV_NAME },
};

static int __init b8k_cam_init(void)
{
	int ret;

	gpiod_add_lookup_table(&b8k_cam_gpios);
	ret = platform_driver_register(&b8k_cam_driver);
	if (ret)
		goto err_table;
	b8k_cam_pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(b8k_cam_pdev)) {
		ret = PTR_ERR(b8k_cam_pdev);
		platform_driver_unregister(&b8k_cam_driver);
		goto err_table;
	}
	return 0;
err_table:
	gpiod_remove_lookup_table(&b8k_cam_gpios);
	return ret;
}

static void __exit b8k_cam_exit(void)
{
	platform_device_unregister(b8k_cam_pdev);
	platform_driver_unregister(&b8k_cam_driver);
	gpiod_remove_lookup_table(&b8k_cam_gpios);
}

module_init(b8k_cam_init);
module_exit(b8k_cam_exit);

MODULE_DESCRIPTION("Lenovo B8000-H front camera power probe (test tool)");
MODULE_LICENSE("GPL");
