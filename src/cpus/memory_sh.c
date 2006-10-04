/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: memory_sh.c,v 1.6 2006-10-04 11:56:21 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "sh4_mmu.h"


/*
 *  sh_translate_v2p():
 *
 *  TODO: DON'T HARDCODE VALUES! Use NetBSD's definitions instead.
 */
int sh_translate_v2p(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_paddr, int flags)
{
	int user = cpu->cd.sh.sr & SH_SR_MD? 0 : 1;

	vaddr = (uint32_t)vaddr;

	/*  U0/P0: Userspace addresses.  */
	if (!(vaddr & 0x80000000)) {
		if (flags & FLAG_NOEXCEPTIONS) {
			*return_paddr = 0;
			return 2;
		}

		/*  Address translation turned off?  */
		if (!(cpu->cd.sh.mmucr & SH4_MMUCR_AT)) {
			/*  Then return raw physical address:  */
			*return_paddr = vaddr & 0x1fffffff;
			return 2;
		}

		fatal("TODO: sh_translate_v2p(): User space\n");
		exit(1);
	}

	/*  Store queue region:  */
	if (vaddr >= 0xe0000000 && vaddr < 0xe4000000) {
		fatal("Store queue region: TODO! (vaddr=0x%08"PRIx32"\n",
		    (uint32_t) vaddr);
		/*  Note/TODO: Take SH4_MMUCR_SQMD into account.  */
		exit(1);
	}

	if (user) {
		fatal("Userspace tried to access non-user space memory."
		    " TODO: cause exception! (vaddr=0x%08"PRIx32"\n",
		    (uint32_t) vaddr);
		exit(1);
	}

	/*  P1,P2: Direct-mapped physical memory.  */
	if (vaddr >= 0x80000000 && vaddr < 0xc0000000) {
		*return_paddr = vaddr & 0x1fffffff;
		return 2;
	}

	/*  P3: Kernel virtual memory.  */
	if (vaddr >= 0xc0000000 && vaddr < 0xe0000000) {
		if (flags & FLAG_NOEXCEPTIONS) {
			*return_paddr = 0;
			return 2;
		}

		/*  Address translation turned off?  */
		if (!(cpu->cd.sh.mmucr & SH4_MMUCR_AT)) {
			/*  Then return raw physical address:  */
			*return_paddr = vaddr & 0x1fffffff;
			return 2;
		}

		fatal("TODO: sh_translate_v2p(): Kernel virtual memory\n");
		exit(1);
	}

	/*  P4: Special registers mapped at 0xf0000000 .. 0xffffffff:  */
	if ((vaddr & 0xf0000000) == 0xf0000000) {
		*return_paddr = vaddr;
		return 2;
	}

	/*  TODO  */
	fatal("Unimplemented SH vaddr 0x%08"PRIx32"\n", (uint32_t)vaddr);
	exit(1);
}

