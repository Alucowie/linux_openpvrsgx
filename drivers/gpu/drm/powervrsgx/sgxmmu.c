/*************************************************************************/ /*!
@Title          MMU Management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements basic low level control of MMU.
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
#include "buffer_manager.h"
#include "hash.h"
#include "ra.h"
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "mmu.h"
#include "sgxconfig.h"
#include "sgx_bridge_km.h"

#define UINT32_MAX_VALUE	0xFFFFFFFFUL

/*
	MMU performs device virtual to physical translation.
	terminology:
	page directory (PD)
	pagetable (PT)
	data page (DP)

	Incoming 32bit Device Virtual Addresses are deconstructed into 3 fields:
	---------------------------------------------------------
	|	PD Index/tag:	|	PT Index:	|	DP offset:		|
	|	bits 31:22		|	bits 21:n	|	bits (n-1):0	|
	---------------------------------------------------------
		where typically n=12 for a standard 4k DP
		but n=16 for a 64k DP

	MMU page directory (PD), pagetable (PT) and data page (DP) config:
	PD:
	- always one page per address space
	- up to 4k in size to span 4Gb (32bit)
	- contains up to 1024 32bit entries
	- entries are indexed by the top 12 bits of an incoming 32bit device virtual address
	- the PD entry selected contains the physical address of the PT to
	  perform the next stage of the V to P translation

	PT:
	- size depends on the DP size, e.g. 4k DPs have 4k PTs but 16k DPs have 1k PTs
	- each PT always spans 4Mb of device virtual address space irrespective of DP size
	- number of entries in a PT depend on DP size and ranges from 1024 to 4 entries
	- entries are indexed by the PT Index field of the device virtual address (21:n)
	- the PT entry selected contains the physical address of the DP to access

	DP:
	- size varies from 4k to 4M in multiple of 4 steppings
	- DP offset field of the device virtual address ((n-1):0) is used as a byte offset
	  to address into the DP itself
*/

#define SGX_MAX_PD_ENTRIES	(1<<(SGX_FEATURE_ADDRESS_SPACE_SIZE - SGX_MMU_PT_SHIFT - SGX_MMU_PAGE_SHIFT))

typedef struct _MMU_PT_INFO_
{
	/* note: may need a union here to accommodate a PT page address for local memory */
	IMG_VOID *hPTPageOSMemHandle;
	IMG_CPU_VIRTADDR PTPageCpuVAddr;
	/* Map of reserved PTEs.
	 * Reserved PTEs are like "valid" PTEs in that they (and the DevVAddrs they represent)
	 * cannot be assigned to another allocation but their "reserved" status persists through
	 * any amount of mapping and unmapping, until the allocation is finally destroyed.
	 *
	 * Reserved and Valid are independent.
	 * When a PTE is first reserved, it will have Reserved=1 and Valid=0.
	 * When the PTE is actually mapped, it will have Reserved=1 and Valid=1.
	 * When the PTE is unmapped, it will have Reserved=1 and Valid=0.
	 * At this point, the PT will can not be destroyed because although there is
	 * not an active mapping on the PT, it is known a PTE is reserved for use.
	 *
	 * The above sequence of mapping and unmapping may repeat any number of times
	 * until the allocation is unmapped and destroyed which causes the PTE to have
	 * Valid=0 and Reserved=0.
	 */
	/* Number of PTEs set up.
	 * i.e. have a valid SGX Phys Addr and the "VALID" PTE bit == 1
	 */
	IMG_UINT32 ui32ValidPTECount;
} MMU_PT_INFO;

#define MMU_CONTEXT_NAME_SIZE	50
struct _MMU_CONTEXT_
{
	/* the device node */
	PVRSRV_DEVICE_NODE *psDeviceNode;

	/* Page Directory CPUVirt and DevPhys Addresses */
	IMG_CPU_VIRTADDR pvPDCpuVAddr;
	IMG_DEV_PHYADDR sPDDevPAddr;

	IMG_VOID *hPDOSMemHandle;

	/* information about dynamically allocated pagetables */
	MMU_PT_INFO *apsPTInfoList[SGX_MAX_PD_ENTRIES];

	PVRSRV_SGXDEV_INFO *psDevInfo;

	IMG_UINT32 ui32PID;
	IMG_CHAR szName[MMU_CONTEXT_NAME_SIZE];

	struct _MMU_CONTEXT_ *psNext;
};

struct _MMU_HEAP_
{
	/* MMU context */
	MMU_CONTEXT			*psMMUContext;

	/*
		heap specific details:
	*/
	/* the Base PD index for the heap */
	IMG_UINT32			ui32PDBaseIndex;
	/* number of pagetables in this heap */
	IMG_UINT32			ui32PageTableCount;
	/* total number of pagetable entries in this heap which may be mapped to data pages */
	IMG_UINT32			ui32PTETotalUsable;
	/* PD entry DP size control field */
	IMG_UINT32			ui32PDEPageSizeCtrl;

	/*
		Data Page (DP) Details:
	*/
	/* size in bytes of a data page */
	IMG_UINT32			ui32DataPageSize;
	/* bit width of the data page offset addressing field */
	IMG_UINT32			ui32DataPageBitWidth;
	/* bit mask of the data page offset addressing field */
	IMG_UINT32			ui32DataPageMask;

	/*
		PageTable (PT) Details:
	*/
	/* bit shift to base of PT addressing field */
	IMG_UINT32			ui32PTShift;
	/* bit width of the PT addressing field */
	IMG_UINT32			ui32PTBitWidth;
	/* bit mask of the PT addressing field */
	IMG_UINT32			ui32PTMask;
	/* size in bytes of a pagetable */
	IMG_UINT32			ui32PTSize;
	/* Allocated PT Entries per PT */
	IMG_UINT32			ui32PTNumEntriesAllocated;
	/* Usable PT Entries per PT (may be different to num allocated for 4MB data page) */
	IMG_UINT32			ui32PTNumEntriesUsable;

	/*
		PageDirectory Details:
	*/
	/* bit shift to base of PD addressing field */
	IMG_UINT32			ui32PDShift;
	/* bit width of the PD addressing field */
	IMG_UINT32			ui32PDBitWidth;
	/* bit mask of the PT addressing field */
	IMG_UINT32			ui32PDMask;

	/*
		Arena Info:
	*/
	RA_ARENA *psVMArena;
	DEV_ARENA_DESCRIPTOR *psDevArena;

	/* If we have sparse mappings then we can't do PT level sanity checks */
	IMG_BOOL bHasSparseMappings;
};


/* local prototypes: */
static IMG_VOID
_DeferredFreePageTable (MMU_HEAP *pMMUHeap, IMG_UINT32 ui32PTIndex, IMG_BOOL bOSFreePT);

/* This option tests page table memory, for use during device bring-up. */
#define PAGE_TEST					0
#if PAGE_TEST
static IMG_VOID PageTest(IMG_VOID* pMem, IMG_DEV_PHYADDR sDevPAddr);
#endif

/* This option dumps out the PT if an assert fails */
#define PT_DUMP 1

/* This option sanity checks page table PTE valid count matches active PTEs */
#define PT_DEBUG 0
#if (PT_DEBUG || PT_DUMP) && defined(PVRSRV_NEED_PVR_DPF)
static IMG_VOID DumpPT(MMU_PT_INFO *psPTInfoList)
{
	IMG_UINT32 *p = (IMG_UINT32*)psPTInfoList->PTPageCpuVAddr;
	IMG_UINT32 i;

	/* 1024 entries in a 4K page table */
	for(i = 0; i < 1024; i += 8)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%08X %08X %08X %08X %08X %08X %08X %08X\n",
				 p[i + 0], p[i + 1], p[i + 2], p[i + 3],
				 p[i + 4], p[i + 5], p[i + 6], p[i + 7]));
	}
}
#else /* (PT_DEBUG || PT_DUMP) && defined(PVRSRV_NEED_PVR_DPF) */
static INLINE IMG_VOID DumpPT(MMU_PT_INFO *psPTInfoList)
{
	PVR_UNREFERENCED_PARAMETER(psPTInfoList);
}
#endif /* (PT_DEBUG || PT_DUMP) && defined(PVRSRV_NEED_PVR_DPF) */

#if PT_DEBUG
static IMG_VOID CheckPT(MMU_PT_INFO *psPTInfoList)
{
	IMG_UINT32 *p = (IMG_UINT32*) psPTInfoList->PTPageCpuVAddr;
	IMG_UINT32 i, ui32Count = 0;

	/* 1024 entries in a 4K page table */
	for(i = 0; i < 1024; i++)
		if(p[i] & SGX_MMU_PTE_VALID)
			ui32Count++;

	if(psPTInfoList->ui32ValidPTECount != ui32Count)
	{
		PVR_DPF((PVR_DBG_ERROR, "ui32ValidPTECount: %u ui32Count: %u\n",
				 psPTInfoList->ui32ValidPTECount, ui32Count));
		DumpPT(psPTInfoList);
		BUG();
	}
}
#else /* PT_DEBUG */
static INLINE IMG_VOID CheckPT(MMU_PT_INFO *psPTInfoList)
{
	PVR_UNREFERENCED_PARAMETER(psPTInfoList);
}
#endif /* PT_DEBUG */

/*
	Debug functionality that allows us to make the CPU
	mapping of pagetable memory readonly and only make
	it read/write when we alter it. This allows us
	to check that our memory isn't being overwritten
*/
#if defined(PVRSRV_MMU_MAKE_READWRITE_ON_DEMAND)

#include <generated/autoconf.h>

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

static IMG_VOID MakeKernelPageReadWrite(IMG_PVOID ulCPUVAddr)
{
    pgd_t *psPGD;
    pud_t *psPUD;
    pmd_t *psPMD;
    pte_t *psPTE;
    pte_t ptent;
    IMG_UINT32 ui32CPUVAddr = (IMG_UINT32) ulCPUVAddr;

    psPGD = pgd_offset_k(ui32CPUVAddr);
    if (pgd_none(*psPGD) || pgd_bad(*psPGD))
    {
        PVR_ASSERT(0);
    }

    psPUD = pud_offset(psPGD, ui32CPUVAddr);
    if (pud_none(*psPUD) || pud_bad(*psPUD))
    {
        PVR_ASSERT(0);
    }

    psPMD = pmd_offset(psPUD, ui32CPUVAddr);
    if (pmd_none(*psPMD) || pmd_bad(*psPMD))
    {
        PVR_ASSERT(0);
    }
	psPTE = (pte_t *)pte_offset_kernel(psPMD, ui32CPUVAddr);

	ptent = ptep_modify_prot_start(&init_mm, ui32CPUVAddr, psPTE);
	ptent = pte_mkwrite(ptent);
	ptep_modify_prot_commit(&init_mm, ui32CPUVAddr, psPTE, ptent);

	flush_tlb_all();
}

static IMG_VOID MakeKernelPageReadOnly(IMG_PVOID ulCPUVAddr)
{
    pgd_t *psPGD;
    pud_t *psPUD;
    pmd_t *psPMD;
    pte_t *psPTE;
    pte_t ptent;
    IMG_UINT32 ui32CPUVAddr = (IMG_UINT32) ulCPUVAddr;

	OSWriteMemoryBarrier();

    psPGD = pgd_offset_k(ui32CPUVAddr);
    if (pgd_none(*psPGD) || pgd_bad(*psPGD))
    {
        PVR_ASSERT(0);
    }

    psPUD = pud_offset(psPGD, ui32CPUVAddr);
    if (pud_none(*psPUD) || pud_bad(*psPUD))
    {
        PVR_ASSERT(0);
    }

    psPMD = pmd_offset(psPUD, ui32CPUVAddr);
    if (pmd_none(*psPMD) || pmd_bad(*psPMD))
    {
        PVR_ASSERT(0);
    }

	psPTE = (pte_t *)pte_offset_kernel(psPMD, ui32CPUVAddr);

	ptent = ptep_modify_prot_start(&init_mm, ui32CPUVAddr, psPTE);
	ptent = pte_wrprotect(ptent);
	ptep_modify_prot_commit(&init_mm, ui32CPUVAddr, psPTE, ptent);

	flush_tlb_all();

}

#else /* defined(PVRSRV_MMU_MAKE_READWRITE_ON_DEMAND) */

static INLINE IMG_VOID MakeKernelPageReadWrite(IMG_PVOID ulCPUVAddr)
{
	PVR_UNREFERENCED_PARAMETER(ulCPUVAddr);
}

static INLINE IMG_VOID MakeKernelPageReadOnly(IMG_PVOID ulCPUVAddr)
{
	PVR_UNREFERENCED_PARAMETER(ulCPUVAddr);
}

#endif /* defined(PVRSRV_MMU_MAKE_READWRITE_ON_DEMAND) */

/*___________________________________________________________________________

	Information for SUPPORT_PDUMP_MULTI_PROCESS feature.
	
	The client marked for pdumping will set the bPDumpActive flag in
	the MMU Context (see MMU_Initialise).
	
	Shared heap allocations should be persistent so all apps which
	are pdumped will see the allocation. Persistent flag over-rides
	the bPDumpActive flag (see pdump_common.c/DbgWrite function).
	
	The idea is to dump PT,DP for shared heap allocations, but only
	dump the PDE if the allocation is mapped into the kernel or active
	client context. This ensures if a background app allocates on a
	shared heap then all clients can access it in the pdump toolchain.
	
	
	
	PD		PT		DP
	+-+
	| |--->	+-+
	+-+		| |--->	+-+
			+-+		+ +
					+-+
					
	PD allocation/free: pdump flags are 0 (only need PD for active apps)
	PT allocation/free: pdump flags are 0
						unless PT is for a shared heap, in which case persistent is set
	PD entries (MMU init/insert shared heap):
						only pdump if PDE is on the active MMU context, flags are 0
	PD entries (PT alloc):
						pdump flags are 0 if kernel heap
						pdump flags are 0 if shared heap and PDE is on active MMU context
						otherwise ignore.
	PT entries			pdump flags are 0
						unless PTE is for a shared heap, in which case persistent is set
						
	NOTE: PDump common code:-
	PDumpMallocPages and PDumpMemKM also set the persistent flag for
	shared heap allocations.
	
  ___________________________________________________________________________
*/


/*!
******************************************************************************
	FUNCTION:   MMU_IsHeapShared

	PURPOSE:    Is this heap shared?
	PARAMETERS: In: pMMU_Heap
	RETURNS:    true if heap is shared
******************************************************************************/
IMG_BOOL MMU_IsHeapShared(MMU_HEAP* pMMUHeap)
{
	switch(pMMUHeap->psDevArena->DevMemHeapType)
	{
		case DEVICE_MEMORY_HEAP_SHARED :
		case DEVICE_MEMORY_HEAP_SHARED_EXPORTED :
			return IMG_TRUE;
		case DEVICE_MEMORY_HEAP_PERCONTEXT :
		case DEVICE_MEMORY_HEAP_KERNEL :
			return IMG_FALSE;
		default:
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_IsHeapShared: ERROR invalid heap type"));
			return IMG_FALSE;
		}
	}
}

#ifdef SUPPORT_SGX_MMU_BYPASS
/*!
******************************************************************************
	FUNCTION:   EnableHostAccess

	PURPOSE:    Enables Host accesses to device memory, by passing the device
				MMU address translation

	PARAMETERS: In: psMMUContext
	RETURNS:    None
******************************************************************************/
IMG_VOID
EnableHostAccess (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 ui32RegVal;
	IMG_VOID *pvRegsBaseKM = psMMUContext->psDevInfo->pvRegsBaseKM;

	/*
		bypass the MMU for the host port requestor,
		conserving bypass state of other requestors
	*/
	ui32RegVal = OSReadHWReg(pvRegsBaseKM, EUR_CR_BIF_CTRL);

	OSWriteHWReg(pvRegsBaseKM,
				EUR_CR_BIF_CTRL,
				ui32RegVal | EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
	/* assume we're not wiping-out any other bits */
	PDUMPREG(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
}

/*!
******************************************************************************
	FUNCTION:   DisableHostAccess

	PURPOSE:    Disables Host accesses to device memory, by passing the device
				MMU address translation

	PARAMETERS: In: psMMUContext
	RETURNS:    None
******************************************************************************/
IMG_VOID
DisableHostAccess (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 ui32RegVal;
	IMG_VOID *pvRegsBaseKM = psMMUContext->psDevInfo->pvRegsBaseKM;

	/*
		disable MMU-bypass for the host port requestor,
		conserving bypass state of other requestors
		and flushing all caches/tlbs
	*/
	OSWriteHWReg(pvRegsBaseKM,
				EUR_CR_BIF_CTRL,
				ui32RegVal & ~EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
	/* assume we're not wiping-out any other bits */
	PDUMPREG(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, 0);
}
#endif


/*!
******************************************************************************
	FUNCTION:   MMU_InvalidateDirectoryCache

	PURPOSE:    Invalidates the page directory cache + page table cache + requestor TLBs

	PARAMETERS: In: psDevInfo
	RETURNS:    None

******************************************************************************/
IMG_VOID MMU_InvalidateDirectoryCache(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	psDevInfo->ui32CacheControl |= SGXMKIF_CC_INVAL_BIF_PD;
}


/*!
******************************************************************************
	FUNCTION:   MMU_InvalidatePageTableCache

	PURPOSE:    Invalidates the page table cache + requestor TLBs

	PARAMETERS: In: psDevInfo
	RETURNS:    None

******************************************************************************/
static IMG_VOID MMU_InvalidatePageTableCache(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	psDevInfo->ui32CacheControl |= SGXMKIF_CC_INVAL_BIF_PT;
}

/*!
******************************************************************************
	FUNCTION:   _AllocPageTableMemory

	PURPOSE:    Allocate physical memory for a page table

	PARAMETERS: In: pMMUHeap - the mmu
				In: psPTInfoList - PT info
				Out: psDevPAddr - device physical address for new PT
	RETURNS:    IMG_TRUE - Success
	            IMG_FALSE - Failed
******************************************************************************/
static IMG_BOOL
_AllocPageTableMemory (MMU_HEAP *pMMUHeap,
						MMU_PT_INFO *psPTInfoList,
						IMG_DEV_PHYADDR	*psDevPAddr)
{
	IMG_DEV_PHYADDR	sDevPAddr;
	IMG_CPU_PHYADDR sCpuPAddr;

	/*
		depending on the specific system, pagetables are allocated from system memory
		or device local memory.  For now, just look for at least a valid local heap/arena
	*/
	if(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena == IMG_NULL)
	{
		//FIXME: replace with an RA, this allocator only handles 4k allocs
		if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						 pMMUHeap->ui32PTSize,
						 SGX_MMU_PAGE_SIZE,//FIXME: assume 4K page size for now (wastes memory for smaller pagetables
						 IMG_NULL,
						 0,
						 IMG_NULL,
						 (IMG_VOID **)&psPTInfoList->PTPageCpuVAddr,
						 &psPTInfoList->hPTPageOSMemHandle) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "_AllocPageTableMemory: ERROR call to OSAllocPages failed"));
			return IMG_FALSE;
		}

		/*
			Force the page to read only, we will make it read/write as
			and when we need to
		*/
		MakeKernelPageReadOnly(psPTInfoList->PTPageCpuVAddr);

		/* translate address to device physical */
		if(psPTInfoList->PTPageCpuVAddr)
		{
			sCpuPAddr = OSMapLinToCPUPhys(psPTInfoList->hPTPageOSMemHandle,
										  psPTInfoList->PTPageCpuVAddr);
		}
		else
		{
			/* This isn't used in all cases since not all ports currently support
			 * OSMemHandleToCpuPAddr() */
			sCpuPAddr = OSMemHandleToCpuPAddr(psPTInfoList->hPTPageOSMemHandle, 0);
		}

		sDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);
	}
	else
	{
		IMG_SYS_PHYADDR sSysPAddr;

		/*
			just allocate from the first local memory arena
			(unlikely to be more than one local mem area(?))
		*/
		//FIXME: just allocate a 4K page for each PT for now
		if(RA_Alloc(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena,
					SGX_MMU_PAGE_SIZE,//pMMUHeap->ui32PTSize,
					IMG_NULL,
					IMG_NULL,
					0,
					SGX_MMU_PAGE_SIZE,//pMMUHeap->ui32PTSize,
					0,
					IMG_NULL,
					0,
					&(sSysPAddr.uiAddr))!= IMG_TRUE)
		{
			PVR_DPF((PVR_DBG_ERROR, "_AllocPageTableMemory: ERROR call to RA_Alloc failed"));
			return IMG_FALSE;
		}

		/* derive the CPU virtual address */
		sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
		/* note: actual ammount is pMMUHeap->ui32PTSize but must be a multiple of 4k pages */
		psPTInfoList->PTPageCpuVAddr = OSMapPhysToLin(sCpuPAddr,
													SGX_MMU_PAGE_SIZE,
													PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
													&psPTInfoList->hPTPageOSMemHandle);
		if(!psPTInfoList->PTPageCpuVAddr)
		{
			PVR_DPF((PVR_DBG_ERROR, "_AllocPageTableMemory: ERROR failed to map page tables"));
			return IMG_FALSE;
		}

		/* translate address to device physical */
		sDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);

		#if PAGE_TEST
		PageTest(psPTInfoList->PTPageCpuVAddr, sDevPAddr);
		#endif
	}

	MakeKernelPageReadWrite(psPTInfoList->PTPageCpuVAddr);
	/* Zero the page table. */
	OSMemSet(psPTInfoList->PTPageCpuVAddr, 0, pMMUHeap->ui32PTSize);
	MakeKernelPageReadOnly(psPTInfoList->PTPageCpuVAddr);

	/* return the DevPAddr */
	*psDevPAddr = sDevPAddr;

	return IMG_TRUE;
}


/*!
******************************************************************************
	FUNCTION:   _FreePageTableMemory

	PURPOSE:    Free physical memory for a page table

	PARAMETERS: In: pMMUHeap - the mmu
				In: psPTInfoList - PT info to free
	RETURNS:    NONE
******************************************************************************/
static IMG_VOID
_FreePageTableMemory (MMU_HEAP *pMMUHeap, MMU_PT_INFO *psPTInfoList)
{
	/*
		free the PT page:
		depending on the specific system, pagetables are allocated from system memory
		or device local memory.  For now, just look for at least a valid local heap/arena
	*/
	if(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena == IMG_NULL)
	{
		/* Force the page to read write before we free it*/
		MakeKernelPageReadWrite(psPTInfoList->PTPageCpuVAddr);

		//FIXME: replace with an RA, this allocator only handles 4k allocs
		OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
					  pMMUHeap->ui32PTSize,
					  psPTInfoList->PTPageCpuVAddr,
					  psPTInfoList->hPTPageOSMemHandle);
	}
	else
	{
		IMG_SYS_PHYADDR sSysPAddr;
		IMG_CPU_PHYADDR sCpuPAddr;

		/*  derive the system physical address */
		sCpuPAddr = OSMapLinToCPUPhys(psPTInfoList->hPTPageOSMemHandle, 
									  psPTInfoList->PTPageCpuVAddr);
		sSysPAddr = SysCpuPAddrToSysPAddr (sCpuPAddr);

		/* unmap the CPU mapping */
		/* note: actual ammount is pMMUHeap->ui32PTSize but must be a multiple of 4k pages */
		OSUnMapPhysToLin(psPTInfoList->PTPageCpuVAddr,
                         SGX_MMU_PAGE_SIZE,
                         PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
                         psPTInfoList->hPTPageOSMemHandle);

		/*
			just free from the first local memory arena
			(unlikely to be more than one local mem area(?))
		*/
		RA_Free (pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);
	}
}



/*!
******************************************************************************
	FUNCTION:   _DeferredFreePageTable

	PURPOSE:    Free one page table associated with an MMU.

	PARAMETERS: In:  pMMUHeap - the mmu heap
				In:  ui32PTIndex - index of the page table to free relative
								   to the base of heap.
	RETURNS:    None
******************************************************************************/
static IMG_VOID
_DeferredFreePageTable (MMU_HEAP *pMMUHeap, IMG_UINT32 ui32PTIndex, IMG_BOOL bOSFreePT)
{
	IMG_UINT32 *pui32PDEntry;
	IMG_UINT32 i;
	IMG_UINT32 ui32PDIndex;
	SYS_DATA *psSysData;
	MMU_PT_INFO **ppsPTInfoList;

	SysAcquireData(&psSysData);

	/* find the index/offset in PD entries  */
	ui32PDIndex = pMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* set the base PT info */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

	{
#if PT_DEBUG
		if(ppsPTInfoList[ui32PTIndex] && ppsPTInfoList[ui32PTIndex]->ui32ValidPTECount > 0)
		{
			DumpPT(ppsPTInfoList[ui32PTIndex]);
			/* Fall-through, will fail assert */
		}
#endif

		/* Assert that all mappings have gone */
		PVR_ASSERT(ppsPTInfoList[ui32PTIndex] == IMG_NULL || ppsPTInfoList[ui32PTIndex]->ui32ValidPTECount == 0);
	}

	switch(pMMUHeap->psDevArena->DevMemHeapType)
	{
		case DEVICE_MEMORY_HEAP_SHARED :
		case DEVICE_MEMORY_HEAP_SHARED_EXPORTED :
		{
			/* Remove Page Table from all memory contexts */
			MMU_CONTEXT *psMMUContext = (MMU_CONTEXT*)pMMUHeap->psMMUContext->psDevInfo->pvMMUContextList;

			while(psMMUContext)
			{
				/* get the PD CPUVAddr base and advance to the first entry */
				MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
				pui32PDEntry = (IMG_UINT32*)psMMUContext->pvPDCpuVAddr;
				pui32PDEntry += ui32PDIndex;

				/* free the entry */
				if(bOSFreePT)
				{
					pui32PDEntry[ui32PTIndex] = 0;
				}
				MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);
				/* advance to next context */
				psMMUContext = psMMUContext->psNext;
			}
			break;
		}
		case DEVICE_MEMORY_HEAP_PERCONTEXT :
		case DEVICE_MEMORY_HEAP_KERNEL :
		{
			MakeKernelPageReadWrite(pMMUHeap->psMMUContext->pvPDCpuVAddr);
			/* Remove Page Table from this memory context only */
			pui32PDEntry = (IMG_UINT32*)pMMUHeap->psMMUContext->pvPDCpuVAddr;
			pui32PDEntry += ui32PDIndex;

			/* free the entry */
			if(bOSFreePT)
			{
				pui32PDEntry[ui32PTIndex] = 0;
			}
			MakeKernelPageReadOnly(pMMUHeap->psMMUContext->pvPDCpuVAddr);
			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR, "_DeferredFreePagetable: ERROR invalid heap type"));
			return;
		}
	}

	/* clear the PT entries in each PT page */
	if(ppsPTInfoList[ui32PTIndex] != IMG_NULL)
	{
		if(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr != IMG_NULL)
		{
			IMG_PUINT32 pui32Tmp;

			MakeKernelPageReadWrite(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr);
			pui32Tmp = (IMG_UINT32*)ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr;

			/* clear the entries */
			for(i=0;
				(i<pMMUHeap->ui32PTETotalUsable) && (i<pMMUHeap->ui32PTNumEntriesUsable);
				 i++)
			{
				/* over-allocated PT entries for 4MB data page case should never be non-zero */
				pui32Tmp[i] = 0;
			}
			MakeKernelPageReadOnly(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr);

			/*
				free the pagetable memory
			*/
			if(bOSFreePT)
			{
				_FreePageTableMemory(pMMUHeap, ppsPTInfoList[ui32PTIndex]);
			}

			/*
				decrement the PT Entry Count by the number
				of entries we've cleared in this pass
			*/
			pMMUHeap->ui32PTETotalUsable -= i;
		}
		else
		{
			/* decrement the PT Entry Count by a page's worth of entries  */
			pMMUHeap->ui32PTETotalUsable -= pMMUHeap->ui32PTNumEntriesUsable;
		}

		if(bOSFreePT)
		{
			/* free the pt info */
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof(MMU_PT_INFO),
						ppsPTInfoList[ui32PTIndex],
						IMG_NULL);
			ppsPTInfoList[ui32PTIndex] = IMG_NULL;
		}
	}
	else
	{
		/* decrement the PT Entry Count by a page's worth of usable entries */
		pMMUHeap->ui32PTETotalUsable -= pMMUHeap->ui32PTNumEntriesUsable;
	}
}

/*!
******************************************************************************
	FUNCTION:   _DeferredFreePageTables

	PURPOSE:    Free the page tables associated with an MMU.

	PARAMETERS: In:  pMMUHeap - the mmu
	RETURNS:    None
******************************************************************************/
static IMG_VOID
_DeferredFreePageTables (MMU_HEAP *pMMUHeap)
{
	IMG_UINT32 i;
	for(i=0; i<pMMUHeap->ui32PageTableCount; i++)
	{
		_DeferredFreePageTable(pMMUHeap, i, IMG_TRUE);
	}
	MMU_InvalidateDirectoryCache(pMMUHeap->psMMUContext->psDevInfo);
}


/*!
******************************************************************************
	FUNCTION:   _DeferredAllocPagetables

	PURPOSE:    allocates page tables at time of allocation

	PARAMETERS: In:  pMMUHeap - the mmu heap
					 DevVAddr - devVAddr of allocation
					 ui32Size - size of allocation
	RETURNS:    IMG_TRUE - Success
	            IMG_FALSE - Failed
******************************************************************************/
static IMG_BOOL
_DeferredAllocPagetables(MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR DevVAddr, IMG_UINT32 ui32Size)
{
	IMG_UINT32 ui32PageTableCount;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 i;
	IMG_UINT32 *pui32PDEntry;
	MMU_PT_INFO **ppsPTInfoList;
	SYS_DATA *psSysData;
	IMG_DEV_VIRTADDR sHighDevVAddr;

	/* Check device linear address */
	PVR_ASSERT(DevVAddr.uiAddr < (1<<SGX_FEATURE_ADDRESS_SPACE_SIZE));

	/* get the sysdata */
	SysAcquireData(&psSysData);

	/* find the index/offset in PD entries  */
	ui32PDIndex = DevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* how many PDs does the allocation occupy? */
	/* first check for overflows */
	if((UINT32_MAX_VALUE - DevVAddr.uiAddr)
		< (ui32Size + pMMUHeap->ui32DataPageMask + pMMUHeap->ui32PTMask))
	{
		/* detected overflow, clamp to highest address */
		sHighDevVAddr.uiAddr = UINT32_MAX_VALUE;
	}
	else
	{
		sHighDevVAddr.uiAddr = DevVAddr.uiAddr
								+ ui32Size
								+ pMMUHeap->ui32DataPageMask
								+ pMMUHeap->ui32PTMask;
	}

	ui32PageTableCount = sHighDevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* Fix allocation of last 4MB */
	if (ui32PageTableCount == 0)
		ui32PageTableCount = 1024;

	ui32PageTableCount -= ui32PDIndex;

	/* get the PD CPUVAddr base and advance to the first entry */
	pui32PDEntry = (IMG_UINT32*)pMMUHeap->psMMUContext->pvPDCpuVAddr;
	pui32PDEntry += ui32PDIndex;

	/* and advance to the first PT info list */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

	/* walk the psPTInfoList to see what needs allocating: */
	for(i=0; i<ui32PageTableCount; i++)
	{
		if(ppsPTInfoList[i] == IMG_NULL)
		{
			OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						 sizeof (MMU_PT_INFO),
						 (IMG_VOID **)&ppsPTInfoList[i], IMG_NULL,
						 "MMU Page Table Info");
			if (ppsPTInfoList[i] == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR call to OSAllocMem failed"));
				return IMG_FALSE;
			}
			OSMemSet (ppsPTInfoList[i], 0, sizeof(MMU_PT_INFO));
		}
		if(ppsPTInfoList[i]->hPTPageOSMemHandle == IMG_NULL
		&& ppsPTInfoList[i]->PTPageCpuVAddr == IMG_NULL)
		{
			IMG_DEV_PHYADDR	sDevPAddr;
			/* no page table has been allocated so allocate one */
			PVR_ASSERT(pui32PDEntry[i] == 0);
			if(_AllocPageTableMemory (pMMUHeap, ppsPTInfoList[i], &sDevPAddr) != IMG_TRUE)
			{
				PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR call to _AllocPageTableMemory failed"));
				return IMG_FALSE;
			}
			switch(pMMUHeap->psDevArena->DevMemHeapType)
			{
				case DEVICE_MEMORY_HEAP_SHARED :
				case DEVICE_MEMORY_HEAP_SHARED_EXPORTED :
				{
					/* insert Page Table into all memory contexts */
					MMU_CONTEXT *psMMUContext = (MMU_CONTEXT*)pMMUHeap->psMMUContext->psDevInfo->pvMMUContextList;

					while(psMMUContext)
					{
						MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
						/* get the PD CPUVAddr base and advance to the first entry */
						pui32PDEntry = (IMG_UINT32*)psMMUContext->pvPDCpuVAddr;
						pui32PDEntry += ui32PDIndex;

						/* insert the page, specify the data page size and make the pde valid */
						pui32PDEntry[i] = (sDevPAddr.uiAddr>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
										| pMMUHeap->ui32PDEPageSizeCtrl
										| SGX_MMU_PDE_VALID;
						MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);
						/* advance to next context */
						psMMUContext = psMMUContext->psNext;
					}
					break;
				}
				case DEVICE_MEMORY_HEAP_PERCONTEXT :
				case DEVICE_MEMORY_HEAP_KERNEL :
				{
					MakeKernelPageReadWrite(pMMUHeap->psMMUContext->pvPDCpuVAddr);
					/* insert Page Table into only this memory context */
					pui32PDEntry[i] = (sDevPAddr.uiAddr>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
									| pMMUHeap->ui32PDEPageSizeCtrl
									| SGX_MMU_PDE_VALID;
					MakeKernelPageReadOnly(pMMUHeap->psMMUContext->pvPDCpuVAddr);
					break;
				}
				default:
				{
					PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR invalid heap type"));
					return IMG_FALSE;
				}
			}

			/* This is actually not to do with multiple mem contexts, but to do with the directory cache.
			   In the 1 context implementation of the MMU, the directory "cache" is actually a copy of the
			   page directory memory, and requires updating whenever the page directory changes, even if there
			   was no previous value in a particular entry
			 */
			MMU_InvalidateDirectoryCache(pMMUHeap->psMMUContext->psDevInfo);
		}
		else
		{
			/* already have an allocated PT */
			PVR_ASSERT(pui32PDEntry[i] != 0);
		}
	}

	return IMG_TRUE;
}


/*!
******************************************************************************
	FUNCTION:   MMU_Initialise

	PURPOSE:	Called from BM_CreateContext.
				Allocates the top level Page Directory 4k Page for the new context.

	PARAMETERS: None
	RETURNS:    PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR
MMU_Initialise (PVRSRV_DEVICE_NODE *psDeviceNode, MMU_CONTEXT **ppsMMUContext, IMG_DEV_PHYADDR *psPDDevPAddr)
{
	IMG_UINT32 *pui32Tmp;
	IMG_UINT32 i;
	IMG_CPU_VIRTADDR pvPDCpuVAddr;
	IMG_DEV_PHYADDR sPDDevPAddr;
	IMG_CPU_PHYADDR sCpuPAddr;
	MMU_CONTEXT *psMMUContext;
	IMG_HANDLE hPDOSMemHandle = IMG_NULL;
	SYS_DATA *psSysData;
	PVRSRV_SGXDEV_INFO *psDevInfo;
	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Initialise"));

	SysAcquireData(&psSysData);

	OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				 sizeof (MMU_CONTEXT),
				 (IMG_VOID **)&psMMUContext, IMG_NULL,
				 "MMU Context");
	if (psMMUContext == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocMem failed"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet (psMMUContext, 0, sizeof(MMU_CONTEXT));

	/* stick the devinfo in the context for subsequent use */
	psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	psMMUContext->psDevInfo = psDevInfo;

	/* record device node for subsequent use */
	psMMUContext->psDeviceNode = psDeviceNode;

	/* allocate 4k page directory page for the new context */
	if(psDeviceNode->psLocalDevMemArena == IMG_NULL)
	{
		if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						 SGX_MMU_PAGE_SIZE,
						 SGX_MMU_PAGE_SIZE,
						 IMG_NULL,
						 0,
						 IMG_NULL,
						 &pvPDCpuVAddr,
						 &hPDOSMemHandle) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
			return PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES;
		}

		if(pvPDCpuVAddr)
		{
			sCpuPAddr = OSMapLinToCPUPhys(hPDOSMemHandle,
										  pvPDCpuVAddr);
		}
		else
		{
			/* This is not used in all cases, since not all ports currently
			 * support OSMemHandleToCpuPAddr */
			sCpuPAddr = OSMemHandleToCpuPAddr(hPDOSMemHandle, 0);
		}
		sPDDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);

		#if PAGE_TEST
		PageTest(pvPDCpuVAddr, sPDDevPAddr);
		#endif
	}
	else
	{
		IMG_SYS_PHYADDR sSysPAddr;

		/* allocate from the device's local memory arena */
		if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
					SGX_MMU_PAGE_SIZE,
					IMG_NULL,
					IMG_NULL,
					0,
					SGX_MMU_PAGE_SIZE,
					0,
					IMG_NULL,
					0,
					&(sSysPAddr.uiAddr))!= IMG_TRUE)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
			return PVRSRV_ERROR_FAILED_TO_ALLOC_VIRT_MEMORY;
		}

		/* derive the CPU virtual address */
		sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
		sPDDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
		pvPDCpuVAddr = OSMapPhysToLin(sCpuPAddr,
										SGX_MMU_PAGE_SIZE,
										PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
										&hPDOSMemHandle);
		if(!pvPDCpuVAddr)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
			return PVRSRV_ERROR_FAILED_TO_MAP_PAGE_TABLE;
		}

		#if PAGE_TEST
		PageTest(pvPDCpuVAddr, sPDDevPAddr);
		#endif
	}

#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(psMMUContext);
#endif

	if (pvPDCpuVAddr)
	{
		pui32Tmp = (IMG_UINT32 *)pvPDCpuVAddr;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: pvPDCpuVAddr invalid"));
		return PVRSRV_ERROR_INVALID_CPU_ADDR;
	}

	/* initialise the PD to invalid address state */
	MakeKernelPageReadWrite(pvPDCpuVAddr);
	for(i=0; i<SGX_MMU_PD_SIZE; i++)
	{
		/* invalid, no read, no write, no cache consistency */
		pui32Tmp[i] = 0;
	}
	MakeKernelPageReadOnly(pvPDCpuVAddr);

	/* store PD info in the MMU context */
	psMMUContext->pvPDCpuVAddr = pvPDCpuVAddr;
	psMMUContext->sPDDevPAddr = sPDDevPAddr;
	psMMUContext->hPDOSMemHandle = hPDOSMemHandle;

	/* Get some process information to aid debug */
	psMMUContext->ui32PID = OSGetCurrentProcessIDKM();
	psMMUContext->szName[0] = '\0';
	OSGetCurrentProcessNameKM(psMMUContext->szName, MMU_CONTEXT_NAME_SIZE);

	/* return context */
	*ppsMMUContext = psMMUContext;

	/* return the PD DevVAddr */
	*psPDDevPAddr = sPDDevPAddr;


	/* add the new MMU context onto the list of MMU contexts */
	psMMUContext->psNext = (MMU_CONTEXT*)psDevInfo->pvMMUContextList;
	psDevInfo->pvMMUContextList = (IMG_VOID*)psMMUContext;

#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(psMMUContext);
#endif

	return PVRSRV_OK;
}

/*!
******************************************************************************
	FUNCTION:   MMU_Finalise

	PURPOSE:    Finalise the mmu module, deallocate all resources.

	PARAMETERS: In: psMMUContext - MMU context to deallocate
	RETURNS:    None.
******************************************************************************/
IMG_VOID
MMU_Finalise (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 *pui32Tmp, i;
	SYS_DATA *psSysData;
	MMU_CONTEXT **ppsMMUContext;

	SysAcquireData(&psSysData);

	pui32Tmp = (IMG_UINT32 *)psMMUContext->pvPDCpuVAddr;

	MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
	/* initialise the PD to invalid address state */
	for(i=0; i<SGX_MMU_PD_SIZE; i++)
	{
		/* invalid, no read, no write, no cache consistency */
		pui32Tmp[i] = 0;
	}
	MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);

	/*
		free the PD:
		depending on the specific system, the PD is allocated from system memory
		or device local memory.  For now, just look for at least a valid local heap/arena
	*/
	if(psMMUContext->psDeviceNode->psLocalDevMemArena == IMG_NULL)
	{
		MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
		OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						SGX_MMU_PAGE_SIZE,
						psMMUContext->pvPDCpuVAddr,
						psMMUContext->hPDOSMemHandle);
	}
	else
	{
		IMG_SYS_PHYADDR sSysPAddr;
		IMG_CPU_PHYADDR sCpuPAddr;

		/*  derive the system physical address */
		sCpuPAddr = OSMapLinToCPUPhys(psMMUContext->hPDOSMemHandle,
									  psMMUContext->pvPDCpuVAddr);
		sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);

		/* unmap the CPU mapping */
		OSUnMapPhysToLin(psMMUContext->pvPDCpuVAddr,
							SGX_MMU_PAGE_SIZE,
                            PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
							psMMUContext->hPDOSMemHandle);
		/* and free the memory */
		RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);
	}

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Finalise"));

	/* remove the MMU context from the list of MMU contexts */
	ppsMMUContext = (MMU_CONTEXT**)&psMMUContext->psDevInfo->pvMMUContextList;
	while(*ppsMMUContext)
	{
		if(*ppsMMUContext == psMMUContext)
		{
			/* remove item from the list */
			*ppsMMUContext = psMMUContext->psNext;
			break;
		}

		/* advance to next next */
		ppsMMUContext = &((*ppsMMUContext)->psNext);
	}

	/* free the context itself. */
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_CONTEXT), psMMUContext, IMG_NULL);
	/*not nulling pointer, copy on stack*/
}


/*!
******************************************************************************
	FUNCTION:   MMU_InsertHeap

	PURPOSE:    Copies PDEs from shared/exported heap into current MMU context.

	PARAMETERS:	In:  psMMUContext - the mmu
	            In:  psMMUHeap - a shared/exported heap

	RETURNS:	None
******************************************************************************/
IMG_VOID
MMU_InsertHeap(MMU_CONTEXT *psMMUContext, MMU_HEAP *psMMUHeap)
{
	IMG_UINT32 *pui32PDCpuVAddr = (IMG_UINT32 *) psMMUContext->pvPDCpuVAddr;
	IMG_UINT32 *pui32KernelPDCpuVAddr = (IMG_UINT32 *) psMMUHeap->psMMUContext->pvPDCpuVAddr;
	IMG_UINT32 ui32PDEntry;
	IMG_BOOL bInvalidateDirectoryCache = IMG_FALSE;

	/* advance to the first entry */
	pui32PDCpuVAddr += psMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> psMMUHeap->ui32PDShift;
	pui32KernelPDCpuVAddr += psMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> psMMUHeap->ui32PDShift;

	/*
		update the PD range relating to the heap's
		device virtual address range
	*/
#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(psMMUContext);
#endif

	for (ui32PDEntry = 0; ui32PDEntry < psMMUHeap->ui32PageTableCount; ui32PDEntry++)
	{
		/* check we have invalidated target PDEs */
		PVR_ASSERT(pui32PDCpuVAddr[ui32PDEntry] == 0);
		MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
		/* copy over the PDEs */
		pui32PDCpuVAddr[ui32PDEntry] = pui32KernelPDCpuVAddr[ui32PDEntry];
		MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);
		if (pui32PDCpuVAddr[ui32PDEntry])
		{
			/* Ensure the shared heap allocation is mapped into the context/PD
			 * for the active pdump process/app. The PTs and backing physical
			 * should also be pdumped (elsewhere).
			 *		MALLOC (PT)
			 *		LDB (init PT)
			 *		MALLOC (data page)
			 *		WRW (PTE->data page)
			 *		LDB (init data page) -- could be useful to ensure page is initialised
			 */
			bInvalidateDirectoryCache = IMG_TRUE;
		}
	}

#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(psMMUContext);
#endif

	if (bInvalidateDirectoryCache)
	{
		/* This is actually not to do with multiple mem contexts, but to do with the directory cache.
			In the 1 context implementation of the MMU, the directory "cache" is actually a copy of the
			page directory memory, and requires updating whenever the page directory changes, even if there
			was no previous value in a particular entry
		*/
		MMU_InvalidateDirectoryCache(psMMUContext->psDevInfo);
	}
}


/*!
******************************************************************************
	FUNCTION:   MMU_UnmapPagesAndFreePTs

	PURPOSE:    unmap pages, invalidate virtual address and try to free the PTs

	PARAMETERS:	In:  psMMUHeap - the mmu.
	            In:  sDevVAddr - the device virtual address.
	            In:  ui32PageCount - page count
	            In:  hUniqueTag - A unique ID for use as a tag identifier

	RETURNS:	None
******************************************************************************/
static IMG_VOID
MMU_UnmapPagesAndFreePTs (MMU_HEAP *psMMUHeap,
						  IMG_DEV_VIRTADDR sDevVAddr,
						  IMG_UINT32 ui32PageCount,
						  IMG_HANDLE hUniqueTag)
{
	IMG_DEV_VIRTADDR	sTmpDevVAddr;
	IMG_UINT32			i;
	IMG_UINT32			ui32PDIndex;
	IMG_UINT32			ui32PTIndex;
	IMG_UINT32			*pui32Tmp;
	IMG_BOOL			bInvalidateDirectoryCache = IMG_FALSE;

	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
	/* setup tmp devvaddr to base of allocation */
	sTmpDevVAddr = sDevVAddr;

	for(i=0; i<ui32PageCount; i++)
	{
		MMU_PT_INFO **ppsPTInfoList;

		/* find the index/offset in PD entries  */
		ui32PDIndex = sTmpDevVAddr.uiAddr >> psMMUHeap->ui32PDShift;

		/* and advance to the first PT info list */
		ppsPTInfoList = &psMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

		{
			/* find the index/offset of the first PT in the first PT page */
			ui32PTIndex = (sTmpDevVAddr.uiAddr & psMMUHeap->ui32PTMask) >> psMMUHeap->ui32PTShift;

			/* Is the PT page valid? */
			if (!ppsPTInfoList[0])
			{
				/*
					With sparse mappings we expect that the PT could be freed
					before we reach the end of it as the unmapped pages don't
					bump ui32ValidPTECount so it can reach zero before we reach
					the end of the PT.
				*/
				if (!psMMUHeap->bHasSparseMappings)
				{
					PVR_DPF((PVR_DBG_MESSAGE, "MMU_UnmapPagesAndFreePTs: Invalid PT for alloc at VAddr:0x%08X (VaddrIni:0x%08X AllocPage:%u) PDIdx:%u PTIdx:%u",sTmpDevVAddr.uiAddr, sDevVAddr.uiAddr,i, ui32PDIndex, ui32PTIndex ));
				}

				/* advance the sTmpDevVAddr by one page */
				sTmpDevVAddr.uiAddr += psMMUHeap->ui32DataPageSize;

				/* Try to unmap the remaining allocation pages */
				continue;
			}

			/* setup pointer to the first entry in the PT page */
			pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

			/* Is PTPageCpuVAddr valid ? */
			if (!pui32Tmp)
			{
				continue;
			}

			CheckPT(ppsPTInfoList[0]);

			/* Decrement the valid page count only if the current page is valid*/
			if (pui32Tmp[ui32PTIndex] & SGX_MMU_PTE_VALID)
			{
				ppsPTInfoList[0]->ui32ValidPTECount--;
			}
			else
			{
				if (!psMMUHeap->bHasSparseMappings)
				{
					PVR_DPF((PVR_DBG_MESSAGE, "MMU_UnmapPagesAndFreePTs: Page is already invalid for alloc at VAddr:0x%08X (VAddrIni:0x%08X AllocPage:%u) PDIdx:%u PTIdx:%u",sTmpDevVAddr.uiAddr, sDevVAddr.uiAddr,i, ui32PDIndex, ui32PTIndex ));
				}
			}

			/* The page table count should not go below zero */
			PVR_ASSERT((IMG_INT32)ppsPTInfoList[0]->ui32ValidPTECount >= 0);
			MakeKernelPageReadWrite(ppsPTInfoList[0]->PTPageCpuVAddr);
			/* invalidate entry */
			pui32Tmp[ui32PTIndex] = 0;
			MakeKernelPageReadOnly(ppsPTInfoList[0]->PTPageCpuVAddr);
			CheckPT(ppsPTInfoList[0]);
		}

		/*
			Free a page table if we can.
		*/
		if (ppsPTInfoList[0] && (ppsPTInfoList[0]->ui32ValidPTECount == 0)
			)
		{
			_DeferredFreePageTable(psMMUHeap, ui32PDIndex - psMMUHeap->ui32PDBaseIndex, IMG_TRUE);
			bInvalidateDirectoryCache = IMG_TRUE;
		}

		/* advance the sTmpDevVAddr by one page */
		sTmpDevVAddr.uiAddr += psMMUHeap->ui32DataPageSize;
	}

	if(bInvalidateDirectoryCache)
	{
		MMU_InvalidateDirectoryCache(psMMUHeap->psMMUContext->psDevInfo);
	}
	else
	{
		MMU_InvalidatePageTableCache(psMMUHeap->psMMUContext->psDevInfo);
	}
}


/*!
******************************************************************************
	FUNCTION:   MMU_FreePageTables

	PURPOSE:    Call back from RA_Free to zero page table entries used by freed
				spans.

	PARAMETERS: In: pvMMUHeap
				In: ui32Start
				In: ui32End
				In: hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:
******************************************************************************/
static IMG_VOID MMU_FreePageTables(IMG_PVOID pvMMUHeap,
                                   IMG_SIZE_T ui32Start,
                                   IMG_SIZE_T ui32End,
                                   IMG_HANDLE hUniqueTag)
{
	MMU_HEAP *pMMUHeap = (MMU_HEAP*)pvMMUHeap;
	IMG_DEV_VIRTADDR Start;

	Start.uiAddr = (IMG_UINT32)ui32Start;

	MMU_UnmapPagesAndFreePTs(pMMUHeap, Start, (IMG_UINT32)((ui32End - ui32Start) >> pMMUHeap->ui32PTShift), hUniqueTag);
}

/*!
******************************************************************************
	FUNCTION:   MMU_Create

	PURPOSE:    Create an mmu device virtual heap.

	PARAMETERS: In: psMMUContext - MMU context
	            In: psDevArena - device memory resource arena
				Out: ppsVMArena - virtual mapping arena
	RETURNS:	MMU_HEAP
	RETURNS:
******************************************************************************/
MMU_HEAP *
MMU_Create (MMU_CONTEXT *psMMUContext,
			DEV_ARENA_DESCRIPTOR *psDevArena,
			RA_ARENA **ppsVMArena,
			PDUMP_MMU_ATTRIB **ppsMMUAttrib)
{
	MMU_HEAP *pMMUHeap;
	IMG_UINT32 ui32ScaleSize;

	PVR_UNREFERENCED_PARAMETER(ppsMMUAttrib);

	PVR_ASSERT (psDevArena != IMG_NULL);

	if (psDevArena == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: invalid parameter"));
		return IMG_NULL;
	}

	OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				 sizeof (MMU_HEAP),
				 (IMG_VOID **)&pMMUHeap, IMG_NULL,
				 "MMU Heap");
	if (pMMUHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: ERROR call to OSAllocMem failed"));
		return IMG_NULL;
	}

	pMMUHeap->psMMUContext = psMMUContext;
	pMMUHeap->psDevArena = psDevArena;

	/*
		generate page table and data page mask and shift values
		based on the data page size
	*/
	switch(pMMUHeap->psDevArena->ui32DataPageSize)
	{
		case 0x1000:
			ui32ScaleSize = 0;
			pMMUHeap->ui32PDEPageSizeCtrl = SGX_MMU_PDE_PAGE_SIZE_4K;
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR, "MMU_Create: invalid data page size"));
			goto ErrorFreeHeap;
	}

	/* number of bits of address offset into the data page */
	pMMUHeap->ui32DataPageSize = psDevArena->ui32DataPageSize;
	pMMUHeap->ui32DataPageBitWidth = SGX_MMU_PAGE_SHIFT + ui32ScaleSize;
	pMMUHeap->ui32DataPageMask = pMMUHeap->ui32DataPageSize - 1;
	/* number of bits of address indexing into a pagetable */
	pMMUHeap->ui32PTShift = pMMUHeap->ui32DataPageBitWidth;
	pMMUHeap->ui32PTBitWidth = SGX_MMU_PT_SHIFT - ui32ScaleSize;
	pMMUHeap->ui32PTMask = SGX_MMU_PT_MASK & (SGX_MMU_PT_MASK<<ui32ScaleSize);
	pMMUHeap->ui32PTSize = (IMG_UINT32)(1UL<<pMMUHeap->ui32PTBitWidth) * sizeof(IMG_UINT32);

	/* note: PT size must be at least 4 entries, even for 4Mb data page size */
	if(pMMUHeap->ui32PTSize < 4 * sizeof(IMG_UINT32))
	{
		pMMUHeap->ui32PTSize = 4 * sizeof(IMG_UINT32);
	}
	pMMUHeap->ui32PTNumEntriesAllocated = pMMUHeap->ui32PTSize >> 2;

	/* find the number of actual PT entries per PD entry range. For 4MB data
	 * pages we only use the first entry although the PT has 16 byte allocation/alignment
	 * (due to 4 LSbits of the PDE are reserved for control) */
	pMMUHeap->ui32PTNumEntriesUsable = (IMG_UINT32)(1UL << pMMUHeap->ui32PTBitWidth);

	/* number of bits of address indexing into a page directory */
	pMMUHeap->ui32PDShift = pMMUHeap->ui32PTBitWidth + pMMUHeap->ui32PTShift;
	pMMUHeap->ui32PDBitWidth = SGX_FEATURE_ADDRESS_SPACE_SIZE - pMMUHeap->ui32PTBitWidth - pMMUHeap->ui32DataPageBitWidth;
	pMMUHeap->ui32PDMask = SGX_MMU_PD_MASK & (SGX_MMU_PD_MASK>>(32-SGX_FEATURE_ADDRESS_SPACE_SIZE));

	/* External system cache violates this rule */
#if !defined (SUPPORT_EXTERNAL_SYSTEM_CACHE)
	/*
		The heap must start on a PT boundary to avoid PT sharing across heaps
		The only exception is the first heap which can start at any address
		from 0 to the end of the first PT boundary
	*/
	if(psDevArena->BaseDevVAddr.uiAddr > (pMMUHeap->ui32DataPageMask | pMMUHeap->ui32PTMask))
	{
		/*
			if for some reason the first heap starts after the end of the first PT boundary
			but is not aligned to a PT boundary then the assert will trigger unncessarily
		*/
		PVR_ASSERT ((psDevArena->BaseDevVAddr.uiAddr
						& (pMMUHeap->ui32DataPageMask
							| pMMUHeap->ui32PTMask)) == 0);
	}
#endif
	/* how many PT entries do we need? */
	pMMUHeap->ui32PTETotalUsable = pMMUHeap->psDevArena->ui32Size >> pMMUHeap->ui32PTShift;

	/* calculate the PD Base index for the Heap (required for page mapping) */
	pMMUHeap->ui32PDBaseIndex = (pMMUHeap->psDevArena->BaseDevVAddr.uiAddr & pMMUHeap->ui32PDMask) >> pMMUHeap->ui32PDShift;

	/*
		how many page tables?
		round up to nearest entries to the nearest page table sized block
	*/
	pMMUHeap->ui32PageTableCount = (pMMUHeap->ui32PTETotalUsable + pMMUHeap->ui32PTNumEntriesUsable - 1)
										>> pMMUHeap->ui32PTBitWidth;
	PVR_ASSERT(pMMUHeap->ui32PageTableCount > 0);

	/* Create the arena */
	pMMUHeap->psVMArena = RA_Create(psDevArena->pszName,
									psDevArena->BaseDevVAddr.uiAddr,
									psDevArena->ui32Size,
									IMG_NULL,
									MAX(HOST_PAGESIZE(), pMMUHeap->ui32DataPageSize),
									IMG_NULL,
									IMG_NULL,
									&MMU_FreePageTables,
									pMMUHeap);

	if (pMMUHeap->psVMArena == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: ERROR call to RA_Create failed"));
		goto ErrorFreePagetables;
	}

	/*
		And return the RA for VM arena management
	*/
	*ppsVMArena = pMMUHeap->psVMArena;

	return pMMUHeap;

	/* drop into here if errors */
ErrorFreePagetables:
	_DeferredFreePageTables (pMMUHeap);

ErrorFreeHeap:
	OSFreeMem (PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_HEAP), pMMUHeap, IMG_NULL);
	/*not nulling pointer, out of scope*/

	return IMG_NULL;
}

/*!
******************************************************************************
	FUNCTION:   MMU_Delete

	PURPOSE:    Delete an MMU device virtual heap.

	PARAMETERS: In:  pMMUHeap - The MMU heap to delete.
	RETURNS:
******************************************************************************/
IMG_VOID
MMU_Delete (MMU_HEAP *pMMUHeap)
{
	if (pMMUHeap != IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Delete"));

		if(pMMUHeap->psVMArena)
		{
			RA_Delete (pMMUHeap->psVMArena);
		}

#ifdef SUPPORT_SGX_MMU_BYPASS
		EnableHostAccess(pMMUHeap->psMMUContext);
#endif
		_DeferredFreePageTables (pMMUHeap);
#ifdef SUPPORT_SGX_MMU_BYPASS
		DisableHostAccess(pMMUHeap->psMMUContext);
#endif

		OSFreeMem (PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_HEAP), pMMUHeap, IMG_NULL);
		/*not nulling pointer, copy on stack*/
	}
}

/*!
******************************************************************************
	FUNCTION:   MMU_Alloc
	PURPOSE:    Allocate space in an mmu's virtual address space.
	PARAMETERS:	In:  pMMUHeap - MMU to allocate on.
	            In:  uSize - Size in bytes to allocate.
	            Out: pActualSize - If non null receives actual size allocated.
	            In:  uFlags - Allocation flags.
	            In:  uDevVAddrAlignment - Required alignment.
	            Out: DevVAddr - Receives base address of allocation.
	RETURNS:	IMG_TRUE - Success
	            IMG_FALSE - Failure
******************************************************************************/
IMG_BOOL
MMU_Alloc (MMU_HEAP *pMMUHeap,
		   IMG_SIZE_T uSize,
		   IMG_SIZE_T *pActualSize,
		   IMG_UINT32 uFlags,
		   IMG_UINT32 uDevVAddrAlignment,
		   IMG_DEV_VIRTADDR *psDevVAddr)
{
	IMG_BOOL bStatus;

	PVR_DPF ((PVR_DBG_MESSAGE,
		"MMU_Alloc: uSize=0x%x, flags=0x%x, align=0x%x",
		uSize, uFlags, uDevVAddrAlignment));

	/*
		Only allocate a VM address if the caller did not supply one
	*/
	if((uFlags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR) == 0)
	{
		IMG_UINTPTR_T uiAddr;

		bStatus = RA_Alloc (pMMUHeap->psVMArena,
							uSize,
							pActualSize,
							IMG_NULL,
							0,
							uDevVAddrAlignment,
							0,
							IMG_NULL,
							0,
							&uiAddr);
		if(!bStatus)
		{
			PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: RA_Alloc of VMArena failed"));
			PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: Alloc of DevVAddr failed from heap %s ID%d",
									pMMUHeap->psDevArena->pszName,
									pMMUHeap->psDevArena->ui32HeapID));
			return bStatus;
		}

		psDevVAddr->uiAddr = IMG_CAST_TO_DEVVADDR_UINT(uiAddr);
	}

	#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(pMMUHeap->psMMUContext);
	#endif

	/* allocate page tables to cover allocation as required */
	bStatus = _DeferredAllocPagetables(pMMUHeap, *psDevVAddr, (IMG_UINT32)uSize);

	#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(pMMUHeap->psMMUContext);
	#endif

	if (!bStatus)
	{
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: _DeferredAllocPagetables failed"));
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: Failed to alloc pagetable(s) for DevVAddr 0x%8.8x from heap %s ID%d",
								psDevVAddr->uiAddr,
								pMMUHeap->psDevArena->pszName,
								pMMUHeap->psDevArena->ui32HeapID));
		if((uFlags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR) == 0)
		{
			/* free the VM address */
			RA_Free (pMMUHeap->psVMArena, psDevVAddr->uiAddr, IMG_FALSE);
		}
	}

	return bStatus;
}

/*!
******************************************************************************
	FUNCTION:   MMU_Free
	PURPOSE:    Free space in an mmu's virtual address space.
	PARAMETERS:	In:  pMMUHeap - MMU to deallocate on.
	            In:  DevVAddr - Base address to deallocate.
	RETURNS:	None
******************************************************************************/
IMG_VOID
MMU_Free (MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR DevVAddr, IMG_UINT32 ui32Size)
{
	PVR_ASSERT (pMMUHeap != IMG_NULL);

	if (pMMUHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Free: invalid parameter"));
		return;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "MMU_Free: Freeing DevVAddr 0x%08X from heap %s ID%d",
								DevVAddr.uiAddr,
								pMMUHeap->psDevArena->pszName,
								pMMUHeap->psDevArena->ui32HeapID));

	if((DevVAddr.uiAddr >= pMMUHeap->psDevArena->BaseDevVAddr.uiAddr) &&
		(DevVAddr.uiAddr + ui32Size <= pMMUHeap->psDevArena->BaseDevVAddr.uiAddr + pMMUHeap->psDevArena->ui32Size))
	{
		RA_Free (pMMUHeap->psVMArena, DevVAddr.uiAddr, IMG_TRUE);
		return;
	}

	PVR_DPF((PVR_DBG_ERROR,"MMU_Free: Couldn't free DevVAddr %08X from heap %s ID%d (not in range of heap))",
							DevVAddr.uiAddr,
							pMMUHeap->psDevArena->pszName,
							pMMUHeap->psDevArena->ui32HeapID));
}

/*!
******************************************************************************
	FUNCTION:   MMU_Enable

	PURPOSE:    Enable an mmu. Establishes pages tables and takes the mmu out
	            of bypass and waits for the mmu to acknowledge enabled.

	PARAMETERS: In:  pMMUHeap - the mmu
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_Enable (MMU_HEAP *pMMUHeap)
{
	PVR_UNREFERENCED_PARAMETER(pMMUHeap);
	/* SGX mmu is always enabled (stub function) */
}

/*!
******************************************************************************
	FUNCTION:   MMU_Disable

	PURPOSE:    Disable an mmu, takes the mmu into bypass.

	PARAMETERS: In:  pMMUHeap - the mmu
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_Disable (MMU_HEAP *pMMUHeap)
{
	PVR_UNREFERENCED_PARAMETER(pMMUHeap);
	/* SGX mmu is always enabled (stub function) */
}


/*!
******************************************************************************
	FUNCTION:   MMU_MapPage

	PURPOSE:    Create a mapping for one page at a specified virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  DevPAddr - the device physical address of the page to map.
	            In:  ui32MemFlags - BM r/w/cache flags
	RETURNS:    None
******************************************************************************/
static IMG_VOID
MMU_MapPage (MMU_HEAP *pMMUHeap,
			 IMG_DEV_VIRTADDR DevVAddr,
			 IMG_DEV_PHYADDR DevPAddr,
			 IMG_UINT32 ui32MemFlags)
{
	IMG_UINT32 ui32Index;
	IMG_UINT32 *pui32Tmp;
	IMG_UINT32 ui32MMUFlags = 0;
	MMU_PT_INFO **ppsPTInfoList;

	/* check the physical alignment of the memory to map */
	PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

	/*
		unravel the read/write/cache flags
	*/
	if(((PVRSRV_MEM_READ|PVRSRV_MEM_WRITE) & ui32MemFlags) == (PVRSRV_MEM_READ|PVRSRV_MEM_WRITE))
	{
		/* read/write */
		ui32MMUFlags = 0;
	}
	else if(PVRSRV_MEM_READ & ui32MemFlags)
	{
		/* read only */
		ui32MMUFlags |= SGX_MMU_PTE_READONLY;
	}
	else if(PVRSRV_MEM_WRITE & ui32MemFlags)
	{
		/* write only */
		ui32MMUFlags |= SGX_MMU_PTE_WRITEONLY;
	}

	/* cache coherency */
	if(PVRSRV_MEM_CACHE_CONSISTENT & ui32MemFlags)
	{
		ui32MMUFlags |= SGX_MMU_PTE_CACHECONSISTENT;
	}

	/* EDM protection */
	if(PVRSRV_MEM_EDM_PROTECT & ui32MemFlags)
	{
		ui32MMUFlags |= SGX_MMU_PTE_EDMPROTECT;
	}

	/*
		we receive a device physical address for the page that is to be mapped
		and a device virtual address representing where it should be mapped to
	*/

	/* find the index/offset in PD entries  */
	ui32Index = DevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* and advance to the first PT info list */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32Index];

	CheckPT(ppsPTInfoList[0]);

	/* find the index/offset of the first PT in the first PT page */
	ui32Index = (DevVAddr.uiAddr & pMMUHeap->ui32PTMask) >> pMMUHeap->ui32PTShift;

	/* setup pointer to the first entry in the PT page */
	pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

	{
		IMG_UINT32 uTmp = pui32Tmp[ui32Index];
		
		/* Is the current page already valid? (should not be unless it was allocated and not deallocated) */
		if ((uTmp & SGX_MMU_PTE_VALID) != 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_MapPage: Page is already valid for alloc at VAddr:0x%08X PDIdx:%u PTIdx:%u",
									DevVAddr.uiAddr,
									DevVAddr.uiAddr >> pMMUHeap->ui32PDShift,
									ui32Index ));
			PVR_DPF((PVR_DBG_ERROR, "MMU_MapPage: Page table entry value: 0x%08X", uTmp));
			PVR_DPF((PVR_DBG_ERROR, "MMU_MapPage: Physical page to map: 0x%08X", DevPAddr.uiAddr));
#if PT_DUMP
			DumpPT(ppsPTInfoList[0]);
#endif
		}
		PVR_ASSERT((uTmp & SGX_MMU_PTE_VALID) == 0);
	}

	/* One more valid entry in the page table. */
	ppsPTInfoList[0]->ui32ValidPTECount++;

	MakeKernelPageReadWrite(ppsPTInfoList[0]->PTPageCpuVAddr);
	/* map in the physical page */
	pui32Tmp[ui32Index] = ((DevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
						& ((~pMMUHeap->ui32DataPageMask)>>SGX_MMU_PTE_ADDR_ALIGNSHIFT))
						| SGX_MMU_PTE_VALID
						| ui32MMUFlags;
	MakeKernelPageReadOnly(ppsPTInfoList[0]->PTPageCpuVAddr);
	CheckPT(ppsPTInfoList[0]);
}


/*!
******************************************************************************
	FUNCTION:   MMU_MapScatter

	PURPOSE:    Create a linear mapping for a range of pages at a specified
	            virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  psSysAddr - the device physical address of the page to
	                 map.
	            In:  uSize - size of memory range in bytes
                In:  ui32MemFlags - page table flags.
	            In:  hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapScatter (MMU_HEAP *pMMUHeap,
				IMG_DEV_VIRTADDR DevVAddr,
				IMG_SYS_PHYADDR *psSysAddr,
				IMG_SIZE_T uSize,
				IMG_UINT32 ui32MemFlags,
				IMG_HANDLE hUniqueTag)
{
	IMG_UINT32 uCount, i;
	IMG_DEV_PHYADDR DevPAddr;

	PVR_ASSERT (pMMUHeap != IMG_NULL);

	PVR_UNREFERENCED_PARAMETER(hUniqueTag);

	for (i=0, uCount=0; uCount<uSize; i++, uCount+=pMMUHeap->ui32DataPageSize)
	{
		IMG_SYS_PHYADDR sSysAddr;

		sSysAddr = psSysAddr[i];


		/* check the physical alignment of the memory to map */
		PVR_ASSERT((sSysAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

		DevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysAddr);

		MMU_MapPage (pMMUHeap, DevVAddr, DevPAddr, ui32MemFlags);
		DevVAddr.uiAddr += pMMUHeap->ui32DataPageSize;

		PVR_DPF ((PVR_DBG_MESSAGE,
				 "MMU_MapScatter: devVAddr=%08X, SysAddr=%08X, size=0x%x/0x%x",
				  DevVAddr.uiAddr, sSysAddr.uiAddr, uCount, uSize));
	}
}

/*!
******************************************************************************
	FUNCTION:   MMU_MapPages

	PURPOSE:    Create a linear mapping for a ranege of pages at a specified
	            virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  SysPAddr - the system physical address of the page to
	                 map.
	            In:  uSize - size of memory range in bytes
                In:  ui32MemFlags - page table flags.
	            In:  hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapPages (MMU_HEAP *pMMUHeap,
			  IMG_DEV_VIRTADDR DevVAddr,
			  IMG_SYS_PHYADDR SysPAddr,
			  IMG_SIZE_T uSize,
			  IMG_UINT32 ui32MemFlags,
			  IMG_HANDLE hUniqueTag)
{
	IMG_DEV_PHYADDR DevPAddr;
	IMG_UINT32 uCount;
	IMG_UINT32 ui32VAdvance;
	IMG_UINT32 ui32PAdvance;

	PVR_ASSERT (pMMUHeap != IMG_NULL);

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_MapPages: heap:%s, heap_id:%d devVAddr=%08X, SysPAddr=%08X, size=0x%x",
								pMMUHeap->psDevArena->pszName,
								pMMUHeap->psDevArena->ui32HeapID,
								DevVAddr.uiAddr, 
								SysPAddr.uiAddr,
								uSize));

	/* set the virtual and physical advance */
	ui32VAdvance = pMMUHeap->ui32DataPageSize;
	ui32PAdvance = pMMUHeap->ui32DataPageSize;

	PVR_UNREFERENCED_PARAMETER(hUniqueTag);

	DevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, SysPAddr);

	/* check the physical alignment of the memory to map */
	PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

	/*
		for dummy allocations there is only one physical
		page backing the virtual range
	*/
	if(ui32MemFlags & PVRSRV_MEM_DUMMY)
	{
		ui32PAdvance = 0;
	}

	for (uCount=0; uCount<uSize; uCount+=ui32VAdvance)
	{
		MMU_MapPage (pMMUHeap, DevVAddr, DevPAddr, ui32MemFlags);
		DevVAddr.uiAddr += ui32VAdvance;
		DevPAddr.uiAddr += ui32PAdvance;
	}
}


/*!
******************************************************************************
	FUNCTION:   MMU_MapPagesSparse

	PURPOSE:    Create a linear mapping for a ranege of pages at a specified
	            virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  SysPAddr - the system physical address of the page to
	                 map.
				In:  ui32ChunkSize - Size of the chunk (must be page multiple)
				In:  ui32NumVirtChunks - Number of virtual chunks
				In:  ui32NumPhysChunks - Number of physical chunks
				In:  pabMapChunk - Mapping array
                In:  ui32MemFlags - page table flags.
	            In:  hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapPagesSparse (MMU_HEAP *pMMUHeap,
					IMG_DEV_VIRTADDR DevVAddr,
					IMG_SYS_PHYADDR SysPAddr,
					IMG_UINT32 ui32ChunkSize,
					IMG_UINT32 ui32NumVirtChunks,
					IMG_UINT32 ui32NumPhysChunks,
					IMG_BOOL *pabMapChunk,
					IMG_UINT32 ui32MemFlags,
					IMG_HANDLE hUniqueTag)
{
	IMG_DEV_PHYADDR DevPAddr;
	IMG_UINT32 uCount;
	IMG_UINT32 ui32VAdvance;
	IMG_UINT32 ui32PAdvance;
	IMG_SIZE_T uSizeVM = ui32ChunkSize * ui32NumVirtChunks;
#if !defined(PVRSRV_NEED_PVR_DPF)
	PVR_UNREFERENCED_PARAMETER(ui32NumPhysChunks);
#endif

	PVR_ASSERT (pMMUHeap != IMG_NULL);

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_MapPagesSparse: heap:%s, heap_id:%d devVAddr=%08X, SysPAddr=%08X, VM space=0x%x, PHYS space=0x%x",
								pMMUHeap->psDevArena->pszName,
								pMMUHeap->psDevArena->ui32HeapID,
								DevVAddr.uiAddr, 
								SysPAddr.uiAddr,
								uSizeVM,
								ui32ChunkSize * ui32NumPhysChunks));

	/* set the virtual and physical advance */
	ui32VAdvance = pMMUHeap->ui32DataPageSize;
	ui32PAdvance = pMMUHeap->ui32DataPageSize;

	PVR_UNREFERENCED_PARAMETER(hUniqueTag);

	DevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, SysPAddr);

	/* check the physical alignment of the memory to map */
	PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

	/*
		for dummy allocations there is only one physical
		page backing the virtual range
	*/
	if(ui32MemFlags & PVRSRV_MEM_DUMMY)
	{
		ui32PAdvance = 0;
	}

	for (uCount=0; uCount<uSizeVM; uCount+=ui32VAdvance)
	{
		if (pabMapChunk[uCount/ui32ChunkSize])
		{
			MMU_MapPage (pMMUHeap, DevVAddr, DevPAddr, ui32MemFlags);
			DevPAddr.uiAddr += ui32PAdvance;
		}
		DevVAddr.uiAddr += ui32VAdvance;
	}
	pMMUHeap->bHasSparseMappings = IMG_TRUE;
}

/*!
******************************************************************************
	FUNCTION:   MMU_MapShadow

	PURPOSE:    Create a mapping for a range of pages from either a CPU
				virtual adddress, (or if NULL a hOSMemHandle) to a specified
				device virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
                In:  MapBaseDevVAddr - A page aligned device virtual address
                                       to start mapping from.
                In:  uByteSize - A page aligned mapping length in bytes.
                In:  CpuVAddr - A page aligned CPU virtual address.
                In:  hOSMemHandle - An alternative OS specific memory handle
                                    for mapping RAM without a CPU virtual
                                    address
                Out: pDevVAddr - deprecated - It used to return a byte aligned
                                 device virtual address corresponding to the
                                 cpu virtual address (When CpuVAddr wasn't
                                 constrained to be page aligned.) Now it just
                                 returns MapBaseDevVAddr. Unaligned semantics
                                 can easily be handled above this API if required.
                In: hUniqueTag - A unique ID for use as a tag identifier
                In: ui32MemFlags - page table flags.
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapShadow (MMU_HEAP          *pMMUHeap,
			   IMG_DEV_VIRTADDR   MapBaseDevVAddr,
			   IMG_SIZE_T         uByteSize,
			   IMG_CPU_VIRTADDR   CpuVAddr,
			   IMG_HANDLE         hOSMemHandle,
			   IMG_DEV_VIRTADDR  *pDevVAddr,
			   IMG_UINT32         ui32MemFlags,
			   IMG_HANDLE         hUniqueTag)
{
	IMG_UINT32			i;
	IMG_UINT32			uOffset = 0;
	IMG_DEV_VIRTADDR	MapDevVAddr;
	IMG_UINT32			ui32VAdvance;
	IMG_UINT32			ui32PAdvance;

	PVR_UNREFERENCED_PARAMETER(hUniqueTag);

	PVR_DPF ((PVR_DBG_MESSAGE,
			"MMU_MapShadow: DevVAddr:%08X, Bytes:0x%x, CPUVAddr:%08X",
			MapBaseDevVAddr.uiAddr,
			uByteSize,
			(IMG_UINTPTR_T)CpuVAddr));

	/* set the virtual and physical advance */
	ui32VAdvance = pMMUHeap->ui32DataPageSize;
	ui32PAdvance = pMMUHeap->ui32DataPageSize;

	/* note: can't do useful check on the CPU Addr other than it being at least 4k alignment */
	PVR_ASSERT(((IMG_UINTPTR_T)CpuVAddr & (SGX_MMU_PAGE_SIZE - 1)) == 0);
	PVR_ASSERT(((IMG_UINT32)uByteSize & pMMUHeap->ui32DataPageMask) == 0);
	pDevVAddr->uiAddr = MapBaseDevVAddr.uiAddr;

	/*
		for dummy allocations there is only one physical
		page backing the virtual range
	*/
	if(ui32MemFlags & PVRSRV_MEM_DUMMY)
	{
		ui32PAdvance = 0;
	}

	/* Loop through cpu memory and map page by page */
	MapDevVAddr = MapBaseDevVAddr;
	for (i=0; i<uByteSize; i+=ui32VAdvance)
	{
		IMG_CPU_PHYADDR CpuPAddr;
		IMG_DEV_PHYADDR DevPAddr;

		if(CpuVAddr)
		{
			CpuPAddr = OSMapLinToCPUPhys (hOSMemHandle,
										  (IMG_VOID *)((IMG_UINTPTR_T)CpuVAddr + uOffset));
		}
		else
		{
			CpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, uOffset);
		}
		DevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, CpuPAddr);

		/* check the physical alignment of the memory to map */
		PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

		PVR_DPF ((PVR_DBG_MESSAGE,
				"Offset=0x%x: CpuVAddr=%08X, CpuPAddr=%08X, DevVAddr=%08X, DevPAddr=%08X",
				uOffset,
				(IMG_UINTPTR_T)CpuVAddr + uOffset,
				CpuPAddr.uiAddr,
				MapDevVAddr.uiAddr,
				DevPAddr.uiAddr));

		MMU_MapPage (pMMUHeap, MapDevVAddr, DevPAddr, ui32MemFlags);

		/* loop update */
		MapDevVAddr.uiAddr += ui32VAdvance;
		uOffset += ui32PAdvance;
	}
}

/*!
******************************************************************************
	FUNCTION:   MMU_MapShadowSparse

	PURPOSE:    Create a mapping for a range of pages from either a CPU
				virtual adddress, (or if NULL a hOSMemHandle) to a specified
				device virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
                In:  MapBaseDevVAddr - A page aligned device virtual address
                                       to start mapping from.
				In:  ui32ChunkSize - Size of the chunk (must be page multiple)
				In:  ui32NumVirtChunks - Number of virtual chunks
				In:  ui32NumPhysChunks - Number of physical chunks
				In:  pabMapChunk - Mapping array
                In:  CpuVAddr - A page aligned CPU virtual address.
                In:  hOSMemHandle - An alternative OS specific memory handle
                                    for mapping RAM without a CPU virtual
                                    address
                Out: pDevVAddr - deprecated - It used to return a byte aligned
                                 device virtual address corresponding to the
                                 cpu virtual address (When CpuVAddr wasn't
                                 constrained to be page aligned.) Now it just
                                 returns MapBaseDevVAddr. Unaligned semantics
                                 can easily be handled above this API if required.
                In: hUniqueTag - A unique ID for use as a tag identifier
                In: ui32MemFlags - page table flags.
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapShadowSparse (MMU_HEAP          *pMMUHeap,
					 IMG_DEV_VIRTADDR   MapBaseDevVAddr,
					 IMG_UINT32         ui32ChunkSize,
					 IMG_UINT32         ui32NumVirtChunks,
					 IMG_UINT32         ui32NumPhysChunks,
					 IMG_BOOL          *pabMapChunk,
					 IMG_CPU_VIRTADDR   CpuVAddr,
					 IMG_HANDLE         hOSMemHandle,
					 IMG_DEV_VIRTADDR  *pDevVAddr,
					 IMG_UINT32         ui32MemFlags,
					 IMG_HANDLE         hUniqueTag)
{
	IMG_UINT32			i;
	IMG_UINT32			uOffset = 0;
	IMG_DEV_VIRTADDR	MapDevVAddr;
	IMG_UINT32			ui32VAdvance;
	IMG_UINT32			ui32PAdvance;
	IMG_SIZE_T			uiSizeVM = ui32ChunkSize * ui32NumVirtChunks;
	IMG_UINT32			ui32ChunkIndex = 0;
	IMG_UINT32			ui32ChunkOffset = 0;
#if !defined(PVRSRV_NEED_PVR_DPF)
	PVR_UNREFERENCED_PARAMETER(ui32NumPhysChunks);
#endif
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);

	PVR_DPF ((PVR_DBG_MESSAGE,
			"MMU_MapShadowSparse: DevVAddr:%08X, VM space:0x%x, CPUVAddr:%08X PHYS space:0x%x",
			MapBaseDevVAddr.uiAddr,
			uiSizeVM,
			(IMG_UINTPTR_T)CpuVAddr,
			ui32ChunkSize * ui32NumPhysChunks));

	/* set the virtual and physical advance */
	ui32VAdvance = pMMUHeap->ui32DataPageSize;
	ui32PAdvance = pMMUHeap->ui32DataPageSize;

	/* note: can't do useful check on the CPU Addr other than it being at least 4k alignment */
	PVR_ASSERT(((IMG_UINTPTR_T)CpuVAddr & (SGX_MMU_PAGE_SIZE - 1)) == 0);
	PVR_ASSERT(((IMG_UINT32)uiSizeVM & pMMUHeap->ui32DataPageMask) == 0);
	pDevVAddr->uiAddr = MapBaseDevVAddr.uiAddr;

	/* Shouldn't come through the sparse interface */
	PVR_ASSERT((ui32MemFlags & PVRSRV_MEM_DUMMY) == 0);

	/* Loop through cpu memory and map page by page */
	MapDevVAddr = MapBaseDevVAddr;
	for (i=0; i<uiSizeVM; i+=ui32VAdvance)
	{
		IMG_CPU_PHYADDR CpuPAddr;
		IMG_DEV_PHYADDR DevPAddr;

		if (pabMapChunk[i/ui32ChunkSize])
		/*if (pabMapChunk[ui32ChunkIndex])*/
		{
			if(CpuVAddr)
			{
				CpuPAddr = OSMapLinToCPUPhys (hOSMemHandle,
											  (IMG_VOID *)((IMG_UINTPTR_T)CpuVAddr + uOffset));
			}
			else
			{
				CpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, uOffset);
			}
			DevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, CpuPAddr);
	
			/* check the physical alignment of the memory to map */
			PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);
	
			PVR_DPF ((PVR_DBG_MESSAGE,
					"Offset=0x%x: CpuVAddr=%08X, CpuPAddr=%08X, DevVAddr=%08X, DevPAddr=%08X",
					uOffset,
					(IMG_UINTPTR_T)CpuVAddr + uOffset,
					CpuPAddr.uiAddr,
					MapDevVAddr.uiAddr,
					DevPAddr.uiAddr));
	
			MMU_MapPage (pMMUHeap, MapDevVAddr, DevPAddr, ui32MemFlags);
			uOffset += ui32PAdvance;
		}

		/* loop update */
		MapDevVAddr.uiAddr += ui32VAdvance;

		if (ui32ChunkOffset == ui32ChunkSize)
		{
			ui32ChunkIndex++;
			ui32ChunkOffset = 0;
		}
	}

	pMMUHeap->bHasSparseMappings = IMG_TRUE;
}

/*!
******************************************************************************
	FUNCTION:   MMU_UnmapPages

	PURPOSE:    unmap pages and invalidate virtual address

	PARAMETERS:	In:  psMMUHeap - the mmu.
	            In:  sDevVAddr - the device virtual address.
	            In:  ui32PageCount - page count
	            In:  hUniqueTag - A unique ID for use as a tag identifier

	RETURNS:	None
******************************************************************************/
IMG_VOID
MMU_UnmapPages (MMU_HEAP *psMMUHeap,
				IMG_DEV_VIRTADDR sDevVAddr,
				IMG_UINT32 ui32PageCount,
				IMG_HANDLE hUniqueTag)
{
	IMG_UINT32			uPageSize = psMMUHeap->ui32DataPageSize;
	IMG_DEV_VIRTADDR	sTmpDevVAddr;
	IMG_UINT32			i;
	IMG_UINT32			ui32PDIndex;
	IMG_UINT32			ui32PTIndex;
	IMG_UINT32			*pui32Tmp;

	PVR_UNREFERENCED_PARAMETER(hUniqueTag);

	/* setup tmp devvaddr to base of allocation */
	sTmpDevVAddr = sDevVAddr;

	for(i=0; i<ui32PageCount; i++)
	{
		MMU_PT_INFO **ppsPTInfoList;

		/* find the index/offset in PD entries  */
		ui32PDIndex = sTmpDevVAddr.uiAddr >> psMMUHeap->ui32PDShift;

		/* and advance to the first PT info list */
		ppsPTInfoList = &psMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

		/* find the index/offset of the first PT in the first PT page */
		ui32PTIndex = (sTmpDevVAddr.uiAddr & psMMUHeap->ui32PTMask) >> psMMUHeap->ui32PTShift;

		/* Is the PT page valid? */
		if ((!ppsPTInfoList[0]) && (!psMMUHeap->bHasSparseMappings))
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: ERROR Invalid PT for alloc at VAddr:0x%08X (VaddrIni:0x%08X AllocPage:%u) PDIdx:%u PTIdx:%u",
									sTmpDevVAddr.uiAddr,
									sDevVAddr.uiAddr,
									i,
									ui32PDIndex,
									ui32PTIndex));

			/* advance the sTmpDevVAddr by one page */
			sTmpDevVAddr.uiAddr += uPageSize;

			/* Try to unmap the remaining allocation pages */
			continue;
		}

		CheckPT(ppsPTInfoList[0]);

		/* setup pointer to the first entry in the PT page */
		pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

		/* Decrement the valid page count only if the current page is valid*/
		if (pui32Tmp[ui32PTIndex] & SGX_MMU_PTE_VALID)
		{
			ppsPTInfoList[0]->ui32ValidPTECount--;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: Page is already invalid for alloc at VAddr:0x%08X (VAddrIni:0x%08X AllocPage:%u) PDIdx:%u PTIdx:%u",
									sTmpDevVAddr.uiAddr,
									sDevVAddr.uiAddr,
									i,
									ui32PDIndex,
									ui32PTIndex));
			PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: Page table entry value: 0x%08X", pui32Tmp[ui32PTIndex]));
		}

		/* The page table count should not go below zero */
		PVR_ASSERT((IMG_INT32)ppsPTInfoList[0]->ui32ValidPTECount >= 0);

		MakeKernelPageReadWrite(ppsPTInfoList[0]->PTPageCpuVAddr);
		/* invalidate entry */
		pui32Tmp[ui32PTIndex] = 0;
		MakeKernelPageReadOnly(ppsPTInfoList[0]->PTPageCpuVAddr);

		CheckPT(ppsPTInfoList[0]);

		/* advance the sTmpDevVAddr by one page */
		sTmpDevVAddr.uiAddr += uPageSize;
	}

	MMU_InvalidatePageTableCache(psMMUHeap->psMMUContext->psDevInfo);
}


/*!
******************************************************************************
	FUNCTION:   MMU_GetPhysPageAddr

	PURPOSE:    extracts physical address from MMU page tables

	PARAMETERS: In:  pMMUHeap - the mmu
	PARAMETERS: In:  sDevVPageAddr - the virtual address to extract physical
					page mapping from
	RETURNS:    None
******************************************************************************/
IMG_DEV_PHYADDR
MMU_GetPhysPageAddr(MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR sDevVPageAddr)
{
	IMG_UINT32 *pui32PageTable;
	IMG_UINT32 ui32Index;
	IMG_DEV_PHYADDR sDevPAddr;
	MMU_PT_INFO **ppsPTInfoList;

	/* find the index/offset in PD entries  */
	ui32Index = sDevVPageAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* and advance to the first PT info list */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32Index];
	if (!ppsPTInfoList[0])
	{
		/* Heaps with sparse mappings are allowed invalid pages */
		if (!pMMUHeap->bHasSparseMappings)
		{
			PVR_DPF((PVR_DBG_ERROR,"MMU_GetPhysPageAddr: Not mapped in at 0x%08x", sDevVPageAddr.uiAddr));
		}
		sDevPAddr.uiAddr = 0;
		return sDevPAddr;
	}

	/* find the index/offset of the first PT in the first PT page */
	ui32Index = (sDevVPageAddr.uiAddr & pMMUHeap->ui32PTMask) >> pMMUHeap->ui32PTShift;

	/* setup pointer to the first entry in the PT page */
	pui32PageTable = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

	/* read back physical page */
	sDevPAddr.uiAddr = pui32PageTable[ui32Index];

	/* Mask off non-address bits */
	sDevPAddr.uiAddr &= ~(pMMUHeap->ui32DataPageMask>>SGX_MMU_PTE_ADDR_ALIGNSHIFT);

	/* and align the address */
	sDevPAddr.uiAddr <<= SGX_MMU_PTE_ADDR_ALIGNSHIFT;

	return sDevPAddr;
}


IMG_DEV_PHYADDR MMU_GetPDDevPAddr(MMU_CONTEXT *pMMUContext)
{
	return (pMMUContext->sPDDevPAddr);
}


/*!
******************************************************************************
	FUNCTION:   SGXGetPhysPageAddr

	PURPOSE:    Gets DEV and CPU physical address of sDevVAddr

	PARAMETERS: In:  hDevMemHeap - device mem heap handle
	PARAMETERS: In:  sDevVAddr - the base virtual address to unmap from
	PARAMETERS: Out: pDevPAddr - DEV physical address
	PARAMETERS: Out: pCpuPAddr - CPU physical address
	RETURNS:    None
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR SGXGetPhysPageAddrKM (IMG_HANDLE hDevMemHeap,
								   IMG_DEV_VIRTADDR sDevVAddr,
								   IMG_DEV_PHYADDR *pDevPAddr,
								   IMG_CPU_PHYADDR *pCpuPAddr)
{
	MMU_HEAP *pMMUHeap;
	IMG_DEV_PHYADDR DevPAddr;

	/*
		Get MMU Heap From hDevMemHeap
	*/
	pMMUHeap = (MMU_HEAP*)BM_GetMMUHeap(hDevMemHeap);

	DevPAddr = MMU_GetPhysPageAddr(pMMUHeap, sDevVAddr);
	pCpuPAddr->uiAddr = DevPAddr.uiAddr; /* SysDevPAddrToCPUPAddr(DevPAddr) */
	pDevPAddr->uiAddr = DevPAddr.uiAddr;

	return (pDevPAddr->uiAddr != 0) ? PVRSRV_OK : PVRSRV_ERROR_INVALID_PARAMS;
}


/*!
******************************************************************************
    FUNCTION:   SGXGetMMUPDAddrKM

    PURPOSE:    Gets PD device physical address of hDevMemContext

    PARAMETERS: In:  hDevCookie - device cookie
	PARAMETERS: In:  hDevMemContext - memory context
	PARAMETERS: Out: psPDDevPAddr - MMU PD address
    RETURNS:    None
******************************************************************************/
PVRSRV_ERROR SGXGetMMUPDAddrKM(IMG_HANDLE		hDevCookie,
								IMG_HANDLE 		hDevMemContext,
								IMG_DEV_PHYADDR *psPDDevPAddr)
{
	if (!hDevCookie || !hDevMemContext || !psPDDevPAddr)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* return the address */
	*psPDDevPAddr = ((BM_CONTEXT*)hDevMemContext)->psMMUContext->sPDDevPAddr;

	return PVRSRV_OK;
}

/*!
******************************************************************************
	FUNCTION:   MMU_BIFResetPDAlloc

	PURPOSE:    Allocate a dummy Page Directory, Page Table and Page which can
				be used for dynamic dummy page mapping during SGX reset.
				Note: since this is only used for hardware recovery, no
				pdumping is performed.

	PARAMETERS: In:  psDevInfo - device info
	RETURNS:    PVRSRV_OK or error
******************************************************************************/
PVRSRV_ERROR MMU_BIFResetPDAlloc(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR eError;
	SYS_DATA *psSysData;
	RA_ARENA *psLocalDevMemArena;
	IMG_HANDLE hOSMemHandle = IMG_NULL;
	IMG_BYTE *pui8MemBlock = IMG_NULL;
	IMG_SYS_PHYADDR sMemBlockSysPAddr;
	IMG_CPU_PHYADDR sMemBlockCpuPAddr;

	SysAcquireData(&psSysData);

	psLocalDevMemArena = psSysData->apsLocalDevMemArena[0];

	/* allocate 3 pages - for the PD, PT and dummy page */
	if(psLocalDevMemArena == IMG_NULL)
	{
		/* UMA system */
		eError = OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						      3 * SGX_MMU_PAGE_SIZE,
						      SGX_MMU_PAGE_SIZE,
							  IMG_NULL,
							  0,
							  IMG_NULL,
						      (IMG_VOID **)&pui8MemBlock,
						      &hOSMemHandle);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR call to OSAllocPages failed"));
			return eError;
		}

		/* translate address to device physical */
		if(pui8MemBlock)
		{
			sMemBlockCpuPAddr = OSMapLinToCPUPhys(hOSMemHandle,
												  pui8MemBlock);
		}
		else
		{
			/* This isn't used in all cases since not all ports currently support
			 * OSMemHandleToCpuPAddr() */
			sMemBlockCpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, 0);
		}
	}
	else
	{
		/* non-UMA system */

		if(RA_Alloc(psLocalDevMemArena,
					3 * SGX_MMU_PAGE_SIZE,
					IMG_NULL,
					IMG_NULL,
					0,
					SGX_MMU_PAGE_SIZE,
					0,
					IMG_NULL,
					0,
					&(sMemBlockSysPAddr.uiAddr)) != IMG_TRUE)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR call to RA_Alloc failed"));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		/* derive the CPU virtual address */
		sMemBlockCpuPAddr = SysSysPAddrToCpuPAddr(sMemBlockSysPAddr);
		pui8MemBlock = OSMapPhysToLin(sMemBlockCpuPAddr,
									  SGX_MMU_PAGE_SIZE * 3,
									  PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
									  &hOSMemHandle);
		if(!pui8MemBlock)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR failed to map page tables"));
			return PVRSRV_ERROR_BAD_MAPPING;
		}
	}

	psDevInfo->hBIFResetPDOSMemHandle = hOSMemHandle;
	psDevInfo->sBIFResetPDDevPAddr = SysCpuPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sMemBlockCpuPAddr);
	psDevInfo->sBIFResetPTDevPAddr.uiAddr = psDevInfo->sBIFResetPDDevPAddr.uiAddr + SGX_MMU_PAGE_SIZE;
	psDevInfo->sBIFResetPageDevPAddr.uiAddr = psDevInfo->sBIFResetPTDevPAddr.uiAddr + SGX_MMU_PAGE_SIZE;
	/* override pointer cast warnings */
	/* PRQA S 3305,509 2 */
	psDevInfo->pui32BIFResetPD = (IMG_UINT32 *)pui8MemBlock;
	psDevInfo->pui32BIFResetPT = (IMG_UINT32 *)(pui8MemBlock + SGX_MMU_PAGE_SIZE);

	/* Invalidate entire PD and PT. */
	OSMemSet(psDevInfo->pui32BIFResetPD, 0, SGX_MMU_PAGE_SIZE);
	OSMemSet(psDevInfo->pui32BIFResetPT, 0, SGX_MMU_PAGE_SIZE);
	/* Fill dummy page with markers. */
	OSMemSet(pui8MemBlock + (2 * SGX_MMU_PAGE_SIZE), 0xDB, SGX_MMU_PAGE_SIZE);

	return PVRSRV_OK;
}

/*!
******************************************************************************
	FUNCTION:   MMU_BIFResetPDFree

	PURPOSE:    Free resources allocated in MMU_BIFResetPDAlloc.

	PARAMETERS: In:  psDevInfo - device info
	RETURNS:
******************************************************************************/
IMG_VOID MMU_BIFResetPDFree(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	SYS_DATA *psSysData;
	RA_ARENA *psLocalDevMemArena;
	IMG_SYS_PHYADDR sPDSysPAddr;

	SysAcquireData(&psSysData);

	psLocalDevMemArena = psSysData->apsLocalDevMemArena[0];

	/* free the page directory */
	if(psLocalDevMemArena == IMG_NULL)
	{
		OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
					3 * SGX_MMU_PAGE_SIZE,
					psDevInfo->pui32BIFResetPD,
					psDevInfo->hBIFResetPDOSMemHandle);
	}
	else
	{
		OSUnMapPhysToLin(psDevInfo->pui32BIFResetPD,
                         3 * SGX_MMU_PAGE_SIZE,
                         PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
                         psDevInfo->hBIFResetPDOSMemHandle);

		sPDSysPAddr = SysDevPAddrToSysPAddr(PVRSRV_DEVICE_TYPE_SGX, psDevInfo->sBIFResetPDDevPAddr);
		RA_Free(psLocalDevMemArena, sPDSysPAddr.uiAddr, IMG_FALSE);
	}
}

IMG_VOID MMU_CheckFaultAddr(PVRSRV_SGXDEV_INFO *psDevInfo, IMG_UINT32 ui32PDDevPAddr, IMG_UINT32 ui32FaultAddr)
{
	MMU_CONTEXT *psMMUContext = psDevInfo->pvMMUContextList;

	while (psMMUContext && (psMMUContext->sPDDevPAddr.uiAddr != ui32PDDevPAddr))
	{
		psMMUContext = psMMUContext->psNext;
	}

	if (psMMUContext)
	{
		IMG_UINT32 ui32PTIndex;
		IMG_UINT32 ui32PDIndex;

		PVR_LOG(("Found MMU context for page fault 0x%08x", ui32FaultAddr));
		PVR_LOG(("GPU memory context is for PID=%d (%s)", psMMUContext->ui32PID, psMMUContext->szName));

		ui32PTIndex = (ui32FaultAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;
		ui32PDIndex = (ui32FaultAddr & SGX_MMU_PD_MASK) >> (SGX_MMU_PT_SHIFT + SGX_MMU_PAGE_SHIFT);

		if (psMMUContext->apsPTInfoList[ui32PDIndex])
		{
			if (psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr)
			{
				IMG_UINT32 *pui32Ptr = psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr;
				IMG_UINT32 ui32PTE = pui32Ptr[ui32PTIndex];

				PVR_LOG(("PDE valid: PTE = 0x%08x (PhysAddr = 0x%08x, %s)",
						  ui32PTE,
						  ui32PTE & SGX_MMU_PTE_ADDR_MASK,
						  ui32PTE & SGX_MMU_PTE_VALID?"valid":"Invalid"));
			}
			else
			{
				PVR_LOG(("Found PT info but no CPU address"));
			}
		}
		else
		{
			PVR_LOG(("No PDE found"));
		}
	}
}

#if defined(SUPPORT_EXTERNAL_SYSTEM_CACHE)
/*!
******************************************************************************
	FUNCTION:   MMU_MapExtSystemCacheRegs

	PURPOSE:    maps external system cache control registers into SGX MMU

	PARAMETERS: In:  psDeviceNode - device node
	RETURNS:
******************************************************************************/
PVRSRV_ERROR MMU_MapExtSystemCacheRegs(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT32 *pui32PT;
	PVRSRV_SGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 ui32PTIndex;
	PDUMP_MMU_ATTRIB sMMUAttrib;

	psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;

	sMMUAttrib = psDevInfo->sMMUAttrib;

	ui32PDIndex = (SGX_EXT_SYSTEM_CACHE_REGS_DEVVADDR_BASE & SGX_MMU_PD_MASK) >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
	ui32PTIndex = (SGX_EXT_SYSTEM_CACHE_REGS_DEVVADDR_BASE & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

	pui32PT = (IMG_UINT32 *) psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr;

	MakeKernelPageReadWrite(pui32PT);
	/* map the PT to the registers */
	pui32PT[ui32PTIndex] = (psDevInfo->sExtSysCacheRegsDevPBase.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
							| SGX_MMU_PTE_VALID;
	MakeKernelPageReadOnly(pui32PT);
	return PVRSRV_OK;
}


/*!
******************************************************************************
	FUNCTION:   MMU_UnmapExtSystemCacheRegs

	PURPOSE:    unmaps external system cache control registers

	PARAMETERS: In:  psDeviceNode - device node
	RETURNS:
******************************************************************************/
PVRSRV_ERROR MMU_UnmapExtSystemCacheRegs(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	SYS_DATA *psSysData;
	RA_ARENA *psLocalDevMemArena;
	PVRSRV_SGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 ui32PTIndex;
	IMG_UINT32 *pui32PT;
	PDUMP_MMU_ATTRIB sMMUAttrib;

	psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;

	sMMUAttrib = psDevInfo->sMMUAttrib;

	SysAcquireData(&psSysData);

	psLocalDevMemArena = psSysData->apsLocalDevMemArena[0];

	/* unmap the MMU page table from the PD */
	ui32PDIndex = (SGX_EXT_SYSTEM_CACHE_REGS_DEVVADDR_BASE & SGX_MMU_PD_MASK) >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
	ui32PTIndex = (SGX_EXT_SYSTEM_CACHE_REGS_DEVVADDR_BASE & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

	/* Only unmap it if the PT hasn't already been freed */
	if (psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex])
	{
		if (psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr)
		{
			pui32PT = (IMG_UINT32 *) psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr;
		}
	}

	MakeKernelPageReadWrite(pui32PT);
	pui32PT[ui32PTIndex] = 0;
	MakeKernelPageReadOnly(pui32PT);

	PDUMPMEMPTENTRIES(&sMMUAttrib, psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->hPDOSMemHandle, &pui32PT[ui32PTIndex], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

	return PVRSRV_OK;
}
#endif


#if PAGE_TEST
/*!
******************************************************************************
	FUNCTION:   PageTest

	PURPOSE:    Tests page table memory, for use during device bring-up.

	PARAMETERS: In:  void* pMem - page address (CPU mapped)
	PARAMETERS: In:  IMG_DEV_PHYADDR sDevPAddr - page device phys address
	RETURNS:    None, provides debug output and breaks if an error is detected.
******************************************************************************/
static IMG_VOID PageTest(IMG_VOID* pMem, IMG_DEV_PHYADDR sDevPAddr)
{
	volatile IMG_UINT32 ui32WriteData;
	volatile IMG_UINT32 ui32ReadData;
	volatile IMG_UINT32 *pMem32 = (volatile IMG_UINT32 *)pMem;
	IMG_INT n;
	IMG_BOOL bOK=IMG_TRUE;

	ui32WriteData = 0xffffffff;

	for (n=0; n<1024; n++)
	{
		pMem32[n] = ui32WriteData;
		ui32ReadData = pMem32[n];

		if (ui32WriteData != ui32ReadData)
		{
			// Mem fault
			PVR_DPF ((PVR_DBG_ERROR, "Error - memory page test failed at device phys address 0x%08X", sDevPAddr.uiAddr + (n<<2) ));
			PVR_DBG_BREAK;
			bOK = IMG_FALSE;
		}
 	}

	ui32WriteData = 0;

	for (n=0; n<1024; n++)
	{
		pMem32[n] = ui32WriteData;
		ui32ReadData = pMem32[n];

		if (ui32WriteData != ui32ReadData)
		{
			// Mem fault
			PVR_DPF ((PVR_DBG_ERROR, "Error - memory page test failed at device phys address 0x%08X", sDevPAddr.uiAddr + (n<<2) ));
			PVR_DBG_BREAK;
			bOK = IMG_FALSE;
		}
 	}

	if (bOK)
	{
		PVR_DPF ((PVR_DBG_VERBOSE, "MMU Page 0x%08X is OK", sDevPAddr.uiAddr));
	}
	else
	{
		PVR_DPF ((PVR_DBG_VERBOSE, "MMU Page 0x%08X *** FAILED ***", sDevPAddr.uiAddr));
	}
}
#endif

/******************************************************************************
 End of file (mmu.c)
******************************************************************************/


