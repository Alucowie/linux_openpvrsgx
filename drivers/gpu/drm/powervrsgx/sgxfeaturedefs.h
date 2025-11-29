/*************************************************************************/ /*!
@Title          SGX fexture definitions
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
	#define SGX_CORE_FRIENDLY_NAME							"SGX530"
	#define SGX_CORE_ID										SGX_CORE_ID_530
	#define SGX_FEATURE_ADDRESS_SPACE_SIZE					(28)
	#define SGX_FEATURE_NUM_USE_PIPES						(2)
	#define SGX_FEATURE_AUTOCLOCKGATING

#if defined(SGX_FEATURE_SLAVE_VDM_CONTEXT_SWITCH) \
	|| defined(SGX_FEATURE_MASTER_VDM_CONTEXT_SWITCH)
/* Enable the define so common code for HW VDMCS code is compiled */
#define SGX_FEATURE_VDM_CONTEXT_SWITCH
#endif

/*
	'switch-off' features if defined BRNs affect the feature
*/

#if defined(FIX_HW_BRN_27266)
#undef SGX_FEATURE_36BIT_MMU
#endif

#if defined(FIX_HW_BRN_22934)	\
	|| defined(FIX_HW_BRN_25499)
#undef SGX_FEATURE_MULTI_EVENT_KICK
#endif

#if defined(SGX_FEATURE_SYSTEM_CACHE)
	#if defined(SGX_FEATURE_36BIT_MMU)
		#error SGX_FEATURE_SYSTEM_CACHE is incompatible with SGX_FEATURE_36BIT_MMU
	#endif
	#if defined(FIX_HW_BRN_26620) && !defined(SGX_FEATURE_MULTI_EVENT_KICK)
		#define SGX_BYPASS_SYSTEM_CACHE
	#endif
#endif

#if defined(FIX_HW_BRN_29954)
#undef SGX_FEATURE_PERPIPE_BKPT_REGS
#endif

#if defined(FIX_HW_BRN_31620)
#undef SGX_FEATURE_MULTIPLE_MEM_CONTEXTS
#undef SGX_FEATURE_BIF_NUM_DIRLISTS
#endif

/*
	Derive other definitions:
*/

/* define default MP core count */
#if defined(SGX_FEATURE_MP)
#if defined(SGX_FEATURE_MP_CORE_COUNT_TA) && defined(SGX_FEATURE_MP_CORE_COUNT_3D)
#if (SGX_FEATURE_MP_CORE_COUNT_TA > SGX_FEATURE_MP_CORE_COUNT_3D)
#error Number of TA cores larger than number of 3D cores not supported in current driver
#endif /* (SGX_FEATURE_MP_CORE_COUNT_TA > SGX_FEATURE_MP_CORE_COUNT_3D) */
#else
#if defined(SGX_FEATURE_MP_CORE_COUNT)
#define SGX_FEATURE_MP_CORE_COUNT_TA		(SGX_FEATURE_MP_CORE_COUNT)
#define SGX_FEATURE_MP_CORE_COUNT_3D		(SGX_FEATURE_MP_CORE_COUNT)
#else
#error Either SGX_FEATURE_MP_CORE_COUNT or \
both SGX_FEATURE_MP_CORE_COUNT_TA and SGX_FEATURE_MP_CORE_COUNT_3D \
must be defined when SGX_FEATURE_MP is defined
#endif /* SGX_FEATURE_MP_CORE_COUNT */
#endif /* defined(SGX_FEATURE_MP_CORE_COUNT_TA) && defined(SGX_FEATURE_MP_CORE_COUNT_3D) */
#else
#define SGX_FEATURE_MP_CORE_COUNT		(1)
#define SGX_FEATURE_MP_CORE_COUNT_TA	(1)
#define SGX_FEATURE_MP_CORE_COUNT_3D	(1)
#endif /* SGX_FEATURE_MP */

#include "img_types.h"

/******************************************************************************
 End of file (sgxfeaturedefs.h)
******************************************************************************/
