#ifndef	CPU_HPPA_H
#define	CPU_HPPA_H

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
 *  $Id: cpu_hppa.h,v 1.2 2005-03-09 17:11:03 debug Exp $
 */

#include "misc.h"

#define	N_HPPA_GRS		32

struct cpu_family;

struct hppa_cpu {
	int		bits;
	uint64_t	pc_last;
	uint64_t	gr[N_HPPA_GRS];
};


/*
 *  Why on earth did they make it this way? Hm. See Appendix E in the HPPA 2.0
 *  specs for more info. This is insane.
 *
 *	assemble_21(x) = x[20],x[9..19],x[5..6],x[0..4],x[7..8]
 *
 *  Written in the normal way, where bit 0 is the lowest:
 *
 *	asm_21(x) = x[0],x[11..1],x[15..14],x[20..16],x[13..12]
 *
 *  That is:
 *
 *	    bits   0,11,10, 9, 8, 7, 6, 5, 4, 3, 2,1,15,14,20,19,18,17,16,13,12
 *      positions 20,19,18,17,16,15,14,13,12,11,10,9, 8, 7, 6, 5, 4, 3, 2, 1, 0
 *
 *  YUCK!
 */
#define	assemble_21(x) (			\
	(((x) & 1) << 20) |			\
	((((x) >> 1) & 0x7ff) << 9) |		\
	((((x) >> 14) & 3) << 7) |		\
	((((x) >> 16) & 0x1f) << 2) |		\
	(((x) >> 12) & 3)			)

/*  cpu_hppa.c:  */
int hppa_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int hppa_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_HPPA_H  */
