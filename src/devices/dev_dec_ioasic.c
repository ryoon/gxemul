/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_dec_ioasic.c,v 1.2 2004-03-04 03:15:50 debug Exp $
 *  
 *  DECstation "3MIN" and "3MAX" IOASIC device.
 *
 *  TODO:  Lots of stuff, such as DMA and all bits in the control registers.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "tc_ioasicreg.h"

#define IOASIC_DEBUG


/*
 *  dev_dec_ioasic_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_dec_ioasic_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct dec_ioasic_data *d = (struct dec_ioasic_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

#ifdef IOASIC_DEBUG
	if (writeflag == MEM_WRITE)
		debug("[ dec_ioasic: write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
	else
		debug("[ dec_ioasic: read from address 0x%llx ]\n", (long long)relative_addr);
#endif

	switch (relative_addr) {

	case IOASIC_SCC_T1_DMAPTR:
		if (writeflag == MEM_WRITE) {
			/*  TODO: some left-over bits, see tc_ioasicreg.h  */
			d->t1_dmaptr = idata >> 3;
			d->t1_cur_ptr = d->t1_dmaptr;
		}
		break;

	case IOASIC_SCC_T2_DMAPTR:
		if (writeflag == MEM_WRITE) {
			/*  TODO: some left-over bits, see tc_ioasicreg.h  */
			d->t2_dmaptr = idata >> 3;
			d->t2_cur_ptr = d->t2_dmaptr;
		}
		break;

	case IOASIC_CSR:
		if (writeflag == MEM_WRITE) {
			d->csr = idata;

			if (d->csr & IOASIC_CSR_DMAEN_T1) {
				/*
				 *  Transmit all data here:
				 */

				while ((d->t1_cur_ptr & 0xffc) != 0) {
					unsigned char word[4];
					memory_rw(cpu, mem, d->t1_cur_ptr, &word[0], sizeof(word), MEM_READ, NO_EXCEPTIONS | PHYSICAL);

					/*  TODO:  this is hardcoded for scc  */
					printf("%c", word[1]);

					d->t1_cur_ptr += 4;
				}

				/*  and signal the end of page:  */
				d->intr |= IOASIC_INTR_T1_PAGE_END;
			} else
				d->intr &= ~IOASIC_INTR_T1_PAGE_END;

			if (d->csr & IOASIC_CSR_DMAEN_T2) {
				/*
				 *  Transmit all data here:
				 */

				while ((d->t2_cur_ptr & 0xffc) != 0) {
					unsigned char word[4];
					memory_rw(cpu, mem, d->t2_cur_ptr, &word[0], sizeof(word), MEM_READ, NO_EXCEPTIONS | PHYSICAL);

					/*  TODO:  this is hardcoded for scc  */
					printf("%c", word[1]);

					d->t2_cur_ptr += 4;
				}

				/*  and signal the end of page:  */
				d->intr |= IOASIC_INTR_T2_PAGE_END;
			} else
				d->intr &= ~IOASIC_INTR_T2_PAGE_END;
		} else
			odata = d->csr;
		break;
	case IOASIC_INTR:
		if (writeflag == MEM_READ)
			odata = d->intr;
		else {
			/*  Clear bits on write.  */
			d->intr &= ~idata;
		}
		break;
	case IOASIC_IMSK:
		if (writeflag == MEM_WRITE)
			d->imsk = idata;
		else
			odata = d->imsk;
		break;

	case IOASIC_CTR:
		if (writeflag == MEM_READ)
			odata = d->intr;
		break;

	case 0x80000:
	case 0x80004:
	case 0x80008:
	case 0x8000c:
	case 0x80010:
	case 0x80014:
		/*  Station's ethernet address:  */
		if (writeflag == MEM_WRITE) {
		} else {
			odata = ((relative_addr - 0x80000) / 4 + 1) * 0x11;
		}
		break;

	default:
		if (writeflag == MEM_WRITE)
			debug("[ dec_ioasic: unimplemented write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
		else
			debug("[ dec_ioasic: unimplemented read from address 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_dec_ioasic_init():
 */
struct dec_ioasic_data *dev_dec_ioasic_init(struct memory *mem, uint64_t baseaddr)
{
	struct dec_ioasic_data *d = malloc(sizeof(struct dec_ioasic_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec_ioasic_data));

	memory_device_register(mem, "dec_ioasic", baseaddr, DEV_DEC_IOASIC_LENGTH, dev_dec_ioasic_access, (void *)d);

	return d;
}

