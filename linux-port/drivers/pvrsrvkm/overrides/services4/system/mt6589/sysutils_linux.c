/* SPDX-License-Identifier: GPL-2.0 */
/* Linux 7.x DT/CCF/runtime-PM platform glue for the MT6589 SGX544. */
#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <dt-bindings/clock/mediatek,mt6589-clk.h>

#include "services_headers.h"
#include "sgxapi_km.h"
#include "sgxinfokm.h"
#include "sysconfig.h"
#include "sysinfo.h"
#include "syslocal.h"

#define MT6589_SGX_CLOCK_HZ 286000000U
#define MT6589_SPM_BASE     0x10006000
#define MT6589_MFGSYS_BASE  0x10206000
#define MT6589_WAY_EN       0x10001200
#define MT6589_MAP_SIZE     0x1000
#define MFG_PWR_CON         0x214
#define MFG_CG_SET          0x004
#define MFG_CG_CLR          0x008
#define MFG_RST             0x00c
#define MFG_WAY_EN_BIT      0x400

extern struct platform_device *gpsPVRLDMDev;

static struct clk_bulk_data *g_clks;
static int g_num_clks;
static struct clk *g_hyd_clk;
static struct clk *g_infra_axi_clk;
static struct clk *g_smi_clk;
static bool g_runtime_prepared;

static PVRSRV_ERROR PowerLockWrap(SYS_SPECIFIC_DATA *data, IMG_BOOL trylock)
{
	if (in_interrupt())
		return PVRSRV_OK;
	if (trylock) {
		if (!mutex_trylock(&data->sPowerLock))
			return PVRSRV_ERROR_RETRY;
	} else {
		mutex_lock(&data->sPowerLock);
	}
	return PVRSRV_OK;
}

static IMG_VOID PowerLockUnwrap(SYS_SPECIFIC_DATA *data)
{
	if (!in_interrupt())
		mutex_unlock(&data->sPowerLock);
}

PVRSRV_ERROR SysPowerLockWrap(IMG_BOOL trylock)
{
	SYS_DATA *sysdata;

	SysAcquireData(&sysdata);
	return PowerLockWrap(sysdata->pvSysSpecificData, trylock);
}

IMG_VOID SysPowerLockUnwrap(IMG_VOID)
{
	SYS_DATA *sysdata;

	SysAcquireData(&sysdata);
	PowerLockUnwrap(sysdata->pvSysSpecificData);
}

IMG_BOOL WrapSystemPowerChange(SYS_SPECIFIC_DATA *data)
{
	(void)data;
	return IMG_TRUE;
}

IMG_VOID UnwrapSystemPowerChange(SYS_SPECIFIC_DATA *data)
{
	(void)data;
}

#if defined(SGX_DYNAMIC_TIMING_INFO)
IMG_VOID SysGetSGXTimingInformation(SGX_TIMING_INFORMATION *timing)
{
	timing->ui32CoreClockSpeed = MT6589_SGX_CLOCK_HZ;
	timing->ui32HWRecoveryFreq = SYS_SGX_HWRECOVERY_TIMEOUT_FREQ;
	timing->ui32uKernelFreq = SYS_SGX_PDS_TIMER_FREQ;
	timing->bEnableActivePM = IMG_TRUE;
	timing->ui32ActivePowManLatencyms = SYS_SGX_ACTIVE_POWER_LATENCY_MS;
}
#endif

static int get_extra_clock(const char *compatible, unsigned int id,
			   struct clk **clock)
{
	struct of_phandle_args args = { .args_count = 1 };
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, compatible);
	if (!node)
		return -ENODEV;
	args.np = node;
	args.args[0] = id;
	*clock = of_clk_get_from_provider(&args);
	of_node_put(node);
	return IS_ERR(*clock) ? PTR_ERR(*clock) : 0;
}

static int mt6589_mfg_reset(void)
{
	void __iomem *spm = ioremap(MT6589_SPM_BASE, MT6589_MAP_SIZE);
	void __iomem *mfg = ioremap(MT6589_MFGSYS_BASE, MT6589_MAP_SIZE);
	void __iomem *way = ioremap(MT6589_WAY_EN, sizeof(u32));
	u32 value;

	if (!spm || !mfg || !way) {
		if (way)
			iounmap(way);
		if (mfg)
			iounmap(mfg);
		if (spm)
			iounmap(spm);
		return -ENOMEM;
	}

	/* Exact vendor MFGReset sequence, using physical mappings. */
	writel(readl(way) & ~MFG_WAY_EN_BIT, way);
	value = (readl(spm + MFG_PWR_CON) & ~0x1) | 0x10;
	writel(value, spm + MFG_PWR_CON);
	writel(readl(spm + MFG_PWR_CON) | 0x2, spm + MFG_PWR_CON);
	udelay(1);
	writel(readl(spm + MFG_PWR_CON) & ~0x2, spm + MFG_PWR_CON);
	writel(readl(spm + MFG_PWR_CON) & ~0x10, spm + MFG_PWR_CON);
	writel(readl(spm + MFG_PWR_CON) | 0x1, spm + MFG_PWR_CON);
	udelay(1);

	writel(0x1, mfg + MFG_RST);
	writel(0xf, mfg + MFG_CG_CLR);
	udelay(10);
	writel(0xf, mfg + MFG_CG_SET);
	writel(0x0, mfg + MFG_RST);
	writel(0xf, mfg + MFG_CG_CLR);
	writel(readl(way) | MFG_WAY_EN_BIT, way);

	iounmap(way);
	iounmap(mfg);
	iounmap(spm);
	return 0;
}

PVRSRV_ERROR EnableSGXClocks(SYS_DATA *sysdata)
{
	SYS_SPECIFIC_DATA *data = sysdata->pvSysSpecificData;
	struct device *dev = &gpsPVRLDMDev->dev;
	void __iomem *spm;
	int ret;

	if (atomic_read(&data->sSGXClocksEnabled))
		return PVRSRV_OK;

	if (!g_runtime_prepared) {
		/* Reproduce the safe probe's first-cycle genpd sequencing. */
		pm_runtime_get_noresume(dev);
		pm_runtime_set_active(dev);
		pm_runtime_put_sync(dev);
		g_runtime_prepared = true;
	}
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK;

	spm = ioremap(MT6589_SPM_BASE, MT6589_MAP_SIZE);
	if (!spm) {
		pm_runtime_put_sync(dev);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	/* MT6589 has four SRAM_PDN bits; the generic genpd clears only bit 8. */
	writel(readl(spm + MFG_PWR_CON) & ~0xf00, spm + MFG_PWR_CON);
	udelay(50);
	iounmap(spm);

	ret = clk_bulk_prepare_enable(g_num_clks, g_clks);
	if (!ret)
		ret = clk_prepare_enable(g_hyd_clk);
	if (ret)
		goto err_clocks;

	ret = mt6589_mfg_reset();
	if (ret)
		goto err_reset;

	SysEnableSGXInterrupts(sysdata);
	atomic_set(&data->sSGXClocksEnabled, 1);
	return PVRSRV_OK;

err_reset:
	clk_disable_unprepare(g_hyd_clk);
err_clocks:
	clk_bulk_disable_unprepare(g_num_clks, g_clks);
	pm_runtime_put_sync(dev);
	return PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK;
}

IMG_VOID DisableSGXClocks(SYS_DATA *sysdata)
{
	SYS_SPECIFIC_DATA *data = sysdata->pvSysSpecificData;

	if (!atomic_read(&data->sSGXClocksEnabled))
		return;
	SysDisableSGXInterrupts(sysdata);
	clk_disable_unprepare(g_hyd_clk);
	clk_bulk_disable_unprepare(g_num_clks, g_clks);
	pm_runtime_put_sync(&gpsPVRLDMDev->dev);
	atomic_set(&data->sSGXClocksEnabled, 0);
}

PVRSRV_ERROR EnableSystemClocks(SYS_DATA *sysdata)
{
	SYS_SPECIFIC_DATA *data = sysdata->pvSysSpecificData;

	if (!data->bSysClocksOneTimeInit) {
		mutex_init(&data->sPowerLock);
		atomic_set(&data->sSGXClocksEnabled, 0);
		data->bSysClocksOneTimeInit = IMG_TRUE;
	}
	return PVRSRV_OK;
}

IMG_VOID DisableSystemClocks(SYS_DATA *sysdata)
{
	DisableSGXClocks(sysdata);
}

PVRSRV_ERROR SysPMRuntimeRegister(void)
{
	struct device *dev = &gpsPVRLDMDev->dev;
	int ret;

	g_num_clks = devm_clk_bulk_get_all(dev, &g_clks);
	if (g_num_clks < 0)
		return PVRSRV_ERROR_CLOCK_REQUEST_FAILED;
	/*
	 * mfg_mem hangs off smi_sel, which also feeds the display SMI
	 * clocks and infra_smi.  Kernel #20 has no display driver holding
	 * them (the LK pipeline runs under simpledrm), so our first
	 * enable/disable cycle gated smi_sel and blanked the screen.  Keep
	 * a reference for as long as the module is loaded.
	 */
	ret = get_extra_clock("mediatek,mt6589-topckgen", CLK_TOP_MUX_SMI,
			      &g_smi_clk);
	if (ret)
		return PVRSRV_ERROR_CLOCK_REQUEST_FAILED;
	ret = clk_prepare_enable(g_smi_clk);
	if (ret)
		goto err_put_smi;
	ret = get_extra_clock("mediatek,mt6589-mfgsys", CLK_MFG_HYD,
			      &g_hyd_clk);
	if (ret)
		goto err_disable_smi;
	ret = get_extra_clock("mediatek,mt6589-infracfg", CLK_INFRA_MFGAXI,
			      &g_infra_axi_clk);
	if (ret)
		goto err_put_hyd;
	ret = clk_prepare_enable(g_infra_axi_clk);
	if (ret)
		goto err_put_axi;
	pm_runtime_enable(dev);
	return PVRSRV_OK;

err_put_axi:
	clk_put(g_infra_axi_clk);
	g_infra_axi_clk = NULL;
err_put_hyd:
	clk_put(g_hyd_clk);
	g_hyd_clk = NULL;
err_disable_smi:
	clk_disable_unprepare(g_smi_clk);
err_put_smi:
	clk_put(g_smi_clk);
	g_smi_clk = NULL;
	return PVRSRV_ERROR_CLOCK_REQUEST_FAILED;
}

PVRSRV_ERROR SysPMRuntimeUnregister(void)
{
	pm_runtime_disable(&gpsPVRLDMDev->dev);
	if (g_infra_axi_clk) {
		clk_disable_unprepare(g_infra_axi_clk);
		clk_put(g_infra_axi_clk);
		g_infra_axi_clk = NULL;
	}
	/*
	 * Deliberately leave smi_sel enabled: dropping the last reference
	 * would gate it and blank the LK display pipeline again.
	 */
	if (g_smi_clk) {
		clk_put(g_smi_clk);
		g_smi_clk = NULL;
	}
	if (g_hyd_clk) {
		clk_put(g_hyd_clk);
		g_hyd_clk = NULL;
	}
	g_runtime_prepared = false;
	return PVRSRV_OK;
}
