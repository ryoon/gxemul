/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_macepci.c,v 1.1 2003-12-28 20:56:15 debug Exp $
 *  
 *  SGI "macepci".
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "devices.h"


struct macepci_data {
	uint32_t	reg[DEV_MACEPCI_LENGTH / 4];
};


/*
 *  dev_macepci_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_macepci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct macepci_data *d = (struct macepci_data *) extra;
	int regnr;
	int idata = 0, odata=0, odata_set=0, i;

	/*  Switch byte order for incoming data, if neccessary:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		for (i=0; i<len; i++) {
			idata <<= 8;
			idata |= data[i];
		}
	else
		for (i=len-1; i>=0; i--) {
			idata <<= 8;
			idata |= data[i];
		}

	regnr = relative_addr / sizeof(uint32_t);

	/*  Read from/write to the macepci:  */
	switch (relative_addr) {
	case 0x00:	/*  Error address  */
		odata_set = 1;
		odata = 0;
		break;
	case 0x04:	/*  Error flags  */
		odata_set = 1;
		odata = 0x06;
		break;
	case 0x0c:	/*  Revision number  */
		odata_set = 1;
		odata = 0x01;
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ macepci: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ macepci: unimplemented read from address 0x%x ]\n", relative_addr);
		}
	}

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
	}

	return 1;
}


/*
 *  dev_macepci_init():
 */
void dev_macepci_init(struct memory *mem, uint64_t baseaddr)
{
	struct macepci_data *d = malloc(sizeof(struct macepci_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct macepci_data));

memset(d->reg, 0xff, sizeof(d->reg));

	memory_device_register(mem, "macepci", baseaddr, DEV_MACEPCI_LENGTH, dev_macepci_access, (void *)d);
}

