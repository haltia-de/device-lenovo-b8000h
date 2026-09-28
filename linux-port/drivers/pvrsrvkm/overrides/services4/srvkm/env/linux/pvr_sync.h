/* SPDX-License-Identifier: MIT OR GPL-2.0 */
#ifndef _PVR_SYNC_H
#define _PVR_SYNC_H

#include "pvr_sync_user.h"
#include "servicesint.h"

/* Android's pre-dma_fence sync_timeline API was removed from Linux. */
struct sync_fence;

int PVRSyncDeviceInit(void);
void PVRSyncDeviceDeInit(void);
void PVRSyncUpdateAllSyncs(void);
PVRSRV_ERROR PVRSyncPatchCCBKickSyncInfos(
	IMG_HANDLE ahSyncs[SGX_MAX_SRC_SYNCS_TA],
	PVRSRV_DEVICE_SYNC_OBJECT asDevSyncs[SGX_MAX_SRC_SYNCS_TA],
	IMG_UINT32 *pui32NumSrcSyncs);
PVRSRV_ERROR PVRSyncPatchTransferSyncInfos(
	IMG_HANDLE ahSyncs[SGX_MAX_SRC_SYNCS_TA],
	PVRSRV_DEVICE_SYNC_OBJECT asDevSyncs[SGX_MAX_SRC_SYNCS_TA],
	IMG_UINT32 *pui32NumSrcSyncs);
PVRSRV_ERROR PVRSyncFencesToSyncInfos(PVRSRV_KERNEL_SYNC_INFO *apsSyncs[],
	IMG_UINT32 *pui32NumSyncs,
	struct sync_fence *apsFence[SGX_MAX_SRC_SYNCS_TA]);

#endif

