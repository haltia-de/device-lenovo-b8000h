// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT6628 SDIO/cfg80211 glue for Lenovo B8000-H.
 *
 * Register offsets, SDIO identity and block size come from MediaTek's
 * GPL-2.0 wlan_gen2 driver.  This modern glue intentionally does not carry
 * the old Android WEXT/procfs/HAL interfaces.
 */

#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <net/cfg80211.h>

#include "mt6628.h"

#define MT6628_VENDOR_ID        0x037a
#define MT6628_DEVICE_ID        0x6628
#define MT6628_BLOCK_SIZE       512
#define MT6628_WIFI_FIRMWARE    "WIFI_RAM_CODE_MT6628"

#define MT6628_WCIR             0x0000
#define MT6628_WHLPCR           0x0004
#define MT6628_WHISR            0x0010
#define MT6628_WHIER            0x0014
#define MT6628_WASR             0x0018
#define MT6628_WCIR_CHIP_ID     GENMASK(15, 0)
#define MT6628_WCIR_WLAN_READY  BIT(21)
#define MT6628_WHLPCR_INT_EN_CLR BIT(1)
#define MT6628_WHLPCR_INT_EN_SET BIT(0)
#define MT6628_WHIER_DEFAULT    (GENMASK(31, 8) | BIT(3) | BIT(2) | BIT(1) | BIT(0))

struct mt6628_wlan {
	struct sdio_func *func;
	struct wiphy *wiphy;
	struct wireless_dev wdev;
	struct net_device *netdev;
	struct rtnl_link_stats64 stats;
	u32 last_irq_status;
	bool irq_claimed;
	bool wmt_on;
};

struct mt6628_wiphy_priv {
	struct mt6628_wlan *wlan;
};

struct mt6628_netdev_priv {
	struct mt6628_wlan *wlan;
};

static struct ieee80211_rate mt6628_rates[] = {
	{ .bitrate = 10,  .hw_value = 0 },
	{ .bitrate = 20,  .hw_value = 1 },
	{ .bitrate = 55,  .hw_value = 2 },
	{ .bitrate = 110, .hw_value = 3 },
	{ .bitrate = 60,  .hw_value = 4 },
	{ .bitrate = 90,  .hw_value = 5 },
	{ .bitrate = 120, .hw_value = 6 },
	{ .bitrate = 180, .hw_value = 7 },
	{ .bitrate = 240, .hw_value = 8 },
	{ .bitrate = 360, .hw_value = 9 },
	{ .bitrate = 480, .hw_value = 10 },
	{ .bitrate = 540, .hw_value = 11 },
};

#define MT6628_CHAN(_ch, _freq) { \
	.band = NL80211_BAND_2GHZ, .center_freq = (_freq), .hw_value = (_ch), \
	.max_power = 20 }

static struct ieee80211_channel mt6628_channels[] = {
	MT6628_CHAN(1, 2412), MT6628_CHAN(2, 2417),
	MT6628_CHAN(3, 2422), MT6628_CHAN(4, 2427),
	MT6628_CHAN(5, 2432), MT6628_CHAN(6, 2437),
	MT6628_CHAN(7, 2442), MT6628_CHAN(8, 2447),
	MT6628_CHAN(9, 2452), MT6628_CHAN(10, 2457),
	MT6628_CHAN(11, 2462), MT6628_CHAN(12, 2467),
	MT6628_CHAN(13, 2472), MT6628_CHAN(14, 2484),
};

static struct ieee80211_supported_band mt6628_band_2ghz = {
	.band = NL80211_BAND_2GHZ,
	.channels = mt6628_channels,
	.n_channels = ARRAY_SIZE(mt6628_channels),
	.bitrates = mt6628_rates,
	.n_bitrates = ARRAY_SIZE(mt6628_rates),
};

static int mt6628_cfg_change_iface(struct wiphy *wiphy,
				   struct net_device *dev,
				   enum nl80211_iftype type,
				   struct vif_params *params)
{
	(void)wiphy;
	(void)params;
	if (type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;
	dev->ieee80211_ptr->iftype = type;
	return 0;
}

static int mt6628_cfg_scan(struct wiphy *wiphy,
			   struct cfg80211_scan_request *request)
{
	(void)wiphy;
	(void)request;
	/* The wlan_gen2 command/event engine is the next hardware-gated step. */
	return -EOPNOTSUPP;
}

static int mt6628_cfg_connect(struct wiphy *wiphy, struct net_device *dev,
			      struct cfg80211_connect_params *sme)
{
	(void)wiphy;
	(void)dev;
	(void)sme;
	return -EOPNOTSUPP;
}

static int mt6628_cfg_disconnect(struct wiphy *wiphy, struct net_device *dev,
				 u16 reason_code)
{
	(void)wiphy;
	(void)reason_code;
	cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING, NULL, 0, true,
			GFP_KERNEL);
	return 0;
}

static const struct cfg80211_ops mt6628_cfg80211_ops = {
	.change_virtual_intf = mt6628_cfg_change_iface,
	.scan = mt6628_cfg_scan,
	.connect = mt6628_cfg_connect,
	.disconnect = mt6628_cfg_disconnect,
};

static int mt6628_ndo_open(struct net_device *dev)
{
	netif_carrier_off(dev);
	netif_start_queue(dev);
	return 0;
}

static int mt6628_ndo_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static netdev_tx_t mt6628_ndo_xmit(struct sk_buff *skb,
				   struct net_device *dev)
{
	struct mt6628_netdev_priv *priv = netdev_priv(dev);
	struct mt6628_wlan *wlan = priv->wlan;

	wlan->stats.tx_dropped++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void mt6628_ndo_get_stats64(struct net_device *dev,
				   struct rtnl_link_stats64 *stats)
{
	struct mt6628_netdev_priv *priv = netdev_priv(dev);
	struct mt6628_wlan *wlan = priv->wlan;

	*stats = wlan->stats;
}

static const struct net_device_ops mt6628_netdev_ops = {
	.ndo_open = mt6628_ndo_open,
	.ndo_stop = mt6628_ndo_stop,
	.ndo_start_xmit = mt6628_ndo_xmit,
	.ndo_get_stats64 = mt6628_ndo_get_stats64,
};

static void mt6628_sdio_irq(struct sdio_func *func)
{
	struct mt6628_wlan *wlan = sdio_get_drvdata(func);
	int err = 0;

	wlan->last_irq_status = sdio_readl(func, MT6628_WHISR, &err);
	if (err) {
		dev_warn_ratelimited(&func->dev,
				     "cannot read interrupt status: %d\n", err);
		return;
	}

	/* Reading WHISR clears status in the vendor default configuration. */
	if (wlan->last_irq_status & BIT(3)) {
		u32 abnormal = sdio_readl(func, MT6628_WASR, &err);

		if (!err)
			dev_warn_ratelimited(&func->dev,
					     "abnormal SDIO status %#x\n", abnormal);
	}
	pm_wakeup_event(&func->dev, 250);
}

static int mt6628_register_cfg80211(struct mt6628_wlan *wlan)
{
	struct mt6628_wiphy_priv *priv;
	struct net_device *ndev;
	struct wiphy *wiphy;
	int ret;

	wiphy = wiphy_new(&mt6628_cfg80211_ops, sizeof(*priv));
	if (!wiphy)
		return -ENOMEM;
	priv = wiphy_priv(wiphy);
	priv->wlan = wlan;
	wlan->wiphy = wiphy;

	set_wiphy_dev(wiphy, &wlan->func->dev);
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	wiphy->bands[NL80211_BAND_2GHZ] = &mt6628_band_2ghz;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->max_scan_ssids = 4;
	wiphy->max_scan_ie_len = 512;

	ret = wiphy_register(wiphy);
	if (ret)
		goto err_wiphy;

	ndev = alloc_netdev(sizeof(struct mt6628_netdev_priv), "wlan%d",
			    NET_NAME_ENUM, ether_setup);
	if (!ndev) {
		ret = -ENOMEM;
		goto err_unregister_wiphy;
	}

	((struct mt6628_netdev_priv *)netdev_priv(ndev))->wlan = wlan;
	wlan->netdev = ndev;
	wlan->wdev.wiphy = wiphy;
	wlan->wdev.iftype = NL80211_IFTYPE_STATION;
	wlan->wdev.netdev = ndev;
	ndev->ieee80211_ptr = &wlan->wdev;
	ndev->netdev_ops = &mt6628_netdev_ops;
	SET_NETDEV_DEV(ndev, &wlan->func->dev);
	eth_hw_addr_random(ndev);
	netif_carrier_off(ndev);

	ret = register_netdev(ndev);
	if (ret)
		goto err_free_netdev;

	return 0;

err_free_netdev:
	free_netdev(ndev);
	wlan->netdev = NULL;
err_unregister_wiphy:
	wiphy_unregister(wiphy);
err_wiphy:
	wiphy_free(wiphy);
	wlan->wiphy = NULL;
	return ret;
}

static void mt6628_unregister_cfg80211(struct mt6628_wlan *wlan)
{
	if (wlan->netdev) {
		unregister_netdev(wlan->netdev);
		free_netdev(wlan->netdev);
		wlan->netdev = NULL;
	}
	if (wlan->wiphy) {
		wiphy_unregister(wlan->wiphy);
		wiphy_free(wlan->wiphy);
		wlan->wiphy = NULL;
	}
}

static int mt6628_sdio_probe(struct sdio_func *func,
			     const struct sdio_device_id *id)
{
	const struct firmware *fw;
	struct mt6628_wlan *wlan;
	u32 chip_id;
	int ret;

	(void)id;
	wlan = devm_kzalloc(&func->dev, sizeof(*wlan), GFP_KERNEL);
	if (!wlan)
		return -ENOMEM;
	wlan->func = func;
	sdio_set_drvdata(func, wlan);

	ret = mt6628_wmt_wifi_on();
	if (ret)
		return dev_err_probe(&func->dev, ret, "WMT Wi-Fi power-on\n");
	wlan->wmt_on = true;

	ret = request_firmware(&fw, MT6628_WIFI_FIRMWARE, &func->dev);
	if (ret) {
		ret = dev_err_probe(&func->dev, ret, "cannot load %s\n",
				    MT6628_WIFI_FIRMWARE);
		goto err_wmt;
	}
	dev_info(&func->dev, "loaded %s (%zu bytes)\n",
		 MT6628_WIFI_FIRMWARE, fw->size);
	release_firmware(fw);

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	if (!ret)
		ret = sdio_set_block_size(func, MT6628_BLOCK_SIZE);
	if (!ret) {
		chip_id = sdio_readl(func, MT6628_WCIR, &ret);
		if (!ret && (chip_id & MT6628_WCIR_CHIP_ID) != MT6628_DEVICE_ID)
			ret = -ENODEV;
	}
	if (!ret)
		ret = sdio_claim_irq(func, mt6628_sdio_irq);
	if (!ret) {
		wlan->irq_claimed = true;
		sdio_writel(func, MT6628_WHIER_DEFAULT, MT6628_WHIER, &ret);
		if (!ret)
			sdio_writel(func, MT6628_WHLPCR_INT_EN_SET,
				    MT6628_WHLPCR, &ret);
	}
	sdio_release_host(func);
	if (ret)
		goto err_sdio;

	ret = mt6628_register_cfg80211(wlan);
	if (ret)
		goto err_sdio;

	device_init_wakeup(&func->dev, true);
	dev_warn(&func->dev,
		 "wlan0 registered; firmware command/event engine is not yet connected\n");
	return 0;

err_sdio:
	sdio_claim_host(func);
	if (wlan->irq_claimed) {
		sdio_release_irq(func);
		wlan->irq_claimed = false;
	}
	sdio_disable_func(func);
	sdio_release_host(func);
err_wmt:
	mt6628_wmt_wifi_off();
	wlan->wmt_on = false;
	return ret;
}

static void mt6628_sdio_remove(struct sdio_func *func)
{
	struct mt6628_wlan *wlan = sdio_get_drvdata(func);
	int err = 0;

	device_init_wakeup(&func->dev, false);
	mt6628_unregister_cfg80211(wlan);
	sdio_claim_host(func);
	sdio_writel(func, MT6628_WHLPCR_INT_EN_CLR, MT6628_WHLPCR, &err);
	if (wlan->irq_claimed)
		sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);
	if (wlan->wmt_on)
		mt6628_wmt_wifi_off();
}

static const struct sdio_device_id mt6628_sdio_ids[] = {
	{ SDIO_DEVICE(MT6628_VENDOR_ID, MT6628_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(sdio, mt6628_sdio_ids);

static struct sdio_driver mt6628_sdio_driver = {
	.name = "wlan_mt6628",
	.id_table = mt6628_sdio_ids,
	.probe = mt6628_sdio_probe,
	.remove = mt6628_sdio_remove,
};
module_sdio_driver(mt6628_sdio_driver);

MODULE_AUTHOR("Lenovo B8000-H Linux port contributors");
MODULE_DESCRIPTION("MediaTek MT6628 SDIO cfg80211 driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MT6628_WIFI_FIRMWARE);
MODULE_SOFTDEP("pre: wmt");
