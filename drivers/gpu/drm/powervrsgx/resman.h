/*************************************************************************/ /*!
@Title          Resource Manager API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Provide resource management
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

#ifndef __RESMAN_H__
#define __RESMAN_H__

/******************************************************************************
 * resman definitions 
 *****************************************************************************/

enum {
	/* SGX: */
	RESMAN_TYPE_SHARED_PB_DESC = 1,					/*!< Parameter buffer kernel stubs */
	RESMAN_TYPE_SHARED_PB_DESC_CREATE_LOCK = 2,			/*!< Shared parameter buffer creation lock */
	RESMAN_TYPE_HW_RENDER_CONTEXT = 3,				/*!< Hardware Render Context Resource */
	RESMAN_TYPE_HW_TRANSFER_CONTEXT = 4,				/*!< Hardware transfer Context Resource */
	RESMAN_TYPE_HW_2D_CONTEXT = 5,					/*!< Hardware 2D Context Resource */
	RESMAN_TYPE_TRANSFER_CONTEXT = 6,				/*!< Transfer Queue context */

	/* DISPLAY CLASS: */
	RESMAN_TYPE_DISPLAYCLASS_SWAPCHAIN_REF = 8,			/*!< Display Class Swapchain Reference Resource */
	RESMAN_TYPE_DISPLAYCLASS_DEVICE = 9,				/*!< Display Class Device Resource */

	/* BUFFER CLASS: */
	RESMAN_TYPE_BUFFERCLASS_DEVICE = 10,				/*!< Buffer Class Device Resource */

	/* OS specific User mode Mappings: */
	RESMAN_TYPE_OS_USERMODE_MAPPING = 11,				/*!< OS specific User mode mappings */

	/* COMMON: */
	RESMAN_TYPE_DEVICEMEM_CONTEXT = 12,				/*!< Device Memory Context Resource */
	RESMAN_TYPE_DEVICECLASSMEM_MAPPING = 13,			/*!< Device Memory Mapping Resource */
	RESMAN_TYPE_DEVICEMEM_MAPPING = 14,				/*!< Device Memory Mapping Resource */
	RESMAN_TYPE_DEVICEMEM_WRAP = 15,				/*!< Device Memory Wrap Resource */
	RESMAN_TYPE_DEVICEMEM_ALLOCATION = 16,				/*!< Device Memory Allocation Resource */
	RESMAN_TYPE_DEVICEMEM_ION = 17,					/*!< Device Memory Ion Resource */
	RESMAN_TYPE_EVENT_OBJECT = 18,					/*!< Event Object */
	RESMAN_TYPE_SHARED_MEM_INFO = 19,				/*!< Shared system memory meminfo */
	RESMAN_TYPE_MODIFY_SYNC_OPS = 20,				/*!< Syncobject synchronisation Resource*/
	RESMAN_TYPE_SYNC_INFO = 21,				        /*!< Syncobject Resource*/

	/* KERNEL: */
	RESMAN_TYPE_KERNEL_DEVICEMEM_ALLOCATION = 22			/*!< Device Memory Allocation Resource */
};

#define RESMAN_CRITERIA_ALL				0x00000000	/*!< match by criteria all */
#define RESMAN_CRITERIA_RESTYPE			0x00000001	/*!< match by criteria type */
#define RESMAN_CRITERIA_PVOID_PARAM		0x00000002	/*!< match by criteria param1 */
#define RESMAN_CRITERIA_UI32_PARAM		0x00000004	/*!< match by criteria param2 */

typedef PVRSRV_ERROR (*RESMAN_FREE_FN)(IMG_PVOID pvParam, IMG_UINT32 ui32Param, IMG_BOOL bForceCleanup); 

typedef struct _RESMAN_ITEM_ *PRESMAN_ITEM;
typedef struct _RESMAN_CONTEXT_ *PRESMAN_CONTEXT;

/******************************************************************************
 * resman functions 
 *****************************************************************************/

/*
	Note:
	Resource cleanup can fail with retry in which case we don't remove
	it from resman's list and either UM or KM will try to release the
	resource at a later date (and will keep trying until a non-retry
	error is returned)
*/

PVRSRV_ERROR ResManInit(IMG_VOID);
IMG_VOID ResManDeInit(IMG_VOID);

PRESMAN_ITEM ResManRegisterRes(PRESMAN_CONTEXT	hResManContext,
							   IMG_UINT32		ui32ResType, 
							   IMG_PVOID		pvParam, 
							   IMG_UINT32		ui32Param, 
							   RESMAN_FREE_FN	pfnFreeResource);

PVRSRV_ERROR ResManFreeResByPtr(PRESMAN_ITEM	psResItem,
								IMG_BOOL		bForceCleanup);

PVRSRV_ERROR ResManFreeResByCriteria(PRESMAN_CONTEXT	hResManContext,
									 IMG_UINT32			ui32SearchCriteria, 
									 IMG_UINT32			ui32ResType, 
									 IMG_PVOID			pvParam, 
									 IMG_UINT32			ui32Param);

PVRSRV_ERROR ResManDissociateRes(PRESMAN_ITEM		psResItem,
							 PRESMAN_CONTEXT	psNewResManContext);

PVRSRV_ERROR ResManFindResourceByPtr(PRESMAN_CONTEXT	hResManContext,
									 PRESMAN_ITEM		psItem);

PVRSRV_ERROR PVRSRVResManConnect(IMG_HANDLE			hPerProc,
								 PRESMAN_CONTEXT	*phResManContext);
IMG_VOID PVRSRVResManDisconnect(PRESMAN_CONTEXT hResManContext,
								IMG_BOOL		bKernelContext);

#endif /* __RESMAN_H__ */

/******************************************************************************
 End of file (resman.h)
******************************************************************************/

