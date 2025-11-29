/*************************************************************************/ /*!
@Title          pdump functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main APIs for pdump functions
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
#ifndef __PDUMP_KM_H__
#define __PDUMP_KM_H__


/*
 * Include the OS abstraction APIs
 */
#include "pdump_osfunc.h"

/*
 *	Pull in pdump flags from services include
 */
#include "pdump.h"

#define PDUMP_PD_UNIQUETAG			(IMG_HANDLE)0
#define PDUMP_PT_UNIQUETAG			(IMG_HANDLE)0

/*
 * PDump streams (common to all OSes)
 */
#define PDUMP_STREAM_PARAM2			0
#define PDUMP_STREAM_SCRIPT2		1
#define PDUMP_STREAM_DRIVERINFO		2
#define PDUMP_NUM_STREAMS			3

#if defined(PDUMP_DEBUG_OUTFILES)
/* counter increments each time debug write is called */
extern IMG_UINT32 g_ui32EveryLineCounter;
#endif

#ifndef PDUMP
#define MAKEUNIQUETAG(hMemInfo)	(0)
#endif

#define PDUMPMEMPOL(args...)
#define PDUMPMEM(args...)
#define PDUMPMEMPTENTRIES(args...)
#define PDUMPPDENTRIES(args...)
#define PDUMPMEMUM(args...)
#define PDUMPINIT(args...)
#define PDUMPDEINIT(args...)
#define PDUMPISLASTFRAME(args...)
#define PDUMPTESTFRAME(args...)
#define PDUMPTESTNEXTFRAME(args...)
#define PDUMPREGWITHFLAGS(args...)
#define PDUMPREG(args...)
#define PDUMPCOMMENT(args...)
#define PDUMPREGPOL(args...)
#define PDUMPREGPOLWITHFLAGS(args...)
#define PDUMPMALLOCPAGES(args...)
#define PDUMPMALLOCPAGETABLE(args...)
#define PDUMPSETMMUCONTEXT(args...)
#define PDUMPCLEARMMUCONTEXT(args...)
#define PDUMPPDDEVPADDR(args...)
#define PDUMPFREEPAGES(args...)
#define PDUMPFREEPAGETABLE(args...)
#define PDUMPPDREG(args...)
#define PDUMPPDREGWITHFLAGS(args...)
#define PDUMPSYNC(args...)
#define PDUMPCOPYTOMEM(args...)
#define PDUMPWRITE(args...)
#define PDUMPCBP(args...)
#define PDUMPREGBASEDCBP(args...)
#define PDUMPCOMMENTWITHFLAGS(args...)
#define PDUMPMALLOCPAGESPHYS(args...)
#define PDUMPENDINITPHASE(args...)
#define PDUMPBITMAPKM(args...)
#define PDUMPDRIVERINFO(args...)
#define PDUMPIDLWITHFLAGS(args...)
#define PDUMPIDL(args...)
#define PDUMPSUSPEND(args...)
#define PDUMPRESUME(args...)

#endif /* __PDUMP_KM_H__ */

/******************************************************************************
 End of file (pdump_km.h)
******************************************************************************/
