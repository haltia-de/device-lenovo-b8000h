/* SPDX-License-Identifier: MIT OR GPL-2.0 */
/* The clock is fixed at 286 MHz by DT/CCF; legacy MTK DVFS is not used. */
#include "mtk_sysfreq.h"

PVRSRV_ERROR MTKSetFreqInfo(unsigned int freq, unsigned int tbltype)
{
	(void)freq;
	(void)tbltype;
	return PVRSRV_OK;
}

PVRSRV_ERROR MtkInitSetFreq(void)
{
	return PVRSRV_OK;
}

void MtkInitSetFreqTbl(unsigned int tbltype)
{
	(void)tbltype;
}

void MtkSetKeepFreq(void)
{
}

int get_gpu_power_src(void)
{
	return 0;
}
