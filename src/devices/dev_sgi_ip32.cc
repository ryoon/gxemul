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
 *  COMMENT: SGI IP32 stuff (CRIME, MACE, MACEPCI, ust)
 *
 *	o)  CRIME (interrupt controller)
 *	o)  MACE (Multimedia, Audio and Communications Engine)
 *	o)  MACE PCI bus
 *	o)  ust (unknown device)
 *
 *  TODO:
 *	o)  VICE (Video and Image Compression Engine)
 *		(perhaps best to place in the Graphics Back End?)
 *
 *  The GBE graphics (Graphics Back End) is in dev_sgi_gbe.cc.
 *
 *  Some info here: http://bukosek.si/hardware/collection/sgi-o2.html
 *  but mostly based on how NetBSD, OpenBSD, and Linux use the hardware.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "bus_pci.h"
#include "console.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"

#include "thirdparty/crimereg.h"
#include "thirdparty/sgi_macereg.h"

// #define debug fatal

// #define MACEPCI_DEBUG

#define	CRIME_TICKSHIFT			14


struct macepci_data {
	struct pci_data *pci_data;
};

#define	DEV_CRIME_LENGTH		0x280
struct crime_data {
	uint64_t		reg[DEV_CRIME_LENGTH / sizeof(uint64_t)];

	uint64_t		last_microseconds;

	struct interrupt	irq;
	int			prev_asserted;

	int			use_fb;
};


void mips_pc_to_pointers(struct cpu *);
void mips32_pc_to_pointers(struct cpu *);


void crime_interrupt_reassert(struct crime_data *d)
{
	uint64_t status =
		d->reg[CRIME_SOFTINT / sizeof(uint64_t)] |
		d->reg[CRIME_HARDINT / sizeof(uint64_t)];

	d->reg[CRIME_INTSTAT / sizeof(uint64_t)] = status;

	status &= d->reg[CRIME_INTMASK / sizeof(uint64_t)];

#if 0
	printf("CRIME SOFTINT=0x%08x HARDINT=0x%08x => 0x%08x, INTMASK=0x%08x\n",
		(uint32_t)d->reg[CRIME_SOFTINT / sizeof(uint64_t)],
		(uint32_t)d->reg[CRIME_HARDINT / sizeof(uint64_t)],
		(uint32_t)status,
		(uint32_t)d->reg[CRIME_INTMASK / sizeof(uint64_t)]);
#endif
	
	int asserted = !!status;

	if (asserted && !d->prev_asserted)
		INTERRUPT_ASSERT(d->irq);
	else if (!asserted && d->prev_asserted)
		INTERRUPT_DEASSERT(d->irq);
	
	d->prev_asserted = asserted;
}

/*
 *  crime_interrupt_assert():
 *  crime_interrupt_deassert():
 */
void crime_interrupt_assert(struct interrupt *interrupt)
{
	struct crime_data *d = (struct crime_data *) interrupt->extra;
	d->reg[CRIME_HARDINT / sizeof(uint64_t)] |= interrupt->line;
	//printf("CRIME asserting 0x%08x\n", interrupt->line);
	crime_interrupt_reassert(d);
}
void crime_interrupt_deassert(struct interrupt *interrupt)
{
	struct crime_data *d = (struct crime_data *) interrupt->extra;
	d->reg[CRIME_HARDINT / sizeof(uint64_t)] &= ~interrupt->line;
	//printf("CRIME deasserting 0x%08x\n", interrupt->line);
	crime_interrupt_reassert(d);
}


void crime_update_crime_time(struct crime_data* d)
{
	struct timeval tv;
	
	gettimeofday(&tv, NULL);

	uint64_t microseconds = tv.tv_sec * 1000000 + tv.tv_usec;
	if (d->last_microseconds == 0)
		d->last_microseconds = microseconds;

	int64_t delta = microseconds - d->last_microseconds;

	if (delta < 0) {
		fatal("[ crime_update_crime_time: host system time went backwards? ]\n");
		d->last_microseconds = microseconds;
		delta = 0;
	}

	// The delta to add is 66 per microsecond.
	int64_t to_add = delta * 66;

	if (to_add >= 1) {
		// NetBSD says CRIME_TIME_MASK = 0x0000ffffffffffffULL
		// but my O2 seems to use only the lower 32 bits as an
		// _unsigned_ value. (TODO: Double-check this again.)
		d->reg[CRIME_TIME / sizeof(uint64_t)] =
			(uint32_t)(d->reg[CRIME_TIME / sizeof(uint64_t)] + to_add);

		d->last_microseconds = microseconds;
	}
}


/*
 *  dev_crime_tick():
 *
 *  Updates CRIME_TIME (at 66 MHz) and reassert CRIME interrupts.
 */
DEVICE_TICK(crime)
{
	struct crime_data *d = (struct crime_data *) extra;

	crime_update_crime_time(d);
	crime_interrupt_reassert(d);
}


DEVICE_ACCESS(crime)
{
	/*
	 *  The CRIME is memory mapped as 0x80 bytes, starting at
	 *  physical 0x14000000.
	 *
	 *  On my real O2:
	 *
	 *  0x14000080..0x140000ff is the same as 0x14000000..0x1400007f.
	 *  0x14000100..0x1400017f is the same as 0x14000000..0x1400007f.
	 *  0x14000180..0x140001ff is the same as 0x14000000..0x1400007f.
	 *
	 *  (I assume that real code will never access the above "mirrors",
	 *  but it would be trivial to add redirection so that such code
	 *  would work too.)
	 *
	 *  Memory control then starts at 0x14000200.
	 */
	struct crime_data *d = (struct crime_data *) extra;
	uint64_t idata = 0, odata = 0;
	uint64_t preserved_CRIME_HARDINT = d->reg[CRIME_HARDINT / sizeof(uint64_t)];
	size_t i;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	/*
	 *  Set crime version/revision:
	 *
	 *  This might not be the most elegant or correct solution, but it
	 *  seems that the IP32 PROM likes 0x11 for machines without graphics,
	 *  and 0xa1 for machines with graphics.
	 *
	 *  NetBSD 2.0 complains about "unknown" crime for 0x11, but I guess
	 *  that's something one has to live with.  (TODO?)
	 */
	d->reg[CRIME_REV / sizeof(uint64_t)] = d->use_fb? 0xa1 : 0x11;

	d->reg[CRIME_CONTROL / sizeof(uint64_t)] = CRIME_CONTROL_ENDIANESS;

	d->reg[CRIME_INTSTAT / sizeof(uint64_t)] =
		(d->reg[CRIME_SOFTINT / sizeof(uint64_t)] |
		d->reg[CRIME_HARDINT / sizeof(uint64_t)]);

	/*
	 *  Amount of memory.  Bit 8 of bank control set ==> 128MB instead
	 *  of 32MB per bank (?)
	 *
	 *  When the bank control registers contain the same value as the
	 *  previous one, that bank is not valid. (?)
	 *
	 *  TODO: Make this work reliably with other sizes than 128 or 256 MB.
	 *  128 MB is what I have in my machine. Theoretically, up to 1 GB
	 *  could be supported in the O2, of which the first 256 MB is
	 *  accessible at low physical addresses (below 0x10000000).
	 */
	if (cpu->machine->physical_ram_in_mb >= 1024) {
		fatal("[ sgi_crime: SGI O2 can not have more than 1024 MB RAM ]\n");
		exit(1);
	}

	int mb_per_bank;
	if (cpu->machine->physical_ram_in_mb <= 256)
		mb_per_bank = 32;
	else
		mb_per_bank = 128;

	if (cpu->machine->physical_ram_in_mb % mb_per_bank) {
		fatal("[ sgi_crime: for up to 256 MB RAM, RAM size needs to be divisible "
			"by 32 MB. for larger RAM sizes (up to 1024 MB), it needs to be "
			"divisible by 128 MB. ]\n");
		exit(1);
	}

	int flag_for_128MB = mb_per_bank == 128 ? 0x100 : 0x000;
	int total_mb = 0;
	for (int bank = 0; bank < 8; ++bank) {
		int b = mb_per_bank == 128 ? (bank << 2) : bank;
		if (total_mb >= cpu->machine->physical_ram_in_mb)
			d->reg[CRIME_MEM_BANK_CTRL0 / sizeof(uint64_t) + bank] =
				d->reg[CRIME_MEM_BANK_CTRL0 / sizeof(uint64_t) + 0];
		else
			d->reg[CRIME_MEM_BANK_CTRL0 / sizeof(uint64_t) + bank] = flag_for_128MB | b;
		total_mb += mb_per_bank;
	}
	
	if (len == 8) {
		if (writeflag == MEM_WRITE)
			d->reg[relative_addr / 8] = idata;
		else
			odata = d->reg[relative_addr / 8];
	} else if (len == 4) {
		if (writeflag == MEM_WRITE) {
			if (relative_addr & 4) {
				d->reg[relative_addr / 8] &= ~0xffffffffULL;
				d->reg[relative_addr / 8] |= (uint32_t)idata;
			} else {
				d->reg[relative_addr / 8] &= 0xffffffffULL;
				d->reg[relative_addr / 8] |= (uint64_t)(idata << 32ULL);
			}
		} else {
			odata = d->reg[relative_addr / 8];
			if (relative_addr & 4)
				odata = (int32_t)odata;
			else
				odata = (int32_t)(odata >> 32);
		}
	} else {
		fatal("crime access len = %i!\n", len);
		exit(1);
	}

	switch (relative_addr) {

	case CRIME_REV:		/*  0x000  */
		/*
		 *  A contender for winning a prize for the worst hack
		 *  in history:  the IP32 PROM probes the CPU caches during
		 *  bootup, but they are not really emulated, so it fails.
		 *  During the probe, the CRIME_REV is read a lot. By
		 *  "returning" from the probe function, i.e. jumping to ra,
		 *  when this register is read the second time, the probe
		 *  can be skipped, and the PROM thus runs further.
		 *
		 *  The address where this happens is:
		 *	0xbfc0517c	PROM v2.3
		 *	0xbfc051ac	PROM v4.13
		 *
		 *  4.18 works too.
		 *
		 *  By extrapolating a bit (allowing for variations for other
		 *  versions of the PROM), let's return if the read of the
		 *  CRIME_REV register occurs anywhere near 0xbfc051XX.
		 */
		if (((uint32_t)cpu->pc & 0xffffff00) == (uint32_t)0xbfc05100) {
			cpu->pc = cpu->cd.mips.gpr[MIPS_GPR_RA];
			if (cpu->is_32bit)
				mips32_pc_to_pointers(cpu);
			else
				mips_pc_to_pointers(cpu);
		}
		break;

	case CRIME_CONTROL:	/*  0x008  */
		/*  TODO: 64-bit write to CRIME_CONTROL, but some things
		    (such as NetBSD 1.6.2) write to 0x00c!  */
		if (writeflag == MEM_WRITE) {
			/*
			 *  0x200 = watchdog timer (according to NetBSD)
			 *  0x800 = "reboot" used by the IP32 PROM
			 */
			if (idata & CRIME_CONTROL_DOG_ENABLE) {
				idata &= ~CRIME_CONTROL_DOG_ENABLE;
			}
			if (idata & CRIME_CONTROL_HARD_RESET) {
				int j;

				/*  This is used by the IP32 PROM's
				    "reboot" command:  */
				for (j=0; j<cpu->machine->ncpus; j++)
					cpu->machine->cpus[j]->running = 0;
				cpu->machine->
				    exit_without_entering_debugger = 1;
				idata &= ~CRIME_CONTROL_HARD_RESET;
			}

			idata &= ~CRIME_CONTROL_ENDIANESS;

			if (idata != 0)
				fatal("[ CRIME_CONTROL: unimplemented "
				    "control 0x%016llx ]\n", (long long)idata);
		}
		break;

	case CRIME_INTSTAT:	/*  0x010, Current interrupt status  */
	case CRIME_INTMASK:	/*  0x018, Current interrupt mask  */
		crime_interrupt_reassert(d);
		break;

	case CRIME_DOG:		/*  0x030  */
		// No warning.
		break;

	case CRIME_TIME:	/*  0x038  */
		crime_update_crime_time(d);
		break;

	default:
		if (writeflag==MEM_READ) {
			debug("[ crime: read from 0x%x, len=%i:",
			    (int)relative_addr, len);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
		} else {
			debug("[ crime: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		}
	}

	d->reg[CRIME_HARDINT / sizeof(uint64_t)] = preserved_CRIME_HARDINT;
	d->reg[CRIME_INTSTAT / sizeof(uint64_t)] =
		(d->reg[CRIME_SOFTINT / sizeof(uint64_t)] |
		d->reg[CRIME_HARDINT / sizeof(uint64_t)]);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_crime_init():
 */
void dev_crime_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, char *irq_path, int use_fb)
{
	struct crime_data *d;
	char tmpstr[200];
	int i;

	CHECK_ALLOCATION(d = (struct crime_data *) malloc(sizeof(struct crime_data)));
	memset(d, 0, sizeof(struct crime_data));

	d->use_fb = use_fb;

	INTERRUPT_CONNECT(irq_path, d->irq);

	/*  Register 32 crime interrupts (hexadecimal names):  */
	for (i=0; i<32; i++) {
		struct interrupt templ;
		char name[400];
		snprintf(name, sizeof(name), "%s.crime.0x%x", irq_path, 1 << i);
		memset(&templ, 0, sizeof(templ));
                templ.line = 1 << i;
		templ.name = name;
		templ.extra = d;
		templ.interrupt_assert = crime_interrupt_assert;
		templ.interrupt_deassert = crime_interrupt_deassert;
		interrupt_handler_register(&templ);
        }

	memory_device_register(mem, "crime", baseaddr, DEV_CRIME_LENGTH,
	    dev_crime_access, d, DM_DEFAULT, NULL);

	snprintf(tmpstr, sizeof(tmpstr), "mace addr=0x1f310000 irq=%s.crime",
	    irq_path);
	device_add(machine, tmpstr);

	machine_add_tickfunction(machine, dev_crime_tick, d,
	    CRIME_TICKSHIFT);
}


/****************************************************************************/


#define DEV_MACE_LENGTH		0x100
struct mace_data {
	unsigned char		reg[DEV_MACE_LENGTH];
	struct interrupt	irq_periph;
	struct interrupt	irq_misc;
	int			prev_assert_periph;
	int			prev_assert_misc;
};


void mace_interrupt_reassert(struct mace_data *d)
{
	uint8_t s4 = d->reg[MACE_ISA_INT_STATUS+4] & d->reg[MACE_ISA_INT_MASK+4];
	uint8_t s5 = d->reg[MACE_ISA_INT_STATUS+5] & d->reg[MACE_ISA_INT_MASK+5];
	uint8_t s6 = d->reg[MACE_ISA_INT_STATUS+6] & d->reg[MACE_ISA_INT_MASK+6];
	uint8_t s7 = d->reg[MACE_ISA_INT_STATUS+7] & d->reg[MACE_ISA_INT_MASK+7];

	int assert_periph = s4 | s5 ? 1 : 0;
	int assert_misc = s6 | s7 ? 1 : 0;

/*
printf("status=%02x%02x%02x%02x mask=%02x%02x%02x%02x => periph = %i misc = %i\n",
d->reg[MACE_ISA_INT_STATUS+4],
d->reg[MACE_ISA_INT_STATUS+5],
d->reg[MACE_ISA_INT_STATUS+6],
d->reg[MACE_ISA_INT_STATUS+7],
d->reg[MACE_ISA_INT_MASK+4],
d->reg[MACE_ISA_INT_MASK+5],
d->reg[MACE_ISA_INT_MASK+6],
d->reg[MACE_ISA_INT_MASK+7],
 assert_periph, assert_misc);
*/

	if (assert_periph != d->prev_assert_periph) {
		d->prev_assert_periph = assert_periph;
		if (assert_periph)
			INTERRUPT_ASSERT(d->irq_periph);
		else
			INTERRUPT_DEASSERT(d->irq_periph);
	}

	if (assert_misc != d->prev_assert_misc) {
		d->prev_assert_misc = assert_misc;
		if (assert_misc)
			INTERRUPT_ASSERT(d->irq_misc);
		else
			INTERRUPT_DEASSERT(d->irq_misc);
	}
}



/*
 *  mace_interrupt_assert():
 *  mace_interrupt_deassert():
 */
void mace_interrupt_assert(struct interrupt *interrupt)
{
	struct mace_data *d = (struct mace_data *) interrupt->extra;
	uint32_t line = 1 << interrupt->line;

	d->reg[MACE_ISA_INT_STATUS + 4] |= ((line >> 24) & 255);
	d->reg[MACE_ISA_INT_STATUS + 5] |= ((line >> 16) & 255);
	d->reg[MACE_ISA_INT_STATUS + 6] |= ((line >> 8) & 255);
	d->reg[MACE_ISA_INT_STATUS + 7] |= (line & 255);

	mace_interrupt_reassert(d);
}
void mace_interrupt_deassert(struct interrupt *interrupt)
{
	struct mace_data *d = (struct mace_data *) interrupt->extra;
	uint32_t line = 1 << interrupt->line;

	d->reg[MACE_ISA_INT_STATUS + 4] &= ~((line >> 24) & 255);
	d->reg[MACE_ISA_INT_STATUS + 5] &= ~((line >> 16) & 255);
	d->reg[MACE_ISA_INT_STATUS + 6] &= ~((line >> 8) & 255);
	d->reg[MACE_ISA_INT_STATUS + 7] &= ~(line & 255);

	mace_interrupt_reassert(d);
}


DEVICE_ACCESS(mace)
{
	size_t i;
	struct mace_data *d = (struct mace_data *) extra;

	/*
	 *  My O2 returns the following when dumping 0xbf310000 and forward:
	 *
	 *  +0x00: 0x0000000000000010 0x000000000000001e
	 *  +0x10: 0x0000000000000000 0x0000000000000000
	 *
	 *  and then the following addresses result in the same data as
	 *  0xbf310000.
	 */
	d->reg[MACE_ISA_RINGBASE + 7] = 0x10;
	d->reg[MACE_ISA_FLASH_NIC_REG + 7] |= MACE_ISA_PWD_CLEAR;

	uint8_t old_mace_isa_flash_nic_reg =
		d->reg[MACE_ISA_FLASH_NIC_REG + 7];

	if (writeflag == MEM_WRITE)
		memcpy(&d->reg[relative_addr], data, len);
	else
		memcpy(data, &d->reg[relative_addr], len);

	switch (relative_addr & ~7) {

	case MACE_ISA_RINGBASE:
		break;

	case MACE_ISA_FLASH_NIC_REG:
		// I think the PROM attempts to read the machine's ethernet
		// address from a DS2502 EPROM, by writing MACE_ISA_NIC_DEASSERT
		// to this register in various patterns, and looking at the
		// resulting MACE_ISA_NIC_DATA (or does it write data as well?)
		// In any case, onewire is too complicated to implement right
		// now. TODO.
		if (writeflag)
		{
			/*
			 *  The NetBSD defines are called "RED" and "GREEN",
			 *  and are documented like this:
			 *
			 *	1=> Illuminate RED LED
			 *
			 *  (and similarly for GREEN). But on my O2, it feels
			 *  like the other way around (i.e. 1 = do NOT illuminate
			 *  that LED color). Also, having it this way is more
			 *  compatible with how NetBSD, OpenBSD, and the PROM
			 *  actually use the LED.
			 */
			uint8_t change = (d->reg[MACE_ISA_FLASH_NIC_REG + 7]
				^ old_mace_isa_flash_nic_reg) &
				(MACE_ISA_LED_RED | MACE_ISA_LED_GREEN);
			if (change) {
				switch (d->reg[MACE_ISA_FLASH_NIC_REG + 7] & (MACE_ISA_LED_RED | MACE_ISA_LED_GREEN)) {
				case 0:	debug("[ mace: turning LED WHITE/ORANGE ]\n");
					break;
				case MACE_ISA_LED_RED:
					debug("[ mace: turning LED GREEN ]\n");
					break;
				case MACE_ISA_LED_GREEN:
					debug("[ mace: turning LED RED ]\n");
					break;
				default:fatal("[ mace: turning LED OFF ]\n");
					break;
				}
			}
			
//printf("%02x: MACE_ISA_NIC_DEASSERT = %i, DATA = %i\n",
	//d->reg[MACE_ISA_FLASH_NIC_REG + 7],
//d->reg[MACE_ISA_FLASH_NIC_REG + 7] & MACE_ISA_NIC_DEASSERT,
	//d->reg[MACE_ISA_FLASH_NIC_REG + 7] & MACE_ISA_NIC_DATA);
		} else {
//			data[len-1] ^= (random() & MACE_ISA_NIC_DATA);
//			data[len-1] |= MACE_ISA_NIC_DEASSERT;
		}
		break;

	case MACE_ISA_INT_STATUS:	/*  Current interrupt assertions  */
		/*  don't dump debug info for these  */
		if (writeflag == MEM_WRITE) {
			fatal("[ NOTE/TODO: WRITE to mace intr: "
			    "reladdr=0x%x data=", (int)relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" (len=%i) ]\n", len);
		}
		break;
	case MACE_ISA_INT_MASK:		/*  Current interrupt mask  */
		mace_interrupt_reassert(d);
		break;

	default:
		if (writeflag == MEM_READ) {
			debug("[ mace: read from 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		} else {
			debug("[ mace: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		}
	}

	return 1;
}


DEVINIT(mace)
{
	struct mace_data *d;
	char tmpstr[300];
	int i;

	CHECK_ALLOCATION(d = (struct mace_data *) malloc(sizeof(struct mace_data)));
	memset(d, 0, sizeof(struct mace_data));

	snprintf(tmpstr, sizeof(tmpstr), "%s.0x%x",
	    devinit->interrupt_path, CRIME_INT_PERIPH_SERIAL);
	INTERRUPT_CONNECT(tmpstr, d->irq_periph);

	snprintf(tmpstr, sizeof(tmpstr), "%s.0x%x",
	    devinit->interrupt_path, CRIME_INT_PERIPH_MISC);
	INTERRUPT_CONNECT(tmpstr, d->irq_misc);

	/*
	 *  For CRIME interrupts PERIPH_SERIAL and PERIPH_MISC,
	 *  register 32 mace interrupts each.
	 */
	for (i=0; i<32; i++) {
		struct interrupt templ;
		char name[400];
		snprintf(name, sizeof(name), "%s.0x%x.mace.%i",
		    devinit->interrupt_path, CRIME_INT_PERIPH_SERIAL, i);
		memset(&templ, 0, sizeof(templ));
                templ.line = i;
		templ.name = name;
		templ.extra = d;
		templ.interrupt_assert = mace_interrupt_assert;
		templ.interrupt_deassert = mace_interrupt_deassert;
		interrupt_handler_register(&templ);

		snprintf(name, sizeof(name), "%s.0x%x.mace.%i",
		    devinit->interrupt_path, CRIME_INT_PERIPH_MISC, i);
		memset(&templ, 0, sizeof(templ));
                templ.line = i;
		templ.name = name;
		templ.extra = d;
		templ.interrupt_assert = mace_interrupt_assert;
		templ.interrupt_deassert = mace_interrupt_deassert;
		interrupt_handler_register(&templ);
        }

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_MACE_LENGTH, dev_mace_access, d,
	    DM_DEFAULT, NULL);

	devinit->return_ptr = d;
	return 1;
}


/****************************************************************************/


DEVICE_ACCESS(macepci)
{
	struct macepci_data *d = (struct macepci_data *) extra;
	uint64_t idata = 0, odata=0;
	int res = 1, bus, dev, func, pcireg;

	if (len != 4)
		fatal("[ macepci: unimplemented len %i ]\n", len);

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);
#ifdef MACEPCI_DEBUG
		fatal("[ macepci: write to address 0x%x, data=0x%02x (len %i) ]\n",
		    (int)relative_addr, (int)idata, len);
#endif
	}

	/*  Read from/write to the macepci:  */
	switch (relative_addr) {

	case 0x00:	/*  Error address  */
		if (writeflag == MEM_WRITE) {
		} else {
			/*  My real O2 returns 0x4000.  */
			odata = 0x4000;
		}
		break;

	case 0x04:	/*  Error flags  */
		if (writeflag == MEM_WRITE) {
		} else {
			/*  My real O2 returns 0x00100006.  */
			odata = 0x00100006;
		}
		break;

	case 0x08:	/*  TODO: Unknown?  */
		if (writeflag == MEM_WRITE) {
		} else {
			/*  My real O2 returns 0xff000500.  */
			odata = 0xff000500;
		}
		break;

	case 0x0c:	/*  Revision number  */
		if (writeflag == MEM_WRITE) {
		} else {
			/*  My real O2 returns 0x00000001.  */
			odata = 0x00000001;
		}
		break;

	case 0xcf8:	/*  PCI ADDR  */
		bus_pci_decompose_1(idata, &bus, &dev, &func, &pcireg);
		bus_pci_setaddr(cpu, d->pci_data, bus, dev, func, pcireg);
		break;

	case 0xcfc:	/*  PCI DATA  */
		bus_pci_data_access(cpu, d->pci_data, writeflag == MEM_READ?
		    &odata : &idata, len, writeflag);
		break;

	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ macepci: unimplemented write to address "
			    "0x%x, data=0x%02x ]\n",
			    (int)relative_addr, (int)idata);
		} else {
			fatal("[ macepci: unimplemented read from address "
			    "0x%x ]\n", (int)relative_addr);

			/*  My real O2 returns 0xffffffff for all unimplemented
				registers.  */
			odata = 0xffffffff;
		}
	}

	if (writeflag == MEM_READ) {
		memory_writemax64(cpu, data, len, odata);
#ifdef MACEPCI_DEBUG
		fatal("[ macepci: read from address 0x%x, data=0x%02x (len %i) ]\n",
		    (int)relative_addr, (int)odata, len);
#endif
	}

	return res;
}


/*
 *  dev_macepci_init():
 */
struct pci_data *dev_macepci_init(struct machine *machine,
	struct memory *mem, uint64_t baseaddr, char *irq_path)
{
	struct macepci_data *d;

	CHECK_ALLOCATION(d = (struct macepci_data *) malloc(sizeof(struct macepci_data)));
	memset(d, 0, sizeof(struct macepci_data));

	/*  TODO: PCI vs ISA interrupt?  */

	d->pci_data = bus_pci_init(machine,
	    irq_path,
	    0,
	    0,
	    0,
	    0,
	    "TODO: pci irq path",
	    0x18000003,		/*  ISA portbase  */
	    0,
	    irq_path);

	memory_device_register(mem, "macepci", baseaddr, DEV_MACEPCI_LENGTH,
	    dev_macepci_access, (void *)d, DM_DEFAULT, NULL);

	return d->pci_data;
}


/****************************************************************************/


struct sgi_ust_data {
	uint64_t	reg[DEV_SGI_UST_LENGTH / sizeof(uint64_t)];
};


DEVICE_ACCESS(sgi_ust)
{
	struct sgi_ust_data *d = (struct sgi_ust_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint64_t);

	/*  Treat all registers as read/write, by default.  */
	if (writeflag == MEM_WRITE)
		d->reg[regnr] = idata;
	else
		odata = d->reg[regnr];

	switch (relative_addr) {
	case 0:
		d->reg[regnr] += 0x2710;	// HUH?
		break;
	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_ust: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			debug("[ sgi_ust: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_ust_init():
 */
void dev_sgi_ust_init(struct memory *mem, uint64_t baseaddr)
{
	struct sgi_ust_data *d;

	CHECK_ALLOCATION(d = (struct sgi_ust_data *) malloc(sizeof(struct sgi_ust_data)));
	memset(d, 0, sizeof(struct sgi_ust_data));

	memory_device_register(mem, "sgi_ust", baseaddr,
	    DEV_SGI_UST_LENGTH, dev_sgi_ust_access, (void *)d,
	    DM_DEFAULT, NULL);
}


