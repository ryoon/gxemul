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
 *  $Id: dev_kn01_csr.c,v 1.7 2004-11-17 20:37:39 debug Exp $
 *  
 *  PMAX (KN01) System Control Register.
 *
 *  TODO:  This is just a dummy device.
 *
 *  One of the few usable bits in the csr would be KN01_CSR_MONO.
 *  If that bit is set, the framebuffer is treated as a monochrome
 *  one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "dec_kn01.h"


struct kn01_csr_data {
	int		color_fb;
	int		csr;
};


/*
 *  dev_kn01_csr_access():
 */
int dev_kn01_csr_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct kn01_csr_data *k = extra;
	int csr;

	if (writeflag == MEM_WRITE) {
		/*  TODO  */
		return 1;
	}

	/*  Read:  */
	if (len != 2 || relative_addr != 0) {
		fatal("[ kn01_csr: trying to read something which is not the first half-word of the csr ]");
	}

	csr = k->csr;

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		data[0] = csr & 0xff;
		data[1] = (csr >> 8) & 0xff;
	} else {
		data[1] = csr & 0xff;
		data[0] = (csr >> 8) & 0xff;
	}

	return 1;
}


/*
 *  dev_kn01_csr_init():
 */
void dev_kn01_csr_init(struct memory *mem, uint64_t baseaddr, int color_fb)
{
	struct kn01_csr_data *k = malloc(sizeof(struct kn01_csr_data));
	if (k == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(k, 0, sizeof(struct kn01_csr_data));
	k->color_fb = color_fb;

	k->csr = 0;
	k->csr |= (color_fb? 0 : KN01_CSR_MONO);

	memory_device_register(mem, "kn01_csr", baseaddr,
	    DEV_KN01_CSR_LENGTH, dev_kn01_csr_access, k, MEM_DEFAULT, NULL);
}

