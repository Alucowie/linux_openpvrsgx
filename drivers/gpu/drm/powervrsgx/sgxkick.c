/*************************************************************************/ /*!
@Title          Device specific kickTA routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <linux/stddef.h> /* For the macro offsetof() */
#include "services_headers.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "sgx_bridge_km.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "sgxutils.h"

/*!
******************************************************************************

 @Function	SGXDoKickKM

 @Description

 Really kicks the TA

 @Input hDevHandle - Device handle

 @Return ui32Error - success or failure

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR SGXDoKickKM(IMG_HANDLE hDevHandle, SGX_CCB_KICK *psCCBKick)
{
	PVRSRV_ERROR eError;
	PVRSRV_KERNEL_SYNC_INFO	*psSyncInfo;
	PVRSRV_KERNEL_MEM_INFO	*psCCBMemInfo = (PVRSRV_KERNEL_MEM_INFO *) psCCBKick->hCCBKernelMemInfo;
	SGXMKIF_CMDTA_SHARED *psTACmd;
	IMG_UINT32 i;
	IMG_HANDLE hDevMemContext = IMG_NULL;
	if (!CCB_OFFSET_IS_VALID(SGXMKIF_CMDTA_SHARED, psCCBMemInfo, psCCBKick, ui32CCBOffset))
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXDoKickKM: Invalid CCB offset"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	/* override QAC warning about stricter alignment */
	/* PRQA S 3305 1 */
	psTACmd = CCB_DATA_FROM_OFFSET(SGXMKIF_CMDTA_SHARED, psCCBMemInfo, psCCBKick, ui32CCBOffset);

	/* TA/3D dependency */
	if (psCCBKick->hTA3DSyncInfo)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->hTA3DSyncInfo;

		psTACmd->sTA3DDependency.sWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;

		psTACmd->sTA3DDependency.ui32WriteOpsPendingVal   = psSyncInfo->psSyncData->ui32WriteOpsPending;

		if (psCCBKick->bTADependency)
		{
			psSyncInfo->psSyncData->ui32WriteOpsPending++;
		}
	}

	if (psCCBKick->hTASyncInfo != IMG_NULL)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->hTASyncInfo;

		psTACmd->sTATQSyncReadOpsCompleteDevVAddr  = psSyncInfo->sReadOpsCompleteDevVAddr;
		psTACmd->sTATQSyncWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;

		psTACmd->ui32TATQSyncReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending++;
		psTACmd->ui32TATQSyncWriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending;
	}

	if (psCCBKick->h3DSyncInfo != IMG_NULL)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->h3DSyncInfo;

		psTACmd->s3DTQSyncReadOpsCompleteDevVAddr  = psSyncInfo->sReadOpsCompleteDevVAddr;
		psTACmd->s3DTQSyncWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;

		psTACmd->ui323DTQSyncReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending++;
		psTACmd->ui323DTQSyncWriteOpsPendingVal  = psSyncInfo->psSyncData->ui32WriteOpsPending;
	}

	psTACmd->ui32NumTAStatusVals = psCCBKick->ui32NumTAStatusVals;
	if (psCCBKick->ui32NumTAStatusVals != 0)
	{
		/* Copy status vals over */
		for (i = 0; i < psCCBKick->ui32NumTAStatusVals; i++)
		{
			psTACmd->sCtlTAStatusInfo[i] = psCCBKick->asTAStatusUpdate[i].sCtlStatus;
		}
	}

	psTACmd->ui32Num3DStatusVals = psCCBKick->ui32Num3DStatusVals;
	if (psCCBKick->ui32Num3DStatusVals != 0)
	{
		/* Copy status vals over */
		for (i = 0; i < psCCBKick->ui32Num3DStatusVals; i++)
		{
			psTACmd->sCtl3DStatusInfo[i] = psCCBKick->as3DStatusUpdate[i].sCtlStatus;
		}
	}


#if defined(SUPPORT_SGX_GENERALISED_SYNCOBJECTS)
	/* SRC and DST sync dependencies */
	psTACmd->ui32NumTASrcSyncs = psCCBKick->ui32NumTASrcSyncs;
	for (i=0; i<psCCBKick->ui32NumTASrcSyncs; i++)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahTASrcKernelSyncInfo[i];

		psTACmd->asTASrcSyncs[i].sWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTACmd->asTASrcSyncs[i].sReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;

		/* Get ui32ReadOpsPending snapshot and copy into the CCB - before incrementing. */
		psTACmd->asTASrcSyncs[i].ui32ReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending++;
		/* Copy ui32WriteOpsPending snapshot into the CCB. */
		psTACmd->asTASrcSyncs[i].ui32WriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending;
	}

	psTACmd->ui32NumTADstSyncs = psCCBKick->ui32NumTADstSyncs;
	for (i=0; i<psCCBKick->ui32NumTADstSyncs; i++)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahTADstKernelSyncInfo[i];

		psTACmd->asTADstSyncs[i].sWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTACmd->asTADstSyncs[i].sReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;

		/* Get ui32ReadOpsPending snapshot and copy into the CCB */
		psTACmd->asTADstSyncs[i].ui32ReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending;
		/* Copy ui32WriteOpsPending snapshot into the CCB - before incrementing */
		psTACmd->asTADstSyncs[i].ui32WriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending++;
	}

	psTACmd->ui32Num3DSrcSyncs = psCCBKick->ui32Num3DSrcSyncs;
	for (i=0; i<psCCBKick->ui32Num3DSrcSyncs; i++)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ah3DSrcKernelSyncInfo[i];

		psTACmd->as3DSrcSyncs[i].sWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTACmd->as3DSrcSyncs[i].sReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;

		/* Get ui32ReadOpsPending snapshot and copy into the CCB - before incrementing. */
		psTACmd->as3DSrcSyncs[i].ui32ReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending++;
		/* Copy ui32WriteOpsPending snapshot into the CCB. */
		psTACmd->as3DSrcSyncs[i].ui32WriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending;
	}
#else /* SUPPORT_SGX_GENERALISED_SYNCOBJECTS */
	/* texture dependencies */
	psTACmd->ui32NumSrcSyncs = psCCBKick->ui32NumSrcSyncs;
	for (i=0; i<psCCBKick->ui32NumSrcSyncs; i++)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahSrcKernelSyncInfo[i];

		psTACmd->asSrcSyncs[i].sWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTACmd->asSrcSyncs[i].sReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;

		/* Get ui32ReadOpsPending snapshot and copy into the CCB - before incrementing. */
		psTACmd->asSrcSyncs[i].ui32ReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending++;
		/* Copy ui32WriteOpsPending snapshot into the CCB. */
		psTACmd->asSrcSyncs[i].ui32WriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending;
	}
#endif/* SUPPORT_SGX_GENERALISED_SYNCOBJECTS */

	if (psCCBKick->bFirstKickOrResume && psCCBKick->ui32NumDstSyncObjects > 0)
	{
		PVRSRV_KERNEL_MEM_INFO	*psHWDstSyncListMemInfo =
								(PVRSRV_KERNEL_MEM_INFO *)psCCBKick->hKernelHWSyncListMemInfo;
		SGXMKIF_HWDEVICE_SYNC_LIST *psHWDeviceSyncList = psHWDstSyncListMemInfo->pvLinAddrKM;
		IMG_UINT32	ui32NumDstSyncs = psCCBKick->ui32NumDstSyncObjects;

		PVR_ASSERT(((PVRSRV_KERNEL_MEM_INFO *)psCCBKick->hKernelHWSyncListMemInfo)->uAllocSize >= (sizeof(SGXMKIF_HWDEVICE_SYNC_LIST) +
								(sizeof(PVRSRV_DEVICE_SYNC_OBJECT) * ui32NumDstSyncs)));

		psHWDeviceSyncList->ui32NumSyncObjects = ui32NumDstSyncs;

		for (i=0; i<ui32NumDstSyncs; i++)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->pahDstSyncHandles[i];

			if (psSyncInfo)
			{
				psSyncInfo->psSyncData->ui64LastWrite = ui64KickCount;

				psHWDeviceSyncList->asSyncData[i].sWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;
				psHWDeviceSyncList->asSyncData[i].sReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;
				psHWDeviceSyncList->asSyncData[i].sReadOps2CompleteDevVAddr = psSyncInfo->sReadOps2CompleteDevVAddr;

				psHWDeviceSyncList->asSyncData[i].ui32ReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending;
				psHWDeviceSyncList->asSyncData[i].ui32WriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending++;
				psHWDeviceSyncList->asSyncData[i].ui32ReadOps2PendingVal = psSyncInfo->psSyncData->ui32ReadOps2Pending;

			}
			else
			{
				psHWDeviceSyncList->asSyncData[i].sWriteOpsCompleteDevVAddr.uiAddr = 0;
				psHWDeviceSyncList->asSyncData[i].sReadOpsCompleteDevVAddr.uiAddr = 0;
				psHWDeviceSyncList->asSyncData[i].sReadOps2CompleteDevVAddr.uiAddr = 0;

				psHWDeviceSyncList->asSyncData[i].ui32ReadOpsPendingVal = 0;
				psHWDeviceSyncList->asSyncData[i].ui32ReadOps2PendingVal = 0;
				psHWDeviceSyncList->asSyncData[i].ui32WriteOpsPendingVal = 0;
			}
		}
	}
	
	/* 
		NOTE: THIS MUST BE THE LAST THING WRITTEN TO THE TA COMMAND!
		Set the ready for so the uKernel will process the command.
	*/
	psTACmd->ui32CtrlFlags |= SGXMKIF_CMDTA_CTRLFLAGS_READY;

	eError = SGXScheduleCCBCommandKM(hDevHandle, SGXMKIF_CMD_TA, &psCCBKick->sCommand, KERNEL_ID, hDevMemContext, psCCBKick->bLastInScene);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		if (psCCBKick->bFirstKickOrResume && psCCBKick->ui32NumDstSyncObjects > 0)
		{
			for (i=0; i < psCCBKick->ui32NumDstSyncObjects; i++)
			{
				/* Client will retry, so undo the write ops pending increment done above. */
				psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->pahDstSyncHandles[i];

				if (psSyncInfo)
				{
					psSyncInfo->psSyncData->ui32WriteOpsPending--;
				}
			}
		}

#if defined(SUPPORT_SGX_GENERALISED_SYNCOBJECTS)
		for (i=0; i<psCCBKick->ui32NumTASrcSyncs; i++)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahTASrcKernelSyncInfo[i];
			psSyncInfo->psSyncData->ui32ReadOpsPending--;
		}
		for (i=0; i<psCCBKick->ui32NumTADstSyncs; i++)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahTADstKernelSyncInfo[i];
			psSyncInfo->psSyncData->ui32WriteOpsPending--;
		}
		for (i=0; i<psCCBKick->ui32Num3DSrcSyncs; i++)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ah3DSrcKernelSyncInfo[i];
			psSyncInfo->psSyncData->ui32ReadOpsPending--;
		}
#else/* SUPPORT_SGX_GENERALISED_SYNCOBJECTS */
		for (i=0; i<psCCBKick->ui32NumSrcSyncs; i++)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahSrcKernelSyncInfo[i];
			psSyncInfo->psSyncData->ui32ReadOpsPending--;
		}
#endif/* SUPPORT_SGX_GENERALISED_SYNCOBJECTS */

		if (psCCBKick->hTA3DSyncInfo)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->hTA3DSyncInfo;
			psSyncInfo->psSyncData->ui32ReadOpsPending--;
		}
	
		if (psCCBKick->hTASyncInfo)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->hTASyncInfo;
			psSyncInfo->psSyncData->ui32ReadOpsPending--;
		}
	
		if (psCCBKick->h3DSyncInfo)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->h3DSyncInfo;
			psSyncInfo->psSyncData->ui32ReadOpsPending--;
		}

		return eError;
	}
	else if (PVRSRV_OK != eError)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXDoKickKM: SGXScheduleCCBCommandKM failed."));
		return eError;
	}


#if defined(NO_HARDWARE)


	/* TA/3D dependency */
	if (psCCBKick->hTA3DSyncInfo)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->hTA3DSyncInfo;

		if (psCCBKick->bTADependency)
		{
			psSyncInfo->psSyncData->ui32WriteOpsComplete = psSyncInfo->psSyncData->ui32WriteOpsPending;
		}
	}

	if (psCCBKick->hTASyncInfo != IMG_NULL)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->hTASyncInfo;

		psSyncInfo->psSyncData->ui32ReadOpsComplete =  psSyncInfo->psSyncData->ui32ReadOpsPending;
	}

	if (psCCBKick->h3DSyncInfo != IMG_NULL)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->h3DSyncInfo;

		psSyncInfo->psSyncData->ui32ReadOpsComplete =  psSyncInfo->psSyncData->ui32ReadOpsPending;
	}

	/* Copy status vals over */
	for (i = 0; i < psCCBKick->ui32NumTAStatusVals; i++)
	{
		PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo = (PVRSRV_KERNEL_MEM_INFO*)psCCBKick->asTAStatusUpdate[i].hKernelMemInfo;
		/* derive offset into meminfo and write the status value */
		*(IMG_UINT32*)((IMG_UINTPTR_T)psKernelMemInfo->pvLinAddrKM
						+ (psTACmd->sCtlTAStatusInfo[i].sStatusDevAddr.uiAddr
						- psKernelMemInfo->sDevVAddr.uiAddr)) = psTACmd->sCtlTAStatusInfo[i].ui32StatusValue;
	}

#if defined(SUPPORT_SGX_GENERALISED_SYNCOBJECTS)
	/* SRC and DST dependencies */
	for (i=0; i<psCCBKick->ui32NumTASrcSyncs; i++)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahTASrcKernelSyncInfo[i];
		psSyncInfo->psSyncData->ui32ReadOpsComplete =  psSyncInfo->psSyncData->ui32ReadOpsPending;
	}
	for (i=0; i<psCCBKick->ui32NumTADstSyncs; i++)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahTADstKernelSyncInfo[i];
		psSyncInfo->psSyncData->ui32WriteOpsComplete =  psSyncInfo->psSyncData->ui32WriteOpsPending;
	}
	for (i=0; i<psCCBKick->ui32Num3DSrcSyncs; i++)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ah3DSrcKernelSyncInfo[i];
		psSyncInfo->psSyncData->ui32ReadOpsComplete =  psSyncInfo->psSyncData->ui32ReadOpsPending;
	}
#else/* SUPPORT_SGX_GENERALISED_SYNCOBJECTS */
	/* texture dependencies */
	for (i=0; i<psCCBKick->ui32NumSrcSyncs; i++)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->ahSrcKernelSyncInfo[i];
		psSyncInfo->psSyncData->ui32ReadOpsComplete =  psSyncInfo->psSyncData->ui32ReadOpsPending;
	}
#endif/* SUPPORT_SGX_GENERALISED_SYNCOBJECTS */

	if (psCCBKick->bTerminateOrAbort)
	{
		if (psCCBKick->ui32NumDstSyncObjects > 0)
		{
			PVRSRV_KERNEL_MEM_INFO	*psHWDstSyncListMemInfo =
								(PVRSRV_KERNEL_MEM_INFO *)psCCBKick->hKernelHWSyncListMemInfo;
			SGXMKIF_HWDEVICE_SYNC_LIST *psHWDeviceSyncList = psHWDstSyncListMemInfo->pvLinAddrKM;

			for (i=0; i<psCCBKick->ui32NumDstSyncObjects; i++)
			{
				psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psCCBKick->pahDstSyncHandles[i];
				if (psSyncInfo)
					psSyncInfo->psSyncData->ui32WriteOpsComplete = psHWDeviceSyncList->asSyncData[i].ui32WriteOpsPendingVal+1;
			}
		}

		/* Copy status vals over */
		for (i = 0; i < psCCBKick->ui32Num3DStatusVals; i++)
		{
			PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo = (PVRSRV_KERNEL_MEM_INFO*)psCCBKick->as3DStatusUpdate[i].hKernelMemInfo;
			/* derive offset into meminfo and write the status value */
			*(IMG_UINT32*)((IMG_UINTPTR_T)psKernelMemInfo->pvLinAddrKM
							+ (psTACmd->sCtl3DStatusInfo[i].sStatusDevAddr.uiAddr
							- psKernelMemInfo->sDevVAddr.uiAddr)) = psTACmd->sCtl3DStatusInfo[i].ui32StatusValue;
		}
	}
#endif
	return eError;
}

/******************************************************************************
 End of file (sgxkick.c)
******************************************************************************/
