#ifndef	DEVICE_H
#define	DEVICE_H

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
 *  $Id: device.h,v 1.12 2005-11-16 23:26:40 debug Exp $
 *
 *  Device registry.  (See device.c for more info.)
 */

#include "misc.h"
#include "bus_pci.h"

struct machine;

struct devinit {
	struct machine	*machine;
	char		*name;		/*  e.g. "cons"  */
	char		*name2;		/*  e.g. "secondary serial port"  */

	uint64_t	addr;		/*  Device base address  */
	uint64_t	len;
	int		irq_nr;
	int		in_use;
	int		addr_mult;

	void		*return_ptr;
};

struct device_entry {
	char		*name;
	int		(*initf)(struct devinit *);
};

struct pci_entry {
	char		*name;
	void		(*initf)(struct machine *, struct memory *,
			    struct pci_device *);
};

/*  autodev.c: (built automatically in the devices/ directory):  */
void autodev_init(void);

/*  device.c:  */
int device_register(char *name, int (*initf)(struct devinit *));
struct device_entry *device_lookup(char *name);
int device_unregister(char *name);
void *device_add(struct machine *machine, char *name_and_params);
void device_dumplist(void);
void device_set_exit_on_error(int exit_on_error);
void device_init(void);

/*  ISA stuff:  (TODO: move somewhere else?)  */
void bus_isa(struct machine *machine, uint32_t bus_isa_flags,
	uint64_t isa_portbase, uint64_t isa_membase, int isa_irqbase,
	int reassert_irq);
/*  ISA bus flags:  */
#define	BUS_ISA_IDE0			1
#define	BUS_ISA_IDE1			2
#define	BUS_ISA_FDC			4
#define	BUS_ISA_VGA			8
#define	BUS_ISA_VGA_FORCE		16
#define	BUS_ISA_PCKBC_FORCE_USE		32
#define	BUS_ISA_PCKBC_NONPCSTYLE	64
#define	BUS_ISA_NO_SECOND_PIC		128

/*  PCI stuff:  (TODO: move somewhere else?)  */
int pci_register(char *name, void (*initf)(struct machine *, struct memory *,
	struct pci_device *));
void (*pci_lookup_initf(char *name))(struct machine *machine,
	struct memory *mem, struct pci_device *pd);

#endif	/*  DEVICE_H  */
