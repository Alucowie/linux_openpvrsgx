/**********************************************************************
 *
 * Copyright (C) Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#include "sysconfig.h"
#include "services_headers.h"
#include "kerneldisplay.h"
#include "oemfuncs.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "syslocal.h"

#include "ocpdefs.h"

SYS_DATA* gpsSysData = (SYS_DATA*)IMG_NULL;
SYS_DATA  gsSysData;

static SYS_SPECIFIC_DATA gsSysSpecificData;
SYS_SPECIFIC_DATA *gpsSysSpecificData;

static IMG_UINT32	gui32SGXDeviceID;
static SGX_DEVICE_MAP	gsSGXDeviceMap;
static PVRSRV_DEVICE_NODE *gpsSGXDevNode;

#define DEVICE_SGX_INTERRUPT (1 << 0)

#if defined(PVR_LINUX_DYNAMIC_SGX_RESOURCE_INFO)
extern struct platform_device *gpsPVRLDMDev;
#endif

IMG_UINT32 PVRSRV_BridgeDispatchKM(IMG_UINT32	Ioctl,
								   IMG_BYTE		*pInBuf,
								   IMG_UINT32	InBufLen,
								   IMG_BYTE		*pOutBuf,
								   IMG_UINT32	OutBufLen,
								   IMG_UINT32	*pdwBytesTransferred);

static INLINE PVRSRV_ERROR EnableSGXClocksWrap(SYS_DATA *psSysData)
{
	return EnableSGXClocks(psSysData);
}

static INLINE PVRSRV_ERROR EnableSystemClocksWrap(SYS_DATA *psSysData)
{
	PVRSRV_ERROR eError = EnableSystemClocks(psSysData);

	return eError;
}

static PVRSRV_ERROR SysLocateDevices(SYS_DATA *psSysData)
{
#if defined(PVR_LINUX_DYNAMIC_SGX_RESOURCE_INFO)
	struct resource *dev_res;
	int dev_irq;
#endif

	PVR_UNREFERENCED_PARAMETER(psSysData);

	gsSGXDeviceMap.ui32Flags = 0x0;

#if defined(PVR_LINUX_DYNAMIC_SGX_RESOURCE_INFO)
	
	dev_res = platform_get_resource(gpsPVRLDMDev, IORESOURCE_MEM, 0);
	if (dev_res == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: platform_get_resource failed", __FUNCTION__));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	dev_irq = platform_get_irq(gpsPVRLDMDev, 0);
	if (dev_irq < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: platform_get_irq failed (%d)", __FUNCTION__, -dev_irq));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}
	
	gsSGXDeviceMap.sRegsSysPBase.uiAddr = dev_res->start;
	gsSGXDeviceMap.sRegsCpuPBase =
		SysSysPAddrToCpuPAddr(gsSGXDeviceMap.sRegsSysPBase);
	PVR_TRACE(("SGX register base: 0x%lx", (unsigned long)gsSGXDeviceMap.sRegsCpuPBase.uiAddr));

	gsSGXDeviceMap.ui32RegsSize = (unsigned int)(dev_res->end - dev_res->start);
	PVR_TRACE(("SGX register size: %d",gsSGXDeviceMap.ui32RegsSize));

	gsSGXDeviceMap.ui32IRQ = dev_irq;
	PVR_TRACE(("SGX IRQ: %d", gsSGXDeviceMap.ui32IRQ));
#else	
	gsSGXDeviceMap.sRegsSysPBase.uiAddr = SYS_OMAP3430_SGX_REGS_SYS_PHYS_BASE;
	gsSGXDeviceMap.sRegsCpuPBase = SysSysPAddrToCpuPAddr(gsSGXDeviceMap.sRegsSysPBase);
	gsSGXDeviceMap.ui32RegsSize = SYS_OMAP3430_SGX_REGS_SIZE;

	gsSGXDeviceMap.ui32IRQ = SYS_OMAP3430_SGX_IRQ;

#endif	
	return PVRSRV_OK;
}


static IMG_CHAR *SysCreateVersionString(void)
{
	static IMG_CHAR aszVersionString[100];
	SYS_DATA	*psSysData;
	IMG_UINT32	ui32SGXRevision;
	IMG_INT32	i32Count;
	IMG_VOID	*pvRegsLinAddr;

	pvRegsLinAddr = OSMapPhysToLin(gsSGXDeviceMap.sRegsCpuPBase,
								   SYS_OMAP3430_SGX_REGS_SIZE,
								   PVRSRV_HAP_UNCACHED|PVRSRV_HAP_KERNEL_ONLY,
								   IMG_NULL);
	if(!pvRegsLinAddr)
	{
		return IMG_NULL;
	}

	ui32SGXRevision = OSReadHWReg((IMG_PVOID)((IMG_PBYTE)pvRegsLinAddr),
								  EUR_CR_CORE_REVISION);

	SysAcquireData(&psSysData);

	i32Count = OSSNPrintf(aszVersionString, 100,
						  "SGX revision = %u.%u.%u",
						  (IMG_UINT)((ui32SGXRevision & EUR_CR_CORE_REVISION_MAJOR_MASK)
							>> EUR_CR_CORE_REVISION_MAJOR_SHIFT),
						  (IMG_UINT)((ui32SGXRevision & EUR_CR_CORE_REVISION_MINOR_MASK)
							>> EUR_CR_CORE_REVISION_MINOR_SHIFT),
						  (IMG_UINT)((ui32SGXRevision & EUR_CR_CORE_REVISION_MAINTENANCE_MASK)
							>> EUR_CR_CORE_REVISION_MAINTENANCE_SHIFT)
						 );

	OSUnMapPhysToLin(pvRegsLinAddr,
					 SYS_OMAP3430_SGX_REGS_SIZE,
					 PVRSRV_HAP_UNCACHED|PVRSRV_HAP_KERNEL_ONLY,
					 IMG_NULL);

	if(i32Count == -1)
	{
		return IMG_NULL;
	}

	return aszVersionString;
}


PVRSRV_ERROR SysInitialise(IMG_VOID)
{
	IMG_UINT32			i;
	PVRSRV_ERROR 		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
#if !defined(PVR_NO_OMAP_TIMER)
	IMG_CPU_PHYADDR		TimerRegPhysBase;
#endif
	gpsSysData = &gsSysData;
	OSMemSet(gpsSysData, 0, sizeof(SYS_DATA));

	gpsSysSpecificData =  &gsSysSpecificData;
	OSMemSet(gpsSysSpecificData, 0, sizeof(SYS_SPECIFIC_DATA));

	gpsSysData->pvSysSpecificData = gpsSysSpecificData;

	eError = OSInitEnvData(&gpsSysData->pvEnvSpecificData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to setup env structure"));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_ENVDATA);

	gpsSysData->ui32NumDevices = SYS_DEVICE_COUNT;

	
	for(i=0; i<SYS_DEVICE_COUNT; i++)
	{
		gpsSysData->sDeviceID[i].uiID = i;
		gpsSysData->sDeviceID[i].bInUse = false;
	}

	gpsSysData->psDeviceNodeList = IMG_NULL;
	gpsSysData->psQueueList = IMG_NULL;

	eError = SysInitialiseCommon(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed in SysInitialiseCommon"));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

	gpsSysSpecificData->ui32SrcClockDiv = 3;

	eError = SysLocateDevices(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to locate devices"));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LOCATEDEV);
#if 0
	eError = SysPMRuntimeRegister();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to register with OSPM!"));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_PM_RUNTIME);
#endif
	


	eError = PVRSRVRegisterDevice(gpsSysData, SGXRegisterDevice,
								  DEVICE_SGX_INTERRUPT, &gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to register device!"));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_REGDEV);

	


	
	psDeviceNode = gpsSysData->psDeviceNodeList;
	while(psDeviceNode)
	{
		
		switch(psDeviceNode->sDevId.eDeviceType)
		{
			case PVRSRV_DEVICE_TYPE_SGX:
			{
				DEVICE_MEMORY_INFO *psDevMemoryInfo;
				DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;

				


				psDeviceNode->psLocalDevMemArena = IMG_NULL;

				
				psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
				psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;

				
				for(i=0; i<psDevMemoryInfo->ui32HeapCount; i++)
				{
					psDeviceMemoryHeap[i].ui32Attribs |= PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG;
				}

				gpsSGXDevNode = psDeviceNode;
				gsSysSpecificData.psSGXDevNode = psDeviceNode;

				break;
			}
			default:
				PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to find SGX device node!"));
				return PVRSRV_ERROR_INIT_FAILURE;
		}

		
		psDeviceNode = psDeviceNode->psNext;
	}

	eError = EnableSystemClocksWrap(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to Enable system clocks (%d)", eError));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);
	eError = EnableSGXClocksWrap(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to Enable SGX clocks (%d)", eError));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

	eError = PVRSRVInitialiseDevice(gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to initialise device!"));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_INITDEV);

	DisableSGXClocks(gpsSysData);

#if !defined(PVR_NO_OMAP_TIMER)
#if defined(PVR_OMAP_TIMER_BASE_IN_SYS_SPEC_DATA)
	TimerRegPhysBase = gsSysSpecificData.sTimerRegPhysBase;
#else
	TimerRegPhysBase.uiAddr = SYS_OMAP3430_GP11TIMER_REGS_SYS_PHYS_BASE;
#endif
	gpsSysData->pvSOCTimerRegisterKM = IMG_NULL;
	gpsSysData->hSOCTimerRegisterOSMemHandle = 0;
	if (TimerRegPhysBase.uiAddr != 0)
	{
		OSReservePhys(TimerRegPhysBase,
				  4,
				  PVRSRV_HAP_MULTI_PROCESS|PVRSRV_HAP_UNCACHED,
				  IMG_NULL,						
				  (IMG_VOID **)&gpsSysData->pvSOCTimerRegisterKM,
				  &gpsSysData->hSOCTimerRegisterOSMemHandle);
	}
#endif 

	return PVRSRV_OK;
}


PVRSRV_ERROR SysFinalise(IMG_VOID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	eError = EnableSGXClocksWrap(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to Enable SGX clocks (%d)", eError));
		return eError;
	}

	eError = OSInstallMISR(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to install MISR"));
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_MISR);

	eError = OSInstallDeviceLISR(gpsSysData, gsSGXDeviceMap.ui32IRQ, "SGX ISR", gpsSGXDevNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to install ISR"));
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);

	gpsSysData->pszVersionString = SysCreateVersionString();
	if (!gpsSysData->pszVersionString)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to create a system version string"));
	}
	else
	{
		PVR_TRACE(("SysFinalise: Version string: %s", gpsSysData->pszVersionString));
	}

	DisableSGXClocks(gpsSysData);

	gpsSysSpecificData->bSGXInitComplete = true;

	return eError;
}


PVRSRV_ERROR SysDeinitialise (SYS_DATA *psSysData)
{
	PVRSRV_ERROR eError;

	if(gpsSysData->pvSOCTimerRegisterKM)
	{
		OSUnReservePhys(gpsSysData->pvSOCTimerRegisterKM,
						4,
						PVRSRV_HAP_MULTI_PROCESS|PVRSRV_HAP_UNCACHED,
						gpsSysData->hSOCTimerRegisterOSMemHandle);
	}

	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR))
	{
		eError = OSUninstallDeviceLISR(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: OSUninstallDeviceLISR failed"));
			return eError;
		}
	}

	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_MISR))
	{
		eError = OSUninstallMISR(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: OSUninstallMISR failed"));
			return eError;
		}
	}

	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_INITDEV))
	{
		PVR_ASSERT(SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS));

		eError = EnableSGXClocksWrap(gpsSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: EnableSGXClocks failed"));
			return eError;
		}

		eError = PVRSRVDeinitialiseDevice (gui32SGXDeviceID);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: failed to de-init the device"));
			return eError;
		}
	}
#if 0
	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_PM_RUNTIME))
	{
		eError = SysPMRuntimeUnregister();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: Failed to unregister with OSPM!"));
			gpsSysData = IMG_NULL;
			return eError;
		}
	}
#endif
	

	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS))
	{
		DisableSystemClocks(gpsSysData);
	}

	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_ENVDATA))
	{	
		eError = OSDeInitEnvData(gpsSysData->pvEnvSpecificData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: failed to de-init env structure"));
			return eError;
		}
	}

	SysDeinitialiseCommon(gpsSysData);

	gpsSysSpecificData->ui32SysSpecificData = 0;
	gpsSysSpecificData->bSGXInitComplete = false;

	gpsSysData = IMG_NULL;

	return PVRSRV_OK;
}


PVRSRV_ERROR SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE	eDeviceType,
								   IMG_VOID				**ppvDeviceMap)
{

	switch(eDeviceType)
	{
		case PVRSRV_DEVICE_TYPE_SGX:
		{
			
			*ppvDeviceMap = (IMG_VOID*)&gsSGXDeviceMap;

			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR,"SysGetDeviceMemoryMap: unsupported device type"));
		}
	}
	return PVRSRV_OK;
}


IMG_DEV_PHYADDR SysCpuPAddrToDevPAddr(PVRSRV_DEVICE_TYPE	eDeviceType,
									  IMG_CPU_PHYADDR		CpuPAddr)
{
	IMG_DEV_PHYADDR DevPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	DevPAddr.uiAddr = CpuPAddr.uiAddr;
	
	return DevPAddr;
}

IMG_CPU_PHYADDR SysSysPAddrToCpuPAddr (IMG_SYS_PHYADDR sys_paddr)
{
	IMG_CPU_PHYADDR cpu_paddr;

	
	cpu_paddr.uiAddr = sys_paddr.uiAddr;
	return cpu_paddr;
}

IMG_SYS_PHYADDR SysCpuPAddrToSysPAddr (IMG_CPU_PHYADDR cpu_paddr)
{
	IMG_SYS_PHYADDR sys_paddr;

	
	sys_paddr.uiAddr = cpu_paddr.uiAddr;
	return sys_paddr;
}


IMG_DEV_PHYADDR SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE eDeviceType, IMG_SYS_PHYADDR SysPAddr)
{
	IMG_DEV_PHYADDR DevPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	DevPAddr.uiAddr = SysPAddr.uiAddr;

	return DevPAddr;
}


IMG_SYS_PHYADDR SysDevPAddrToSysPAddr(PVRSRV_DEVICE_TYPE eDeviceType, IMG_DEV_PHYADDR DevPAddr)
{
	IMG_SYS_PHYADDR SysPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	SysPAddr.uiAddr = DevPAddr.uiAddr;

	return SysPAddr;
}


IMG_VOID SysRegisterExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}


IMG_VOID SysRemoveExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}


IMG_UINT32 SysGetInterruptSource(SYS_DATA			*psSysData,
								 PVRSRV_DEVICE_NODE	*psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psSysData);
	return psDeviceNode->ui32SOCInterruptBit;
}


IMG_VOID SysClearInterrupts(SYS_DATA* psSysData, IMG_UINT32 ui32ClearBits)
{
	PVR_UNREFERENCED_PARAMETER(ui32ClearBits);
#if defined(SGX_OCP_NO_INT_BYPASS)
	OSWriteHWReg(gpvOCPRegsLinAddr, EUR_CR_OCP_IRQSTATUS_2, 0x1);
#endif
	OSReadHWReg(((PVRSRV_SGXDEV_INFO *)gpsSGXDevNode->pvDevice)->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR);
}

#if defined(SGX_OCP_NO_INT_BYPASS)
IMG_VOID SysEnableSGXInterrupts(SYS_DATA *psSysData)
{
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *)psSysData->pvSysSpecificData;
	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_LISR) && !SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_IRQ_ENABLED))
	{
		OSWriteHWReg(gpvOCPRegsLinAddr, EUR_CR_OCP_IRQSTATUS_2, 0x1);
		OSWriteHWReg(gpvOCPRegsLinAddr, EUR_CR_OCP_IRQENABLE_SET_2, 0x1);
		SYS_SPECIFIC_DATA_SET(psSysSpecData, SYS_SPECIFIC_DATA_IRQ_ENABLED);
	}
}

IMG_VOID SysDisableSGXInterrupts(SYS_DATA *psSysData)
{
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *)psSysData->pvSysSpecificData;

	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_IRQ_ENABLED))
	{
		OSWriteHWReg(gpvOCPRegsLinAddr, EUR_CR_OCP_IRQENABLE_CLR_2, 0x1);
		SYS_SPECIFIC_DATA_CLEAR(psSysSpecData, SYS_SPECIFIC_DATA_IRQ_ENABLED);
	}
}
#endif	

PVRSRV_ERROR SysSystemPrePowerState(PVRSRV_SYS_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (eNewPowerState == PVRSRV_SYS_POWER_STATE_D3)
	{
		PVR_TRACE(("SysSystemPrePowerState: Entering state D3"));

		if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR))
		{
			bool bWrapped = WrapSystemPowerChange(&gsSysSpecificData);
			eError = OSUninstallDeviceLISR(gpsSysData);
			if (bWrapped)
			{
				UnwrapSystemPowerChange(&gsSysSpecificData);
			}
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SysSystemPrePowerState: OSUninstallDeviceLISR failed (%d)", eError));
				return eError;
			}
			SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR);
			SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);
		}

		if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS))
		{
			DisableSystemClocks(gpsSysData);

			SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS);
			SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);
		}
	}

	return eError;
}


PVRSRV_ERROR SysSystemPostPowerState(PVRSRV_SYS_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (eNewPowerState == PVRSRV_SYS_POWER_STATE_D0)
	{
		PVR_TRACE(("SysSystemPostPowerState: Entering state D0"));

		if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS))
		{
			eError = EnableSystemClocksWrap(gpsSysData);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SysSystemPostPowerState: EnableSystemClocksWrap failed (%d)", eError));
				return eError;
			}
			SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);
			SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS);
		}

		if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR))
		{
			bool bWrapped = WrapSystemPowerChange(&gsSysSpecificData);

			eError = OSInstallDeviceLISR(gpsSysData, gsSGXDeviceMap.ui32IRQ, "SGX ISR", gpsSGXDevNode);
			if (bWrapped)
			{
				UnwrapSystemPowerChange(&gsSysSpecificData);
			}
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SysSystemPostPowerState: OSInstallDeviceLISR failed to install ISR (%d)", eError));
				return eError;
			}
			SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);
			SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR);
		}
	}
	return eError;
}


PVRSRV_ERROR SysDevicePrePowerState(IMG_UINT32				ui32DeviceIndex,
									PVRSRV_DEV_POWER_STATE	eNewPowerState,
									PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVR_UNREFERENCED_PARAMETER(eCurrentPowerState);

	if (ui32DeviceIndex != gui32SGXDeviceID)
	{
		return PVRSRV_OK;
	}

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SysDevicePrePowerState: SGX Entering state D3"));
		DisableSGXClocks(gpsSysData);
	}
	return PVRSRV_OK;
}


PVRSRV_ERROR SysDevicePostPowerState(IMG_UINT32				ui32DeviceIndex,
									 PVRSRV_DEV_POWER_STATE	eNewPowerState,
									 PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(eNewPowerState);

	if (ui32DeviceIndex != gui32SGXDeviceID)
	{
		return eError;
	}

	if (eCurrentPowerState == PVRSRV_DEV_POWER_STATE_OFF)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SysDevicePostPowerState: SGX Leaving state D3"));
		eError = EnableSGXClocksWrap(gpsSysData);
	}

	return eError;
}


PVRSRV_ERROR SysOEMFunction (	IMG_UINT32	ui32ID,
								IMG_VOID	*pvIn,
								IMG_UINT32	ulInSize,
								IMG_VOID	*pvOut,
								IMG_UINT32	ulOutSize)
{
	PVR_UNREFERENCED_PARAMETER(ui32ID);
	PVR_UNREFERENCED_PARAMETER(pvIn);
	PVR_UNREFERENCED_PARAMETER(ulInSize);
	PVR_UNREFERENCED_PARAMETER(pvOut);
	PVR_UNREFERENCED_PARAMETER(ulOutSize);

	if ((ui32ID == OEM_GET_EXT_FUNCS) &&
		(ulOutSize == sizeof(PVRSRV_DC_OEM_JTABLE)))
	{
		
		PVRSRV_DC_OEM_JTABLE *psOEMJTable = (PVRSRV_DC_OEM_JTABLE*) pvOut;
		psOEMJTable->pfnOEMBridgeDispatch = &PVRSRV_BridgeDispatchKM;
		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_INVALID_PARAMS;
}
