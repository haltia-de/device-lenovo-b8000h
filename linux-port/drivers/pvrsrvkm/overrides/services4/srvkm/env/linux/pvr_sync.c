/* SPDX-License-Identifier: MIT OR GPL-2.0 */
/*
 * Build-time compatibility for the Android JB sync bridge.  Linux removed
 * sync_timeline/sync_pt; keep the DDK 1.12 bridge layout and symbols intact
 * while refusing fence-backed operations until a dma_fence port is supplied.
 */
#include "services_headers.h"
#include "pvr_sync.h"

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
	return PVRSRV_ERROR_NOT_SUPPORTED;
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
	return PVRSRV_ERROR_NOT_SUPPORTED;
}

