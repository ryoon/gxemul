#ifndef	BUS_PCI_H
#define	BUS_PCI_H

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
 *  $Id: bus_pci.h,v 1.19 2005-11-08 12:03:27 debug Exp $
 */

#include "misc.h"
#include "pcireg.h"

struct machine;
struct memory;

#define	PCI_CFG_MEM_SIZE	0x80		/*  TODO  */

struct pci_device {
	struct pci_device	*next;
	int			bus, device, function;
	unsigned char		cfg_mem[PCI_CFG_MEM_SIZE];
};

struct pci_data {
	int		irq_nr;
	uint32_t	pci_addr;
	int		last_was_write_ffffffff;

	struct pci_device *first_device;
};

#define	BUS_PCI_ADDR	0xcf8
#define	BUS_PCI_DATA	0xcfc


/*  bus_pci.c:  */
int bus_pci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	uint64_t *data, int len, int writeflag, struct pci_data *pci_data);
void bus_pci_add(struct machine *machine, struct pci_data *pci_data,
	struct memory *mem, int bus, int device, int function,
	char *name);
struct pci_data *bus_pci_init(int irq_nr);


#define	PCIINIT(name)	void pciinit_ ## name(struct machine *machine,	\
	struct memory *mem, struct pci_device *pd)

/*  Store little-endian config data in the pci_data struct:  */
#define PCI_SET_DATA(ofs,value)	{					\
	if ((ofs) >= PCI_CFG_MEM_SIZE) {				\
		fatal("PCI_SET_DATA(): ofs too high (%i)\n", (ofs));	\
		exit(1);						\
	}								\
	pd->cfg_mem[(ofs)]     = (value) & 255;				\
	pd->cfg_mem[(ofs) + 1] = ((value) >> 8) & 255;			\
	pd->cfg_mem[(ofs) + 2] = ((value) >> 16) & 255;			\
	pd->cfg_mem[(ofs) + 3] = ((value) >> 24) & 255;			\
	}


#endif	/*  BUS_PCI_H  */
