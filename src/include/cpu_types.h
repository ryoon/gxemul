#ifndef	CPU_TYPES_H
#define	CPU_TYPES_H

/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: cpu_types.h,v 1.2 2004-12-08 19:55:44 debug Exp $
 *
 *  CPU types.
 */

#include <misc.h>

/*  CPU types:  */
#include "cpuregs.h"			/*  from NetBSD  */
#define	MIPS_5K		129		/*  according to MIPS64 5K User's Manual  */
#define	MIPS_5K_REV	    1		/*  according to MIPS64 5K User's Manual  */

#define	EXC3K		3
#define	EXC4K		4
#define	EXC32		32
#define	EXC64		64

#define	MMU3K		3
#define	MMU4K		4
#define	MMU8K		8
#define	MMU10K		10
#define	MMU32		32
#define	MMU64		64

/*  Bit-field values for the flags field:  */
#define	NOLLSC		1
#define	DCOUNT		2

#define	CPU_DEFAULT	"R4000"

#define	CPU_TYPE_DEFS	{	\
	{ "R2000",	MIPS_R2000, 0x00,	NOLLSC,	EXC3K, MMU3K,	1,	64, 1,13,13, 2, 2, 0, 0 }, \
	{ "R2000A",	MIPS_R2000, 0x10,	NOLLSC,	EXC3K, MMU3K,	1,	64, 1,13,13, 2, 2, 0, 0 }, \
	{ "R3000",	MIPS_R3000, 0x20,	NOLLSC,	EXC3K, MMU3K,	1,	64, 1,12,12, 2, 2, 0, 0 }, \
	{ "R3000A",	MIPS_R3000, 0x30,	NOLLSC,	EXC3K, MMU3K,	1,	64, 1,13,13, 2, 2, 0, 0 }, \
	{ "R6000",	MIPS_R6000, 0x00,	0,	EXC3K, MMU3K,	2,	32, 1,16,16, 2, 2, 0, 0 }, /*  instrs/cycle?  */  \
	{ "R4000",	MIPS_R4000, 0x00,	DCOUNT,	EXC4K, MMU4K,	3,	48, 2,13,13, 4, 4,19, 6 }, \
	{ "R4000PC",	MIPS_R4000, 0x00,	DCOUNT,	EXC4K, MMU4K,	3,	48, 2,13,13, 4, 4, 0, 6 }, \
	{ "R10000",	MIPS_R10000,0x26,	0,	EXC4K, MMU10K,	4,	64, 4,15,15, 6, 5,20, 6 }, /*  2way I,D,Secondary  */ \
	{ "R4200",	MIPS_R4200, 0x00,	0,	EXC4K, MMU4K,	3,	32, 2, 0, 0, 0, 0, 0, 0 }, /*  No DCOUNT?  */ \
	{ "R4300",	MIPS_R4300, 0x00,	0,	EXC4K, MMU4K,	3,	32, 2, 0, 0, 0, 0, 0, 0 }, /*  No DCOUNT?  */ \
	{ "R4100",	MIPS_R4100, 0x00,	0,	EXC4K, MMU4K,	3,	32, 2, 0, 0, 0, 0, 0, 0 }, /*  No DCOUNT?  */ \
	{ "R4400",	MIPS_R4000, 0x40,	DCOUNT,	EXC4K, MMU4K,	3,	48, 2,14,14, 4, 4,20, 6 }, /*  direct mapped I,D,Sec  */ \
	{ "R4600",	MIPS_R4600, 0x00,	DCOUNT,	EXC4K, MMU4K,	3,	48, 2, 0, 0, 0, 0, 0, 0 }, \
	{ "R4700",	MIPS_R4700, 0x00,	0,	EXC4K, MMU4K,	3,	48, 2, 0, 0, 0, 0, 0, 0 }, /*  No DCOUNT?  */ \
	{ "R4650",	MIPS_R4650, 0x00,	0,	EXC4K, MMU4K,	3,	48, 2, 0, 0, 0, 0, 0, 0 }, /*  No DCOUNT?  */ \
	{ "R8000",	MIPS_R8000, 0,		0,	EXC4K, MMU8K,	4,     192, 2, 0, 0, 0, 0, 0, 0 }, /*  192 tlb entries? or 384? instrs/cycle?  */ \
	{ "R12000",	MIPS_R12000,0x23,	0,	EXC4K, MMU10K,	4,	64, 4,15,15, 6, 5,23, 6 }, \
	{ "R14000",	MIPS_R14000,0,		0,	EXC4K, MMU10K,	4,	64, 4,15,15, 6, 5,22, 6 }, \
	{ "R5000",	MIPS_R5000, 0x21,	DCOUNT,	EXC4K, MMU4K,	4,	48, 4,15,15, 5, 5, 0, 0 }, /*  2way I,D; instrs/cycle?  */ \
	{ "R5900",	MIPS_R5900, 0x20,	0,	EXC4K, MMU4K,	3,	48, 4,14,13, 6, 6, 0, 0 }, /*  instrs/cycle?  */ \
	{ "TX3920",	MIPS_TX3900,0x30,	0,	EXC32, MMU32,	1,	32, 2, 0, 0, 0, 0, 0, 0 }, /*  TODO: bogus?  */ \
	{ "TX7901",	0x38,	    0x01,	0,	EXC4K, MMU4K,  64,	48, 4, 0, 0, 0, 0, 0, 0 }, /*  TODO: bogus?  */ \
	{ "VR5432",	MIPS_R5400, 13,		0,	EXC4K, MMU4K,	-1,	-1, 4, 0, 0, 0, 0, 0, 0 }, /*  DCOUNT?  instrs/cycle?  */ \
	{ "RM5200",	MIPS_RM5200,0xa0,	0,	EXC4K, MMU4K,	4,	48, 4, 0, 0, 0, 0, 0, 0 }, /*  DCOUNT?  instrs/cycle?  */ \
	{ "RM7000",	MIPS_RM7000,0x0 /* ? */,DCOUNT,	EXC4K, MMU4K,	4,	48, 4,14,14, 5, 5,18, 6 }, /*  instrs/cycle? cachelinesize & assoc.? RM7000A? */ \
	{ "RM7900",	0 /*TODO*/, 0x0 /* ? */,DCOUNT,	EXC4K, MMU4K,	4,	64, 4,14,14, 5, 5,18, 6 }, /*  instrs/cycle? cachelinesize? assoc = 4ways for all  */ \
	{ "RC32334",	MIPS_RC32300,0x00,	0,	EXC32, MMU4K,  32,      16, 1, 0, 0, 0, 0, 0, 0 }, \
	{ "5K",		0x100+MIPS_5K, 1,	0,	EXC4K, MMU4K,	5,	48, 4, 0, 0, 0, 0, 0, 0 }, /*  DCOUNT?  instrs/cycle?  */ \
	{ "BCM4710",	0x000240,   0x00,       0,	EXC32, MMU32,  32,      32, 2, 0, 0, 0, 0, 0, 0 }, /*  TODO: this is just bogus  */ \
	{ "BCM4712",	0x000290,   0x07,       0,	EXC32, MMU32,  32,      32, 2,13,12, 4, 4, 0, 0 }, /*  2ways I, 2ways D  */ \
	{ "AU1000",	0x000301,   0x00,       0,	EXC32, MMU32,  32,      32, 2, 0, 0, 0, 0, 0, 0 }, /*  TODO: this is just bogus  */ \
	{ "AU1500",	0x010301,   0x00,       0,	EXC32, MMU32,  32,      32, 2, 0, 0, 0, 0, 0, 0 }, /*  TODO: this is just bogus  */ \
	{ "AU1100",	0x020301,   0x00,       0,	EXC32, MMU32,  32,      32, 2, 0, 0, 0, 0, 0, 0 }, /*  TODO: this is just bogus  */ \
	{ "SB1",	0x000401,   0x00,	0,	EXC64, MMU64,  64,      32, 2, 0, 0, 0, 0, 0, 0 }, /*  TODO: this is just bogus  */ \
	{ "SR7100",	0x000504,   0x00,	0,	EXC64, MMU64,  64,      32, 2, 0, 0, 0, 0, 0, 0 }, /*  TODO: this is just bogus  */ \
	{ NULL,		0,          0,          0,      0,     0,       0,       0, 0, 0, 0, 0, 0, 0, 0 } }


#endif	/*  CPU_TYPES_H  */

