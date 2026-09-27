// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal playback driver for Lenovo B8000-H (MT6589 + MT6320 + SN2145B).
 *
 * The register programming is taken from Lenovo's MT6589 kernel headers and
 * the stripped stock audio HAL (B8000_A442_001_020_140620_WW_3GDATA).  The
 * first version deliberately supports only 48 kHz, stereo, S16_LE playback.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#define DRV_NAME		"b8000-mt6589-audio"
#define AFE_PHYS		0x12070000
#define AFE_SIZE		0x1000
#define INFRACFG_PHYS		0x10001000
#define INFRACFG_SIZE		0x1000
#define INFRA_PDN_SET		0x0040
#define INFRA_PDN_CLR		0x0044
#define INFRA_PDN_STA		0x0048
#define INFRA_AUDIO_BIT		BIT(5)

#define AUDIO_AFE_TOP_CON0	0x0000
#define AFE_DAC_CON0		0x0010
#define AFE_DAC_CON1		0x0014
#define AFE_CONN1		0x0024
#define AFE_CONN2		0x0028
#define AFE_I2S_CON1		0x0034
#define AFE_DL1_BASE		0x0040
#define AFE_DL1_CUR		0x0044
#define AFE_DL1_END		0x0048

/* MT6320 PMIC audio registers (16-bit, byte addressed through pwrap). */
#define TOP_CKPDN_CLR		0x0106
#define AFE_UL_DL_CON0		0x4000
#define AFE_DL_SRC2_CON0_H	0x4002
#define AFE_DL_SRC2_CON0_L	0x4004
#define AFE_DL_SRC2_CON1_H	0x4006
#define AFE_DL_SDM_CON1		0x400c
#define AFE_PREDIS_CON0_H	0x4016
#define AFE_PREDIS_CON0_L	0x4018
#define AFE_PREDIS_CON1_H	0x401a
#define AFE_PREDIS_CON1_L	0x401c
#define ANA_AFE_I2S_CON1	0x401e
#define AFE_I2S_FIFO_DL_CFG0	0x4022
#define ANA_AFE_TOP_CON0	0x4024
#define AFUNC_AUD_CON0		0x4034
#define AFUNC_AUD_CON2		0x4038

#define AUDDAC_CON0		0x0700
#define AUDBUF_CFG0		0x0702
#define AUDBUF_CFG1		0x0704
#define AUDBUF_CFG2		0x0706
#define IBIASDIST_CFG0		0x070c
#define AUDCLKGEN_CFG0		0x0712
#define AUDLDO_CFG0		0x0714
#define AUDNVREGGLB_CFG0	0x0718
#define AUD_NCP0		0x071a
#define ZCD_CON0		0x0738
#define ZCD_CON2		0x073c
#define NCP_CLKDIV_CON0		0x0744
#define NCP_CLKDIV_CON1		0x0746

#define SPEAKER_EN_GPIO		73
#define BUFFER_BYTES		16384
#define RATE_HZ			48000

struct b8k_audio {
	struct device *dev;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	void __iomem *afe;
	void __iomem *infra;
	struct regmap *pmic;
	struct gpio_desc *speaker_enable;
	struct hrtimer timer;
	spinlock_t lock;
	u32 last_period;
	unsigned int volume;
	bool running;
	bool amp_on;
	bool infra_was_off;
};

static struct platform_device *b8k_pdev;

static struct gpiod_lookup_table b8k_gpio_table = {
	.dev_id = DRV_NAME,
	.table = {
		GPIO_LOOKUP("pinctrl_paris", SPEAKER_EN_GPIO, "speaker-enable",
			    GPIO_ACTIVE_HIGH),
		{ }
	},
};

static const struct snd_pcm_hardware b8k_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = RATE_HZ,
	.rate_max = RATE_HZ,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES,
	.period_bytes_min = 256,
	.period_bytes_max = 4096,
	.periods_min = 2,
	.periods_max = 64,
};

static inline void afe_update(struct b8k_audio *a, u32 reg, u32 val, u32 mask)
{
	u32 old = readl(a->afe + reg);

	writel((old & ~mask) | (val & mask), a->afe + reg);
}

static int ana_write(struct b8k_audio *a, unsigned int reg, unsigned int val)
{
	return regmap_write(a->pmic, reg, val);
}

static int ana_update(struct b8k_audio *a, unsigned int reg,
		      unsigned int val, unsigned int mask)
{
	return regmap_update_bits(a->pmic, reg, mask, val);
}

/*
 * HP output muxes in AUDBUF_CFG0: right [12:9], left [8:5]; 4'b0100 selects
 * the audio DAC (stock HAL AudioMachineDevice::AnalogSetMux, MUX_AUDIO).
 * Without it the DAC runs but the HP buffers, which feed the SN2145B, stay
 * disconnected.
 */
#define HP_MUX_MASK		0x1fe0
static unsigned int hp_mux = 0x0880;
module_param(hp_mux, uint, 0644);
MODULE_PARM_DESC(hp_mux, "AUDBUF_CFG0 HP mux bits (0x0880 = audio DAC)");

/* Values used by the stock HAL for the headset DAC path used by speaker. */
static const u16 hp_gain_table[] = {
	0x0f9f, 0x0e0e, 0x0d0d, 0x0c0c, 0x0909, 0x0606, 0x0303, 0x0000,
};

static int b8k_analog_on(struct b8k_audio *a)
{
	int ret;

	/* Digital PMIC downlink, exact 48 kHz stock-HAL sequence. */
	ret = ana_write(a, TOP_CKPDN_CLR, 0x0003);
	if (ret)
		return ret;
	ana_write(a, AFUNC_AUD_CON2, 0x0006);
	ana_write(a, AFUNC_AUD_CON0, 0xc3a1);
	ana_write(a, AFUNC_AUD_CON2, 0x0003);
	ana_write(a, AFUNC_AUD_CON2, 0x000b);
	ana_write(a, AFE_DL_SDM_CON1, 0x001e);
	ana_write(a, AFE_DL_SRC2_CON0_H, 0x8300);
	ana_write(a, AFE_UL_DL_CON0, 0x007f);
	ana_write(a, AFE_DL_SRC2_CON0_L, 0x1801);
	ana_write(a, AFE_DL_SRC2_CON1_H, 0x0000);
	ana_write(a, ANA_AFE_I2S_CON1, 0x00e1);
	ana_write(a, ANA_AFE_TOP_CON0, 0x0000);
	ana_write(a, AFE_I2S_FIFO_DL_CFG0, 0x004f);

	/* Cap-less HP/line path; keep gain at the quietest table entry. */
	ana_update(a, AFUNC_AUD_CON2, 0x0080, 0x0080);
	ana_write(a, AUDLDO_CFG0, 0x0d92);
	ana_write(a, AUDNVREGGLB_CFG0, 0x000c);
	ana_write(a, AUD_NCP0, 0xe000);
	ana_write(a, NCP_CLKDIV_CON0, 0x102b);
	ana_write(a, NCP_CLKDIV_CON1, 0x0000);
	usleep_range(1000, 1500);
	ana_write(a, AUDBUF_CFG0, 0x0008);
	ana_write(a, ZCD_CON0, 0x0101);
	ana_write(a, IBIASDIST_CFG0, 0x0552);
	ana_write(a, AUDBUF_CFG1, 0x0900);
	ana_write(a, AUDBUF_CFG2, 0x0082);
	ana_write(a, AUDBUF_CFG0, 0x0009);
	usleep_range(1000, 1500);
	ana_write(a, AUDBUF_CFG1, 0x0940);
	ana_write(a, AUDBUF_CFG0, 0x000f);
	usleep_range(1000, 1500);
	ana_write(a, AUDBUF_CFG1, 0x0100);
	ana_write(a, AUDBUF_CFG2, 0x0082);
	ana_write(a, ZCD_CON2, hp_gain_table[a->volume]);
	ana_write(a, AUDCLKGEN_CFG0, 0x0001);
	ana_write(a, AUDDAC_CON0, 0x000f);
	usleep_range(1000, 1500);
	ana_update(a, AUDBUF_CFG0, 0x0006, 0x0007);
	ana_update(a, AUDBUF_CFG0, hp_mux & HP_MUX_MASK, HP_MUX_MASK);
	ana_update(a, AFUNC_AUD_CON2, 0x0000, 0x0080);

	gpiod_set_value(a->speaker_enable, 1);
	msleep(40);
	a->amp_on = true;
	return 0;
}

static void b8k_analog_off(struct b8k_audio *a)
{
	if (a->amp_on) {
		gpiod_set_value(a->speaker_enable, 0);
		a->amp_on = false;
	}
	ana_write(a, AUDDAC_CON0, 0x0000);
	ana_write(a, AUDCLKGEN_CFG0, 0x0000);
	ana_write(a, AUDBUF_CFG0, 0x0000);
	ana_write(a, AUD_NCP0, 0x8000);
	ana_write(a, AUDNVREGGLB_CFG0, 0x0006);
	ana_write(a, AUDLDO_CFG0, 0x0192);
	ana_write(a, AFE_UL_DL_CON0, 0x0000);
}

static int b8k_hw_start(struct b8k_audio *a, struct snd_pcm_runtime *rt)
{
	int ret;

	a->infra_was_off = !!(readl(a->infra + INFRA_PDN_STA) & INFRA_AUDIO_BIT);
	writel(INFRA_AUDIO_BIT, a->infra + INFRA_PDN_CLR);

	/* Vendor clock manager clears bit 2 (AFE gate); bit 14 stays set. */
	afe_update(a, AUDIO_AFE_TOP_CON0, BIT(14), BIT(14) | BIT(2));
	writel(lower_32_bits(rt->dma_addr), a->afe + AFE_DL1_BASE);
	writel(lower_32_bits(rt->dma_addr + snd_pcm_lib_buffer_bytes(a->substream) - 1),
	       a->afe + AFE_DL1_END);
	/* DL1 L/R (I05/I06) to PMIC DAC L/R (O03/O04). */
	afe_update(a, AFE_CONN1, BIT(21), BIT(21));
	afe_update(a, AFE_CONN2, BIT(6), BIT(6));
	afe_update(a, AFE_DAC_CON1, 10, 0xf); /* 48 kHz */
	afe_update(a, AFE_I2S_CON1, (10 << 8) | 1, 0xf01);

	ret = b8k_analog_on(a);
	if (ret) {
		if (a->infra_was_off)
			writel(INFRA_AUDIO_BIT, a->infra + INFRA_PDN_SET);
		return ret;
	}
	afe_update(a, AFE_DAC_CON0, BIT(0) | BIT(1), BIT(0) | BIT(1));
	return 0;
}

static void b8k_hw_stop(struct b8k_audio *a)
{
	afe_update(a, AFE_DAC_CON0, 0, BIT(0) | BIT(1));
	b8k_analog_off(a);
	afe_update(a, AFE_CONN1, 0, BIT(21));
	afe_update(a, AFE_CONN2, 0, BIT(6));
	if (a->infra_was_off)
		writel(INFRA_AUDIO_BIT, a->infra + INFRA_PDN_SET);
}

static enum hrtimer_restart b8k_timer(struct hrtimer *timer)
{
	struct b8k_audio *a = container_of(timer, struct b8k_audio, timer);
	struct snd_pcm_substream *s;
	unsigned long flags;
	u32 cur, base, off, period;
	bool elapsed = false;

	spin_lock_irqsave(&a->lock, flags);
	s = a->substream;
	if (!a->running || !s) {
		spin_unlock_irqrestore(&a->lock, flags);
		return HRTIMER_NORESTART;
	}
	base = lower_32_bits(s->runtime->dma_addr);
	cur = readl(a->afe + AFE_DL1_CUR);
	off = (cur >= base && cur < base + snd_pcm_lib_buffer_bytes(s)) ?
		cur - base : 0;
	period = off / snd_pcm_lib_period_bytes(s);
	if (period != a->last_period) {
		a->last_period = period;
		elapsed = true;
	}
	hrtimer_forward_now(timer, ms_to_ktime(2));
	spin_unlock_irqrestore(&a->lock, flags);
	if (elapsed)
		snd_pcm_period_elapsed(s);
	return HRTIMER_RESTART;
}

static int b8k_pcm_open(struct snd_pcm_substream *substream)
{
	struct b8k_audio *a = snd_pcm_substream_chip(substream);

	substream->runtime->hw = b8k_pcm_hw;
	a->substream = substream;
	return 0;
}

static int b8k_pcm_close(struct snd_pcm_substream *substream)
{
	struct b8k_audio *a = snd_pcm_substream_chip(substream);

	a->substream = NULL;
	return 0;
}

static int b8k_pcm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static int b8k_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int b8k_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct b8k_audio *a = snd_pcm_substream_chip(substream);

	memset(substream->runtime->dma_area, 0, substream->runtime->dma_bytes);
	a->last_period = 0;
	return 0;
}

static int b8k_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct b8k_audio *a = snd_pcm_substream_chip(substream);
	unsigned long flags;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = b8k_hw_start(a, substream->runtime);
		if (ret)
			return ret;
		spin_lock_irqsave(&a->lock, flags);
		a->running = true;
		spin_unlock_irqrestore(&a->lock, flags);
		hrtimer_start(&a->timer, ms_to_ktime(2), HRTIMER_MODE_REL);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		spin_lock_irqsave(&a->lock, flags);
		a->running = false;
		spin_unlock_irqrestore(&a->lock, flags);
		hrtimer_cancel(&a->timer);
		b8k_hw_stop(a);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static snd_pcm_uframes_t b8k_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct b8k_audio *a = snd_pcm_substream_chip(substream);
	u32 base = lower_32_bits(substream->runtime->dma_addr);
	u32 cur = readl(a->afe + AFE_DL1_CUR);

	if (cur < base || cur >= base + snd_pcm_lib_buffer_bytes(substream))
		return 0;
	return bytes_to_frames(substream->runtime, cur - base);
}

static const struct snd_pcm_ops b8k_pcm_ops = {
	.open = b8k_pcm_open,
	.close = b8k_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = b8k_pcm_hw_params,
	.hw_free = b8k_pcm_hw_free,
	.prepare = b8k_pcm_prepare,
	.trigger = b8k_pcm_trigger,
	.pointer = b8k_pcm_pointer,
	.mmap = snd_pcm_lib_default_mmap,
};

static int b8k_volume_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = ARRAY_SIZE(hp_gain_table) - 1;
	return 0;
}

static int b8k_volume_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct b8k_audio *a = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = a->volume;
	return 0;
}

static int b8k_volume_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct b8k_audio *a = snd_kcontrol_chip(kcontrol);
	unsigned int v = clamp_val(ucontrol->value.integer.value[0], 0,
				   ARRAY_SIZE(hp_gain_table) - 1);
	int changed = v != a->volume;

	a->volume = v;
	if (a->running)
		ana_write(a, ZCD_CON2, hp_gain_table[v]);
	return changed;
}

static const struct snd_kcontrol_new b8k_volume_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Speaker Playback Volume",
	.info = b8k_volume_info,
	.get = b8k_volume_get,
	.put = b8k_volume_put,
};

static int b8k_audio_probe(struct platform_device *pdev)
{
	struct b8k_audio *a;
	struct device *pwrap;
	int ret;

	a = devm_kzalloc(&pdev->dev, sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;
	a->dev = &pdev->dev;
	/* Full analog gain; PipeWire/PulseAudio scales in software. */
	a->volume = ARRAY_SIZE(hp_gain_table) - 1;
	spin_lock_init(&a->lock);
	hrtimer_setup(&a->timer, b8k_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	a->afe = devm_ioremap(&pdev->dev, AFE_PHYS, AFE_SIZE);
	if (!a->afe)
		return -ENOMEM;
	a->infra = devm_ioremap(&pdev->dev, INFRACFG_PHYS, INFRACFG_SIZE);
	if (!a->infra)
		return -ENOMEM;
	pwrap = bus_find_device_by_name(&platform_bus_type, NULL, "1000f000.pwrap");
	if (!pwrap)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "cannot find pwrap device\n");
	a->pmic = dev_get_regmap(pwrap, NULL);
	put_device(pwrap);
	if (!a->pmic)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "cannot get pwrap regmap\n");
	a->speaker_enable = devm_gpiod_get(&pdev->dev, "speaker-enable",
					  GPIOD_OUT_LOW);
	if (IS_ERR(a->speaker_enable))
		return dev_err_probe(&pdev->dev, PTR_ERR(a->speaker_enable),
				     "cannot request speaker enable GPIO\n");

	ret = snd_card_new(&pdev->dev, -1, "B8000", THIS_MODULE, 0, &a->card);
	if (ret)
		return ret;
	strscpy(a->card->driver, "mt6589-audio", sizeof(a->card->driver));
	strscpy(a->card->shortname, "Lenovo B8000-H", sizeof(a->card->shortname));
	strscpy(a->card->longname, "MT6589 AFE + MT6320 + SN2145B",
		sizeof(a->card->longname));
	ret = snd_pcm_new(a->card, "MT6589 DL1", 0, 1, 0, &a->pcm);
	if (ret)
		goto err_card;
	a->pcm->private_data = a;
	strscpy(a->pcm->name, "MT6589 DL1", sizeof(a->pcm->name));
	snd_pcm_set_ops(a->pcm, SNDRV_PCM_STREAM_PLAYBACK, &b8k_pcm_ops);
	snd_pcm_set_managed_buffer_all(a->pcm, SNDRV_DMA_TYPE_DEV,
				       &pdev->dev, BUFFER_BYTES, BUFFER_BYTES);
	ret = snd_ctl_add(a->card, snd_ctl_new1(&b8k_volume_control, a));
	if (ret)
		goto err_card;
	ret = snd_card_register(a->card);
	if (ret)
		goto err_card;
	platform_set_drvdata(pdev, a);
	dev_info(&pdev->dev, "ALSA playback card registered; amplifier remains off\n");
	return 0;

err_card:
	snd_card_free(a->card);
	return ret;
}

static void b8k_audio_remove(struct platform_device *pdev)
{
	struct b8k_audio *a = platform_get_drvdata(pdev);

	hrtimer_cancel(&a->timer);
	if (a->running)
		b8k_hw_stop(a);
	snd_card_free(a->card);
}

static struct platform_driver b8k_audio_driver = {
	.probe = b8k_audio_probe,
	.remove = b8k_audio_remove,
	.driver = { .name = DRV_NAME },
};

static int __init b8k_audio_init(void)
{
	int ret;

	gpiod_add_lookup_table(&b8k_gpio_table);
	ret = platform_driver_register(&b8k_audio_driver);
	if (ret) {
		gpiod_remove_lookup_table(&b8k_gpio_table);
		return ret;
	}
	b8k_pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(b8k_pdev)) {
		ret = PTR_ERR(b8k_pdev);
		platform_driver_unregister(&b8k_audio_driver);
		gpiod_remove_lookup_table(&b8k_gpio_table);
		return ret;
	}
	return 0;
}

static void __exit b8k_audio_exit(void)
{
	platform_device_unregister(b8k_pdev);
	platform_driver_unregister(&b8k_audio_driver);
	gpiod_remove_lookup_table(&b8k_gpio_table);
}

module_init(b8k_audio_init);
module_exit(b8k_audio_exit);

MODULE_DESCRIPTION("Lenovo B8000-H MT6589/MT6320 minimal ALSA playback");
MODULE_AUTHOR("kz-creator");
MODULE_LICENSE("GPL");
