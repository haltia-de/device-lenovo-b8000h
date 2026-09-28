/* SPDX-License-Identifier: MIT OR GPL-2.0 */
/* Android's legacy trace/events/gpu.h was removed; keep the DDK hooks inert. */
#include "systrace.h"

void SystraceCreateFS(void)
{
}

void SystraceDestroyFS(void)
{
}

IMG_BOOL SystraceIsCapturingHWData(void)
{
	return IMG_FALSE;
}

void SystraceTAKick(PVRSRV_SGXDEV_INFO *psDevInfo, IMG_UINT32 ui32FrameNum,
	IMG_UINT32 ui32RTData, IMG_BOOL bIsFirstKick)
{
	(void)psDevInfo;
	(void)ui32FrameNum;
	(void)ui32RTData;
	(void)bIsFirstKick;
}

void SystraceHWPerfPackets(PVRSRV_SGXDEV_INFO *psDevInfo,
	PVRSRV_SGX_HWPERF_CB_ENTRY *psSGXHWPerf, IMG_UINT32 ui32DataCount,
	IMG_UINT32 ui32SgxClockspeed)
{
	(void)psDevInfo;
	(void)psSGXHWPerf;
	(void)ui32DataCount;
	(void)ui32SgxClockspeed;
}
