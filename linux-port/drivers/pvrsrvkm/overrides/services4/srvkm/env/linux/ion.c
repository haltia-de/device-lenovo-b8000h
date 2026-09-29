/* SPDX-License-Identifier: MIT OR GPL-2.0 */
/*
 * DDK 1.12 ION import on Linux 7.x without in-kernel ION.
 *
 * The stock gralloc passes ION share fds; with ion_legacy.ko those are
 * dma-bufs.  Import them through the dma-buf API against the SGX device and
 * hand Services the same per-page physical address array and "unique"
 * value (first page address) as the MTK ion.c did.  The ion_client handle
 * from osperproc.c is unused here.
 */
#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/ion_drv.h>

#include "ion.h"

MODULE_IMPORT_NS("DMA_BUF");

#define MAX_IMPORT_ION_FDS 3

extern struct platform_device *gpsPVRLDMDev;

static struct ion_device gsIonCompatDevice;
struct ion_device *gpsIonDev;

typedef struct _ION_IMPORT_DATA_
{
	IMG_UINT32 ui32NumBufs;
	struct dma_buf *apsBuf[MAX_IMPORT_ION_FDS];
	struct dma_buf_attachment *apsAttach[MAX_IMPORT_ION_FDS];
	struct sg_table *apsSgt[MAX_IMPORT_ION_FDS];
	IMG_SYS_PHYADDR *psSysPhysAddr;
} ION_IMPORT_DATA;

PVRSRV_ERROR IonInit(IMG_VOID)
{
	gpsIonDev = &gsIonCompatDevice;
	return PVRSRV_OK;
}

IMG_VOID IonDeinit(IMG_VOID)
{
	gpsIonDev = IMG_NULL;
}

static void IonReleaseImport(ION_IMPORT_DATA *psImportData)
{
	IMG_UINT32 i;

	for (i = 0; i < psImportData->ui32NumBufs; i++)
	{
		if (psImportData->apsSgt[i])
			dma_buf_unmap_attachment_unlocked(psImportData->apsAttach[i],
				psImportData->apsSgt[i], DMA_BIDIRECTIONAL);
		if (psImportData->apsAttach[i])
			dma_buf_detach(psImportData->apsBuf[i],
				psImportData->apsAttach[i]);
		dma_buf_put(psImportData->apsBuf[i]);
	}
	kfree(psImportData->psSysPhysAddr);
	kfree(psImportData);
}

PVRSRV_ERROR IonImportBufferAndAcquirePhysAddr(IMG_HANDLE hIonDev,
	IMG_UINT32 ui32NumFDs, IMG_INT32 *pi32BufferFDs,
	IMG_UINT32 *pui32PageCount, IMG_SYS_PHYADDR **ppsSysPhysAddr,
	IMG_PVOID *ppvKernAddr0, IMG_HANDLE *phPriv, IMG_HANDLE *phUnique)
{
	ION_IMPORT_DATA *psImportData;
	struct scatterlist *sg;
	IMG_UINT32 i, j, k, ui32PageCount = 0;
	PVRSRV_ERROR eError;

	(void)hIonDev;

	if (ui32NumFDs == 0 || ui32NumFDs > MAX_IMPORT_ION_FDS)
	{
		printk(KERN_ERR "%s: %u ion fds passed (1..%d supported)\n",
		       __func__, ui32NumFDs, MAX_IMPORT_ION_FDS);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psImportData = kzalloc(sizeof(*psImportData), GFP_KERNEL);
	if (!psImportData)
		return PVRSRV_ERROR_OUT_OF_MEMORY;

	for (i = 0; i < ui32NumFDs; i++)
	{
		struct dma_buf *psBuf = dma_buf_get(pi32BufferFDs[i]);
		struct dma_buf_attachment *psAttach;
		struct sg_table *psSgt;

		if (IS_ERR(psBuf))
		{
			eError = PVRSRV_ERROR_BAD_MAPPING;
			goto err_release;
		}
		psImportData->apsBuf[i] = psBuf;
		psImportData->ui32NumBufs = i + 1;

		psAttach = dma_buf_attach(psBuf, &gpsPVRLDMDev->dev);
		if (IS_ERR(psAttach))
		{
			eError = PVRSRV_ERROR_BAD_MAPPING;
			goto err_release;
		}
		psImportData->apsAttach[i] = psAttach;

		psSgt = dma_buf_map_attachment_unlocked(psAttach,
			DMA_BIDIRECTIONAL);
		if (IS_ERR(psSgt))
		{
			eError = PVRSRV_ERROR_BAD_MAPPING;
			goto err_release;
		}
		psImportData->apsSgt[i] = psSgt;

		for_each_sgtable_sg(psSgt, sg, j)
		{
			if (sg->offset || (sg->length & ~PAGE_MASK))
			{
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto err_release;
			}
			ui32PageCount += sg->length >> PAGE_SHIFT;
		}
	}

	if (ui32PageCount == 0)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_release;
	}

	psImportData->psSysPhysAddr = kmalloc_array(ui32PageCount,
		sizeof(IMG_SYS_PHYADDR), GFP_KERNEL);
	if (!psImportData->psSysPhysAddr)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_release;
	}

	/* The SGX has its own MMU: it wants CPU physical page addresses. */
	k = 0;
	for (i = 0; i < ui32NumFDs; i++)
	{
		for_each_sgtable_sg(psImportData->apsSgt[i], sg, j)
		{
			IMG_UINT32 off;

			for (off = 0; off < sg->length; off += PAGE_SIZE)
				psImportData->psSysPhysAddr[k++].uiAddr =
					sg_phys(sg) + off;
		}
	}

	*pui32PageCount = ui32PageCount;
	*ppsSysPhysAddr = psImportData->psSysPhysAddr;
	*ppvKernAddr0 = IMG_NULL;
	*phPriv = psImportData;
	*phUnique = (IMG_HANDLE)(uintptr_t)psImportData->psSysPhysAddr[0].uiAddr;
	return PVRSRV_OK;

err_release:
	IonReleaseImport(psImportData);
	return eError;
}

IMG_VOID IonUnimportBufferAndReleasePhysAddr(IMG_HANDLE hPriv)
{
	if (hPriv)
		IonReleaseImport(hPriv);
}
