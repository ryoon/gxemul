#ifndef	ARM_CPU_TYPES_H
#define	ARM_CPU_TYPES_H

/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  $Id: arm_cpu_types.h,v 1.3 2005-09-18 19:54:16 debug Exp $
 */

/*  See cpu_arm.h for struct arm_cpu_type_def.  */

/*  Flags:  */
#define	ARM_NO_MMU		1
#define	ARM_DUAL_ENDIAN		2

#include "armreg.h"

#define	ARM_CPU_TYPE_DEFS					      {	 \
	{ "ARM610",	CPU_ID_ARM610,	ARM_DUAL_ENDIAN, 12, 1,  0, 1 }, \
	{ "ARM620",	CPU_ID_ARM620,	ARM_DUAL_ENDIAN, 12, 1,  0, 1 }, \
	{ "SA110",	CPU_ID_SA110 | 3, 0,		 14, 1, 14, 1 }, \
	{ "SA1110",	CPU_ID_SA1110,	0,		 14, 1, 14, 1 }, \
	{ "PXA210",	CPU_ID_PXA210,	0,		 16, 1,  0, 1 }, \
	{ "XSCALE600",	CPU_ID_80321_600_B0,0,		 14, 1, 14, 1 }, \
	{ NULL, 0, 0, 0,0, 0,0 } }


#endif	/*  ARM_CPU_TYPES_H  */
