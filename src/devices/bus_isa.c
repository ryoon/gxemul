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
 *  $Id: bus_isa.c,v 1.4 2005-11-21 09:17:26 debug Exp $
 *  
 *  Generic ISA bus. This is not a normal device, but it can be used as a quick
 *  way of adding most of the common legacy ISA devices to a machine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_isa.h"
#include "device.h"
#include "devices.h"
#include "diskimage.h"
#include "machine.h"
#include "misc.h"


/*
 *  bus_isa():
 *
 *  Flags are zero or more of the following, ORed together:
 *
 *  BUS_ISA_IDE0		Include wdc0.
 *  BUS_ISA_IDE1		Include wdc1.
 *  BUS_ISA_FDC			Include a floppy controller. (Dummy.)
 *  BUS_ISA_VGA			Include old-style (non-PCI) VGA. (*1)
 *  BUS_ISA_VGA_FORCE		Include VGA even when running without X11. (*2)
 *  BUS_ISA_PCKBC_FORCE_USE	Always assume keyboard console, not serial. (*3)
 *  BUS_ISA_PCKBC_NONPCSTYLE	Don't set the pc-style flag for the keyboard.
 *  BUS_ISA_NO_SECOND_PIC	Only useful for 8086 XT (pre-AT) emulation. :-)
 *
 *  (*1) For machines with a PCI bus, this flag should not be used. Instead, a
 *       PCI VGA card should be added to the PCI bus.
 *
 *  (*2) For machines where it is easy to select VGA vs serial console during
 *       boot, this flag should not be used. Machines that "always" boot up
 *       in VGA console mode should have it set.
 *
 *  (*3) Similar to *2 above; machines that always boot up with VGA console
 *       should have this flag set, so that the keyboard is always used.
 */
void bus_isa(struct machine *machine, uint32_t bus_isa_flags,
	uint64_t isa_portbase, uint64_t isa_membase, int isa_irqbase,
	int reassert_irq)
{
	char tmpstr[300];
	int tmp_handle, kbd_in_use;

	kbd_in_use = ((bus_isa_flags & BUS_ISA_PCKBC_FORCE_USE) ||
	    (machine->use_x11))? 1 : 0;

	snprintf(tmpstr, sizeof(tmpstr), "8259 irq=%i addr=0x%llx",
	    reassert_irq, (long long)(isa_portbase + 0x20));
	machine->isa_pic_data.pic1 = device_add(machine, tmpstr);

	if (bus_isa_flags & BUS_ISA_NO_SECOND_PIC)
		bus_isa_flags &= ~BUS_ISA_NO_SECOND_PIC;
	else {
		snprintf(tmpstr, sizeof(tmpstr), "8259 irq=%i addr=0x%llx",
		    reassert_irq, (long long)(isa_portbase + 0xa0));
		machine->isa_pic_data.pic2 = device_add(machine, tmpstr);
	}

	snprintf(tmpstr, sizeof(tmpstr), "8253 irq=%i addr=0x%llx in_use=0",
	    isa_irqbase + 0, (long long)(isa_portbase + 0x40));
	device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "pccmos addr=0x%llx",
	    (long long)(isa_portbase + 0x70));
	device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=%i addr=0x%llx name2="
	    "tty0 in_use=%i", isa_irqbase+4, (long long)(isa_portbase + 0x3f8),
	    1 - kbd_in_use);
	machine->main_console_handle = (size_t)device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=%i addr=0x%llx name2=tty1"
	    " in_use=0", isa_irqbase +3, (long long)(isa_portbase + 0x2f8));
	device_add(machine, tmpstr);

	snprintf(tmpstr, sizeof(tmpstr), "lpt irq=%i addr=0x%llx name2=lpt"
	    " in_use=0", isa_irqbase + 7, (long long)(isa_portbase + 0x378));
	device_add(machine, tmpstr);

	if (bus_isa_flags & BUS_ISA_IDE0) {
		bus_isa_flags &= ~BUS_ISA_IDE0;
		snprintf(tmpstr, sizeof(tmpstr), "wdc irq=%i addr=0x%llx",
		    isa_irqbase + 14, (long long)(isa_portbase + 0x1f0));
		if (diskimage_exist(machine, 0, DISKIMAGE_IDE) ||
		    diskimage_exist(machine, 1, DISKIMAGE_IDE))
			device_add(machine, tmpstr);
	}

	if (bus_isa_flags & BUS_ISA_IDE1) {
		bus_isa_flags &= ~BUS_ISA_IDE1;
		snprintf(tmpstr, sizeof(tmpstr), "wdc irq=%i addr=0x%llx",
		    isa_irqbase + 15, (long long)(isa_portbase + 0x170));
		if (diskimage_exist(machine, 2, DISKIMAGE_IDE) ||
		    diskimage_exist(machine, 3, DISKIMAGE_IDE))
			device_add(machine, tmpstr);
	}

	if (bus_isa_flags & BUS_ISA_FDC) {
		bus_isa_flags &= ~BUS_ISA_FDC;
		snprintf(tmpstr, sizeof(tmpstr), "fdc irq=%i addr=0x%llx",
		    isa_irqbase + 6, (long long)(isa_portbase + 0x3f0));
		device_add(machine, tmpstr);
	}

	if (bus_isa_flags & BUS_ISA_VGA) {
		if (machine->use_x11 || bus_isa_flags & BUS_ISA_VGA_FORCE)
			dev_vga_init(machine, machine->memory,
			    isa_membase + 0xa0000, isa_portbase + 0x3c0,
			    machine->machine_name);
		bus_isa_flags &= ~(BUS_ISA_VGA | BUS_ISA_VGA_FORCE);
	}

	tmp_handle = dev_pckbc_init(machine, machine->memory,
	    isa_portbase + 0x60, PCKBC_8042, isa_irqbase + 1, isa_irqbase + 12,
	    kbd_in_use, bus_isa_flags & BUS_ISA_PCKBC_NONPCSTYLE? 0 : 1);

	if (kbd_in_use)
		machine->main_console_handle = tmp_handle;

	bus_isa_flags &= ~(BUS_ISA_PCKBC_NONPCSTYLE | BUS_ISA_PCKBC_FORCE_USE);

	if (bus_isa_flags != 0)
		fatal("WARNING! bus_isa(): unimplemented bus_isa_flags 0x%x\n",
		    bus_isa_flags);
}

