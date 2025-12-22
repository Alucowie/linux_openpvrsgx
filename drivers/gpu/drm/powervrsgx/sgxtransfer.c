/*************************************************************************/ /*!
@Title          Device specific transfer queue routines
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

#include <linux/stddef.h>

#include "sgxdefs.h"
#include "services_headers.h"
#include "buffer_manager.h"
#include "sgxinfo.h"
#include "sysconfig.h"
#include "mmu.h"
#include "pvr_bridge.h"
#include "sgx_bridge_km.h"
#include "sgxinfokm.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "sgxutils.h"

IMG_EXPORT PVRSRV_ERROR SGXSubmitTransferKM(IMG_HANDLE hDevHandle, PVRSRV_TRANSFER_SGX_KICK *psKick)
{
	PVRSRV_KERNEL_MEM_INFO		*psCCBMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psKick->hCCBMemInfo;
	SGXMKIF_COMMAND				sCommand = {0};
	SGXMKIF_TRANSFERCMD_SHARED *psSharedTransferCmd;
	PVRSRV_KERNEL_SYNC_INFO 	*psSyncInfo;
	PVRSRV_ERROR				eError;
	IMG_UINT32					loop;
	IMG_HANDLE					hDevMemContext = IMG_NULL;
	bool					abSrcSyncEnable[SGX_MAX_TRANSFER_SYNC_OPS];
	IMG_UINT32					ui32RealSrcSyncNum = 0;
	bool					abDstSyncEnable[SGX_MAX_TRANSFER_SYNC_OPS];
	IMG_UINT32					ui32RealDstSyncNum = 0;

	for (loop = 0; loop < SGX_MAX_TRANSFER_SYNC_OPS; loop++)
	{
		abSrcSyncEnable[loop] = true;
		abDstSyncEnable[loop] = true;
	}

	if (!CCB_OFFSET_IS_VALID(SGXMKIF_TRANSFERCMD_SHARED, psCCBMemInfo, psKick, ui32SharedCmdCCBOffset))
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXSubmitTransferKM: Invalid CCB offset"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	/* override QAC warning about stricter alignment */
	/* PRQA S 3305 1 */
	psSharedTransferCmd =  CCB_DATA_FROM_OFFSET(SGXMKIF_TRANSFERCMD_SHARED, psCCBMemInfo, psKick, ui32SharedCmdCCBOffset);

	if (psKick->hTASyncInfo != IMG_NULL)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->hTASyncInfo;

		psSharedTransferCmd->ui32TASyncWriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending++;
		psSharedTransferCmd->ui32TASyncReadOpsPendingVal  = psSyncInfo->psSyncData->ui32ReadOpsPending;

		psSharedTransferCmd->sTASyncWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;
		psSharedTransferCmd->sTASyncReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;
	}
	else
	{
		psSharedTransferCmd->sTASyncWriteOpsCompleteDevVAddr.uiAddr = 0;
		psSharedTransferCmd->sTASyncReadOpsCompleteDevVAddr.uiAddr = 0;
	}

	if (psKick->h3DSyncInfo != IMG_NULL)
	{
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->h3DSyncInfo;

		psSharedTransferCmd->ui323DSyncWriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending++;
		psSharedTransferCmd->ui323DSyncReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending;

		psSharedTransferCmd->s3DSyncWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;
		psSharedTransferCmd->s3DSyncReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;
	}
	else
	{
		psSharedTransferCmd->s3DSyncWriteOpsCompleteDevVAddr.uiAddr = 0;
		psSharedTransferCmd->s3DSyncReadOpsCompleteDevVAddr.uiAddr = 0;
	}

	/* filter out multiple occurrences of the same sync object from srcs or dests
	 * note : the same sync can still be used to synchronize both src and dst.
	 */
	for (loop = 0; loop < MIN(SGX_MAX_TRANSFER_SYNC_OPS, psKick->ui32NumSrcSync); loop++)
	{
		IMG_UINT32 i;

		PVRSRV_KERNEL_SYNC_INFO * psMySyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahSrcSyncInfo[loop];
	
		for (i = 0; i < loop; i++)	
		{
			if (abSrcSyncEnable[i])
			{
				psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahSrcSyncInfo[i];

				if (psSyncInfo->sWriteOpsCompleteDevVAddr.uiAddr == psMySyncInfo->sWriteOpsCompleteDevVAddr.uiAddr)
				{
					PVR_DPF((PVR_DBG_WARNING, "SGXSubmitTransferKM : Same src synchronized multiple times!"));
					abSrcSyncEnable[loop] = false;
					break;
				}
			}
		}
		if (abSrcSyncEnable[loop])
		{
			ui32RealSrcSyncNum++;
		}
	}
	for (loop = 0; loop < MIN(SGX_MAX_TRANSFER_SYNC_OPS, psKick->ui32NumDstSync); loop++)
	{
		IMG_UINT32 i;

		PVRSRV_KERNEL_SYNC_INFO * psMySyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahDstSyncInfo[loop];
	
		for (i = 0; i < loop; i++)	
		{
			if (abDstSyncEnable[i])
			{
				psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahDstSyncInfo[i];

				if (psSyncInfo->sWriteOpsCompleteDevVAddr.uiAddr == psMySyncInfo->sWriteOpsCompleteDevVAddr.uiAddr)
				{
					PVR_DPF((PVR_DBG_WARNING, "SGXSubmitTransferKM : Same dst synchronized multiple times!"));
					abDstSyncEnable[loop] = false;
					break;
				}
			}
		}
		if (abDstSyncEnable[loop])
		{
			ui32RealDstSyncNum++;
		}
	}

	psSharedTransferCmd->ui32NumSrcSyncs = ui32RealSrcSyncNum; 
	psSharedTransferCmd->ui32NumDstSyncs = ui32RealDstSyncNum; 

	if ((psKick->ui32Flags & SGXMKIF_TQFLAGS_KEEPPENDING) == 0UL)
	{
		IMG_UINT32 i = 0;

		for (loop = 0; loop < psKick->ui32NumSrcSync; loop++)
		{
			if (abSrcSyncEnable[loop])
			{
				psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahSrcSyncInfo[loop];

				psSharedTransferCmd->asSrcSyncs[i].ui32WriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending;
				psSharedTransferCmd->asSrcSyncs[i].ui32ReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending;

				psSharedTransferCmd->asSrcSyncs[i].sWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr; 
				psSharedTransferCmd->asSrcSyncs[i].sReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;
				i++;
			}
		}
		PVR_ASSERT(i == ui32RealSrcSyncNum);

		i = 0;
		for (loop = 0; loop < psKick->ui32NumDstSync; loop++)
		{
			if (abDstSyncEnable[loop])
			{
				psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahDstSyncInfo[loop];

				psSyncInfo->psSyncData->ui64LastWrite = ui64KickCount;

				psSharedTransferCmd->asDstSyncs[i].ui32WriteOpsPendingVal = psSyncInfo->psSyncData->ui32WriteOpsPending;
				psSharedTransferCmd->asDstSyncs[i].ui32ReadOpsPendingVal = psSyncInfo->psSyncData->ui32ReadOpsPending;
				psSharedTransferCmd->asDstSyncs[i].ui32ReadOps2PendingVal = psSyncInfo->psSyncData->ui32ReadOps2Pending;

				psSharedTransferCmd->asDstSyncs[i].sWriteOpsCompleteDevVAddr = psSyncInfo->sWriteOpsCompleteDevVAddr;
				psSharedTransferCmd->asDstSyncs[i].sReadOpsCompleteDevVAddr = psSyncInfo->sReadOpsCompleteDevVAddr;
				psSharedTransferCmd->asDstSyncs[i].sReadOps2CompleteDevVAddr = psSyncInfo->sReadOps2CompleteDevVAddr;
				i++;
			}
		}
		PVR_ASSERT(i == ui32RealDstSyncNum);

		/*
		 * We allow source and destination sync objects to be the
		 * same, which is why the read/write pending updates are delayed
		 * until the transfer command has been updated with the current
		 * values from the objects.
		 */
		for (loop = 0; loop < psKick->ui32NumSrcSync; loop++)
		{
			if (abSrcSyncEnable[loop])
			{
				psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahSrcSyncInfo[loop];
				psSyncInfo->psSyncData->ui32ReadOpsPending++;
			}
		}
		for (loop = 0; loop < psKick->ui32NumDstSync; loop++)
		{
			if (abDstSyncEnable[loop])
			{
				psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahDstSyncInfo[loop];
				psSyncInfo->psSyncData->ui32WriteOpsPending++;
			}
		}
	}

	sCommand.ui32Data[1] = psKick->sHWTransferContextDevVAddr.uiAddr;

	eError = SGXScheduleCCBCommandKM(hDevHandle, SGXMKIF_CMD_TRANSFER, &sCommand, KERNEL_ID, hDevMemContext, false);

	if (eError == PVRSRV_ERROR_RETRY)
	{
		/* Client will retry, so undo the sync ops pending increment(s) done above. */
		if ((psKick->ui32Flags & SGXMKIF_TQFLAGS_KEEPPENDING) == 0UL)
		{
			for (loop = 0; loop < psKick->ui32NumSrcSync; loop++)
			{
				if (abSrcSyncEnable[loop])
				{
					psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahSrcSyncInfo[loop];
					psSyncInfo->psSyncData->ui32ReadOpsPending--;
				}
			}
			for (loop = 0; loop < psKick->ui32NumDstSync; loop++)
			{
				if (abDstSyncEnable[loop])
				{
					psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->ahDstSyncInfo[loop];
					psSyncInfo->psSyncData->ui32WriteOpsPending--;
				}
			}
		}

		/* Command needed to be synchronised with the TA? */
		if (psKick->hTASyncInfo != IMG_NULL)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->hTASyncInfo;
			psSyncInfo->psSyncData->ui32WriteOpsPending--;
		}

		/* Command needed to be synchronised with the 3D? */
		if (psKick->h3DSyncInfo != IMG_NULL)
		{
			psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *)psKick->h3DSyncInfo;
			psSyncInfo->psSyncData->ui32WriteOpsPending--;
		}
	}

	else if (PVRSRV_OK != eError)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXSubmitTransferKM: SGXScheduleCCBCommandKM failed."));
		return eError;
	}

	return eError;
}
