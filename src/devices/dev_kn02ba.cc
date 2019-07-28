/*
 *  Copyright (C) 2003-2018  Anders Gavare.  All rights reserved.
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
 *  COMMENT: DEC KN02BA "3min" TurboChannel interrupt controller
 *
 *  Used in DECstation 5000/1xx "3MIN".  See include/dec_kmin.h for more info.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/dec_kmin.h"


struct kn02ba_data {
	struct dec_ioasic_data *dec_ioasic;

	uint32_t	mer;	/*  Memory Error Register  */
	uint32_t	msr;	/*  Memory Size Register  */

	struct interrupt irq;
};


/*
 *  kn02ba_interrupt_assert(), kn02ba_interrupt_deassert():
 *
 *  Called whenever a kn02ba interrupt is asserted/deasserted.
 */
void kn02ba_interrupt_assert(struct interrupt *interrupt)
{
	struct kn02ba_data *d = (struct kn02ba_data *) interrupt->extra;
	struct dec_ioasic_data *r = (struct dec_ioasic_data *) d->dec_ioasic;
	r->intr |= interrupt->line;
	dec_ioasic_reassert(r);
}
void kn02ba_interrupt_deassert(struct interrupt *interrupt)
{
	struct kn02ba_data *d = (struct kn02ba_data *) interrupt->extra;
	struct dec_ioasic_data *r = (struct dec_ioasic_data *) d->dec_ioasic;
	r->intr &= ~interrupt->line;
	dec_ioasic_reassert(r);
}


DEVICE_ACCESS(kn02ba_mer)
{
	struct kn02ba_data *d = (struct kn02ba_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0:
		if (writeflag == MEM_WRITE)
			d->mer = idata;
		else
			odata = d->mer;

		debug("[ kn02ba_mer: %s MER, data=0x%08x (%s %s %s %s %s %s %s) ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			d->mer,
			d->mer & KMIN_MER_PAGE_BRY ? "PAGE_BRY" : "--",
			d->mer & KMIN_MER_TLEN ? "TLEN" : "--",
			d->mer & KMIN_MER_PARDIS ? "PARDIS" : "--",
			d->mer & KMIN_LASTB31 ? "LASTB31" : "--",
			d->mer & KMIN_LASTB23 ? "LASTB23" : "--",
			d->mer & KMIN_LASTB15 ? "LASTB15" : "--",
			d->mer & KMIN_LASTB07 ? "LASTB07" : "--"
			);

		break;

	default:
		if (writeflag == MEM_READ) {
			fatal("[ kn02ba_mer: read from offset 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			fatal("[ kn02ba_mer: write to offset 0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);
		}
		exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVICE_ACCESS(kn02ba_msr)
{
	struct kn02ba_data *d = (struct kn02ba_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0:
		if (writeflag == MEM_WRITE)
			d->msr = idata;
		else
			odata = d->msr;

		debug("[ kn02ba_msr: %s MSR, data=0x%08x (%s) ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			d->msr,
			d->msr & KMIN_MSR_SIZE_16Mb ? "16Mb" : "--"
			);

		break;

	default:
		if (writeflag == MEM_READ) {
			fatal("[ kn02ba_msr: read from offset 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			fatal("[ kn02ba_msr: write to offset 0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);
		}
		exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(kn02ba)
{
	struct kn02ba_data *d;
	int i;

	CHECK_ALLOCATION(d = (struct kn02ba_data *) malloc(sizeof(struct kn02ba_data)));
	memset(d, 0, sizeof(struct kn02ba_data));

	/*  Connect the kn02ba to a specific MIPS CPU interrupt line:  */
	INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	/*  Register the interrupts:  */
	for (i = 0; i < 32; i++) {
		struct interrupt templ;
		char tmpstr[300];
		snprintf(tmpstr, sizeof(tmpstr), "%s.kn02ba.0x%x",
		    devinit->interrupt_path, 1 << i);
		// printf("registering '%s'\n", tmpstr);
		memset(&templ, 0, sizeof(templ));
		templ.line = 1 << i;
		templ.name = tmpstr;
		templ.extra = d;
		templ.interrupt_assert = kn02ba_interrupt_assert;
		templ.interrupt_deassert = kn02ba_interrupt_deassert;
		interrupt_handler_register(&templ);
	}

	memory_device_register(devinit->machine->memory, "kn02ba_mer",
	    KMIN_REG_MER, sizeof(uint32_t), dev_kn02ba_mer_access, d,
	    DM_DEFAULT, NULL);

	memory_device_register(devinit->machine->memory, "kn02ba_msr",
	    KMIN_REG_MSR, sizeof(uint32_t), dev_kn02ba_msr_access, d,
	    DM_DEFAULT, NULL);

	d->dec_ioasic = dev_dec_ioasic_init(devinit->machine->cpus[0],
		devinit->machine->memory, KMIN_SYS_ASIC, 0, &d->irq);

	d->msr = KMIN_MSR_SIZE_16Mb;

	return 1;
}

