/* SPDX-License-Identifier: MIT OR GPL-2.0 */
/*
 * Build-time compatibility for the Android JB sync bridge.  Linux removed
 * sync_timeline/sync_pt; keep the DDK 1.12 bridge layout and symbols intact
 * while refusing fence-backed operations until a dma_fence port is supplied.
 *
 * Kicks without fences must still work: like the MTK pvr_sync.c, negative
 * fence fds are skipped without error and "no real syncs" is success.  A
 * real fence fd cannot be resolved here and fails as a failed lookup would.
 */
#include "services_headers.h"
#include "pvr_sync.h"

/* Returns IMG_TRUE when every entry is "no fence" (fd < 0). */
static IMG_BOOL PVRSyncOnlyNullFences(const int *aiFenceFds, IMG_UINT32 ui32Num)
{
	IMG_UINT32 i;

	for (i = 0; i < ui32Num; i++)
	{
		if (aiFenceFds[i] >= 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: fence fd=%d unsupported (no "
					 "Android sync framework)", __func__, aiFenceFds[i]));
			return IMG_FALSE;
		}
	}
	return IMG_TRUE;
}

int PVRSyncDeviceInit(void)
{
	return 0;
}

void PVRSyncDeviceDeInit(void)
{
}

void PVRSyncUpdateAllSyncs(void)
{
}

PVRSRV_ERROR PVRSyncPatchCCBKickSyncInfos(
	IMG_HANDLE ahSyncs[SGX_MAX_SRC_SYNCS_TA],
	PVRSRV_DEVICE_SYNC_OBJECT asDevSyncs[SGX_MAX_SRC_SYNCS_TA],
	IMG_UINT32 *pui32NumSrcSyncs)
{
	(void)asDevSyncs;
	if (*pui32NumSrcSyncs > SGX_MAX_SRC_SYNCS_TA ||
	    !PVRSyncOnlyNullFences((int *)ahSyncs, *pui32NumSrcSyncs))
		return PVRSRV_ERROR_HANDLE_NOT_FOUND;
	*pui32NumSrcSyncs = 0;
	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSyncPatchTransferSyncInfos(
	IMG_HANDLE ahSyncs[SGX_MAX_SRC_SYNCS_TA],
	PVRSRV_DEVICE_SYNC_OBJECT asDevSyncs[SGX_MAX_SRC_SYNCS_TA],
	IMG_UINT32 *pui32NumSrcSyncs)
{
	return PVRSRV_ERROR_NOT_SUPPORTED;
}

PVRSRV_ERROR PVRSyncFencesToSyncInfos(PVRSRV_KERNEL_SYNC_INFO *apsSyncs[],
	IMG_UINT32 *pui32NumSyncs,
	struct sync_fence *apsFence[SGX_MAX_SRC_SYNCS_TA])
{
	memset(apsFence, 0, sizeof(struct sync_fence *) * SGX_MAX_SRC_SYNCS_TA);
	if (*pui32NumSyncs > SGX_MAX_SRC_SYNCS_TA ||
	    !PVRSyncOnlyNullFences((int *)apsSyncs, *pui32NumSyncs))
		return PVRSRV_ERROR_HANDLE_NOT_FOUND;
	*pui32NumSyncs = 0;
	return PVRSRV_OK;
}

