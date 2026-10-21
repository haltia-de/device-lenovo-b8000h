// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT6589 SENINF: sensor interface of the camera block.
 *
 * Stage 1 (front camera of the Lenovo B8000-H): the TG1 sensor master clock
 * (MCLK) as a common clock, and the media device the sensor registers
 * against, so the sensor subdevice shows up in the media graph.  The CSI-2
 * receiver and the raw DMA come in stage 2 into this same driver, which is
 * why MCLK lives here rather than in a clock driver of its own: SENINF owns
 * both, and the stock HAL programs them together.
 *
 * MCLK programming follows the stock libcamdrv (SeninfDrvImp::
 * setTg1PhaseCounter): SENINF_TOP |= 0xc00; TG1_SEN_CK CLKCNT = n,
 * CLKRS = 0, CLKFL = (n + 1) / 2 for MCLK = 48 MHz / (n + 1); TG1_PH_CNT
 * TGCLK_SEL = 1, CLKFL_POL = !(n & 1), ADCLK_EN, PCEN.  See
 * docs/camera-front.md of device-lenovo-b8000h.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define SENINF_TOP		0x000
#define SENINF_TOP_TG_EN	(BIT(10) | BIT(11))
#define TG1_PH_CNT		0x300
#define PH_TGCLK_SEL		GENMASK(1, 0)
#define PH_CLKFL_POL		BIT(2)
#define PH_ADCLK_EN		BIT(29)
#define PH_PCEN			BIT(31)
#define TG1_SEN_CK		0x304
#define SEN_CK_CLKFL		GENMASK(5, 0)
#define SEN_CK_CLKRS		GENMASK(13, 8)
#define SEN_CK_CLKCNT		GENMASK(21, 16)

#define MCLK_MAX_DIV		64

static const char * const seninf_clk_names[] = { "cam", "tg", "sen" };

struct mt6589_seninf {
	struct device *dev;
	void __iomem *base;
	struct clk_bulk_data clks[ARRAY_SIZE(seninf_clk_names)];
	struct clk_hw mclk;
	unsigned int div;		/* MCLK = tg / div, 2..64 */

	struct media_device mdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
};

#define to_seninf(_hw) container_of(_hw, struct mt6589_seninf, mclk)

static unsigned long seninf_mclk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	return parent_rate / to_seninf(hw)->div;
}

static unsigned int seninf_mclk_div(unsigned long rate, unsigned long parent)
{
	unsigned int div = DIV_ROUND_CLOSEST(parent, max(rate, 1UL));

	return clamp(div, 2U, MCLK_MAX_DIV);
}

static int seninf_mclk_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	req->rate = req->best_parent_rate /
		    seninf_mclk_div(req->rate, req->best_parent_rate);
	return 0;
}

static int seninf_mclk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	to_seninf(hw)->div = seninf_mclk_div(rate, parent_rate);
	return 0;
}

static int seninf_mclk_prepare(struct clk_hw *hw)
{
	struct mt6589_seninf *s = to_seninf(hw);
	unsigned int cnt = s->div - 1;
	u32 ph;
	int ret;

	ret = pm_runtime_resume_and_get(s->dev);
	if (ret)
		return ret;
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(s->clks), s->clks);
	if (ret) {
		pm_runtime_put(s->dev);
		return ret;
	}

	writel(readl(s->base + SENINF_TOP) | SENINF_TOP_TG_EN, s->base + SENINF_TOP);
	writel(FIELD_PREP(SEN_CK_CLKFL, (cnt + 1) / 2) |
	       FIELD_PREP(SEN_CK_CLKRS, 0) |
	       FIELD_PREP(SEN_CK_CLKCNT, cnt), s->base + TG1_SEN_CK);
	ph = readl(s->base + TG1_PH_CNT) & ~(PH_TGCLK_SEL | PH_CLKFL_POL);
	ph |= FIELD_PREP(PH_TGCLK_SEL, 1) | ((cnt & 1) ? 0 : PH_CLKFL_POL) |
	      PH_ADCLK_EN | PH_PCEN;
	writel(ph, s->base + TG1_PH_CNT);
	return 0;
}

static void seninf_mclk_unprepare(struct clk_hw *hw)
{
	struct mt6589_seninf *s = to_seninf(hw);

	writel(readl(s->base + TG1_PH_CNT) & ~(PH_ADCLK_EN | PH_PCEN),
	       s->base + TG1_PH_CNT);
	writel(readl(s->base + SENINF_TOP) & ~SENINF_TOP_TG_EN, s->base + SENINF_TOP);
	clk_bulk_disable_unprepare(ARRAY_SIZE(s->clks), s->clks);
	pm_runtime_put(s->dev);
}

static const struct clk_ops seninf_mclk_ops = {
	.prepare = seninf_mclk_prepare,
	.unprepare = seninf_mclk_unprepare,
	.recalc_rate = seninf_mclk_recalc_rate,
	.determine_rate = seninf_mclk_determine_rate,
	.set_rate = seninf_mclk_set_rate,
};

static int seninf_register_mclk(struct mt6589_seninf *s)
{
	struct clk_init_data init = {};
	const char *parent;
	int ret;

	/* "tg" is image_sen_tg, fed by camtg_sel at 48 MHz. */
	parent = __clk_get_name(s->clks[1].clk);
	init.name = devm_kasprintf(s->dev, GFP_KERNEL, "%s-mclk", dev_name(s->dev));
	if (!init.name)
		return -ENOMEM;
	init.ops = &seninf_mclk_ops;
	init.parent_names = &parent;
	init.num_parents = 1;
	/* The divider is written at prepare: change it only while gated. */
	init.flags = CLK_SET_RATE_GATE;
	s->mclk.init = &init;
	s->div = 2;	/* 24 MHz until a consumer asks */

	ret = devm_clk_hw_register(s->dev, &s->mclk);
	if (ret)
		return ret;
	return devm_of_clk_add_hw_provider(s->dev, of_clk_hw_simple_get, &s->mclk);
}

static int seninf_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct mt6589_seninf *s = container_of(notifier, struct mt6589_seninf,
					       notifier);

	return v4l2_device_register_subdev_nodes(&s->v4l2_dev);
}

static const struct v4l2_async_notifier_operations seninf_notify_ops = {
	.complete = seninf_notify_complete,
};

static int seninf_register_media(struct mt6589_seninf *s)
{
	struct v4l2_async_connection *asc;
	struct fwnode_handle *ep;
	int ret;

	s->mdev.dev = s->dev;
	strscpy(s->mdev.model, "MT6589 SENINF", sizeof(s->mdev.model));
	media_device_init(&s->mdev);
	s->v4l2_dev.mdev = &s->mdev;
	ret = v4l2_device_register(s->dev, &s->v4l2_dev);
	if (ret)
		goto err_media;

	v4l2_async_nf_init(&s->notifier, &s->v4l2_dev);
	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(s->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep) {
		ret = dev_err_probe(s->dev, -ENODEV, "no sensor endpoint\n");
		goto err_v4l2;
	}
	asc = v4l2_async_nf_add_fwnode_remote(&s->notifier, ep,
					      struct v4l2_async_connection);
	fwnode_handle_put(ep);
	if (IS_ERR(asc)) {
		ret = PTR_ERR(asc);
		goto err_nf;
	}
	s->notifier.ops = &seninf_notify_ops;
	ret = v4l2_async_nf_register(&s->notifier);
	if (ret)
		goto err_nf;

	ret = media_device_register(&s->mdev);
	if (ret)
		goto err_nf_unreg;
	return 0;

err_nf_unreg:
	v4l2_async_nf_unregister(&s->notifier);
err_nf:
	v4l2_async_nf_cleanup(&s->notifier);
err_v4l2:
	v4l2_device_unregister(&s->v4l2_dev);
err_media:
	media_device_cleanup(&s->mdev);
	return ret;
}

static int mt6589_seninf_probe(struct platform_device *pdev)
{
	struct mt6589_seninf *s;
	int i, ret;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->dev = &pdev->dev;
	platform_set_drvdata(pdev, s);

	s->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(s->base))
		return PTR_ERR(s->base);

	for (i = 0; i < ARRAY_SIZE(s->clks); i++)
		s->clks[i].id = seninf_clk_names[i];
	ret = devm_clk_bulk_get(s->dev, ARRAY_SIZE(s->clks), s->clks);
	if (ret)
		return dev_err_probe(s->dev, ret, "clocks\n");

	ret = devm_pm_runtime_enable(s->dev);
	if (ret)
		return ret;

	ret = seninf_register_mclk(s);
	if (ret)
		return dev_err_probe(s->dev, ret, "MCLK\n");

	return seninf_register_media(s);
}

static void mt6589_seninf_remove(struct platform_device *pdev)
{
	struct mt6589_seninf *s = platform_get_drvdata(pdev);

	media_device_unregister(&s->mdev);
	v4l2_async_nf_unregister(&s->notifier);
	v4l2_async_nf_cleanup(&s->notifier);
	v4l2_device_unregister(&s->v4l2_dev);
	media_device_cleanup(&s->mdev);
}

static const struct of_device_id mt6589_seninf_of_match[] = {
	{ .compatible = "mediatek,mt6589-seninf" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6589_seninf_of_match);

static struct platform_driver mt6589_seninf_driver = {
	.probe = mt6589_seninf_probe,
	.remove = mt6589_seninf_remove,
	.driver = {
		.name = "mt6589-seninf",
		.of_match_table = mt6589_seninf_of_match,
	},
};
module_platform_driver(mt6589_seninf_driver);

MODULE_DESCRIPTION("MediaTek MT6589 SENINF: sensor MCLK and media device");
MODULE_LICENSE("GPL");
