/*************************************************************************/ /*!
@Title          Device specific reset routines
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

#include "sgxdefs.h"
#include "sgxmmu.h"
#include "services_headers.h"
#include "sgxinfokm.h"
#include "sgxconfig.h"
#include "sgxutils.h"

/*!
*******************************************************************************

 @Function	SGXInitClocks

 @Description
	Initialise the SGX clocks

 @Input psDevInfo - device info. structure

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID SGXInitClocks(PVRSRV_SGXDEV_INFO	*psDevInfo)
{
	IMG_UINT32	ui32RegVal;

	ui32RegVal = psDevInfo->ui32ClkGateCtl;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_CLKGATECTL, ui32RegVal);

#if defined(EUR_CR_CLKGATECTL2)
	ui32RegVal = psDevInfo->ui32ClkGateCtl2;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_CLKGATECTL2, ui32RegVal);
#endif
}


/*!
*******************************************************************************

 @Function	SGXResetInitBIFContexts

 @Description
	Initialise the BIF memory contexts

 @Input psDevInfo - SGX Device Info

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID SGXResetInitBIFContexts(PVRSRV_SGXDEV_INFO	*psDevInfo)
{
	IMG_UINT32	ui32RegVal;

	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_DIR_LIST_BASE0, ui32RegVal);
}


/*!
*******************************************************************************

 @Function	SGXResetSetupBIFContexts

 @Description
	Configure the BIF for the EDM context

 @Input psDevInfo - SGX Device Info

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID SGXResetSetupBIFContexts(PVRSRV_SGXDEV_INFO	*psDevInfo)
{
	IMG_UINT32	ui32RegVal;

	{
		IMG_UINT32	ui32EDMDirListReg;

		/* Set up EDM context with kernel page directory */
		#if (SGX_BIF_DIR_LIST_INDEX_EDM == 0)
		ui32EDMDirListReg = EUR_CR_BIF_DIR_LIST_BASE0;
		#else
		/* Bases 0 and 1 are not necessarily contiguous */
		ui32EDMDirListReg = EUR_CR_BIF_DIR_LIST_BASE1 + 4 * (SGX_BIF_DIR_LIST_INDEX_EDM - 1);
		#endif /* SGX_BIF_DIR_LIST_INDEX_EDM */

		ui32RegVal = psDevInfo->sKernelPDDevPAddr.uiAddr >> SGX_MMU_PDE_ADDR_ALIGNSHIFT;

		OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32EDMDirListReg, ui32RegVal);
	}
}


/*!
*******************************************************************************

 @Function	SGXResetSleep

 @Description

 Sleep for a short time to allow reset register writes to complete.
 Required because no status registers are available to poll on.

 @Input psDevInfo - SGX Device Info

 @Return   Nothing

******************************************************************************/
static IMG_VOID SGXResetSleep(PVRSRV_SGXDEV_INFO	*psDevInfo)
{
#if defined(EMULATOR)
	IMG_UINT32	ui32ReadRegister;

	ui32ReadRegister = EUR_CR_SOFT_RESET;
#endif

	/* Sleep for 100 SGX clocks */
	SGXWaitClocks(psDevInfo, 100);
#if defined(EMULATOR)
	/*
		Read a register to make sure we wait long enough on the emulator...
	*/
	OSReadHWReg(psDevInfo->pvRegsBaseKM, ui32ReadRegister);
#endif
}


/*!
*******************************************************************************

 @Function	SGXResetSoftReset

 @Description

 Write to the SGX soft reset register.

 @Input psDevInfo - SGX Device Info
 @Input bResetBIF - Include the BIF in the soft reset

 @Return   Nothing

******************************************************************************/
static IMG_VOID SGXResetSoftReset(PVRSRV_SGXDEV_INFO	*psDevInfo,
								  IMG_BOOL				bResetBIF)
{
	IMG_UINT32 ui32SoftResetRegVal;

	ui32SoftResetRegVal =
					/* add common reset bits: */
					EUR_CR_SOFT_RESET_DPM_RESET_MASK |
					EUR_CR_SOFT_RESET_TA_RESET_MASK  |
					EUR_CR_SOFT_RESET_USE_RESET_MASK |
					EUR_CR_SOFT_RESET_ISP_RESET_MASK |
					EUR_CR_SOFT_RESET_TSP_RESET_MASK;

/* add conditional reset bits: */
#ifdef EUR_CR_SOFT_RESET_TWOD_RESET_MASK
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_TWOD_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_TE_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_TE_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_MTE_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_MTE_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_ISP2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_ISP2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_PDS_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_PDS_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_PBE_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_PBE_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_CACHEL2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_CACHEL2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_TCU_L2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_TCU_L2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_UCACHEL2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_UCACHEL2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_MADD_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_MADD_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_ITR_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_ITR_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_TEX_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_TEX_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_IDXFIFO_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_IDXFIFO_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_VDM_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_VDM_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_DCU_L2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_DCU_L2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_DCU_L0L1_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_DCU_L0L1_RESET_MASK;
#endif

	if (bResetBIF)
	{
		ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_BIF_RESET_MASK;
	}

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_SOFT_RESET, ui32SoftResetRegVal);
}


/*!
*******************************************************************************

 @Function	SGXResetInvalDC

 @Description

 Invalidate the BIF Directory Cache and wait for the operation to complete.

 @Input psDevInfo - SGX Device Info

 @Return   Nothing

******************************************************************************/
static IMG_VOID SGXResetInvalDC(PVRSRV_SGXDEV_INFO	*psDevInfo)
{
	IMG_UINT32 ui32RegVal;

	/* Invalidate BIF Directory cache. */
#if defined(EUR_CR_BIF_CTRL_INVAL)
	ui32RegVal = EUR_CR_BIF_CTRL_INVAL_ALL_MASK;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL_INVAL, ui32RegVal);
#else
	ui32RegVal = EUR_CR_BIF_CTRL_INVALDC_MASK;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);

	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
#endif
	SGXResetSleep(psDevInfo);

	{
		/*
			Wait for the DC invalidate to complete - indicated by
			outstanding reads reaching zero.
		*/
		if (PollForValueKM((IMG_UINT32 *)((IMG_UINT8*)psDevInfo->pvRegsBaseKM + EUR_CR_BIF_MEM_REQ_STAT),
							0,
							EUR_CR_BIF_MEM_REQ_STAT_READS_MASK,
							MAX_HW_TIME_US,
							MAX_HW_TIME_US/WAIT_TRY_COUNT,
							IMG_FALSE) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"Wait for DC invalidate failed."));
			PVR_DBG_BREAK;
		}
	}
}


/*!
*******************************************************************************

 @Function	SGXReset

 @Description

 Reset chip

 @Input psDevInfo - device info. structure
 @Input bHardwareRecovery - true if recovering powered hardware,
							false if powering up

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID SGXReset(PVRSRV_SGXDEV_INFO	*psDevInfo,
				  IMG_BOOL				bHardwareRecovery)
{
	IMG_UINT32 ui32RegVal;
#if defined(EUR_CR_BIF_INT_STAT_FAULT_REQ_MASK)
	const IMG_UINT32 ui32BifFaultMask = EUR_CR_BIF_INT_STAT_FAULT_REQ_MASK;
#else
	const IMG_UINT32 ui32BifFaultMask = EUR_CR_BIF_INT_STAT_FAULT_MASK;
#endif

	/* Reset all including BIF */
	SGXResetSoftReset(psDevInfo, IMG_TRUE);

	SGXResetSleep(psDevInfo);

	/*
		Initialise the BIF state.
	*/
	SGXResetInitBIFContexts(psDevInfo);

#if defined(EUR_CR_BIF_MEM_ARB_CONFIG)
	/*
		Initialise the memory arbiter to its default state
	*/
	ui32RegVal	= (12UL << EUR_CR_BIF_MEM_ARB_CONFIG_PAGE_SIZE_SHIFT) |
				  (7UL << EUR_CR_BIF_MEM_ARB_CONFIG_BEST_CNT_SHIFT) |
				  (12UL << EUR_CR_BIF_MEM_ARB_CONFIG_TTE_THRESH_SHIFT);
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_MEM_ARB_CONFIG, ui32RegVal);
#endif /* EUR_CR_BIF_MEM_ARB_CONFIG */

#if defined(SGX_FEATURE_SYSTEM_CACHE)
			/* set the SLC to bypass cache-coherent accesses */
			ui32RegVal = MNE_CR_CTRL_BYP_CC_MASK;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, MNE_CR_CTRL, ui32RegVal);
#endif /* SGX_FEATURE_SYSTEM_CACHE */

	if (bHardwareRecovery)
	{
		/*
			Set all requestors to the dummy PD which forces all memory
			accesses to page fault.
			This enables us to flush out BIF requests from parts of SGX
			which do not have their own soft reset.
			Note: sBIFResetPDDevPAddr.uiAddr is a relative address (2GB max)
			MSB is the bus master flag; 1 == enabled
		*/
		ui32RegVal = (IMG_UINT32)psDevInfo->sBIFResetPDDevPAddr.uiAddr;
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_DIR_LIST_BASE0, ui32RegVal);

		SGXResetSleep(psDevInfo);

		/* Bring BIF out of reset. */
		SGXResetSoftReset(psDevInfo, IMG_FALSE);
		SGXResetSleep(psDevInfo);

		SGXResetInvalDC(psDevInfo);

		/*
			Check for a page fault from parts of SGX which do not have a reset.
		*/
		for (;;)
		{
			IMG_UINT32 ui32BifIntStat = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_INT_STAT);
			IMG_DEV_VIRTADDR sBifFault;
			IMG_UINT32 ui32PDIndex, ui32PTIndex;

			if ((ui32BifIntStat & ui32BifFaultMask) == 0)
			{
				break;
			}

			/*
				There is a page fault, so reset the BIF again, map in the dummy page,
				bring the BIF up and invalidate the Directory Cache.
			*/
			sBifFault.uiAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_FAULT);
			PVR_DPF((PVR_DBG_WARNING, "SGXReset: Page fault 0x%x/0x%x", ui32BifIntStat, sBifFault.uiAddr));
			ui32PDIndex = sBifFault.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
			ui32PTIndex = (sBifFault.uiAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

			/* Put the BIF into reset. */
			SGXResetSoftReset(psDevInfo, IMG_TRUE);

			/* Map in the dummy page. */
			psDevInfo->pui32BIFResetPD[ui32PDIndex] = (psDevInfo->sBIFResetPTDevPAddr.uiAddr
													>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
													| SGX_MMU_PDE_PAGE_SIZE_4K
													| SGX_MMU_PDE_VALID;
			psDevInfo->pui32BIFResetPT[ui32PTIndex] = (psDevInfo->sBIFResetPageDevPAddr.uiAddr
													>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
													| SGX_MMU_PTE_VALID;

			/* Clear outstanding events. */
			ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS);
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR, ui32RegVal);
			ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS2);
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR2, ui32RegVal);

			SGXResetSleep(psDevInfo);

			/* Bring the BIF out of reset. */
			SGXResetSoftReset(psDevInfo, IMG_FALSE);
			SGXResetSleep(psDevInfo);

			/* Invalidate Directory Cache. */
			SGXResetInvalDC(psDevInfo);

			/* Unmap the dummy page and try again. */
			psDevInfo->pui32BIFResetPD[ui32PDIndex] = 0;
			psDevInfo->pui32BIFResetPT[ui32PTIndex] = 0;
		}
	}
	else
	{
		/* Bring BIF out of reset. */
		SGXResetSoftReset(psDevInfo, IMG_FALSE);
		SGXResetSleep(psDevInfo);
	}	

	/*
		Initialise the BIF memory contexts before bringing the rest of SGX out of reset.
	*/
	SGXResetSetupBIFContexts(psDevInfo);

	/* Invalidate BIF Directory cache. */
	SGXResetInvalDC(psDevInfo);

	PVR_DPF((PVR_DBG_MESSAGE,"Soft Reset of SGX"));

	/* Take chip out of reset */
	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_SOFT_RESET, ui32RegVal);

	/* wait a bit */
	SGXResetSleep(psDevInfo);
}

/******************************************************************************
 End of file (sgxreset.c)
******************************************************************************/
