#ifndef	BINTRANS_H
#define	BINTRANS_H

/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: bintrans.h,v 1.18 2005-02-02 23:55:19 debug Exp $
 *
 *  Binary translation functions.  (See bintrans.c for more info.)
 */

#include <sys/types.h>

#include "misc.h"

struct translation_page_entry {
	struct translation_page_entry	*next;
	uint64_t			paddr;

	int				page_is_potentially_in_use;

	uint32_t			chunk[1024];
	char				flags[1024];
};

#define	UNTRANSLATABLE		0x01

#define	BINTRANS_CACHE_N_INDEX_BITS	15
#define	CACHE_INDEX_MASK		((1 << BINTRANS_CACHE_N_INDEX_BITS) - 1)
#define	PADDR_TO_INDEX(p)		((p >> 12) & CACHE_INDEX_MASK)

#define	CODE_CHUNK_SPACE_MARGIN		65536


/*  bintrans.c:  */
void bintrans_invalidate(struct cpu *cpu, uint64_t paddr);
int bintrans_attempt_translate(struct cpu *cpu, uint64_t paddr);
void bintrans_init_cpu(struct cpu *cpu);
void bintrans_init(struct machine *machine, struct memory *mem);


#endif	/*  BINTRANS_H  */
