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
 *  $Id: dev_sh4.c,v 1.7 2006-10-19 10:15:57 debug Exp $
 *  
 *  SH4 processor specific memory mapped registers (0xf0000000 - 0xffffffff).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "sh4_cache.h"
#include "sh4_exception.h"
#include "sh4_mmu.h"
#include "sh4_scifreg.h"
#include "sh4_tmureg.h"


#define	SH4_REG_BASE	0xff000000

/*  #define debug fatal  */

struct sh4_data {
	int		scif_console_handle;
};


DEVICE_ACCESS(sh4_itlb_aa)
{
	uint64_t idata = 0, odata = 0;
	int e = (relative_addr & SH4_ITLB_E_MASK) >> SH4_ITLB_E_SHIFT;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.itlb_hi[e] &=
		    ~(SH4_PTEH_VPN_MASK | SH4_PTEH_ASID_MASK);
		cpu->cd.sh.itlb_hi[e] |= (idata &
		    (SH4_ITLB_AA_VPN_MASK | SH4_ITLB_AA_ASID_MASK));
		cpu->cd.sh.itlb_lo[e] &= ~SH4_PTEL_V;
		if (idata & SH4_ITLB_AA_V)
			cpu->cd.sh.itlb_lo[e] |= SH4_PTEL_V;
	} else {
		odata = cpu->cd.sh.itlb_hi[e] &
		    (SH4_ITLB_AA_VPN_MASK | SH4_ITLB_AA_ASID_MASK);
		if (cpu->cd.sh.itlb_lo[e] & SH4_PTEL_V)
			odata |= SH4_ITLB_AA_V;
		memory_writemax64(cpu, data, len, odata);
	}

	/*  TODO: Don't invalidate everything.  */
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);

	return 1;
}


DEVICE_ACCESS(sh4_itlb_da1)
{
	uint32_t mask = SH4_PTEL_SH | SH4_PTEL_C | SH4_PTEL_SZ_MASK |
	    SH4_PTEL_PR_MASK | SH4_PTEL_V | 0x1ffffc00;
	uint64_t idata = 0, odata = 0;
	int e = (relative_addr & SH4_ITLB_E_MASK) >> SH4_ITLB_E_SHIFT;

	if (relative_addr & 0x800000) {
		fatal("sh4_itlb_da1: TODO: da2 area\n");
		exit(1);
	}

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.itlb_lo[e] &= ~mask;
		cpu->cd.sh.itlb_lo[e] |= (idata & mask);
	} else {
		odata = cpu->cd.sh.itlb_lo[e] & mask;
		memory_writemax64(cpu, data, len, odata);
	}

	/*  TODO: Don't invalidate everything.  */
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);

	return 1;
}


DEVICE_ACCESS(sh4_utlb_aa)
{
	uint64_t idata = 0, odata = 0;
	int i, e = (relative_addr & SH4_UTLB_E_MASK) >> SH4_UTLB_E_SHIFT;
	int a = relative_addr & SH4_UTLB_E_MASK;

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		if (a) {
			for (i=0; i<SH_N_UTLB_ENTRIES; i++) {
				int sh = cpu->cd.sh.utlb_lo[i] & SH4_PTEL_SH;
				if ((cpu->cd.sh.utlb_hi[i] & SH4_PTEH_VPN_MASK)
				    != (idata & SH4_PTEH_VPN_MASK))
					continue;
				if (!sh && (cpu->cd.sh.utlb_lo[i] &
				    SH4_PTEH_ASID_MASK) != (idata &
				    SH4_PTEH_ASID_MASK))
					continue;
				cpu->cd.sh.utlb_lo[i] &=
				    ~(SH4_PTEL_D | SH4_PTEL_V);
				if (idata & SH4_UTLB_AA_D)
					cpu->cd.sh.utlb_lo[i] |= SH4_PTEL_D;
				if (idata & SH4_UTLB_AA_V)
					cpu->cd.sh.utlb_lo[i] |= SH4_PTEL_V;
			}
			/*  NOTE/TODO: Multiple hit exception is not
			    yet implemented.  */
		} else {
			cpu->cd.sh.utlb_hi[e] &=
			    ~(SH4_PTEH_VPN_MASK | SH4_PTEH_ASID_MASK);
			cpu->cd.sh.utlb_hi[e] |= (idata &
			    (SH4_UTLB_AA_VPN_MASK | SH4_UTLB_AA_ASID_MASK));
			cpu->cd.sh.utlb_lo[e] &= ~(SH4_PTEL_D | SH4_PTEL_V);
			if (idata & SH4_UTLB_AA_D)
				cpu->cd.sh.utlb_lo[e] |= SH4_PTEL_D;
			if (idata & SH4_UTLB_AA_V)
				cpu->cd.sh.utlb_lo[e] |= SH4_PTEL_V;
		}
	} else {
		odata = cpu->cd.sh.utlb_hi[e] &
		    (SH4_UTLB_AA_VPN_MASK | SH4_UTLB_AA_ASID_MASK);
		if (cpu->cd.sh.utlb_lo[e] & SH4_PTEL_D)
			odata |= SH4_UTLB_AA_D;
		if (cpu->cd.sh.utlb_lo[e] & SH4_PTEL_V)
			odata |= SH4_UTLB_AA_V;
		memory_writemax64(cpu, data, len, odata);
	}

	/*  TODO: Don't invalidate everything.  */
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);

	return 1;
}


DEVICE_ACCESS(sh4_utlb_da1)
{
	uint32_t mask = SH4_PTEL_WT | SH4_PTEL_SH | SH4_PTEL_D | SH4_PTEL_C
	    | SH4_PTEL_SZ_MASK | SH4_PTEL_PR_MASK | SH4_PTEL_V | 0x1ffffc00;
	uint64_t idata = 0, odata = 0;
	int e = (relative_addr & SH4_UTLB_E_MASK) >> SH4_UTLB_E_SHIFT;

	if (relative_addr & 0x800000) {
		fatal("sh4_utlb_da1: TODO: da2 area\n");
		exit(1);
	}

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
		cpu->cd.sh.utlb_lo[e] &= ~mask;
		cpu->cd.sh.utlb_lo[e] |= (idata & mask);
	} else {
		odata = cpu->cd.sh.utlb_lo[e] & mask;
		memory_writemax64(cpu, data, len, odata);
	}

	/*  TODO: Don't invalidate everything.  */
	cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);

	return 1;
}


DEVICE_ACCESS(sh4)
{
	struct sh4_data *d = (struct sh4_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	relative_addr += SH4_REG_BASE;

	switch (relative_addr) {

	/*************************************************/

	case SH4_PVR_ADDR:
		odata = cpu->cd.sh.cpu_type.pvr;
		break;

	case SH4_PRR_ADDR:
		odata = cpu->cd.sh.cpu_type.prr;
		break;

	case SH4_PTEH:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.pteh;
		else
			cpu->cd.sh.pteh = idata;
		break;

	case SH4_PTEL:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.ptel;
		else
			cpu->cd.sh.ptel = idata;
		break;

	case SH4_TTB:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.ttb;
		else
			cpu->cd.sh.ttb = idata;
		break;

	case SH4_TEA:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.tea;
		else
			cpu->cd.sh.tea = idata;
		break;

	case SH4_PTEA:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.ptea;
		else
			cpu->cd.sh.ptea = idata;
		break;

	case SH4_MMUCR:
		if (writeflag == MEM_READ) {
			odata = cpu->cd.sh.mmucr;
		} else {
			if (idata & SH4_MMUCR_TI) {
				/*  TLB invalidate.  */

				/*  TODO: Only invalidate something specific?
				    And not everything?  */
				cpu->invalidate_translation_caches(cpu,
				    0, INVALIDATE_ALL);

				/*  Should always read back as 0.  */
				idata &= ~SH4_MMUCR_TI;
			}

			cpu->cd.sh.mmucr = idata;
		}
		break;

	case SH4_CCR:
		if (writeflag == MEM_READ) {
			odata = cpu->cd.sh.ccr;
		} else {
			cpu->cd.sh.ccr = idata;
		}
		break;

	case SH4_TRA:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.tra;
		else
			cpu->cd.sh.tra = idata;
		break;

	case SH4_EXPEVT:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.expevt;
		else
			cpu->cd.sh.expevt = idata;
		break;

	case SH4_INTEVT:
		if (writeflag == MEM_READ)
			odata = cpu->cd.sh.intevt;
		else
			cpu->cd.sh.intevt = idata;
		break;


	/***********************************/
	/*  TMU: Timer Management Unit (?) */

	/*  TODO. ffd80000 ...  */


	/*************************************************/
	/*  SCIF: Serial Controller Interface with FIFO  */

	case SH4_SCIF_BASE + SCIF_FTDR:
		if (writeflag == MEM_WRITE)
			console_putchar(d->scif_console_handle, idata);
		break;

	case SH4_SCIF_BASE + SCIF_SSR:
		/*  TODO: Implement more of this.  */
		odata = SCSSR2_TDFE;
		break;


	/*************************************************/

	default:if (writeflag == MEM_READ) {
			fatal("[ sh4: read from addr 0x%x ]\n",
			    (int)relative_addr);
		} else {
			fatal("[ sh4: write to addr 0x%x: 0x%x ]\n",
			    (int)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(sh4)
{
	struct machine *machine = devinit->machine;
	struct sh4_data *d = malloc(sizeof(struct sh4_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sh4_data));

	d->scif_console_handle = console_start_slave(devinit->machine,
	    "SH4 SCIF", 1);

	memory_device_register(machine->memory, devinit->name,
	    SH4_REG_BASE, 0x01000000, dev_sh4_access, d, DM_DEFAULT, NULL);

	/*  On-chip RAM/cache:  */
	dev_ram_init(machine, 0x1e000000, 0x8000, DEV_RAM_RAM, 0x0);

	/*  0xe0000000: Store queue. TODO  */
	dev_ram_init(machine, 0xe0000000, 0x4000000, DEV_RAM_RAM, 0x0);

	/*
	 *  0xf0000000	SH4_CCIA	I-Cache address array
	 *  0xf1000000	SH4_CCID	I-Cache data array
	 *  0xf4000000	SH4_CCDA	D-Cache address array
	 *  0xf5000000	SH4_CCDD	D-Cache data array
	 *
	 *  TODO: Implement more correct cache behaviour?
	 */
	dev_ram_init(machine, SH4_CCIA, SH4_ICACHE_SIZE, DEV_RAM_RAM, 0x0);
	dev_ram_init(machine, SH4_CCID, SH4_ICACHE_SIZE, DEV_RAM_RAM, 0x0);
	dev_ram_init(machine, SH4_CCDA, SH4_DCACHE_SIZE, DEV_RAM_RAM, 0x0);
	dev_ram_init(machine, SH4_CCDD, SH4_DCACHE_SIZE, DEV_RAM_RAM, 0x0);

	/*  0xf2000000	SH4_ITLB_AA  */
	memory_device_register(machine->memory, devinit->name, SH4_ITLB_AA,
	    0x01000000, dev_sh4_itlb_aa_access, d, DM_DEFAULT, NULL);

	/*  0xf3000000	SH4_ITLB_DA1  */
	memory_device_register(machine->memory, devinit->name, SH4_ITLB_DA1,
	    0x01000000, dev_sh4_itlb_da1_access, d, DM_DEFAULT, NULL);

	/*  0xf6000000	SH4_UTLB_AA  */
	memory_device_register(machine->memory, devinit->name, SH4_UTLB_AA,
	    0x01000000, dev_sh4_utlb_aa_access, d, DM_DEFAULT, NULL);

	/*  0xf7000000	SH4_UTLB_DA1  */
	memory_device_register(machine->memory, devinit->name, SH4_UTLB_DA1,
	    0x01000000, dev_sh4_utlb_da1_access, d, DM_DEFAULT, NULL);

	return 1;
}

