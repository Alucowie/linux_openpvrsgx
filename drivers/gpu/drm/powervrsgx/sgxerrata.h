/*************************************************************************/ /*!
@Title          SGX HW errata definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Specifies associations between SGX core revisions
                and SW workarounds required to fix HW errata that exist
                in specific core revisions
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
#ifndef __SGXERRATA_H__
#define __SGXERRATA_H__

/* ignore warnings about unrecognised preprocessing directives in conditional inclusion directives */
/* PRQA S 3115 ++ */

//#if defined(SGX530) && !defined(SGX_CORE_DEFINED)
	/* define the _current_ SGX530 RTL head revision */
	#define SGX_CORE_REV_HEAD	0
#define SGX_CORE_REV 121
	#if defined(USE_SGX_CORE_REV_HEAD)
		/* build config selects Core Revision to be the Head */
		#define SGX_CORE_REV	SGX_CORE_REV_HEAD
	#endif

		#define FIX_HW_BRN_22934/* Workaround in sgx featuredefs */
		#define FIX_HW_BRN_28889/* Workaround in services (srvkm) */
	/* signal that the Core Version has a valid definition */
	#define SGX_CORE_DEFINED
//#endif

#if !defined(SGX_CORE_DEFINED)
	#warning "sgxerrata.h: SGX Core Version unspecified"
#endif

/* restore warning */
/* PRQA S 3115 -- */

#endif /* __SGXERRATA_H__ */

/******************************************************************************
 End of file (sgxerrata.h)
******************************************************************************/
