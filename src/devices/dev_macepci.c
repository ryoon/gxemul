/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_macepci.c,v 1.7 2004-01-12 00:17:39 debug Exp $
 *  
 *  SGI "macepci".
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "bus_pci.h"

struct macepci_data {
	struct pci_data *pci_data;
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
	uint64_t idata = 0, odata=0;
	int regnr, i, res = 1;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	/*  Read from/write to the macepci:  */
	switch (relative_addr) {
	case 0x00:	/*  Error address  */
		if (writeflag == MEM_WRITE) {
		} else {
			odata = 0;
		}
		break;
	case 0x04:	/*  Error flags  */
		if (writeflag == MEM_WRITE) {
		} else {
			odata = 0x06;
		}
		break;
	case 0x0c:	/*  Revision number  */
		if (writeflag == MEM_WRITE) {
		} else {
			odata = 0x01;
		}
		break;
	case 0xcf8:	/*  PCI ADDR  */
	case 0xcfc:	/*  PCI DATA  */
		if (writeflag == MEM_WRITE) {
			res = bus_pci_access(cpu, mem, relative_addr, &idata, writeflag, d->pci_data);
		} else {
			res = bus_pci_access(cpu, mem, relative_addr, &odata, writeflag, d->pci_data);
			/*  odata = 0;  */
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ macepci: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ macepci: unimplemented read from address 0x%x ]\n", relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return res;
}


/*
 *  dev_macepci_init():
 */
struct pci_data *dev_macepci_init(struct memory *mem, uint64_t baseaddr, int pciirq)
{
	struct macepci_data *d = malloc(sizeof(struct macepci_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct macepci_data));

	d->pci_data = bus_pci_init(mem, pciirq);

	memory_device_register(mem, "macepci", baseaddr, DEV_MACEPCI_LENGTH, dev_macepci_access, (void *)d);

	return d->pci_data;
}

