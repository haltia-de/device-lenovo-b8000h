// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT6628 WMT platform/transport glue for Lenovo B8000-H.
 *
 * Based on the power sequencing and public WMT interface in MediaTek's
 * GPL-2.0 Lenovo 3.4 kernel.  The old n_mtkstp line discipline, launcher
 * character device, Android wakelocks and board-file GPIO calls are replaced
 * by serdev, firmware_class, gpiod, clk and generic wakeup APIs.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pm_wakeup.h>
#include <linux/property.h>
#include <linux/serdev.h>
#include <linux/slab.h>

#include "mt6628.h"

#define MT6628_FW_PATCH_E1       "mt6628_patch_e1_hdr.bin"
#define MT6628_FW_PATCH_E2_0     "mt6628_patch_e2_0_hdr.bin"
#define MT6628_FW_PATCH_E2_1     "mt6628_patch_e2_1_hdr.bin"
#define MT6628_FW_CONFIG         "WMT.cfg"

struct mt6628_wmt {
	struct serdev_device *serdev;
	struct gpio_desc *pmu;
	struct gpio_desc *reset;
	struct clk *clk32k;
	struct mutex lock;
	unsigned int users;
	unsigned int baudrate;
	u32 ldo_stable_ms;
	u32 rtc_stable_ms;
	u32 off_stable_ms;
	u32 reset_stable_ms;
	u32 on_stable_ms;
	const char *patch_name;
	bool powered;
};

static DEFINE_MUTEX(mt6628_global_lock);
static struct mt6628_wmt *mt6628_global;

static void mt6628_read_delay(struct device *dev, const char *name,
			       u32 *value, u32 fallback)
{
	if (device_property_read_u32(dev, name, value))
		*value = fallback;
}

static int mt6628_check_firmware(struct mt6628_wmt *wmt)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, MT6628_FW_CONFIG, &wmt->serdev->dev);
	if (ret)
		return dev_err_probe(&wmt->serdev->dev, ret,
				     "cannot load %s\n", MT6628_FW_CONFIG);
	release_firmware(fw);

	ret = request_firmware(&fw, wmt->patch_name, &wmt->serdev->dev);
	if (ret)
		return dev_err_probe(&wmt->serdev->dev, ret,
				     "cannot load selected patch %s\n",
				     wmt->patch_name);
	release_firmware(fw);

	return 0;
}

static int mt6628_power_on(struct mt6628_wmt *wmt)
{
	int ret;

	if (wmt->powered)
		return 0;

	/* The board has no separate combo LDO GPIO.  PMU_EN is the first
	 * controllable supply signal; the optional clock models RTC 32 kHz.
	 */
	gpiod_set_value_cansleep(wmt->pmu, 0);
	gpiod_set_value_cansleep(wmt->reset, 0);
	msleep(wmt->off_stable_ms);

	if (wmt->clk32k) {
		ret = clk_prepare_enable(wmt->clk32k);
		if (ret)
			return ret;
	}
	msleep(wmt->rtc_stable_ms);

	gpiod_set_value_cansleep(wmt->pmu, 1);
	msleep(wmt->reset_stable_ms);
	gpiod_set_value_cansleep(wmt->reset, 1);
	msleep(wmt->on_stable_ms);

	wmt->powered = true;
	return 0;
}

static void mt6628_power_off(struct mt6628_wmt *wmt)
{
	if (!wmt->powered)
		return;

	gpiod_set_value_cansleep(wmt->reset, 0);
	gpiod_set_value_cansleep(wmt->pmu, 0);
	if (wmt->clk32k)
		clk_disable_unprepare(wmt->clk32k);
	wmt->powered = false;
}

int mt6628_wmt_wifi_on(void)
{
	struct mt6628_wmt *wmt;
	int ret;

	mutex_lock(&mt6628_global_lock);
	wmt = mt6628_global;
	if (!wmt) {
		ret = -EPROBE_DEFER;
		goto out_global;
	}

	mutex_lock(&wmt->lock);
	if (wmt->users++) {
		ret = 0;
		goto out_wmt;
	}

	ret = mt6628_power_on(wmt);
	if (ret)
		goto err_user;

	ret = mt6628_check_firmware(wmt);
	if (ret)
		goto err_power;

	/* Patch transfer is intentionally kept behind this explicit boundary.
	 * The UART is now owned by serdev, but the MT6628 WMT/STP command stream
	 * still needs validation on hardware before bytes are sent to the chip.
	 */
	dev_info(&wmt->serdev->dev,
		 "powered for Wi-Fi; WMT patch transport awaits hardware validation\n");
	goto out_wmt;

err_power:
	mt6628_power_off(wmt);
err_user:
	wmt->users = 0;
out_wmt:
	mutex_unlock(&wmt->lock);
out_global:
	mutex_unlock(&mt6628_global_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mt6628_wmt_wifi_on);

void mt6628_wmt_wifi_off(void)
{
	struct mt6628_wmt *wmt;

	mutex_lock(&mt6628_global_lock);
	wmt = mt6628_global;
	if (!wmt)
		goto out_global;

	mutex_lock(&wmt->lock);
	if (WARN_ON(!wmt->users))
		goto out_wmt;
	if (!--wmt->users)
		mt6628_power_off(wmt);
out_wmt:
	mutex_unlock(&wmt->lock);
out_global:
	mutex_unlock(&mt6628_global_lock);
}
EXPORT_SYMBOL_GPL(mt6628_wmt_wifi_off);

static size_t mt6628_receive_buf(struct serdev_device *serdev,
				 const unsigned char *data, size_t count)
{
	/* Keep the UART drained until the validated STP decoder is connected. */
	dev_dbg(&serdev->dev, "received %zu WMT/STP bytes\n", count);
	return count;
}

static void mt6628_write_wakeup(struct serdev_device *serdev)
{
	dev_dbg(&serdev->dev, "UART write wakeup\n");
}

static const struct serdev_device_ops mt6628_serdev_ops = {
	.receive_buf = mt6628_receive_buf,
	.write_wakeup = mt6628_write_wakeup,
};

static irqreturn_t mt6628_bgf_irq_thread(int irq, void *data)
{
	struct mt6628_wmt *wmt = data;

	pm_wakeup_event(&wmt->serdev->dev, 250);
	dev_dbg(&wmt->serdev->dev, "BGF interrupt %d\n", irq);
	return IRQ_HANDLED;
}

static int mt6628_wmt_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct mt6628_wmt *wmt;
	int irq, ret;

	wmt = devm_kzalloc(dev, sizeof(*wmt), GFP_KERNEL);
	if (!wmt)
		return -ENOMEM;

	wmt->serdev = serdev;
	mutex_init(&wmt->lock);
	wmt->baudrate = 115200;
	device_property_read_u32(dev, "current-speed", &wmt->baudrate);
	mt6628_read_delay(dev, "mediatek,ldo-stable-ms",
			  &wmt->ldo_stable_ms, 100);
	mt6628_read_delay(dev, "mediatek,rtc-stable-ms",
			  &wmt->rtc_stable_ms, 100);
	mt6628_read_delay(dev, "mediatek,off-stable-ms",
			  &wmt->off_stable_ms, 10);
	mt6628_read_delay(dev, "mediatek,reset-stable-ms",
			  &wmt->reset_stable_ms, 30);
	mt6628_read_delay(dev, "mediatek,on-stable-ms",
			  &wmt->on_stable_ms, 30);
	if (device_property_read_string(dev, "mediatek,patch-firmware",
					&wmt->patch_name))
		wmt->patch_name = MT6628_FW_PATCH_E2_0;

	wmt->pmu = devm_gpiod_get(dev, "pmu-en", GPIOD_OUT_LOW);
	if (IS_ERR(wmt->pmu))
		return dev_err_probe(dev, PTR_ERR(wmt->pmu), "PMU GPIO\n");
	wmt->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(wmt->reset))
		return dev_err_probe(dev, PTR_ERR(wmt->reset), "reset GPIO\n");
	wmt->clk32k = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(wmt->clk32k))
		return dev_err_probe(dev, PTR_ERR(wmt->clk32k), "32 kHz clock\n");

	serdev_device_set_client_ops(serdev, &mt6628_serdev_ops);
	ret = serdev_device_open(serdev);
	if (ret)
		return dev_err_probe(dev, ret, "cannot open UART\n");
	serdev_device_set_baudrate(serdev, wmt->baudrate);
	serdev_device_set_flow_control(serdev, false);
	serdev_device_set_drvdata(serdev, wmt);

	irq = of_irq_get(dev->of_node, 0);
	if (irq > 0) {
		ret = devm_request_threaded_irq(dev, irq, NULL,
						mt6628_bgf_irq_thread,
						IRQF_ONESHOT | IRQF_TRIGGER_LOW,
						dev_name(dev), wmt);
		if (ret)
			goto err_close;
	} else if (irq == -EPROBE_DEFER) {
		ret = irq;
		goto err_close;
	}

	device_init_wakeup(dev, true);
	mutex_lock(&mt6628_global_lock);
	if (mt6628_global) {
		mutex_unlock(&mt6628_global_lock);
		ret = -EBUSY;
		goto err_wakeup;
	}
	mt6628_global = wmt;
	mutex_unlock(&mt6628_global_lock);

	dev_info(dev, "MT6628 WMT serdev at %u baud\n", wmt->baudrate);
	return 0;

err_wakeup:
	device_init_wakeup(dev, false);
err_close:
	serdev_device_close(serdev);
	return ret;
}

static void mt6628_wmt_remove(struct serdev_device *serdev)
{
	struct mt6628_wmt *wmt = serdev_device_get_drvdata(serdev);

	mutex_lock(&mt6628_global_lock);
	if (mt6628_global == wmt)
		mt6628_global = NULL;
	mutex_unlock(&mt6628_global_lock);

	mutex_lock(&wmt->lock);
	wmt->users = 0;
	mt6628_power_off(wmt);
	mutex_unlock(&wmt->lock);
	device_init_wakeup(&serdev->dev, false);
	serdev_device_close(serdev);
}

static const struct of_device_id mt6628_wmt_of_match[] = {
	{ .compatible = "mediatek,mt6628-wmt" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6628_wmt_of_match);

static struct serdev_device_driver mt6628_wmt_driver = {
	.probe = mt6628_wmt_probe,
	.remove = mt6628_wmt_remove,
	.driver = {
		.name = "mt6628-wmt",
		.of_match_table = mt6628_wmt_of_match,
	},
};
module_serdev_device_driver(mt6628_wmt_driver);

MODULE_AUTHOR("Lenovo B8000-H Linux port contributors");
MODULE_DESCRIPTION("MediaTek MT6628 WMT power and serdev transport");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MT6628_FW_PATCH_E1);
MODULE_FIRMWARE(MT6628_FW_PATCH_E2_0);
MODULE_FIRMWARE(MT6628_FW_PATCH_E2_1);
MODULE_FIRMWARE(MT6628_FW_CONFIG);
