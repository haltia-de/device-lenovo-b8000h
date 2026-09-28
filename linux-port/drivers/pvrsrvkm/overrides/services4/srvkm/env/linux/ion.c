/* SPDX-License-Identifier: MIT OR GPL-2.0 */
/* Preserve the DDK 1.12 ION bridge ABI; Linux 7.x no longer provides ION. */
#include <linux/ion_drv.h>

#include "ion.h"

static struct ion_device gsIonCompatDevice;
struct ion_device *gpsIonDev;

PVRSRV_ERROR IonInit(IMG_VOID)
{
	gpsIonDev = &gsIonCompatDevice;
	return PVRSRV_OK;
}

IMG_VOID IonDeinit(IMG_VOID)
{
	gpsIonDev = IMG_NULL;
}

PVRSRV_ERROR IonImportBufferAndAcquirePhysAddr(IMG_HANDLE hIonDev,
	IMG_UINT32 ui32NumFDs, IMG_INT32 *pi32BufferFDs,
	IMG_UINT32 *pui32PageCount, IMG_SYS_PHYADDR **ppsSysPhysAddr,
	IMG_PVOID *ppvKernAddr0, IMG_HANDLE *phPriv, IMG_HANDLE *phUnique)
{
	return PVRSRV_ERROR_NOT_SUPPORTED;
}

IMG_VOID IonUnimportBufferAndReleasePhysAddr(IMG_HANDLE hPriv)
{
}
