/*************************************************************************/ /*!
@Title          SGX KM API Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Exported SGX API details
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

#ifndef __SGXAPI_KM_H__
#define __SGXAPI_KM_H__

#include "sgxdefs.h"

#include <asm/unistd.h>

/******************************************************************************
 Some defines...
******************************************************************************/

/* SGX Heap IDs, note: not all heaps are available to clients */
#define SGX_UNDEFINED_HEAP_ID					(~0LU)
#define SGX_GENERAL_HEAP_ID						0
#define SGX_TADATA_HEAP_ID						1
#define SGX_KERNEL_CODE_HEAP_ID					2
#define SGX_KERNEL_DATA_HEAP_ID					3
#define SGX_PIXELSHADER_HEAP_ID					4
#define SGX_VERTEXSHADER_HEAP_ID				5
#define SGX_PDSPIXEL_CODEDATA_HEAP_ID			6
#define SGX_PDSVERTEX_CODEDATA_HEAP_ID			7
#define SGX_SYNCINFO_HEAP_ID					8
#define SGX_SHARED_3DPARAMETERS_HEAP_ID				9
#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_ID			10
#define SGX_MAX_HEAP_ID							16

/*
 * Keep SGX_3DPARAMETERS_HEAP_ID as TQ full custom
 * shaders need it to select which heap to write
 * their ISP controll stream to.
 */
#define SGX_3DPARAMETERS_HEAP_ID			SGX_PERCONTEXT_3DPARAMETERS_HEAP_ID
/* Define for number of bytes between consecutive code base registers */
#define SGX_USE_CODE_SEGMENT_RANGE_BITS		19

#define SGX_MAX_TA_STATUS_VALS	32
#define SGX_MAX_3D_STATUS_VALS	4

/* sync info structure array size */
#define SGX_MAX_SRC_SYNCS_TA				32
#define SGX_MAX_DST_SYNCS_TA				1
/* note: there is implicitly 1 3D Dst Sync */
#define SGX_MAX_SRC_SYNCS_TQ				8
#define SGX_MAX_DST_SYNCS_TQ				1

#define	PVRSRV_SGX_HWPERF_NUM_COUNTERS	9
#define	PVRSRV_SGX_HWPERF_NUM_MISC_COUNTERS 8

#define PVRSRV_SGX_HWPERF_INVALID					0x1

#define PVRSRV_SGX_HWPERF_TRANSFER					0x2
#define PVRSRV_SGX_HWPERF_TA						0x3
#define PVRSRV_SGX_HWPERF_3D						0x4
#define PVRSRV_SGX_HWPERF_2D						0x5
#define PVRSRV_SGX_HWPERF_POWER						0x6
#define PVRSRV_SGX_HWPERF_PERIODIC					0x7
#define PVRSRV_SGX_HWPERF_3DSPM						0x8

#define PVRSRV_SGX_HWPERF_MK_EVENT					0x101
#define PVRSRV_SGX_HWPERF_MK_TA						0x102
#define PVRSRV_SGX_HWPERF_MK_3D						0x103
#define PVRSRV_SGX_HWPERF_MK_2D						0x104
#define PVRSRV_SGX_HWPERF_MK_TRANSFER_DUMMY				0x105
#define PVRSRV_SGX_HWPERF_MK_TA_DUMMY					0x106
#define PVRSRV_SGX_HWPERF_MK_3D_DUMMY					0x107
#define PVRSRV_SGX_HWPERF_MK_2D_DUMMY					0x108
#define PVRSRV_SGX_HWPERF_MK_TA_LOCKUP					0x109
#define PVRSRV_SGX_HWPERF_MK_3D_LOCKUP					0x10A
#define PVRSRV_SGX_HWPERF_MK_2D_LOCKUP					0x10B

#define PVRSRV_SGX_HWPERF_TYPE_STARTEND_BIT			28
#define PVRSRV_SGX_HWPERF_TYPE_OP_MASK				((1UL << PVRSRV_SGX_HWPERF_TYPE_STARTEND_BIT) - 1)
#define PVRSRV_SGX_HWPERF_TYPE_OP_START				(0UL << PVRSRV_SGX_HWPERF_TYPE_STARTEND_BIT)
#define PVRSRV_SGX_HWPERF_TYPE_OP_END				(1Ul << PVRSRV_SGX_HWPERF_TYPE_STARTEND_BIT)

#define PVRSRV_SGX_HWPERF_TYPE_TRANSFER_START		(PVRSRV_SGX_HWPERF_TRANSFER | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_TRANSFER_END			(PVRSRV_SGX_HWPERF_TRANSFER | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_TA_START				(PVRSRV_SGX_HWPERF_TA | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_TA_END				(PVRSRV_SGX_HWPERF_TA | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_3D_START				(PVRSRV_SGX_HWPERF_3D | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_3D_END				(PVRSRV_SGX_HWPERF_3D | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_2D_START				(PVRSRV_SGX_HWPERF_2D | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_2D_END				(PVRSRV_SGX_HWPERF_2D | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_POWER_START			(PVRSRV_SGX_HWPERF_POWER | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_POWER_END			(PVRSRV_SGX_HWPERF_POWER | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_PERIODIC				(PVRSRV_SGX_HWPERF_PERIODIC)
#define PVRSRV_SGX_HWPERF_TYPE_3DSPM_START			(PVRSRV_SGX_HWPERF_3DSPM | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_3DSPM_END			(PVRSRV_SGX_HWPERF_3DSPM | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_MK_TRANSFER_DUMMY_START		(PVRSRV_SGX_HWPERF_MK_TRANSFER_DUMMY | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_MK_TRANSFER_DUMMY_END		(PVRSRV_SGX_HWPERF_MK_TRANSFER_DUMMY | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_MK_TA_DUMMY_START		(PVRSRV_SGX_HWPERF_MK_TA_DUMMY | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_MK_TA_DUMMY_END			(PVRSRV_SGX_HWPERF_MK_TA_DUMMY | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_MK_3D_DUMMY_START		(PVRSRV_SGX_HWPERF_MK_3D_DUMMY | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_MK_3D_DUMMY_END			(PVRSRV_SGX_HWPERF_MK_3D_DUMMY | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_MK_2D_DUMMY_START		(PVRSRV_SGX_HWPERF_MK_2D_DUMMY | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_MK_2D_DUMMY_END			(PVRSRV_SGX_HWPERF_MK_2D_DUMMY | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_MK_TA_LOCKUP			(PVRSRV_SGX_HWPERF_MK_TA_LOCKUP)
#define PVRSRV_SGX_HWPERF_TYPE_MK_3D_LOCKUP			(PVRSRV_SGX_HWPERF_MK_3D_LOCKUP)
#define PVRSRV_SGX_HWPERF_TYPE_MK_2D_LOCKUP			(PVRSRV_SGX_HWPERF_MK_2D_LOCKUP)

#define PVRSRV_SGX_HWPERF_TYPE_MK_EVENT_START		(PVRSRV_SGX_HWPERF_MK_EVENT | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_MK_EVENT_END			(PVRSRV_SGX_HWPERF_MK_EVENT | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_MK_TA_START			(PVRSRV_SGX_HWPERF_MK_TA | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_MK_TA_END			(PVRSRV_SGX_HWPERF_MK_TA | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_MK_3D_START			(PVRSRV_SGX_HWPERF_MK_3D | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_MK_3D_END			(PVRSRV_SGX_HWPERF_MK_3D | PVRSRV_SGX_HWPERF_TYPE_OP_END)
#define PVRSRV_SGX_HWPERF_TYPE_MK_2D_START			(PVRSRV_SGX_HWPERF_MK_2D | PVRSRV_SGX_HWPERF_TYPE_OP_START)
#define PVRSRV_SGX_HWPERF_TYPE_MK_2D_END			(PVRSRV_SGX_HWPERF_MK_2D | PVRSRV_SGX_HWPERF_TYPE_OP_END)

#define PVRSRV_SGX_HWPERF_STATUS_OFF				(0x0)
#define PVRSRV_SGX_HWPERF_STATUS_RESET_COUNTERS		(1UL << 0)
#define PVRSRV_SGX_HWPERF_STATUS_GRAPHICS_ON		(1UL << 1)
#define PVRSRV_SGX_HWPERF_STATUS_PERIODIC_ON		(1UL << 2)
#define PVRSRV_SGX_HWPERF_STATUS_MK_EXECUTION_ON	(1UL << 3)


/*!
 *****************************************************************************
 * One entry in the HWPerf Circular Buffer. 
 *****************************************************************************/
typedef struct _PVRSRV_SGX_HWPERF_CB_ENTRY_
{
	IMG_UINT32	ui32FrameNo;
	IMG_UINT32	ui32PID;
	IMG_UINT32	ui32RTData;
	IMG_UINT32	ui32Type;
	IMG_UINT32	ui32Ordinal;
	IMG_UINT32	ui32Info;
	IMG_UINT32	ui32Clocksx16;
	/* NOTE: There should always be at least as many 3D cores as TA cores. */	
	IMG_UINT32	ui32Counters[SGX_FEATURE_MP_CORE_COUNT_3D][PVRSRV_SGX_HWPERF_NUM_COUNTERS];
	IMG_UINT32	ui32MiscCounters[SGX_FEATURE_MP_CORE_COUNT_3D][PVRSRV_SGX_HWPERF_NUM_MISC_COUNTERS];
} PVRSRV_SGX_HWPERF_CB_ENTRY;


/*
	Status values control structure
*/
typedef struct _CTL_STATUS_
{
	IMG_DEV_VIRTADDR	sStatusDevAddr;
	IMG_UINT32			ui32StatusValue;
} CTL_STATUS;


/*!
	List of possible requests/commands to SGXGetMiscInfo()
*/
typedef enum _SGX_MISC_INFO_REQUEST_
{
	SGX_MISC_INFO_REQUEST_CLOCKSPEED = 0,
	SGX_MISC_INFO_REQUEST_SGXREV,
	SGX_MISC_INFO_REQUEST_DRIVER_SGXREV,
	SGX_MISC_INFO_REQUEST_SET_HWPERF_STATUS,
	SGX_MISC_INFO_DUMP_DEBUG_INFO,
	SGX_MISC_INFO_DUMP_DEBUG_INFO_FORCE_REGS,
	SGX_MISC_INFO_PANIC,
	SGX_MISC_INFO_REQUEST_SPM,
	SGX_MISC_INFO_REQUEST_ACTIVEPOWER,
	SGX_MISC_INFO_REQUEST_LOCKUPS,
	SGX_MISC_INFO_REQUEST_FORCE_I16 				=  0x7fff
} SGX_MISC_INFO_REQUEST;


/******************************************************************************
 * Struct for passing SGX core rev/features from ukernel to driver.
 * This is accessed from the kernel part of the driver and microkernel; it is
 * only accessed in user space during buffer allocation in srvinit.
 ******************************************************************************/
typedef struct _PVRSRV_SGX_MISCINFO_FEATURES
{
	IMG_UINT32			ui32CoreRev;	/*!< SGX Core revision from HW register */
	IMG_UINT32			ui32CoreID;		/*!< SGX Core ID from HW register */
	IMG_UINT32			ui32DDKVersion;	/*!< software DDK version */
	IMG_UINT32			ui32DDKBuild;	/*!< software DDK build no. */
	IMG_UINT32			ui32CoreIdSW;	/*!< software core version (ID), e.g. SGX535, SGX540 */
	IMG_UINT32			ui32CoreRevSW;	/*!< software core revision */
	IMG_UINT32			ui32BuildOptions;	/*!< build options bit-field */
} PVRSRV_SGX_MISCINFO_FEATURES;


/******************************************************************************
 * Struct for getting lock-up stats from the kernel driver
 ******************************************************************************/
typedef struct _PVRSRV_SGX_MISCINFO_LOCKUPS
{
	IMG_UINT32			ui32HostDetectedLockups; /*!< Host timer detected lockups */
	IMG_UINT32			ui32uKernelDetectedLockups; /*!< Microkernel detected lockups */
} PVRSRV_SGX_MISCINFO_LOCKUPS;


/******************************************************************************
 * Struct for getting lock-up stats from the kernel driver
 ******************************************************************************/
typedef struct _PVRSRV_SGX_MISCINFO_ACTIVEPOWER
{
	IMG_UINT32			ui32NumActivePowerEvents; /*!< active power events */
} PVRSRV_SGX_MISCINFO_ACTIVEPOWER;


/******************************************************************************
 * Struct for getting SPM stats fro the kernel driver
 ******************************************************************************/
typedef struct _PVRSRV_SGX_MISCINFO_SPM
{
	IMG_HANDLE			hRTDataSet;				/*!< render target data set handle returned from SGXAddRenderTarget */
	IMG_UINT32			ui32NumOutOfMemSignals; /*!< Number of Out of Mem Signals */
	IMG_UINT32			ui32NumSPMRenders;	/*!< Number of SPM renders */
} PVRSRV_SGX_MISCINFO_SPM;


/*!
 ******************************************************************************
 * Structure for setting the hardware performance status
 *****************************************************************************/
typedef struct _PVRSRV_SGX_MISCINFO_SET_HWPERF_STATUS
{
	/* See PVRSRV_SGX_HWPERF_STATUS_* */
	IMG_UINT32	ui32NewHWPerfStatus;

	/* Specifies the HW's active group */
	IMG_UINT32	ui32PerfGroup;
} PVRSRV_SGX_MISCINFO_SET_HWPERF_STATUS;


/*!
 ******************************************************************************
 * Structure for misc SGX commands in services
 *****************************************************************************/
typedef struct _SGX_MISC_INFO_
{
	SGX_MISC_INFO_REQUEST	eRequest;	/*!< Command request to SGXGetMiscInfo() */
	IMG_UINT32				ui32Padding;
	union
	{
		IMG_UINT32	reserved;	/*!< Unused: ensures valid code in the case everything else is compiled out */
		PVRSRV_SGX_MISCINFO_FEATURES						sSGXFeatures;
		IMG_UINT32											ui32SGXClockSpeed;
		PVRSRV_SGX_MISCINFO_ACTIVEPOWER						sActivePower;
		PVRSRV_SGX_MISCINFO_LOCKUPS							sLockups;
		PVRSRV_SGX_MISCINFO_SPM								sSPM;
		PVRSRV_SGX_MISCINFO_SET_HWPERF_STATUS				sSetHWPerfStatus;
	} uData;
} SGX_MISC_INFO;


#define SGX_KICKTA_DUMPBITMAP_MAX_NAME_LENGTH		256

/*
	Structure for dumping bitmaps
*/
typedef struct _SGX_KICKTA_DUMPBITMAP_
{
	IMG_DEV_VIRTADDR	sDevBaseAddr;
	IMG_UINT32			ui32Flags;
	IMG_UINT32			ui32Width;
	IMG_UINT32			ui32Height;
	IMG_UINT32			ui32Stride;
	IMG_UINT32			ui32PDUMPFormat;
	IMG_UINT32			ui32BytesPP;
	char			pszName[SGX_KICKTA_DUMPBITMAP_MAX_NAME_LENGTH];
} SGX_KICKTA_DUMPBITMAP, *PSGX_KICKTA_DUMPBITMAP;

#define PVRSRV_SGX_PDUMP_CONTEXT_MAX_BITMAP_ARRAY_SIZE	(16)

/*!
 ******************************************************************************
 * Data required only when dumping parameters
 *****************************************************************************/
typedef struct _PVRSRV_SGX_PDUMP_CONTEXT_
{
	/* cache control word for micro kernel cache flush/invalidates */
	IMG_UINT32						ui32CacheControl;

} PVRSRV_SGX_PDUMP_CONTEXT;


typedef struct _SGX_KICKTA_DUMP_ROFF_
{
	IMG_HANDLE			hKernelMemInfo;						/*< Buffer handle */
	IMG_UINT32			uiAllocIndex;						/*< Alloc index for LDDM */
	IMG_UINT32			ui32Offset;							/*< Byte offset to value to dump */
	IMG_UINT32			ui32Value;							/*< Actual value to dump */
	IMG_PCHAR			pszName;							/*< Name of buffer */
} SGX_KICKTA_DUMP_ROFF, *PSGX_KICKTA_DUMP_ROFF;

typedef struct _SGX_KICKTA_DUMP_BUFFER_
{
	IMG_UINT32			ui32SpaceUsed;
	IMG_UINT32			ui32Start;							/*< Byte offset of start to dump */
	IMG_UINT32			ui32End;							/*< Byte offset of end of dump (non-inclusive) */
	IMG_UINT32			ui32BufferSize;						/*< Size of buffer */
	IMG_UINT32			ui32BackEndLength;					/*< Size of back end portion, if End < Start */
	IMG_UINT32			uiAllocIndex;
	IMG_HANDLE			hKernelMemInfo;						/*< MemInfo handle for the circular buffer */
	IMG_PVOID			pvLinAddr;
	IMG_HANDLE			hCtrlKernelMemInfo;					/*< MemInfo handle for the control structure of the
																circular buffer */
	IMG_DEV_VIRTADDR	sCtrlDevVAddr;						/*< Device virtual address of the memory in the 
																control structure to be checked */
	IMG_PCHAR			pszName;							/*< Name of buffer */
} SGX_KICKTA_DUMP_BUFFER, *PSGX_KICKTA_DUMP_BUFFER;

#define SGX_MAX_TRANSFER_STATUS_VALS	2
#define SGX_MAX_TRANSFER_SYNC_OPS	5

#endif /* __SGXAPI_KM_H__ */

/******************************************************************************
 End of file (sgxapi_km.h)
******************************************************************************/
