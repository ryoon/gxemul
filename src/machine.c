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
 *  $Id: machine.c,v 1.207 2004-10-24 10:46:23 debug Exp $
 *
 *  Emulation of specific machines.
 *
 *  This module is quite large. Hopefully it is still clear enough to be
 *  easily understood. The main parts are:
 *
 *	Helper functions.
 *
 *	Machine specific Interrupt routines.
 *
 *	Machine specific Initialization routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#ifdef SOLARIS
#include <strings.h>
#else
#include <string.h>
#endif
#include <time.h>
#include <unistd.h>

#include "misc.h"

#include "bus_pci.h"
#include "devices.h"
#include "diskimage.h"
#include "memory.h"
#include "symbol.h"

/*  For SGI emulation:  */
#include "crimereg.h"

/*  For ARC emulation:  */
#define	ARC_CONSOLE_MAX_X	80
#define	ARC_CONSOLE_MAX_Y	30

/*  For DECstation emulation:  */
#include "dec_5100.h"
#include "dec_kn01.h"
#include "dec_kn02.h"
#include "dec_kn03.h"
#include "dec_kmin.h"
#include "dec_maxine.h"


uint64_t file_loaded_end_addr = 0;

extern struct memory *GLOBAL_gif_mem;

struct kn230_csr *kn230_csr;
struct kn02_csr *kn02_csr;
struct dec_ioasic_data *dec_ioasic_data;
struct ps2_data *ps2_data;
struct dec5800_data *dec5800_csr;
struct au1x00_ic_data *au1x00_ic_data;
struct pica_data *pica_data;
struct crime_data *crime_data;
struct mace_data *mace_data;
struct sgi_ip20_data *sgi_ip20_data;
struct sgi_ip22_data *sgi_ip22_data;


/****************************************************************************
 *                                                                          *
 *                              Helper functions                            *
 *                                                                          *
 ****************************************************************************/


int int_to_bcd(int i)
{
	return (i/10) * 16 + (i % 10);
}


/*
 *  read_char_from_memory():
 *
 *  Reads a byte from emulated RAM. (Helper function.)
 */
unsigned char read_char_from_memory(struct cpu *cpu, int regbase, int offset)
{
	unsigned char ch;
	memory_rw(cpu, cpu->mem, cpu->gpr[regbase] + offset, &ch, sizeof(ch),
	    MEM_READ, CACHE_DATA | NO_EXCEPTIONS);
	return ch;
}


/*
 *  dump_mem_string():
 *
 *  Dump the contents of emulated RAM as readable text.  Bytes that aren't
 *  readable are dumped in [xx] notation, where xx is in hexadecimal.
 *  Dumping ends after DUMP_MEM_STRING_MAX bytes, or when a terminating
 *  zero byte is found.
 */
#define DUMP_MEM_STRING_MAX	45
void dump_mem_string(struct cpu *cpu, uint64_t addr)
{
	int i;
	for (i=0; i<DUMP_MEM_STRING_MAX; i++) {
		unsigned char ch = '\0';

		memory_rw(cpu, cpu->mem, addr + i, &ch, sizeof(ch),
		    MEM_READ, CACHE_DATA | NO_EXCEPTIONS);
		if (ch == '\0')
			return;
		if (ch >= ' ' && ch < 126)
			debug("%c", ch);  
		else
			debug("[%02x]", ch);
	}
}


/*
 *  store_byte():
 *
 *  Stores a byte in emulated ram. (Helper function.)
 */
void store_byte(struct cpu *cpu, uint64_t addr, uint8_t data)
{
	memory_rw(cpu, cpu->mem,
	    addr, &data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_string():
 *
 *  Stores chars into emulated RAM until a zero byte (string terminating
 *  character) is found. The zero byte is also copied.
 *  (strcpy()-like helper function, host-RAM-to-emulated-RAM.)
 */
void store_string(struct cpu *cpu, uint64_t addr, char *s)
{
	do {
		store_byte(cpu, addr++, *s);
	} while (*s++);
}


/*
 *  add_environment_string():
 *
 *  Like store_string(), but advances the pointer afterwards. The most
 *  obvious use is to place a number of strings (such as environment variable
 *  strings) after one-another in emulated memory.
 */
void add_environment_string(struct cpu *cpu, char *s, uint64_t *addr)
{
	store_string(cpu, *addr, s);
	(*addr) += strlen(s) + 1;
}


/*
 *  store_buf():
 *
 *  memcpy()-like helper function, from host RAM to emulated RAM.
 */
void store_buf(struct cpu *cpu, uint64_t addr, char *s, size_t len)
{
	while (len-- != 0)
		store_byte(cpu, addr++, *s++);
}


/*
 *  store_64bit_word():
 *
 *  Stores a 64-bit word in emulated RAM.  Byte order is taken into account.
 *  Helper function.
 */
void store_64bit_word(struct cpu *cpu, uint64_t addr, uint64_t data64)
{
	unsigned char data[8];
	data[0] = (data64 >> 56) & 255;
	data[1] = (data64 >> 48) & 255;
	data[2] = (data64 >> 40) & 255;
	data[3] = (data64 >> 32) & 255;
	data[4] = (data64 >> 24) & 255;
	data[5] = (data64 >> 16) & 255;
	data[6] = (data64 >> 8) & 255;
	data[7] = (data64) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[7]; data[7] = tmp;
		tmp = data[1]; data[1] = data[6]; data[6] = tmp;
		tmp = data[2]; data[2] = data[5]; data[5] = tmp;
		tmp = data[3]; data[3] = data[4]; data[4] = tmp;
	}
	memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_32bit_word():
 *
 *  Stores a 32-bit word in emulated RAM.  Byte order is taken into account.
 *  (This function takes a 64-bit word as argument, to suppress some
 *  warnings, but only the lowest 32 bits are used.)
 */
void store_32bit_word(struct cpu *cpu, uint64_t addr, uint64_t data32)
{
	unsigned char data[4];
	data[0] = (data32 >> 24) & 255;
	data[1] = (data32 >> 16) & 255;
	data[2] = (data32 >> 8) & 255;
	data[3] = (data32) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}
	memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_pointer_and_advance():
 *
 *  Stores a 32-bit or 64-bit pointer in emulated RAM, and advances the
 *  target address. (Used by ARC and SGI initialization.)
 */
void store_pointer_and_advance(struct cpu *cpu, uint64_t *addrp,
	uint64_t data, int flag64)
{
	uint64_t addr = *addrp;
	if (flag64) {
		store_64bit_word(cpu, addr, data);
		addr += 8;
	} else {
		store_32bit_word(cpu, addr, data);
		addr += 4;
	}
	*addrp = addr;
}


/*
 *  load_32bit_word():
 *
 *  Helper function.  Prints a warning and returns 0, if the read failed.
 *  Emulated byte order is taken into account.
 */
uint32_t load_32bit_word(struct cpu *cpu, uint64_t addr)
{
	unsigned char data[4];

	memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_READ, CACHE_DATA);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}

	return (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + data[3];
}


/*
 *  store_64bit_word_in_host():
 *
 *  Stores a 64-bit word in the _host's_ RAM.  Emulated byte order is taken
 *  into account.  This is useful when building structs in the host's RAM
 *  which will later be copied into emulated RAM.
 */
void store_64bit_word_in_host(struct cpu *cpu,
	unsigned char *data, uint64_t data64)
{
	data[0] = (data64 >> 56) & 255;
	data[1] = (data64 >> 48) & 255;
	data[2] = (data64 >> 40) & 255;
	data[3] = (data64 >> 32) & 255;
	data[4] = (data64 >> 24) & 255;
	data[5] = (data64 >> 16) & 255;
	data[6] = (data64 >> 8) & 255;
	data[7] = (data64) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[7]; data[7] = tmp;
		tmp = data[1]; data[1] = data[6]; data[6] = tmp;
		tmp = data[2]; data[2] = data[5]; data[5] = tmp;
		tmp = data[3]; data[3] = data[4]; data[4] = tmp;
	}
}


/*
 *  store_32bit_word_in_host():
 *
 *  See comment for store_64bit_word_in_host().
 *
 *  (Note:  The data32 parameter is a uint64_t. This is done to suppress
 *  some warnings.)
 */
void store_32bit_word_in_host(struct cpu *cpu,
	unsigned char *data, uint64_t data32)
{
	data[0] = (data32 >> 24) & 255;
	data[1] = (data32 >> 16) & 255;
	data[2] = (data32 >> 8) & 255;
	data[3] = (data32) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}
}


/*
 *  store_16bit_word_in_host():
 *
 *  See comment for store_64bit_word_in_host().
 */
void store_16bit_word_in_host(struct cpu *cpu,
	unsigned char *data, uint16_t data16)
{
	data[0] = (data16 >> 8) & 255;
	data[1] = (data16) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[1]; data[1] = tmp;
	}
}


/****************************************************************************
 *                                                                          *
 *                    Machine dependant Interrupt routines                  *
 *                                                                          *
 ****************************************************************************/


/*
 *  DECstation KN02 interrupts:
 */
void kn02_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	int current;

	irq_nr -= 8;
	irq_nr &= 0xff;

	if (assrt) {
		/*  OR in the irq_nr into the CSR:  */
		kn02_csr->csr |= irq_nr;
	} else {
		/*  AND out the irq_nr from the CSR:  */
		kn02_csr->csr = (kn02_csr->csr & 0xffffff00ULL)
		    | ((kn02_csr->csr & 0xff) & ~irq_nr);
	}

	current = (kn02_csr->csr & KN02_CSR_IOINT) &
	    ((kn02_csr->csr & KN02_CSR_IOINTEN) >> KN02_CSR_IOINTEN_SHIFT);

	if (current == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);
}


/*
 *  DECstation KMIN interrupts:
 */
void kmin_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	/*  debug("kmin_interrupt(): irq_nr=%i assrt=%i\n", irq_nr, assrt);  */

	if (assrt) {
		/*  OR into the INTR:  */
		dec_ioasic_data->intr |= irq_nr;

		/*  Assert MIPS interrupt 5 (TC slot 3 = system slot):  */
		cpu_interrupt(cpu, KMIN_INT_TC3);
	} else {
		/*  AND out of the INTR:  */
		dec_ioasic_data->intr &= ~irq_nr;

		if (dec_ioasic_data->intr == 0)
			cpu_interrupt_ack(cpu, KMIN_INT_TC3);
	}
}


/*
 *  DECstation KN03 interrupts:
 */
void kn03_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;

	/*  debug("kn03_interrupt(): irq_nr=0x%x assrt=%i\n", irq_nr, assrt);  */

	if (assrt) {
		/*  OR into the INTR:  */
		dec_ioasic_data->intr |= irq_nr;

		/*  Assert MIPS interrupt 2 (ioasic):  */
		cpu_interrupt(cpu, KN03_INT_ASIC);
	} else {
		/*  AND out of the INTR:  */
		dec_ioasic_data->intr &= ~irq_nr;

		if (dec_ioasic_data->intr == 0)
			cpu_interrupt_ack(cpu, KN03_INT_ASIC);
	}
}


/*
 *  DECstation MAXINE interrupts:
 */
void maxine_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	debug("maxine_interrupt(): irq_nr=0x%x assrt=%i\n", irq_nr, assrt);

	if (assrt) {
		/*  OR into the INTR:  */
		dec_ioasic_data->intr |= irq_nr;

		/*  Assert MIPS interrupt 5 (turbochannel/ioasic):  */
		cpu_interrupt(cpu, XINE_INT_TC3);
	} else {
		/*  AND out of the INTR:  */
		dec_ioasic_data->intr &= ~irq_nr;

		if (dec_ioasic_data->intr == 0)
			cpu_interrupt_ack(cpu, XINE_INT_TC3);
	}
}


/*
 *  DECstation KN230 interrupts:
 */
void kn230_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	int r2 = 0;

	kn230_csr->csr |= irq_nr;

	switch (irq_nr) {
	case KN230_CSR_INTR_SII:
	case KN230_CSR_INTR_LANCE:
		r2 = 3;
		break;
	case KN230_CSR_INTR_DZ0:
	case KN230_CSR_INTR_OPT0:
	case KN230_CSR_INTR_OPT1:
		r2 = 2;
		break;
	default:
		fatal("kn230_interrupt(): irq_nr = %i ?\n", irq_nr);
	}

	if (assrt) {
		/*  OR in the irq_nr mask into the CSR:  */
		kn230_csr->csr |= irq_nr;

		/*  Assert MIPS interrupt 2 or 3:  */
		cpu_interrupt(cpu, r2);
	} else {
		/*  AND out the irq_nr mask from the CSR:  */
		kn230_csr->csr &= ~irq_nr;

		/*  If the CSR interrupt bits are all zero, clear the bit in the cause register as well.  */
		if (r2 == 2) {
			/*  irq 2:  */
			if ((kn230_csr->csr & (KN230_CSR_INTR_DZ0 | KN230_CSR_INTR_OPT0 | KN230_CSR_INTR_OPT1)) == 0)
				cpu_interrupt_ack(cpu, r2);
		} else {
			/*  irq 3:  */
			if ((kn230_csr->csr & (KN230_CSR_INTR_SII | KN230_CSR_INTR_LANCE)) == 0)
				cpu_interrupt_ack(cpu, r2);
		}
	}
}


/*
 *  Acer PICA-61 interrupts:
 */
void pica_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	uint32_t irq;
	irq_nr -= 8;

	/*  debug("pica_interrupt() irq_nr = %i, assrt = %i\n",
		irq_nr, assrt);  */

	irq = 1 << irq_nr;

	if (assrt)
		pica_data->int_asserted |= irq;
	else
		pica_data->int_asserted &= ~irq;

	/*  debug("   %08x %08x\n", pica_data->int_asserted,
		pica_data->int_enable_mask);  */

	/*  TODO: this "15" (0x8000) is the timer... fix this?  */

	if (pica_data->int_asserted /* & pica_data->int_enable_mask */
	    & ~0x8000 )
		cpu_interrupt(cpu, 3);
	else
		cpu_interrupt_ack(cpu, 3);

	if (pica_data->int_asserted & 0x8000)
		cpu_interrupt(cpu, 6);
	else
		cpu_interrupt_ack(cpu, 6);
}


/*
 *  Playstation 2 interrupt routine:
 */
void ps2_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	debug("ps2_interrupt(): irq_nr=0x%x assrt=%i\n", irq_nr, assrt);

	if (assrt) {
		/*  OR into the INTR:  */
		if (irq_nr < 0x10000)
			ps2_data->intr |= irq_nr;
		else
			ps2_data->dmac_reg[0x601] |= (irq_nr >> 16);

		/*  Assert interrupt:   TODO: masks  */
		if (irq_nr >= 0x10000)
			cpu_interrupt(cpu, 3);
		else
			cpu_interrupt(cpu, 2);
	} else {
		/*  AND out of the INTR:  */
		if (irq_nr < 0x10000)
			ps2_data->intr &= ~irq_nr;
		else
			ps2_data->dmac_reg[0x601] &= ~(irq_nr >> 16);

		/*  TODO: masks  */
		if ((ps2_data->intr & 0xffff) == 0)
			cpu_interrupt_ack(cpu, 2);
		if ((ps2_data->dmac_reg[0x601] & 0xffff) == 0)
			cpu_interrupt_ack(cpu, 3);
	}
}


/*
 *  SGI "IP22" interrupt routine:
 */
void sgi_ip22_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	/*
	 *  SGI-IP22 specific interrupt stuff:
	 *
	 *  irq_nr should be 8 + x, where x = 0..31 for local0,
	 *  and 32..63 for local1 interrupts.
	 *  Add 64*y for "mappable" interrupts, where 1<<y is
	 *  the mappable interrupt bitmask. TODO: this misses 64*0 !
	 */

	uint32_t newmask;
	uint32_t stat, mask;

	irq_nr -= 8;
	newmask = 1 << (irq_nr & 31);

	if (irq_nr >= 64) {
		int m = irq_nr / 64;
		uint32_t new = 1 << m;
		if (assrt)
			sgi_ip22_data->reg[4] |= new;
		else
			sgi_ip22_data->reg[4] &= ~new;
		/*  TODO: is this enough?  */
		irq_nr &= 63;
	}

	if (irq_nr < 32) {
		if (assrt)
			sgi_ip22_data->reg[0] |= newmask;
		else
			sgi_ip22_data->reg[0] &= ~newmask;
	} else {
		if (assrt)
			sgi_ip22_data->reg[2] |= newmask;
		else
			sgi_ip22_data->reg[2] &= ~newmask;
	}

	/*  Read stat and mask for local0:  */
	stat = sgi_ip22_data->reg[0];
	mask = sgi_ip22_data->reg[1];
	if ((stat & mask) == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);

	/*  Read stat and mask for local1:  */
	stat = sgi_ip22_data->reg[2];
	mask = sgi_ip22_data->reg[3];
	if ((stat & mask) == 0)
		cpu_interrupt_ack(cpu, 3);
	else
		cpu_interrupt(cpu, 3);
}


/*
 *  SGI "IP32" interrupt routine:
 */
void sgi_ip32_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	/*
	 *  The 64-bit word at crime offset 0x10 is CRIME_INTSTAT,
	 *  which contains the current interrupt bits. CRIME_INTMASK
	 *  contains a mask of which bits are actually in use.
	 *
	 *  crime hardcoded at 0x14000000, for SGI-IP32.
	 *  If any of these bits are asserted, then physical MIPS
	 *  interrupt 2 should be asserted.
	 *
	 *  TODO:  how should all this be done nicely?
	 *
	 *  TODO:  mace interrupt mask
	 */

	uint64_t crime_addr = CRIME_INTSTAT;
	uint64_t mace_addr = 0x14;
	uint64_t crime_interrupts, crime_interrupts_mask, mace_interrupts;
	unsigned int i;
	unsigned char x[8];

	/*
	 *  This mapping of both MACE and CRIME interrupts into the same
	 *  'int' is really ugly.
	 *
	 *  If MACE_PERIPH_MISC or MACE_PERIPH_SERIAL is set, then mask
	 *  that bit out and treat the rest of the word as the mace interrupt
	 *  bitmask.
	 *
	 *  TODO: fix.
	 */
	if (irq_nr & MACE_PERIPH_SERIAL) {
		/*  Read current MACE interrupt bits:  */
		memcpy(x, mace_data->reg + mace_addr, sizeof(uint32_t));
		mace_interrupts = 0;
		for (i=0; i<sizeof(uint32_t); i++) {
			/*  SGI is big-endian...  */
			mace_interrupts <<= 8;
			mace_interrupts |= x[i];
		}

		if (assrt)
			mace_interrupts |= (irq_nr & ~MACE_PERIPH_SERIAL);
		else
			mace_interrupts &= ~(irq_nr & ~MACE_PERIPH_SERIAL);

		/*  Write back MACE interrupt bits:  */
		for (i=0; i<4; i++)
			x[3-i] = mace_interrupts >> (i*8);
		memcpy(mace_data->reg + mace_addr, x, sizeof(uint32_t));

		irq_nr = MACE_PERIPH_SERIAL;
		if (mace_interrupts == 0)
			assrt = 0;
		else
			assrt = 1;
	}

	/*  Hopefully _MISC and _SERIAL will not be both on at the same time.  */
	if (irq_nr & MACE_PERIPH_MISC) {
		/*  Read current MACE interrupt bits:  */
		memcpy(x, mace_data->reg + mace_addr, sizeof(uint32_t));
		mace_interrupts = 0;
		for (i=0; i<sizeof(uint32_t); i++) {
			/*  SGI is big-endian...  */
			mace_interrupts <<= 8;
			mace_interrupts |= x[i];
		}

		if (assrt)
			mace_interrupts |= (irq_nr & ~MACE_PERIPH_MISC);
		else
			mace_interrupts &= ~(irq_nr & ~MACE_PERIPH_MISC);

		/*  Write back MACE interrupt bits:  */
		for (i=0; i<4; i++)
			x[3-i] = mace_interrupts >> (i*8);
		memcpy(mace_data->reg + mace_addr, x, sizeof(uint32_t));

		irq_nr = MACE_PERIPH_MISC;
		if (mace_interrupts == 0)
			assrt = 0;
		else
			assrt = 1;
	}

	/*  Read CRIME_INTSTAT:  */
	memcpy(x, crime_data->reg + crime_addr, sizeof(uint64_t));
	crime_interrupts = 0;
	for (i=0; i<8; i++) {
		/*  SGI is big-endian...  */
		crime_interrupts <<= 8;
		crime_interrupts |= x[i];
	}

	if (assrt)
		crime_interrupts |= irq_nr;
	else
		crime_interrupts &= ~irq_nr;

	/*  Write back CRIME_INTSTAT:  */
	for (i=0; i<8; i++)
		x[7-i] = crime_interrupts >> (i*8);
	memcpy(crime_data->reg + crime_addr, x, sizeof(uint64_t));

	/*  Read CRIME_INTMASK:  */
	memcpy(x, crime_data->reg + CRIME_INTMASK, sizeof(uint64_t));
	crime_interrupts_mask = 0;
	for (i=0; i<8; i++) {
		crime_interrupts_mask <<= 8;
		crime_interrupts_mask |= x[i];
	}

	if ((crime_interrupts & crime_interrupts_mask) == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);

	/*  printf("sgi_crime_machine_irq(%i,%i): new interrupts = 0x%08x\n", assrt, irq_nr, crime_interrupts);  */
}


/*
 *  Au1x00 interrupt routine:
 *
 *  TODO: This is just bogus so far.  For more info, read this:
 *  http://www.meshcube.org/cgi-bin/viewcvs.cgi/kernel/linux/arch/mips/au1000/common/
 *
 *  CPU int 2 = IC 0, request 0
 *  CPU int 3 = IC 0, request 1
 *  CPU int 4 = IC 1, request 0
 *  CPU int 5 = IC 1, request 1
 *
 *  Interrupts 0..31 are on interrupt controller 0, interrupts 32..63 are
 *  on controller 1.
 *
 *  Special case: if irq_nr == 64+8, then this just updates the CPU
 *  interrupt assertions.
 */
void au1x00_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	uint32_t m;

	irq_nr -= 8;
	debug("au1x00_interrupt(): irq_nr=%i assrt=%i\n", irq_nr, assrt);

	if (irq_nr < 64) {
		m = 1 << (irq_nr & 31);

		if (assrt)
			au1x00_ic_data->request0_int |= m;
		else
			au1x00_ic_data->request0_int &= ~m;

		/*  TODO: Controller 1  */
	}

	if ((au1x00_ic_data->request0_int &
	    au1x00_ic_data->mask) != 0)
		cpu_interrupt(cpu, 2);
	else
		cpu_interrupt_ack(cpu, 2);

	/*  TODO: What _is_ request1?  */

	/*  TODO: Controller 1  */
}


/****************************************************************************
 *                                                                          *
 *                  Machine dependant Initialization routines               *
 *                                                                          *
 ****************************************************************************/


/*
 *  machine_init():
 *
 *  This (rather large) function initializes memory, registers, and/or
 *  devices required by specific machine emulations.
 */
void machine_init(struct emul *emul, struct memory *mem)
{
	uint64_t addr, addr2;
	int i;

	/*  DECstation:  */
	char *framebuffer_console_name, *serial_console_name;
	int color_fb_flag;
	int boot_scsi_boardnumber = 3, boot_net_boardnumber = 3;
	char *turbochannel_default_gfx_card = "PMAG-BA";
		/*  PMAG-AA, -BA, -CA/DA/EA/FA, -JA, -RO, PMAGB-BA  */

	/*  HPCmips:  */
	struct xx {
		struct btinfo_magic a;
		struct btinfo_bootpath b;
		struct btinfo_symtab c;
	} xx;
	struct hpc_bootinfo hpc_bootinfo;

	/*  ARCBIOS stuff:  */
	struct arcbios_spb arcbios_spb;
	struct arcbios_spb_64 arcbios_spb_64;
	struct arcbios_sysid arcbios_sysid;
	struct arcbios_dsp_stat arcbios_dsp_stat;
	struct arcbios_mem arcbios_mem;
	struct arcbios_mem64 arcbios_mem64;
	uint64_t mem_base, mem_count, mem_bufaddr;
	int mem_mb_left;
	uint32_t system = 0;
	uint64_t sgi_ram_offset = 0;
	int arc_wordlen = sizeof(uint32_t);
	char *short_machine_name = NULL;

	/*  Generic bootstring stuff:  */
	int bootdev_id = diskimage_bootdev();
	char *bootstr = NULL;
	char *bootarg = NULL;
	char *init_bootpath;

	/*  PCI stuff:  */
	struct pci_data *pci_data;

	/*  Framebuffer stuff:  */
	struct vfb_data *fb;

	/*  Abreviation:  :-)  */
	struct cpu *cpu = emul->cpus[emul->bootstrap_cpu];


	emul->machine_name = NULL;

	switch (emul->emulation_type) {

	case EMULTYPE_NONE:
		break;

	case EMULTYPE_TEST:
		/*
		 *  A "bare" test machine.
		 */
		emul->machine_name = "\"Bare\" test machine";

		dev_cons_init(mem);		/*  TODO: include address here?  */
		dev_mp_init(mem, emul->cpus);
		fb = dev_fb_init(cpu, mem, 0x12000000, VFB_GENERIC,
		    640,480, 640,480, 24, "generic");

		break;

	case EMULTYPE_DEC:
		/*  An R2020 or R3220 memory thingy:  */
		cpu->coproc[3] = coproc_new(cpu, 3);

		/*  There aren't really any good standard values...  */
		framebuffer_console_name = "osconsole=0,3";
		serial_console_name      = "osconsole=1";

		switch (emul->machine) {
		case MACHINE_PMAX_3100:		/*  type  1, KN01  */
			/*  Supposed to have 12MHz or 16.67MHz R2000 CPU, R2010 FPC, R2020 Memory coprocessor  */
			emul->machine_name = "DEC PMAX 3100 (KN01)";

			/*  12 MHz for 2100, 16.67 MHz for 3100  */
			if (emul->emulated_hz == 0)
				emul->emulated_hz = 16670000;

			if (emul->physical_ram_in_mb > 24)
				fprintf(stderr, "WARNING! Real DECstation 3100 machines cannot have more than 24MB RAM. Continuing anyway.\n");

			if ((emul->physical_ram_in_mb % 4) != 0)
				fprintf(stderr, "WARNING! Real DECstation 3100 machines have an integer multiple of 4 MBs of RAM. Continuing anyway.\n");

			color_fb_flag = 1;	/*  1 for color, 0 for mono. TODO: command line option?  */

			/*
			 *  According to NetBSD/pmax:
			 *
			 *  pm0 at ibus0 addr 0xfc00000: 1024x864x1  (or x8 for color)
			 *  dc0 at ibus0 addr 0x1c000000
			 *  le0 at ibus0 addr 0x18000000: address 00:00:00:00:00:00
			 *  sii0 at ibus0 addr 0x1a000000
			 *  mcclock0 at ibus0 addr 0x1d000000: mc146818 or compatible
			 *  0x1e000000 = system status and control register
			 */
			fb = dev_fb_init(cpu, mem, KN01_PHYS_FBUF_START,
			    color_fb_flag? VFB_DEC_VFB02 : VFB_DEC_VFB01,
			    0,0,0,0,0, color_fb_flag? "VFB02":"VFB01");
			dev_colorplanemask_init(mem, KN01_PHYS_COLMASK_START, &fb->color_plane_mask);
			dev_vdac_init(mem, KN01_SYS_VDAC, fb->rgb_palette, color_fb_flag);
			dev_le_init(cpu, mem, KN01_SYS_LANCE, KN01_SYS_LANCE_B_START, KN01_SYS_LANCE_B_END, KN01_INT_LANCE, 4*1048576);
			dev_sii_init(cpu, mem, KN01_SYS_SII, KN01_SYS_SII_B_START, KN01_SYS_SII_B_END, KN01_INT_SII);
			dev_dc7085_init(cpu, mem, KN01_SYS_DZ, KN01_INT_DZ, emul->use_x11);
			dev_mc146818_init(cpu, mem, KN01_SYS_CLOCK, KN01_INT_CLOCK, MC146818_DEC, 1);
			dev_kn01_csr_init(mem, KN01_SYS_CSR, color_fb_flag);

			framebuffer_console_name = "osconsole=0,3";	/*  fb,keyb  */
			serial_console_name      = "osconsole=3";	/*  3  */
			break;

		case MACHINE_3MAX_5000:		/*  type  2, KN02  */
			/*  Supposed to have 25MHz R3000 CPU, R3010 FPC,  */
			/*  and a R3220 Memory coprocessor  */
			emul->machine_name = "DECstation 5000/200 (3MAX, KN02)";

			if (emul->emulated_hz == 0)
				emul->emulated_hz = 25000000;

			if (emul->physical_ram_in_mb < 8)
				fprintf(stderr, "WARNING! Real KN02 machines do not have less than 8MB RAM. Continuing anyway.\n");
			if (emul->physical_ram_in_mb > 480)
				fprintf(stderr, "WARNING! Real KN02 machines cannot have more than 480MB RAM. Continuing anyway.\n");

			/*  An R3220 memory thingy:  */
			cpu->coproc[3] = coproc_new(cpu, 3);

			/*
			 *  According to NetBSD/pmax:
			 *  asc0 at tc0 slot 5 offset 0x0
			 *  le0 at tc0 slot 6 offset 0x0
			 *  ibus0 at tc0 slot 7 offset 0x0
			 *  dc0 at ibus0 addr 0x1fe00000
			 *  mcclock0 at ibus0 addr 0x1fe80000: mc146818
			 *
			 *  kn02 shared irq numbers (IP) are offset by +8
			 *  in the emulator
			 */

			/*  KN02 interrupts:  */
			cpu->md_interrupt = kn02_interrupt;

			/*  TURBOchannel slots 0, 1, and 2 are free for   */
			/*  option cards.  Let's put in a graphics card:  */
			dev_turbochannel_init(cpu, mem, 0,
			    KN02_PHYS_TC_0_START, KN02_PHYS_TC_0_END,
			    turbochannel_default_gfx_card, KN02_IP_SLOT0 +8);

			dev_turbochannel_init(cpu, mem, 1,
			    KN02_PHYS_TC_1_START, KN02_PHYS_TC_1_END,
			    "", KN02_IP_SLOT1 +8);
			dev_turbochannel_init(cpu, mem, 2,
			    KN02_PHYS_TC_2_START, KN02_PHYS_TC_2_END,
			    "", KN02_IP_SLOT2 +8);

			/*  TURBOchannel slots 3 and 4 are reserved.  */

			/*  TURBOchannel slot 5 is PMAZ-AA ("asc" SCSI).  */
			dev_turbochannel_init(cpu, mem, 5,
			    KN02_PHYS_TC_5_START, KN02_PHYS_TC_5_END,
			    "PMAZ-AA", KN02_IP_SCSI +8);

			/*  TURBOchannel slot 6 is PMAD-AA ("le" ethernet).  */
			dev_turbochannel_init(cpu, mem, 6,
			    KN02_PHYS_TC_6_START, KN02_PHYS_TC_6_END,
			    "PMAD-AA", KN02_IP_LANCE +8);

			/*  TURBOchannel slot 7 is system stuff.  */
			dev_dc7085_init(cpu, mem,
			    KN02_SYS_DZ, KN02_IP_DZ +8, emul->use_x11);
			dev_mc146818_init(cpu, mem,
			    KN02_SYS_CLOCK, KN02_INT_CLOCK, MC146818_DEC, 1);

			kn02_csr = dev_kn02_init(cpu, mem, KN02_SYS_CSR);

			framebuffer_console_name = "osconsole=0,7";
								/*  fb,keyb  */
			serial_console_name      = "osconsole=2";
			boot_scsi_boardnumber = 5;
			boot_net_boardnumber = 6;	/*  TODO: 3?  */
			break;

		case MACHINE_3MIN_5000:		/*  type 3, KN02BA  */
			emul->machine_name = "DECstation 5000/112 or 145 (3MIN, KN02BA)";
			if (emul->emulated_hz == 0)
				emul->emulated_hz = 33000000;
			if (emul->physical_ram_in_mb > 128)
				fprintf(stderr, "WARNING! Real 3MIN machines cannot have more than 128MB RAM. Continuing anyway.\n");

			/*  KMIN interrupts:  */
			cpu->md_interrupt = kmin_interrupt;

			/*
			 *  tc0 at mainbus0: 12.5 MHz clock				(0x10000000, slotsize = 64MB)
			 *  tc slot 1:   0x14000000
			 *  tc slot 2:   0x18000000
			 *  ioasic0 at tc0 slot 3 offset 0x0				(0x1c000000) slot 0
			 *  asic regs							(0x1c040000) slot 1
			 *  station's ether address					(0x1c080000) slot 2
			 *  le0 at ioasic0 offset 0xc0000: address 00:00:00:00:00:00	(0x1c0c0000) slot 3
			 *  scc0 at ioasic0 offset 0x100000				(0x1c100000) slot 4
			 *  scc1 at ioasic0 offset 0x180000: console			(0x1c180000) slot 6
			 *  mcclock0 at ioasic0 offset 0x200000: mc146818 or compatible	(0x1c200000) slot 8
			 *  asc0 at ioasic0 offset 0x300000: NCR53C94, 25MHz, SCSI ID 7	(0x1c300000) slot 12
			 *  dma for asc0						(0x1c380000) slot 14
			 */
			dec_ioasic_data = dev_dec_ioasic_init(mem, 0x1c000000);
			dev_scc_init(cpu, mem, 0x1c100000, KMIN_INTR_SCC_0 +8, emul->use_x11, 0, 1);
			dev_scc_init(cpu, mem, 0x1c180000, KMIN_INTR_SCC_1 +8, emul->use_x11, 1, 1);
			dev_mc146818_init(cpu, mem, 0x1c200000, KMIN_INTR_CLOCK +8, MC146818_DEC, 1);
			dev_asc_init(cpu, mem, 0x1c300000, KMIN_INTR_SCSI +8,
			    NULL, DEV_ASC_DEC, NULL, NULL);

			/*
			 *  TURBOchannel slots 0, 1, and 2 are free for
			 *  option cards.  The first one will contain a
			 *  graphics card by default.
			 *
			 *  TODO: irqs 
			 */
			dev_turbochannel_init(cpu, mem, 0,
			    0x10000000, 0x103fffff,
			    turbochannel_default_gfx_card, KMIN_INT_TC0);

			dev_turbochannel_init(cpu, mem, 1,
			    0x14000000, 0x143fffff, "", KMIN_INT_TC1);
			dev_turbochannel_init(cpu, mem, 2,
			    0x18000000, 0x183fffff, "", KMIN_INT_TC2);

			/*  (kmin shared irq numbers (IP) are offset by +8 in the emulator)  */
			/*  kmin_csr = dev_kmin_init(cpu, mem, KMIN_REG_INTR);  */

			framebuffer_console_name = "osconsole=0,3";	/*  fb, keyb (?)  */
			serial_console_name      = "osconsole=3";	/*  ?  */
			break;

		case MACHINE_3MAXPLUS_5000:	/*  type 4, KN03  */
			emul->machine_name = "DECsystem 5900 or 5000 (3MAX+) (KN03)";

			/*  5000/240 (KN03-GA, R3000): 40 MHz  */
			/*  5000/260 (KN05-NB, R4000): 60 MHz  */
			/*  TODO: are both these type 4?  */
			if (emul->emulated_hz == 0)
				emul->emulated_hz = 40000000;
			if (emul->physical_ram_in_mb > 480)
				fprintf(stderr, "WARNING! Real KN03 machines cannot have more than 480MB RAM. Continuing anyway.\n");

			/*  KN03 interrupts:  */
			cpu->md_interrupt = kn03_interrupt;

			/*
			 *  tc0 at mainbus0: 25 MHz clock (slot 0)			(0x1e000000)
			 *  tc0 slot 1							(0x1e800000)
			 *  tc0 slot 2							(0x1f000000)
			 *  ioasic0 at tc0 slot 3 offset 0x0				(0x1f800000)
			 *    something that has to do with interrupts? (?)		(0x1f840000 ?)
			 *  le0 at ioasic0 offset 0xc0000				(0x1f8c0000)
			 *  scc0 at ioasic0 offset 0x100000				(0x1f900000)
			 *  scc1 at ioasic0 offset 0x180000: console			(0x1f980000)
			 *  mcclock0 at ioasic0 offset 0x200000: mc146818 or compatible	(0x1fa00000)
			 *  asc0 at ioasic0 offset 0x300000: NCR53C94, 25MHz, SCSI ID 7	(0x1fb00000)
			 */
			dec_ioasic_data = dev_dec_ioasic_init(mem, 0x1f800000);

			dev_le_init(cpu, mem, KN03_SYS_LANCE,
			    0, 0, KN03_INTR_LANCE +8, 4*65536);
			dev_scc_init(cpu, mem, KN03_SYS_SCC_0, KN03_INTR_SCC_0 +8, emul->use_x11, 0, 1);
			dev_scc_init(cpu, mem, KN03_SYS_SCC_1, KN03_INTR_SCC_1 +8, emul->use_x11, 1, 1);
			dev_mc146818_init(cpu, mem, KN03_SYS_CLOCK, KN03_INT_RTC, MC146818_DEC, 1);
			dev_asc_init(cpu, mem, KN03_SYS_SCSI,
			    KN03_INTR_SCSI +8, NULL, DEV_ASC_DEC, NULL, NULL);

			/*
			 *  TURBOchannel slots 0, 1, and 2 are free for
			 *  option cards.  The first one will contain a
			 *  graphics card by default.
			 *
			 *  TODO: irqs 
			 */
			dev_turbochannel_init(cpu, mem, 0,
			    KN03_PHYS_TC_0_START, KN03_PHYS_TC_0_END,
			    turbochannel_default_gfx_card, KN03_INTR_TC_0 +8);

			dev_turbochannel_init(cpu, mem, 1,
			    KN03_PHYS_TC_1_START, KN03_PHYS_TC_1_END, "",
			    KN03_INTR_TC_1 +8);

			dev_turbochannel_init(cpu, mem, 2,
			    KN03_PHYS_TC_2_START, KN03_PHYS_TC_2_END, "",
			    KN03_INTR_TC_2 +8);

			/*  TODO: interrupts  */
			/*  shared (turbochannel) interrupts are +8  */

			framebuffer_console_name = "osconsole=0,3";	/*  fb, keyb (?)  */
			serial_console_name      = "osconsole=3";	/*  ?  */
			break;

		case MACHINE_5800:		/*  type 5, KN5800  */
			emul->machine_name = "DECsystem 5800";

/*  TODO: this is incorrect, banks multiply by 8 etc  */
			if (emul->physical_ram_in_mb < 48)
				fprintf(stderr, "WARNING! 5800 will probably not run with less than 48MB RAM. Continuing anyway.\n");

			/*
			 *  According to http://www2.no.netbsd.org/Ports/pmax/models.html,
			 *  the 5800-series is based on VAX 6000/300.
			 */

			/*
			 *  Ultrix might support SMP on this machine type.
			 *
			 *  Something at 0x10000000.
			 *  ssc serial console at 0x10140000, interrupt 2 (shared with XMI?).
			 *  xmi 0 at address 0x11800000   (node x at offset x*0x80000)
			 *  Clock uses interrupt 3 (shared with XMI?).
			 */

			dec5800_csr = dev_dec5800_init(cpu, mem, 0x10000000);
			dev_ssc_init(cpu, mem, 0x10140000, 2, emul->use_x11, &dec5800_csr->csr);
			dev_deccca_init(cpu, mem, DEC_DECCCA_BASEADDR);
			dev_decxmi_init(cpu, mem, 0x11800000);
			dev_decbi_init(cpu, mem, 0x10000000);

			break;

		case MACHINE_5400:		/*  type 6, KN210  */
			emul->machine_name = "DECsystem 5400 (KN210)";
			/*
			 *  Misc. info from the KN210 manual:
			 *
			 *  Interrupt lines:
			 *	irq5	fpu
			 *	irq4	halt
			 *	irq3	pwrfl -> mer1 -> mer0 -> wear
			 *	irq2	100 Hz -> birq7
			 *	irq1	dssi -> ni -> birq6
			 *	irq0	birq5 -> console -> timers -> birq4
			 *
			 *  Interrupt status register at 0x10048000.
			 *  Main memory error status register at 0x1008140.
			 *  Interval Timer Register (ITR) at 0x10084010.
			 *  Q22 stuff at 0x10088000 - 0x1008ffff.
			 *  TODR at 0x1014006c.
			 *  TCR0 (timer control register 0) 0x10140100.
			 *  TIR0 (timer interval register 0) 0x10140104.
			 *  TCR1 (timer control register 1) 0x10140110.
			 *  TIR1 (timer interval register 1) 0x10140114.
			 *  VRR0 (Vector Read Register 0) at 0x16000050.
			 *  VRR1 (Vector Read Register 1) at 0x16000054.
			 *  VRR2 (Vector Read Register 2) at 0x16000058.
			 *  VRR3 (Vector Read Register 3) at 0x1600005c.
			 */
			/*  ln (ethernet) at 0x10084x00 ? and 0x10120000 ?  */
			/*  error registers (?) at 0x17000000 and 0x10080000  */
			dev_kn210_init(cpu, mem, 0x10080000);
			dev_ssc_init(cpu, mem, 0x10140000, 0, emul->use_x11, NULL);	/*  TODO:  not irq 0  */
			break;

		case MACHINE_MAXINE_5000:	/*  type 7, KN02CA  */
			emul->machine_name = "Personal DECstation 5000/xxx (MAXINE) (KN02CA)";
			if (emul->emulated_hz == 0)
				emul->emulated_hz = 33000000;

			if (emul->physical_ram_in_mb < 8)
				fprintf(stderr, "WARNING! Real KN02CA machines do not have less than 8MB RAM. Continuing anyway.\n");
			if (emul->physical_ram_in_mb > 40)
				fprintf(stderr, "WARNING! Real KN02CA machines cannot have more than 40MB RAM. Continuing anyway.\n");

			/*  Maxine interrupts:  */
			cpu->md_interrupt = maxine_interrupt;

			/*
			 *  Something at address 0xca00000. (?)
			 *  Something at address 0xe000000. (?)
			 *  tc0 slot 0								(0x10000000)
			 *  tc0 slot 1								(0x14000000)
			 *  (tc0 slot 2 used by the framebuffer)
			 *  ioasic0 at tc0 slot 3 offset 0x0					(0x1c000000)
			 *  le0 at ioasic0 offset 0xc0000: address 00:00:00:00:00:00		(0x1c0c0000)
			 *  scc0 at ioasic0 offset 0x100000: console  <-- serial		(0x1c100000)
			 *  mcclock0 at ioasic0 offset 0x200000: mc146818			(0x1c200000)
			 *  isdn at ioasic0 offset 0x240000 not configured			(0x1c240000)
			 *  bba0 at ioasic0 offset 0x240000 (audio0 at bba0)        <--- which one of isdn and bba0?
			 *  dtop0 at ioasic0 offset 0x280000					(0x1c280000)
			 *  fdc at ioasic0 offset 0x2c0000 not configured  <-- floppy		(0x1c2c0000)
			 *  asc0 at ioasic0 offset 0x300000: NCR53C94, 25MHz, SCSI ID 7		(0x1c300000)
			 *  xcfb0 at tc0 slot 2 offset 0x0: 1024x768x8 built-in framebuffer	(0xa000000)
			 */
			dec_ioasic_data = dev_dec_ioasic_init(mem, 0x1c000000);

			/*  TURBOchannel slots (0 and 1):  */
			dev_turbochannel_init(cpu, mem, 0,
			    0x10000000, 0x103fffff, "", XINE_INTR_TC_0 +8);
			dev_turbochannel_init(cpu, mem, 1,
			    0x14000000, 0x143fffff, "", XINE_INTR_TC_1 +8);

			/*
			 *  TURBOchannel slot 2 is hardwired to be used by
			 *  the framebuffer: (NOTE: 0x8000000, not 0x18000000)
			 */
			dev_turbochannel_init(cpu, mem, 2,
			    0x8000000, 0xbffffff, "PMAG-DV", 0);

			/*
			 *  TURBOchannel slot 3: fixed, ioasic
			 *  (the system stuff), 0x1c000000
			 */
			dev_scc_init(cpu, mem, 0x1c100000,
			    XINE_INTR_SCC_0 +8, emul->use_x11, 0, 1);
			dev_mc146818_init(cpu, mem, 0x1c200000,
			    XINE_INT_TOY, MC146818_DEC, 1);
			dev_asc_init(cpu, mem, 0x1c300000,
			    XINE_INTR_SCSI +8, NULL, DEV_ASC_DEC, NULL, NULL);

			framebuffer_console_name = "osconsole=3,2";	/*  keyb,fb ??  */
			serial_console_name      = "osconsole=3";
			break;

		case MACHINE_5500:	/*  type 11, KN220  */
			emul->machine_name = "DECsystem 5500 (KN220)";

			/*
			 *  According to NetBSD's pmax ports page:
			 *  KN220-AA is a "30 MHz R3000 CPU with R3010 FPU"
			 *  with "512 kBytes of Prestoserve battery backed RAM."
			 */
			if (emul->emulated_hz == 0)
				emul->emulated_hz = 30000000;

			/*
			 *  See KN220 docs for more info.
			 *
			 *  something at 0x10000000
			 *  something at 0x10001000
			 *  something at 0x10040000
			 *  scc at 0x10140000
			 *  qbus at (or around) 0x10080000
			 *  dssi (disk controller) buffers at 0x10100000, registers at 0x10160000.
			 *  sgec (ethernet) registers at 0x10008000, station addresss at 0x10120000.
			 *  asc (scsi) at 0x17100000.
			 */

			dev_ssc_init(cpu, mem, 0x10140000, 0, emul->use_x11, NULL);		/*  TODO:  not irq 0  */

			/*  something at 0x17000000, ultrix says "cpu 0 panic: DS5500 I/O Board is missing" if this is not here  */
			dev_dec5500_ioboard_init(cpu, mem, 0x17000000);

			dev_sgec_init(mem, 0x10008000, 0);		/*  irq?  */

			/*  The asc controller might be TURBOchannel-ish?  */
#if 0
			dev_turbochannel_init(cpu, mem, 0, 0x17100000, 0x171fffff, "PMAZ-AA", 0);	/*  irq?  */
#else
			dev_asc_init(cpu, mem, 0x17100000, 0, NULL, DEV_ASC_DEC, NULL, NULL);		/*  irq?  */
#endif

			framebuffer_console_name = "osconsole=0,0";	/*  TODO (?)  */
			serial_console_name      = "osconsole=0";
			break;

		case MACHINE_MIPSMATE_5100:	/*  type 12  */
			emul->machine_name = "DEC MIPSMATE 5100 (KN230)";
			if (emul->emulated_hz == 0)
				emul->emulated_hz = 20000000;
			if (emul->physical_ram_in_mb > 128)
				fprintf(stderr, "WARNING! Real MIPSMATE 5100 machines cannot have more than 128MB RAM. Continuing anyway.\n");

			if (emul->use_x11)
				fprintf(stderr, "WARNING! Real MIPSMATE 5100 machines cannot have a graphical framebuffer. Continuing anyway.\n");

			/*  KN230 interrupts:  */
			cpu->md_interrupt = kn230_interrupt;

			/*
			 *  According to NetBSD/pmax:
			 *  dc0 at ibus0 addr 0x1c000000
			 *  le0 at ibus0 addr 0x18000000: address 00:00:00:00:00:00
			 *  sii0 at ibus0 addr 0x1a000000
			 */
			dev_mc146818_init(cpu, mem, KN230_SYS_CLOCK, 4, MC146818_DEC, 1);
			dev_dc7085_init(cpu, mem, KN230_SYS_DZ0, KN230_CSR_INTR_DZ0, emul->use_x11);		/*  NOTE: CSR_INTR  */
			/* dev_dc7085_init(cpu, mem, KN230_SYS_DZ1, KN230_CSR_INTR_OPT0, emul->use_x11); */	/*  NOTE: CSR_INTR  */
			/* dev_dc7085_init(cpu, mem, KN230_SYS_DZ2, KN230_CSR_INTR_OPT1, emul->use_x11); */	/*  NOTE: CSR_INTR  */
			dev_le_init(cpu, mem, KN230_SYS_LANCE, KN230_SYS_LANCE_B_START, KN230_SYS_LANCE_B_END, KN230_CSR_INTR_LANCE, 4*1048576);
			dev_sii_init(cpu, mem, KN230_SYS_SII, KN230_SYS_SII_B_START, KN230_SYS_SII_B_END, KN230_CSR_INTR_SII);
			kn230_csr = dev_kn230_init(cpu, mem, KN230_SYS_ICSR);

			serial_console_name = "osconsole=0";
			break;

		default:
			;
		}

		/*  DECstation PROM stuff:  (TODO: endianness)  */
		for (i=0; i<100; i++)
			store_32bit_word(cpu, DEC_PROM_CALLBACK_STRUCT + i*4,
			    DEC_PROM_EMULATION + i*8);

		/*  Fill PROM with dummy return instructions:  (TODO: make this nicer)  */
		for (i=0; i<100; i++) {
			store_32bit_word(cpu, DEC_PROM_EMULATION + i*8,
			    0x03e00008);	/*  return  */
			store_32bit_word(cpu, DEC_PROM_EMULATION + i*8 + 4,
			    0x00000000);	/*  nop  */
		}

		/*
		 *  According to dec_prom.h from NetBSD:
		 *
		 *  "Programs loaded by the new PROMs pass the following arguments:
		 *	a0	argc
		 *	a1	argv
		 *	a2	DEC_PROM_MAGIC
		 *	a3	The callback vector defined below"
		 *
		 *  So we try to emulate a PROM, even though no such thing has been
		 *  loaded.
		 */

		cpu->gpr[GPR_A0] = 3;
		cpu->gpr[GPR_A1] = DEC_PROM_INITIAL_ARGV;
		cpu->gpr[GPR_A2] = DEC_PROM_MAGIC;
		cpu->gpr[GPR_A3] = DEC_PROM_CALLBACK_STRUCT;

		store_32bit_word(cpu, INITIAL_STACK_POINTER + 0x10,
		    BOOTINFO_MAGIC);
		store_32bit_word(cpu, INITIAL_STACK_POINTER + 0x14,
		    BOOTINFO_ADDR);

		store_32bit_word(cpu, DEC_PROM_INITIAL_ARGV,
		    (DEC_PROM_INITIAL_ARGV + 0x10));
		store_32bit_word(cpu, DEC_PROM_INITIAL_ARGV+4,
		    (DEC_PROM_INITIAL_ARGV + 0x70));
		store_32bit_word(cpu, DEC_PROM_INITIAL_ARGV+8,
		    (DEC_PROM_INITIAL_ARGV + 0xe0));
		store_32bit_word(cpu, DEC_PROM_INITIAL_ARGV+12, 0);

		/*
		 *  NetBSD and Ultrix expect the boot args to be like this:
		 *
		 *	"boot" "bootdev" [args?]
		 *
		 *  where bootdev is supposed to be "rz(0,0,0)netbsd" for
		 *  3100/2100 (although that crashes Ultrix :-/), and
		 *  "5/rz0a/netbsd" for all others.  The number '5' is the
		 *  slot number of the boot device.
		 *
		 *  'rz' for disks, 'tz' for tapes.
		 *
		 *  TODO:  Make this nicer.
		 */
		{
			char bootpath[200];

#if 0
			if (emul->machine == MACHINE_PMAX_3100)
				strcpy(bootpath, "rz(0,0,0)");
			else
#endif
				strcpy(bootpath, "5/rz1/");

			if (bootdev_id < 0 || emul->force_netboot) {
				/*  tftp boot:  */
				strcpy(bootpath, "5/tftp/");
				bootpath[0] = '0' + boot_net_boardnumber;
			} else {
				/*  disk boot:  */
				bootpath[0] = '0' + boot_scsi_boardnumber;
				if (diskimage_is_a_tape(bootdev_id))
					bootpath[2] = 't';
				bootpath[4] = '0' + bootdev_id;
			}

			init_bootpath = bootpath;
		}

		bootarg = malloc(strlen(init_bootpath) +
		    strlen(emul->boot_kernel_filename) + 1 +
		    strlen(emul->boot_string_argument) + 1);
		strcpy(bootarg, init_bootpath);
		strcat(bootarg, emul->boot_kernel_filename);

		bootstr = "boot";

		store_string(cpu, DEC_PROM_INITIAL_ARGV+0x10, bootstr);
		store_string(cpu, DEC_PROM_INITIAL_ARGV+0x70, bootarg);
		store_string(cpu, DEC_PROM_INITIAL_ARGV+0xe0,
		    emul->boot_string_argument);

		/*  Decrease the nr of args, if there are no args :-)  */
		if (emul->boot_string_argument == NULL ||
		    emul->boot_string_argument[0] == '\0')
			cpu->gpr[GPR_A0] --;

		if (emul->boot_string_argument[0] != '\0') {
			strcat(bootarg, " ");
			strcat(bootarg, emul->boot_string_argument);
		}

		xx.a.common.next = (char *)&xx.b - (char *)&xx;
		xx.a.common.type = BTINFO_MAGIC;
		xx.a.magic = BOOTINFO_MAGIC;

		xx.b.common.next = (char *)&xx.c - (char *)&xx.b;
		xx.b.common.type = BTINFO_BOOTPATH;
		strcpy(xx.b.bootpath, bootstr);

		xx.c.common.next = 0;
		xx.c.common.type = BTINFO_SYMTAB;
		xx.c.nsym = 0;
		xx.c.ssym = 0;
		xx.c.esym = file_loaded_end_addr;

		store_buf(cpu, BOOTINFO_ADDR, (char *)&xx, sizeof(xx));

		/*
		 *  The system's memmap:  (memmap is a global variable, in
		 *  dec_prom.h)
		 */
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&memmap.pagesize, 4096);
		{
			unsigned int i;
			for (i=0; i<sizeof(memmap.bitmap); i++)
				memmap.bitmap[i] = ((int)i * 4096*8 <
				    1048576*emul->physical_ram_in_mb)?
				    0xff : 0x00;
		}
		store_buf(cpu, DEC_MEMMAP_ADDR, (char *)&memmap, sizeof(memmap));

		/*  Environment variables:  */
		addr = DEC_PROM_STRINGS;

		if (emul->use_x11)
			/*  (0,3)  Keyboard and Framebuffer  */
			add_environment_string(cpu, framebuffer_console_name, &addr);
		else
			/*  Serial console  */
			add_environment_string(cpu, serial_console_name, &addr);

		/*
		 *  The KN5800 (SMP system) uses a CCA (console communications
		 *  area):  (See VAX 6000 documentation for details.)
		 */
		{
			char tmps[300];
			sprintf(tmps, "cca=%x",
			    (int)(DEC_DECCCA_BASEADDR + 0xa0000000ULL));
			add_environment_string(cpu, tmps, &addr);
		}

		/*  These are needed for Sprite to boot:  */
		{
			char tmps[300];

			sprintf(tmps, "boot=%s", bootarg);
			add_environment_string(cpu, tmps, &addr);

			sprintf(tmps, "bitmap=0x%x", (uint32_t)((
			    DEC_MEMMAP_ADDR + sizeof(memmap.pagesize))
			    & 0xffffffffULL));
			add_environment_string(cpu, tmps, &addr);

			sprintf(tmps, "bitmaplen=0x%x",
			    emul->physical_ram_in_mb * 1048576 / 4096 / 8);
			add_environment_string(cpu, tmps, &addr);
		}

		add_environment_string(cpu, "scsiid0=7", &addr);
		add_environment_string(cpu, "bootmode=a", &addr);
		add_environment_string(cpu, "testaction=q", &addr);
		add_environment_string(cpu, "haltaction=h", &addr);
		add_environment_string(cpu, "more=24", &addr);

		/*  Used in at least Ultrix on the 5100:  */
		add_environment_string(cpu, "scsiid=7", &addr);
		add_environment_string(cpu, "baud0=9600", &addr);
		add_environment_string(cpu, "baud1=9600", &addr);
		add_environment_string(cpu, "baud2=9600", &addr);
		add_environment_string(cpu, "baud3=9600", &addr);
		add_environment_string(cpu, "iooption=0x1", &addr);

		/*  The end:  */
		add_environment_string(cpu, "", &addr);

		break;

	case EMULTYPE_COBALT:
		emul->machine_name = "Cobalt";

		/*
		 *  Interrupts seem to be the following:
		 *  (according to http://www.funet.fi/pub/Linux/PEOPLE/Linus/v2.4/patch-html/patch-2.4.19/linux-2.4.19_arch_mips_cobalt_irq.c.html)
		 *
		 *	2	Galileo chip (timer)
		 *	3	Tulip 0 + NCR SCSI
		 *	4	Tulip 1
		 *	5	16550 UART (serial console)
		 *	6	VIA southbridge PIC
		 *	7	PCI
		 */
/*		dev_XXX_init(cpu, mem, 0x10000000, emul->emulated_hz);	*/
		dev_mc146818_init(cpu, mem, 0x10000070, 0, MC146818_PC_CMOS, 4);
		dev_ns16550_init(cpu, mem, 0x1c800000, 5, 1, 1);

		/*
		 *  According to NetBSD/cobalt:
		 *
		 *  pchb0 at pci0 dev 0 function 0: Galileo GT-64111 System Controller, rev 1   (NOTE: added by dev_gt_init())
		 *  tlp0 at pci0 dev 7 function 0: DECchip 21143 Ethernet, pass 4.1
		 *  Symbios Logic 53c860 (SCSI mass storage, revision 0x02) at pci0 dev 8
		 *  pcib0 at pci0 dev 9 function 0, VIA Technologies VT82C586 (Apollo VP) PCI-ISA Bridge, rev 37
		 *  pciide0 at pci0 dev 9 function 1: VIA Technologies VT82C586 (Apollo VP) ATA33 cr
		 *  tlp1 at pci0 dev 12 function 0: DECchip 21143 Ethernet, pass 4.1
		 */
		pci_data = dev_gt_init(cpu, mem, 0x14000000, 2, 6);	/*  7 for PCI, not 6?  */
		/*  bus_pci_add(cpu, pci_data, mem, 0,  7, 0, pci_dec21143_init, pci_dec21143_rr);  */
		bus_pci_add(cpu, pci_data, mem, 0,  8, 0, NULL, NULL);  /*  PCI_VENDOR_SYMBIOS, PCI_PRODUCT_SYMBIOS_860  */
		bus_pci_add(cpu, pci_data, mem, 0,  9, 0, pci_vt82c586_isa_init, pci_vt82c586_isa_rr);
		bus_pci_add(cpu, pci_data, mem, 0,  9, 1, pci_vt82c586_ide_init, pci_vt82c586_ide_rr);
		/*  bus_pci_add(cpu, pci_data, mem, 0, 12, 0, pci_dec21143_init, pci_dec21143_rr);  */

		/*
		 *  NetBSD/cobalt expects memsize in a0, but it seems that what
		 *  it really wants is the end of memory + 0x80000000.
		 *
		 *  The bootstring should be stored starting 512 bytes before end
		 *  of physical ram.
		 */
		cpu->gpr[GPR_A0] = emul->physical_ram_in_mb * 1048576 + 0x80000000;
		bootstr = "root=/dev/hda1 ro";
		/*  bootstr = "nfsroot=/usr/cobalt/";  */
		store_string(cpu, 0xffffffff80000000ULL +
		    emul->physical_ram_in_mb * 1048576 - 512, bootstr);
		break;

	case EMULTYPE_HPCMIPS:
		emul->machine_name = "hpcmips";
		dev_fb_init(cpu, mem, HPCMIPS_FB_ADDR, VFB_HPCMIPS,
		    HPCMIPS_FB_XSIZE, HPCMIPS_FB_YSIZE,
		    HPCMIPS_FB_XSIZE, HPCMIPS_FB_YSIZE, 2, "HPCmips");

		/*
		 *  NetBSD/hpcmips expects the following:
		 */
		cpu->gpr[GPR_A0] = 1;	/*  argc  */
		cpu->gpr[GPR_A1] = emul->physical_ram_in_mb * 1048576 + 0x80000000ULL - 512;	/*  argv  */
		cpu->gpr[GPR_A2] = emul->physical_ram_in_mb * 1048576 + 0x80000000ULL - 256;	/*  ptr to hpc_bootinfo  */
		bootstr = "netbsd";
		store_32bit_word(cpu, 0x80000000 + emul->physical_ram_in_mb * 1048576 - 512, 0x80000000 + emul->physical_ram_in_mb * 1048576 - 512 + 8);
		store_32bit_word(cpu, 0x80000000 + emul->physical_ram_in_mb * 1048576 - 512 + 4, 0);
		store_string(cpu, 0x80000000 + emul->physical_ram_in_mb * 1048576 - 512 + 8, bootstr);
		memset(&hpc_bootinfo, 0, sizeof(hpc_bootinfo));
		hpc_bootinfo.length = sizeof(hpc_bootinfo);
		hpc_bootinfo.magic = HPC_BOOTINFO_MAGIC;
		hpc_bootinfo.fb_addr = 0x80000000 + HPCMIPS_FB_ADDR;
		hpc_bootinfo.fb_line_bytes = HPCMIPS_FB_XSIZE / 4;	/*  for 2-bits-per-pixel  */
		hpc_bootinfo.fb_width = HPCMIPS_FB_XSIZE;
		hpc_bootinfo.fb_height = HPCMIPS_FB_YSIZE;
		hpc_bootinfo.fb_type = BIFB_D2_M2L_3;
		hpc_bootinfo.bi_cnuse = BI_CNUSE_BUILTIN;  /*  _BUILTIN or _SERIAL  */

		/*  TODO:  set platid from netbsd/usr/src/sys/arch/hpc/include/platid*  */
		hpc_bootinfo.platid_cpu = 1 << 14;
		hpc_bootinfo.platid_machine = (2 << 22) + (1 << 16);
/*
#define PLATID_SUBMODEL_SHIFT           0
#define PLATID_MODEL_SHIFT              8
#define PLATID_SERIES_SHIFT             16
#define PLATID_VENDOR_SHIFT             22
*/
		printf("hpc_bootinfo.platid_cpu     = 0x%x\n", hpc_bootinfo.platid_cpu);
		printf("hpc_bootinfo.platid_machine = 0x%x\n", hpc_bootinfo.platid_machine);
		hpc_bootinfo.timezone = 0;
		store_buf(cpu, 0x80000000 + emul->physical_ram_in_mb * 1048576 - 256, (char *)&hpc_bootinfo, sizeof(hpc_bootinfo));
		break;

	case EMULTYPE_PS2:
		emul->machine_name = "Playstation 2";

		if (emul->physical_ram_in_mb != 32)
			fprintf(stderr, "WARNING! Playstation 2 machines are supposed to have exactly 32 MB RAM. Continuing anyway.\n");
		if (!emul->use_x11)
			fprintf(stderr, "WARNING! Playstation 2 without -X is pretty meaningless. Continuing anyway.\n");

		/*
		 *  According to NetBSD:
		 *	Hardware irq 0 is timer/interrupt controller
		 *	Hardware irq 1 is dma controller
		 *
		 *  Some things are not yet emulated (at all), and hence are detected incorrectly:
		 *	sbus0 at mainbus0: controller type 2
		 *	ohci0 at sbus0			(at 0x1f801600, according to linux)
		 *	ohci0: OHCI version 1.0
		 */

		dev_ps2_gs_init(cpu, mem, 0x12000000);
		ps2_data = dev_ps2_stuff_init(cpu, mem, 0x10000000, GLOBAL_gif_mem);
		dev_ps2_ohci_init(cpu, mem, 0x1f801600);
		dev_ram_init(mem, 0x1c000000, 4 * 1048576, DEV_RAM_RAM, 0);	/*  TODO: how much?  */

		cpu->md_interrupt = ps2_interrupt;

		add_symbol_name(&emul->symbol_context,
		    PLAYSTATION2_SIFBIOS, 0x10000, "[SIFBIOS entry]", 0);
		store_32bit_word(cpu, PLAYSTATION2_BDA + 0, PLAYSTATION2_SIFBIOS);
		store_buf(cpu, PLAYSTATION2_BDA + 4, "PS2b", 4);

#if 0
		/*  Harddisk controller present flag:  */
		store_32bit_word(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x0, 0x100);
		dev_ps2_spd_init(cpu, mem, 0x14000000);
#endif

		store_32bit_word(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x4, PLAYSTATION2_OPTARGS);
		bootstr = "root=/dev/hda1 crtmode=vesa0,60";
		store_string(cpu, PLAYSTATION2_OPTARGS, bootstr);

		/*  TODO:  netbsd's bootinfo.h, for symbolic names  */
		{
			time_t timet;
			struct tm *tmp;

			/*  RTC data given by the BIOS:  */
			timet = time(NULL) + 9*3600;	/*  PS2 uses Japanese time  */
			tmp = gmtime(&timet);
			/*  TODO:  are these 0- or 1-based?  */
			store_byte(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 1, int_to_bcd(tmp->tm_sec));
			store_byte(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 2, int_to_bcd(tmp->tm_min));
			store_byte(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 3, int_to_bcd(tmp->tm_hour));
			store_byte(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 5, int_to_bcd(tmp->tm_mday));
			store_byte(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 6, int_to_bcd(tmp->tm_mon + 1));
			store_byte(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 7, int_to_bcd(tmp->tm_year - 100));
		}

		/*  "BOOTINFO_PCMCIA_TYPE" in NetBSD's bootinfo.h. This contains the sbus controller type.  */
		store_32bit_word(cpu, 0xa0000000 + emul->physical_ram_in_mb*1048576 - 0x1000 + 0x1c, 3);

		/*  TODO:  Is this neccessary?  */
		cpu->gpr[GPR_SP] = 0x80007f00;

		break;

	case EMULTYPE_SGI:
	case EMULTYPE_ARC:
		/*
		 *  SGI and ARC emulation share a lot of code. (SGI is a special case of
		 *  "almost ARC".)
		 *
		 *  http://obsolete.majix.org/computers/sgi/iptable.shtml contains a pretty
		 *  detailed list of IP ("Inhouse Processor") model numbers.
		 */
		emul->machine_name = malloc(500);
		if (emul->machine_name == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		short_machine_name = malloc(500);
		if (short_machine_name == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}

		if (emul->emulation_type == EMULTYPE_SGI) {
			cpu->byte_order = EMUL_BIG_ENDIAN;
			sprintf(short_machine_name, "SGI-IP%i", emul->machine);
			sprintf(emul->machine_name, "SGI-IP%i", emul->machine);

			/*  Super-special case for IP24:  */
			if (emul->machine == 24)
				sprintf(short_machine_name, "SGI-IP22");

			/*  Special cases for IP20,22,24,26 memory offset:  */
			if (emul->machine == 20 || emul->machine == 22 ||
			    emul->machine == 24 || emul->machine == 26) {
				sgi_ram_offset = 128*1048576;
				dev_ram_init(mem, 0x00000000, 0x10000, DEV_RAM_MIRROR, sgi_ram_offset);
				dev_ram_init(mem, 0x00050000, sgi_ram_offset-0x50000, DEV_RAM_MIRROR, sgi_ram_offset + 0x50000);
			}

			/*  Special cases for IP28,30 memory offset:  */
			if (emul->machine == 28 || emul->machine == 30) {
				sgi_ram_offset = 0x20000000;	/*  TODO: length below should maybe not be 128MB?  */
				dev_ram_init(mem, 0x00000000, 128*1048576, DEV_RAM_MIRROR, sgi_ram_offset);
			}
		} else {
			cpu->byte_order = EMUL_LITTLE_ENDIAN;
			sprintf(short_machine_name, "ARC");
			sprintf(emul->machine_name, "ARC");
		}

		if (emul->emulation_type == EMULTYPE_SGI) {
			/*  TODO:  Other SGI machine types?  */
			switch (emul->machine) {
			case 19:
				strcat(emul->machine_name, " (Everest IP19)");
				dev_zs_init(cpu, mem, 0x1fbd9830, 0, 1);		/*  serial? netbsd?  */
				dev_scc_init(cpu, mem, 0x10086000, 0, emul->use_x11, 0, 8);	/*  serial? irix?  */

				dev_sgi_ip19_init(cpu, mem, 0x18000000);

				/*  Irix' <everest_du_init+0x130> reads this device:  */
				dev_random_init(mem, 0x10006000, 16);

				/*  Irix' get_mpconf() looks for this:  (TODO)  */
				store_32bit_word(cpu, 0xa0000000 + 0x3000,
				    0xbaddeed2);

				/*  Memory size, not 4096 byte pages, but 256 bytes?  (16 is size of kernel... approx)  */
				store_32bit_word(cpu, 0xa0000000 + 0x26d0,
				    30000);  /* (emul->physical_ram_in_mb - 16) * (1048576 / 256));  */

				break;
			case 20:
				strcat(emul->machine_name, " (Indigo)");

				/*
				 *  Guesses based on NetBSD 2.0 beta, 20040606.
				 *
				 *  int0 at mainbus0 addr 0x1fb801c0: bus 1MHz, CPU 2MHz
				 *  imc0 at mainbus0 addr 0x1fa00000: revision 0
				 *  gio0 at imc0
				 *  unknown GIO card (product 0x00 revision 0x00) at gio0 slot 0 addr 0x1f400000 not configured
				 *  unknown GIO card (product 0x00 revision 0x00) at gio0 slot 1 addr 0x1f600000 not configured
				 *  unknown GIO card (product 0x00 revision 0x00) at gio0 slot 2 addr 0x1f000000 not configured
				 *  hpc0 at gio0 addr 0x1fb80000: SGI HPC1
				 *  zsc0 at hpc0 offset 0xd10   (channels 0 and 1, channel 1 for console)
				 *  zsc1 at hpc0 offset 0xd00   (2 channels)
				 *  sq0 at hpc0 offset 0x100: SGI Seeq 80c03
				 *  wdsc0 at hpc0 offset 0x11f
				 *  dpclock0 at hpc0 offset 0xe00
				 */

				/*  int0 at mainbus0 addr 0x1fb801c0  */
				sgi_ip20_data = dev_sgi_ip20_init(cpu, mem, DEV_SGI_IP20_BASE);

				/*  imc0 at mainbus0 addr 0x1fa00000: revision 0:  TODO (or in dev_sgi_ip20?)  */

				dev_zs_init(cpu, mem, 0x1fbd9830, 0, 1);

				/*  This is the zsc0 reported by NetBSD:  TODO: irqs  */
				dev_zs_init(cpu, mem, 0x1fb80d10, 0, 1);	/*  zsc0  */
				dev_zs_init(cpu, mem, 0x1fb80d00, 0, 1);	/*  zsc1  */

				/*  WDSC SCSI controller:  */
				dev_wdsc_init(cpu, mem, 0x1fb8011f, 0, 0);

				/*  Return memory read errors so that hpc1 and hpc2 are not detected:  */
				dev_unreadable_init(mem, 0x1fb00000, 0x10000);		/*  hpc1  */
				dev_unreadable_init(mem, 0x1f980000, 0x10000);		/*  hpc2  */

				/*  Return nothing for gio slots 0, 1, and 2:  */
				dev_unreadable_init(mem, 0x1f400000, 0x1000);	/*  gio0 slot 0  */
				dev_unreadable_init(mem, 0x1f600000, 0x1000);	/*  gio0 slot 1  */
				dev_unreadable_init(mem, 0x1f000000, 0x1000);	/*  gio0 slot 2  */

				break;
			case 21:
				strcat(emul->machine_name, " (uknown SGI-IP21 ?)");	/*  TODO  */
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);

				dev_random_init(mem, 0x418000200ULL, 0x20000);

				break;
			case 22:
			case 24:
				if (emul->machine == 22) {
					strcat(emul->machine_name, " (Indy, Indigo2, Challenge S; Full-house)");
					sgi_ip22_data = dev_sgi_ip22_init(cpu, mem, 0x1fbd9000, 0);
				} else {
					strcat(emul->machine_name, " (Indy, Indigo2, Challenge S; Guiness)");
					sgi_ip22_data = dev_sgi_ip22_init(cpu, mem, 0x1fbd9880, 1);
				}

				dev_ram_init(mem, 0x88000000ULL,
				    128 * 1048576, DEV_RAM_MIRROR, 0x08000000);

				cpu->md_interrupt = sgi_ip22_interrupt;

				/*
				 *  According to NetBSD 1.6.2:
				 *
				 *  imc0 at mainbus0 addr 0x1fa00000, Revision 0
				 *  gio0 at imc0
				 *  hpc0 at gio0 addr 0x1fb80000: SGI HPC3
				 *  zsc0 at hpc0 offset 0x59830
				 *  zstty0 at zsc0 channel 1 (console i/o)
				 *  zstty1 at zsc0 channel 0
				 *  sq0 at hpc0 offset 0x54000: SGI Seeq 80c03	(Ethernet)
				 *  wdsc0 at hpc0 offset 0x44000: WD33C93 SCSI, rev=0, target 7
				 *  scsibus2 at wdsc0: 8 targets, 8 luns per target
				 *  dsclock0 at hpc0 offset 0x60000
				 *
				 *  According to Linux/IP22:
				 *  tty00 at 0xbfbd9830 (irq = 45) is a Zilog8530
				 *  tty01 at 0xbfbd9838 (irq = 45) is a Zilog8530
				 *
				 *  and according to NetBSD 2.0_BETA (20040606):
				 *
				 *  haltwo0 at hpc0 offset 0x58000: HAL2 revision 0.0.0
				 *  audio0 at haltwo0: half duplex
				 *
				 *  IRQ numbers are of the form 8 + x, where x = 0..31 for local0
				 *  interrupts, and 32..63 for local1.  + y*65 for "mappable".
				 */

				/*  zsc0 serial console.  */
				dev_zs_init(cpu, mem, 0x1fbd9830,
				    8 + 32 + 3 + 64*5, 1);

				/*  Not supported by NetBSD 1.6.2, but by 2.0_BETA:  */
				dev_pckbc_init(cpu, mem, 0x1fbd9840, PCKBC_8242,
				    0, 0, emul->use_x11);  /*  TODO: irq numbers  */

				/*  sq0: Ethernet.  TODO:  This should have irq_nr = 8 + 3  */
				/*  dev_sq_init...  */

	 			/*  wdsc0: SCSI  */
				dev_wdsc_init(cpu, mem, 0x1fbc4000, 0, 8 + 1);

				/*  wdsc1: SCSI  TODO: irq nr  */
				dev_wdsc_init(cpu, mem, 0x1fbcc000, 1, 8 + 1);

				/*  dsclock0: TODO:  possibly irq 8 + 33  */

				/*  Return memory read errors so that hpc1 and hpc2 are not detected:  */
				dev_unreadable_init(mem, 0x1fb00000, 0x10000);
				dev_unreadable_init(mem, 0x1f980000, 0x10000);

				/*  Similarly for gio slots 0, 1, and 2:  */
				dev_unreadable_init(mem, 0x1f400000, 0x1000);	/*  gio0 slot 0  */
				dev_unreadable_init(mem, 0x1f600000, 0x1000);	/*  gio0 slot 1  */
				dev_unreadable_init(mem, 0x1f000000, 0x1000);	/*  gio0 slot 2  */

				break;
			case 25:
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);
				strcat(emul->machine_name, " (Everest IP25)");

				 /*  serial? irix?  */
				dev_scc_init(cpu, mem,
				    0x400086000ULL, 0, emul->use_x11, 0, 8);

				/*  NOTE: ip19! (perhaps not really the same  */
				dev_sgi_ip19_init(cpu, mem,
				    0x18000000);

				/*
				 *  Memory size, not 4096 byte pages, but 256
				 *  bytes?  (16 is size of kernel... approx)
				 */
				store_32bit_word(cpu, 0xa0000000ULL + 0x26d0,
				    30000);  /* (emul->physical_ram_in_mb - 16)
						 * (1048576 / 256));  */

				break;
			case 26:
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);
				strcat(emul->machine_name, " (uknown SGI-IP26 ?)");	/*  TODO  */
				dev_zs_init(cpu, mem, 0x1fbd9830, 0, 1);		/*  serial? netbsd?  */
				break;
			case 27:
				strcat(emul->machine_name, " (Origin 200/2000, Onyx2)");
				/*  2 cpus per node  */

				/*
				 *  IRIX reads from the following addresses, so there's probably
				 *  something interesting there:
				 *
				 *  0x1fcffff0 <.MIPS.options+0x30>
				 *  0x19600000 <get_nasid+0x4>
				 *  0x190020d0 <get_cpuinfo+0x34>
				 */
				dev_zs_init(cpu, mem, 0x1fbd9830, 0, 1);	/*  serial??  */
				dev_sgi_nasid_init(mem, DEV_SGI_NASID_BASE);
				dev_sgi_cpuinfo_init(mem, DEV_SGI_CPUINFO_BASE);
				break;
			case 28:
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);
				strcat(emul->machine_name, " (Impact Indigo2 ?)");

				dev_random_init(mem, 0x1fbe0000ULL, 1);

				/*  Something at paddr 0x1880fb0000.  */

				break;
			case 30:
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);
				strcat(emul->machine_name, " (Octane)");

				/*  This is something unknown:  */
				dev_sgi_ip30_init(cpu, mem, 0x0ff00000);

				dev_ram_init(mem,    0xa0000000ULL,
				    128 * 1048576, DEV_RAM_MIRROR, 0x00000000);

				dev_ram_init(mem,    0x80000000ULL,
				    32 * 1048576, DEV_RAM_RAM, 0x00000000);

				/*
				 *  Something at paddr=1f022004: TODO
				 *  Something at paddr=813f0510 - paddr=813f0570 ?
				 *  Something at paddr=813f04b8
				 *  Something at paddr=f8000003c  used by Linux/Octane
				 *
				 *  16550 serial port at paddr=1f620178, addr mul 1
				 *  (Error messages are printed to this serial port by the PROM.)
				 *
				 *  There seems to also be a serial port at 1f620170. The "symmon"
				 *  program dumps something there, but it doesn't look like
				 *  readable text.  (TODO)
				 */
				dev_ns16550_init(cpu, mem, 0x1f620170, 0, 1,
				    emul->use_x11? 0 : 1);  /*  TODO: irq?  */
				dev_ns16550_init(cpu, mem, 0x1f620178, 0, 1,
				    0);  /*  TODO: irq?  */

				/*  MardiGras graphics:  */
				dev_sgi_mardigras_init(cpu, mem, 0x1c000000);

				break;
			case 32:
				strcat(emul->machine_name, " (O2)");

				/*  TODO:  Find out where the physical ram is actually located.  */
				dev_ram_init(mem,    0x40000000ULL,
				    32 * 1048576, DEV_RAM_RAM, 0);

#if 0
				dev_ram_init(mem,    0x20000000ULL,
				    32 * 1048576, DEV_RAM_RAM, 0);
				dev_ram_init(mem, 0x40000000000ULL,
				    32 * 1048576, DEV_RAM_RAM, 0);
				dev_ram_init(mem, 0x41000000000ULL,
				    32 * 1048576, DEV_RAM_RAM, 0);
#else
				dev_ram_init(mem,    0x20000000ULL,
				    128 * 1048576, DEV_RAM_MIRROR, 0x00000000);
				dev_ram_init(mem, 0x40000000000ULL,
				    128 * 1048576, DEV_RAM_MIRROR, 0x00000000);
				dev_ram_init(mem, 0x41000000000ULL,
				    128 * 1048576, DEV_RAM_MIRROR, 0x00000000);
#endif
				dev_ram_init(mem, 0x42000000000ULL, 128 * 1048576, DEV_RAM_MIRROR, 0x00000000);
				dev_ram_init(mem, 0x47000000000ULL, 128 * 1048576, DEV_RAM_MIRROR, 0x00000000);
				/*
				dev_ram_init(mem,    0x20000000ULL, 128 * 1048576, DEV_RAM_MIRROR, 0x00000000);
				dev_ram_init(mem,    0x40000000ULL, 128 * 1048576, DEV_RAM_MIRROR, 0x10000000);
				*/

				crime_data = dev_crime_init(cpu, mem, 0x14000000, 2, emul->use_x11);	/*  crime0  */
				dev_sgi_mte_init(mem, 0x15000000);			/*  mte ??? memory thing  */
				dev_sgi_gbe_init(cpu, mem, 0x16000000);	/*  gbe?  framebuffer?  */
				/*  0x17000000: something called 'VICE' in linux  */

				/*  mec0 ethernet at 0x1f280000  */			/*  mec0  */
				/*
				 *  A combination of NetBSD and Linux info:
				 *
				 *	1f000000	mace
				 *	1f080000	macepci
				 *	1f100000	vin1
				 *	1f180000	vin2
				 *	1f200000	vout
				 *	1f280000	enet
				 *	1f300000	perif:
				 *	  1f300000	  audio
				 *	  1f310000	  isa
				 *	    1f318000	    (accessed by Irix' pciio_pio_write64)
				 *	  1f320000	  kbdms
				 *	  1f330000	  i2c
				 *	  1f340000	  ust
				 *	1f380000	isa ext
				 * 	  1f390000	  com0 (serial)
				 * 	  1f398000	  com1 (serial)
				 * 	  1f3a0000	  mcclock0
				 */
				mace_data = dev_mace_init(mem, 0x1f310000, 2);					/*  mace0  */
				cpu->md_interrupt = sgi_ip32_interrupt;

				/*
				 *  IRQ mapping is really ugly.  TODO: fix
				 *
				 *  com0 at mace0 offset 0x390000 intr 4 intrmask 0x3f00000: ns16550a, working fifo
				 *  com1 at mace0 offset 0x398000 intr 4 intrmask 0xfc000000: ns16550a, working fifo
				 *  pckbc0 at mace0 offset 0x320000 intr 5 intrmask 0x0
				 *  mcclock0 at mace0 offset 0x3a0000 intrmask 0x0
				 *  macepci0 at mace0 offset 0x80000 intr 7 intrmask 0x0: rev 1
				 *
				 *  intr 4 = MACE_PERIPH_SERIAL
				 *  intr 5 = MACE_PERIPH_MISC
				 *  intr 7 = MACE_PCI_BRIDGE
				 */

				dev_pckbc_init(cpu, mem, 0x1f320000,
				    PCKBC_8242, 0x200 + MACE_PERIPH_MISC,
				    0x800 + MACE_PERIPH_MISC, emul->use_x11);
							/*  keyb+mouse (mace irq numbers)  */

				dev_sgi_ust_init(mem, 0x1f340000);					/*  ust?  */

				dev_ns16550_init(cpu, mem, 0x1f390000,
				    (1<<20) + MACE_PERIPH_SERIAL, 0x100,
				    emul->use_x11? 0 : 1);	/*  com0  */
				dev_ns16550_init(cpu, mem, 0x1f398000,
				    (1<<26) + MACE_PERIPH_SERIAL, 0x100,
				    0);				/*  com1  */

				dev_mc146818_init(cpu, mem, 0x1f3a0000, (1<<8) + MACE_PERIPH_MISC, MC146818_SGI, 0x40);  /*  mcclock0  */
				dev_zs_init(cpu, mem, 0x1fbd9830, 0, 1);	/*  serial??  */

				/*
				 *  PCI devices:   (according to NetBSD's GENERIC config file for sgimips)
				 *
				 *	ne*             at pci? dev ? function ?
				 *	ahc0            at pci0 dev 1 function ?
				 *	ahc1            at pci0 dev 2 function ?
				 */

				pci_data = dev_macepci_init(mem, 0x1f080000, MACE_PCI_BRIDGE);	/*  macepci0  */
				/*  bus_pci_add(cpu, pci_data, mem, 0, 0, 0, pci_ne2000_init, pci_ne2000_rr);  TODO  */
/*				bus_pci_add(cpu, pci_data, mem, 0, 1, 0, pci_ahc_init, pci_ahc_rr);  */
				/*  bus_pci_add(cpu, pci_data, mem, 0, 2, 0, pci_ahc_init, pci_ahc_rr);  */

				break;
			case 35:
				strcat(emul->machine_name, " (Origin 3000)");
				/*  4 cpus per node  */

				dev_zs_init(cpu, mem, 0x1fbd9830, 0, 1);
				break;
			case 53:
				strcat(emul->machine_name, " (Origin 350)");
				/*
				 *  According to http://kumba.drachentekh.net/xml/myguide.html
				 *  Origin 350, Tezro IP53 R16000
				 */
				break;
			default:
				fatal("unimplemented SGI machine type IP%i\n",
				    emul->machine);
				exit(1);
			}
		} else {
			switch (emul->machine) {

			case MACHINE_ARC_NEC_RD94:
			case MACHINE_ARC_NEC_R94:
				/*
				 *  "NEC-RD94" (NEC RISCstation 2250)
				 *  "NEC-R94" (NEC RISCstation 2200)
				 */

				if (emul->machine == MACHINE_ARC_NEC_RD94)
					strcat(emul->machine_name, " (NEC-RD94, NEC RISCstation 2250)");
				else
					strcat(emul->machine_name, " (NEC-R94; NEC RISCstation 2200)");

				/*  TODO:  sync devices and component tree  */

				pci_data = dev_rd94_init(cpu,
				    mem, 0x2000000000ULL, 0);
				dev_mc146818_init(cpu, mem,
				    0x2000004000ULL, 0, MC146818_ARC_NEC, 1);
				dev_pckbc_init(cpu, mem,
				    0x2000005000ULL, PCKBC_8042, 0, 0,
				    emul->use_x11);
				dev_ns16550_init(cpu, mem, 0x2000006000ULL,
				    3, 1, emul->use_x11? 0 : 1);  /*  com0  */
				dev_ns16550_init(cpu, mem, 0x2000007000ULL,
				    0, 1, 0);			  /*  com1  */
				/*  lpt at 0x2000008000  */

				/*  fdc  */
				dev_fdc_init(mem, 0x200000c000ULL, 0);

				/*  PCI devices:  (NOTE: bus must be 0, device must be 3, 4, or 5, for NetBSD to accept interrupts)  */
				bus_pci_add(cpu, pci_data, mem, 0, 3, 0, pci_dec21030_init, pci_dec21030_rr);	/*  tga graphics  */
				break;

			case MACHINE_ARC_NEC_R98:
				/*
				 *  "NEC-R98" (NEC RISCserver 4200)
				 *
				 *  According to http://mail-index.netbsd.org/port-arc/2004/02/01/0001.html:
				 *
				 *  Network adapter at "start: 0x 0 18600000, length: 0x1000, level: 4, vector: 9"
				 *  Disk at "start: 0x 0 18c103f0, length: 0x1000, level: 5, vector: 6"
				 *  Keyboard at "start: 0x 0 18c20060, length: 0x1000, level: 5, vector: 3"
				 *  Serial at "start: 0x 0 18c103f8, length: 0x1000, level: 5, vector: 4"
				 *  Serial at "start: 0x 0 18c102f8, length: 0x1000, level: 5, vector: 4"
				 *  Parallel at "start: 0x 0 18c10278, length: 0x1000, level: 5, vector: 5"
				 */

				strcat(emul->machine_name, " (NEC-R98; NEC RISCserver 4200)");

				break;

			case MACHINE_ARC_PICA:
				/*
				 *  "PICA-61"
				 *
				 *  Something at 0x60000003b4, and something at 0x90000000070.
				 *
				 *  OpenBSD/arc seems to try to draw text onto a "VGA text"
				 *  like device at 0x100000b0000 or 0x100000b8000.
				 *
				 *  According to NetBSD 1.6.2:
				 *
				 *  jazzio0 at mainbus0
				 *  timer0 at jazzio0 addr 0xe0000228
				 *  mcclock0 at jazzio0 addr 0xe0004000: mc146818 or compatible
				 *  lpt at jazzio0 addr 0xe0008000 intr 0 not configured
				 *  fdc at jazzio0 addr 0xe0003000 intr 1 not configured
				 *  MAGNUM at jazzio0 addr 0xe000c000 intr 2 not configured
				 *  ALI_S3 at jazzio0 addr 0xe0800000 intr 3 not configured
				 *  sn0 at jazzio0 addr 0xe0001000 intr 4: SONIC Ethernet
				 *  sn0: Ethernet address 69:6a:6b:6c:00:00
				 *  asc0 at jazzio0 addr 0xe0002000 intr 5: NCR53C94, target 0
				 *  pckbd at jazzio0 addr 0xe0005000 intr 6 not configured
				 *  pms at jazzio0 addr 0xe0005000 intr 7 not configured
				 *  com0 at jazzio0 addr 0xe0006000 intr 8: ns16550a, working fifo
				 *  com at jazzio0 addr 0xe0007000 intr 9 not configured
				 *  jazzisabr0 at mainbus0
				 *  isa0 at jazzisabr0 isa_io_base 0xe2000000 isa_mem_base 0xe3000000
				 */

				strcat(emul->machine_name, " (Acer PICA-61)");

				dev_ram_init(mem, 0x800000000ULL,
				    0x100000000ULL, DEV_RAM_MIRROR,
				    0x2000000000ULL);
				dev_ram_init(mem, 0x6000000000ULL,
				    0x100000000ULL, DEV_RAM_MIRROR,
				    0x2000000000ULL);
				dev_ram_init(mem, 0x10000000000ULL,
				    0x100000000ULL, DEV_RAM_MIRROR,
				    0x2000000000ULL);

				pica_data = dev_pica_init(
				    cpu, mem, 0x2000000000ULL);
				cpu->md_interrupt = pica_interrupt;

				dev_vga_init(cpu, mem,
				    0x20000b8000ULL, 0x20000003d0ULL,
				    ARC_CONSOLE_MAX_X, ARC_CONSOLE_MAX_Y);

				dev_asc_init(cpu, mem,
				    0x2000002000ULL, 8 + 5, NULL, DEV_ASC_PICA,
				    dev_pica_dma_controller, pica_data);

				dev_mc146818_init(cpu, mem,
				    0x2000004000ULL, 2, MC146818_ARC_PICA, 1);

				dev_pckbc_init(cpu, mem, 0x2000005000ULL,
				    PCKBC_PICA, 8 + 6, 8 + 7, emul->use_x11);

				dev_ns16550_init(cpu, mem,
				    0x2000006000ULL, 8 + 8, 1,
				    emul->use_x11? 0 : 1);
				dev_ns16550_init(cpu, mem,
				    0x2000007000ULL, 8 + 9, 1, 0);

				break;

			case MACHINE_ARC_DESKTECH_TYNE:
				/*
				 *  "Deskstation Tyne" (?)
				 *
				 *  TODO
				 *
				 *  0x100000b0000 (or 100000b8000?) is VGA "text" framebuffer.
				 *  0x900000003b5 = cursor control when outputing to the
				 *  framebuffer.
				 *  0x90000000064 written by kbd_cmd() in NetBSD.
				 */

				strcat(emul->machine_name, " (Deskstation Tyne)");

				dev_vga_init(cpu, mem, 0x100000b8000ULL,
				    0x900000003d0ULL,
				    ARC_CONSOLE_MAX_X, ARC_CONSOLE_MAX_Y);

				dev_ns16550_init(cpu, mem, 0x900000003f8ULL,
				    0, 1, emul->use_x11? 0 : 1);
				dev_ns16550_init(cpu, mem, 0x900000002f8ULL,
				    0, 1, 0);
				dev_ns16550_init(cpu, mem, 0x900000003e8ULL,
				    0, 1, 0);
				dev_ns16550_init(cpu, mem, 0x900000002e8ULL,
				    0, 1, 0);

				dev_mc146818_init(cpu, mem,
				    0x90000000070ULL, 2, MC146818_PC_CMOS, 1);

#if 0
				dev_wdc_init(cpu, mem, 0x900000001f0ULL, 0, 0);
				dev_wdc_init(cpu, mem, 0x90000000170ULL, 0, 2);
#endif
				/*  PC kbd  */
				dev_pckbc_init(cpu, mem, 0x90000000060ULL,
				    PCKBC_8042, 0, 0, emul->use_x11);

				break;

			case MACHINE_ARC_JAZZ:
				/*
				 *  "Microsoft-Jazz", "MIPS Magnum"
				 *
				 *  timer0 at jazzio0 addr 0xe0000228
				 *  mcclock0 at jazzio0 addr 0xe0004000: mc146818 or compatible
				 *  lpt at jazzio0 addr 0xe0008000 intr 0 not configured
				 *  fdc at jazzio0 addr 0xe0003000 intr 1 not configured
				 *  MAGNUM at jazzio0 addr 0xe000c000 intr 2 not configured
				 *  VXL at jazzio0 addr 0xe0800000 intr 3 not configured
				 *  sn0 at jazzio0 addr 0xe0001000 intr 4: SONIC Ethernet
				 *  sn0: Ethernet address 69:6a:6b:6c:00:00
				 *  asc0 at jazzio0 addr 0xe0002000 intr 5: NCR53C94, target 0
				 *  scsibus0 at asc0: 8 targets, 8 luns per target
				 *  pckbd at jazzio0 addr 0xe0005000 intr 6 not configured
				 *  pms at jazzio0 addr 0xe0005000 intr 7 not configured
				 *  com0 at jazzio0 addr 0xe0006000 intr 8: ns16550a, working fifo
				 *  com at jazzio0 addr 0xe0007000 intr 9 not configured
				 *  jazzisabr0 at mainbus0
				 *  isa0 at jazzisabr0 isa_io_base 0xe2000000 isa_mem_base 0xe3000000
				 */

				strcat(emul->machine_name, " (Microsoft Jazz, MIPS Magnum)");

				dev_mc146818_init(cpu, mem,
				    0x2000004000ULL, 2, MC146818_ARC_PICA, 1);

				dev_ns16550_init(cpu, mem, 0x2000006000ULL,
				    0, 1, emul->use_x11? 0 : 1);
				dev_ns16550_init(cpu, mem, 0x2000007000ULL,
				    0, 1, 0);

				break;

			default:
				fatal("Unimplemented ARC machine type %i\n",
				    emul->machine);
				exit(1);
			}
		}

		/*
		 *  This is important:  :-)
		 *
		 *  TODO:  There should not be any use of
		 *  ARCBIOS before this statement.
		 */
		if (arc_wordlen == sizeof(uint64_t))
			arcbios_set_64bit_mode(1);

		if (emul->physical_ram_in_mb < 16)
			fprintf(stderr, "WARNING! The ARC platform specification doesn't allow less than 16 MB of RAM. Continuing anyway.\n");

		memset(&arcbios_sysid, 0, sizeof(arcbios_sysid));
		if (emul->emulation_type == EMULTYPE_SGI) {
			/*  Vendor ID, max 8 chars:  */
			strncpy(arcbios_sysid.VendorId,  "SGI", 3);
			switch (emul->machine) {
			case 22:
				strncpy(arcbios_sysid.ProductId,
				    "87654321", 8);	/*  some kind of ID?  */
				break;
			case 32:
				strncpy(arcbios_sysid.ProductId, "8", 1);
				    /*  6 or 8 (?)  */
				break;
			default:
				snprintf(arcbios_sysid.ProductId, 8, "IP%i",
				    emul->machine);
			}
		} else {
			switch (emul->machine) {
			case MACHINE_ARC_NEC_RD94:
				strncpy(arcbios_sysid.VendorId,  "NEC W&S", 8);	/*  NOTE: max 8 chars  */
				strncpy(arcbios_sysid.ProductId, "RD94", 4);	/*  NOTE: max 8 chars  */
				break;
			case MACHINE_ARC_PICA:
				strncpy(arcbios_sysid.VendorId,  "MIPS MAG", 8);/*  NOTE: max 8 chars  */
				strncpy(arcbios_sysid.ProductId, "ijkl", 4);	/*  NOTE: max 8 chars  */
				break;
			case MACHINE_ARC_NEC_R94:
				strncpy(arcbios_sysid.VendorId,  "NEC W&S", 8);	/*  NOTE: max 8 chars  */
				strncpy(arcbios_sysid.ProductId, "ijkl", 4);	/*  NOTE: max 8 chars  */
				break;
			case MACHINE_ARC_DESKTECH_TYNE:
				strncpy(arcbios_sysid.VendorId,  "DESKTECH", 8);/*  NOTE: max 8 chars  */
				strncpy(arcbios_sysid.ProductId, "ijkl", 4);	/*  NOTE: max 8 chars  */
				break;
			case MACHINE_ARC_JAZZ:
				strncpy(arcbios_sysid.VendorId,  "MIPS MAG", 8);/*  NOTE: max 8 chars  */
				strncpy(arcbios_sysid.ProductId, "ijkl", 4);	/*  NOTE: max 8 chars  */
				break;
			case MACHINE_ARC_NEC_R98:
				strncpy(arcbios_sysid.VendorId,  "NEC W&S", 8);	/*  NOTE: max 8 chars  */
				strncpy(arcbios_sysid.ProductId, "R98", 4);	/*  NOTE: max 8 chars  */
				break;
			}
		}
		store_buf(cpu, SGI_SYSID_ADDR, (char *)&arcbios_sysid, sizeof(arcbios_sysid));

		memset(&arcbios_dsp_stat, 0, sizeof(arcbios_dsp_stat));
		/*  TODO:  get 80 and 24 from the current terminal settings?  */
		store_16bit_word_in_host(cpu, (unsigned char *)&arcbios_dsp_stat.CursorXPosition, 1);
		store_16bit_word_in_host(cpu, (unsigned char *)&arcbios_dsp_stat.CursorYPosition, 1);
		store_16bit_word_in_host(cpu, (unsigned char *)&arcbios_dsp_stat.CursorMaxXPosition, ARC_CONSOLE_MAX_X);
		store_16bit_word_in_host(cpu, (unsigned char *)&arcbios_dsp_stat.CursorMaxYPosition, ARC_CONSOLE_MAX_Y);
		arcbios_dsp_stat.ForegroundColor = 7;
		arcbios_dsp_stat.HighIntensity = 15;
		store_buf(cpu, ARC_DSPSTAT_ADDR, (char *)&arcbios_dsp_stat, sizeof(arcbios_dsp_stat));

		/*
		 *  The first 16 MBs of RAM are simply reserved... this simplifies things a lot.
		 *  If there's more than 512MB of RAM, it has to be split in two, according to
		 *  the ARC spec.  This code creates a number of chunks of at most 512MB each.
		 *
		 *  NOTE:  The region of physical address space between 0x10000000 and 0x1fffffff
		 *  (256 - 512 MB) is usually occupied by memory mapped devices, so that portion is "lost".
		 */
		mem_base = 16 * 1048576 / 4096;
		mem_count = emul->physical_ram_in_mb <= 256? emul->physical_ram_in_mb : 256;
		mem_count = (mem_count - 16) * 1048576 / 4096;

		mem_base += (sgi_ram_offset / 4096);

		/*  TODO: Make this nicer!  */

		if (arc_wordlen == sizeof(uint64_t)) {
			memset(&arcbios_mem64, 0, sizeof(arcbios_mem64));
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem64.Type, 
			    emul->emulation_type == EMULTYPE_SGI? 3 : 2);
			store_64bit_word_in_host(cpu, (unsigned char *)&arcbios_mem64.BasePage, mem_base);
			store_64bit_word_in_host(cpu, (unsigned char *)&arcbios_mem64.PageCount, mem_count);
			store_buf(cpu, ARC_MEMDESC_ADDR, (char *)&arcbios_mem64, sizeof(arcbios_mem64));

			mem_mb_left = emul->physical_ram_in_mb - 512;
			mem_base = 512 * (1048576 / 4096);
			mem_base += (sgi_ram_offset / 4096);
			mem_bufaddr = ARC_MEMDESC_ADDR + sizeof(arcbios_mem64);
			while (mem_mb_left > 0) {
				mem_count = (mem_mb_left <= 512? mem_mb_left : 512) * (1048576 / 4096);

				memset(&arcbios_mem64, 0, sizeof(arcbios_mem64));
				store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem64.Type,
				    emul->emulation_type == EMULTYPE_SGI? 3 : 2);
				store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem64.BasePage, mem_base);
				store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem64.PageCount, mem_count);

				store_buf(cpu, mem_bufaddr, (char *)&arcbios_mem64, sizeof(arcbios_mem64));
				mem_bufaddr += sizeof(arcbios_mem64);

				mem_mb_left -= 512;
				mem_base += 512 * (1048576 / 4096);
			}

			/*  End of memory descriptors:  (pagecount = zero)  */
			memset(&arcbios_mem64, 0, sizeof(arcbios_mem64));
			store_buf(cpu, mem_bufaddr, (char *)&arcbios_mem64, sizeof(arcbios_mem64));
		} else {
			memset(&arcbios_mem, 0, sizeof(arcbios_mem));
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem.Type, emul->emulation_type == EMULTYPE_SGI? 3 : 2);
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem.BasePage, mem_base);
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem.PageCount, mem_count);
			store_buf(cpu, ARC_MEMDESC_ADDR, (char *)&arcbios_mem, sizeof(arcbios_mem));

			mem_mb_left = emul->physical_ram_in_mb - 512;
			mem_base = 512 * (1048576 / 4096);
			mem_base += (sgi_ram_offset / 4096);
			mem_bufaddr = ARC_MEMDESC_ADDR + sizeof(arcbios_mem);
			while (mem_mb_left > 0) {
				mem_count = (mem_mb_left <= 512? mem_mb_left : 512) * (1048576 / 4096);

				memset(&arcbios_mem, 0, sizeof(arcbios_mem));
				store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem.Type, emul->emulation_type == EMULTYPE_SGI? 3 : 2);
				store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem.BasePage, mem_base);
				store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_mem.PageCount, mem_count);

				store_buf(cpu, mem_bufaddr,
				    (char *)&arcbios_mem, sizeof(arcbios_mem));
				mem_bufaddr += sizeof(arcbios_mem);

				mem_mb_left -= 512;
				mem_base += 512 * (1048576 / 4096);
			}

			/*  End of memory descriptors:  (pagecount = zero)  */
			memset(&arcbios_mem, 0, sizeof(arcbios_mem));
			store_buf(cpu, mem_bufaddr, (char *)&arcbios_mem, sizeof(arcbios_mem));
		}

		/*
		 *  Components:   (this is an example of what a system could look like)
		 *
		 *  [System]
		 *	[CPU]  (one for each cpu)
		 *	    [FPU]  (one for each cpu)
		 *	    [CPU Caches]
		 *	[Memory]
		 *	[Ethernet]
		 *	[Serial]
		 *	[SCSI]
		 *	    [Disk]
		 *
		 *  Here's a good list of what hardware is in different IP-models:
		 *  http://www.linux-mips.org/archives/linux-mips/2001-03/msg00101.html
		 */

		if (emul->machine_name == NULL)
			fatal("ERROR: machine_name == NULL\n");
		if (short_machine_name == NULL)
			fatal("ERROR: short_machine_name == NULL\n");

		switch (emul->emulation_type) {
		case EMULTYPE_SGI:
			system = arcbios_addchild_manual(cpu, COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
			    0, 1, 2, 0, 0xffffffff, short_machine_name, 0  /*  ROOT  */);
			break;
		default:
			/*  ARC:  */
			switch (emul->machine) {
			case MACHINE_ARC_NEC_RD94:
				system = arcbios_addchild_manual(cpu, COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
				    0, 1, 2, 0, 0xffffffff, "NEC-RD94", 0  /*  ROOT  */);
				break;
			case MACHINE_ARC_PICA:
				system = arcbios_addchild_manual(cpu, COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
				    0, 1, 2, 0, 0xffffffff, "PICA-61", 0  /*  ROOT  */);
				break;
			case MACHINE_ARC_NEC_R94:
				system = arcbios_addchild_manual(cpu, COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
				    0, 1, 2, 0, 0xffffffff, "NEC-R94", 0  /*  ROOT  */);
				break;
			case MACHINE_ARC_DESKTECH_TYNE:
				system = arcbios_addchild_manual(cpu, COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
				    0, 1, 2, 0, 0xffffffff, "DESKTECH-TYNE", 0  /*  ROOT  */);
				break;
			case MACHINE_ARC_JAZZ:
				system = arcbios_addchild_manual(cpu, COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
				    0, 1, 2, 0, 0xffffffff, "Microsoft-Jazz", 0  /*  ROOT  */);
				break;
			case MACHINE_ARC_NEC_R98:
				system = arcbios_addchild_manual(cpu, COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
				    0, 1, 2, 0, 0xffffffff, "NEC-R98", 0  /*  ROOT  */);
				break;
			default:
				fatal("Unimplemented ARC machine type %i\n",
				    emul->machine);
				exit(1);
			}
		}


		/*
		 *  Common stuff for both SGI and ARC:
		 */
		debug("system = 0x%x\n", system);

		for (i=0; i<emul->ncpus; i++) {
			uint32_t cpuaddr, fpu, picache, pdcache, sdcache = 0;
			int cache_size, cache_line_size;
			unsigned int jj;
			char arc_cpu_name[100];
			char arc_fpc_name[105];

			strncpy(arc_cpu_name, emul->emul_cpu_name,
			    sizeof(arc_cpu_name));
			arc_cpu_name[sizeof(arc_cpu_name)-1] = 0;
			for (jj=0; jj<strlen(arc_cpu_name); jj++)
				if (arc_cpu_name[jj] >= 'a' && arc_cpu_name[jj] <= 'z')
					arc_cpu_name[jj] += ('A' - 'a');

			strcpy(arc_fpc_name, arc_cpu_name);
			strcat(arc_fpc_name, "FPC");

			cpuaddr = arcbios_addchild_manual(cpu, COMPONENT_CLASS_ProcessorClass, COMPONENT_TYPE_CPU,
			    0, 1, 2, i, 0xffffffff, arc_cpu_name, system);

			/*  TODO: Maybe this shouldn't be here?  */
			fpu = arcbios_addchild_manual(cpu, COMPONENT_CLASS_ProcessorClass, COMPONENT_TYPE_FPU,
			    0, 1, 2, 0, 0xffffffff, arc_fpc_name, cpuaddr);

			cache_size = DEFAULT_PCACHE_SIZE - 12;
			if (emul->cache_picache)
				cache_size = emul->cache_picache - 12;
			if (cache_size < 0)
				cache_size = 0;

			cache_line_size = DEFAULT_PCACHE_LINESIZE;
			if (emul->cache_picache_linesize)
				cache_line_size = emul->cache_picache_linesize;
			if (cache_line_size < 0)
				cache_line_size = 0;

			picache = arcbios_addchild_manual(cpu, COMPONENT_CLASS_CacheClass,
			    COMPONENT_TYPE_PrimaryICache, 0, 1, 2,
			    /*
			     *  Key bits:  0xXXYYZZZZ
			     *  XX is refill-size.
			     *  Cache line size is 1 << YY,
			     *  Cache size is 4KB << ZZZZ.
			     */
			    0x01000000 + (cache_line_size << 16) + cache_size,
				/*  32 bytes per line, default = 32 KB total  */
			    0xffffffff, NULL, cpuaddr);

			cache_size = DEFAULT_PCACHE_SIZE - 12;
			if (emul->cache_pdcache)
				cache_size = emul->cache_pdcache - 12;
			if (cache_size < 0)
				cache_size = 0;

			cache_line_size = DEFAULT_PCACHE_LINESIZE;
			if (emul->cache_pdcache_linesize)
				cache_line_size = emul->cache_pdcache_linesize;
			if (cache_line_size < 0)
				cache_line_size = 0;

			pdcache = arcbios_addchild_manual(cpu, COMPONENT_CLASS_CacheClass,
			    COMPONENT_TYPE_PrimaryDCache, 0, 1, 2,
			    /*
			     *  Key bits:  0xYYZZZZ
			     *  Cache line size is 1 << YY,
			     *  Cache size is 4KB << ZZZZ.
			     */
			    0x01000000 + (cache_line_size << 16) + cache_size,
				/*  32 bytes per line, default = 32 KB total  */
			    0xffffffff, NULL, cpuaddr);

			if (emul->cache_secondary >= 12) {
				cache_size = emul->cache_secondary - 12;

				cache_line_size = 6;	/*  64 bytes default  */
				if (emul->cache_secondary_linesize)
					cache_line_size = emul->cache_secondary_linesize;
				if (cache_line_size < 0)
					cache_line_size = 0;

				sdcache = arcbios_addchild_manual(cpu, COMPONENT_CLASS_CacheClass,
				    COMPONENT_TYPE_SecondaryDCache, 0, 1, 2,
				    /*
				     *  Key bits:  0xYYZZZZ
				     *  Cache line size is 1 << YY,
				     *  Cache size is 4KB << ZZZZ.
				     */
				    0x01000000 + (cache_line_size << 16) + cache_size,
					/*  64 bytes per line, default = 1 MB total  */
				    0xffffffff, NULL, cpuaddr);
			}

			debug("adding ARC components: cpu%i = 0x%x, "
			    "fpu%i = 0x%x, picache = 0x%x, pdcache = 0x%x",
			    i, cpuaddr, i, fpu, picache, pdcache);
			if (emul->cache_secondary >= 12)
				debug(", sdcache = 0x%x", sdcache);
			debug("\n");
		}


		/*
		 *  Other components:
		 *
		 *  TODO: How to build the component tree intermixed with
		 *  the rest of device initialization?
		 */

		if (emul->emulation_type == EMULTYPE_ARC &&
		    ( emul->machine == MACHINE_ARC_NEC_RD94 ||
		    emul->machine == MACHINE_ARC_NEC_R94 )) {
			/*  This DisplayController needs to be here, to allow NetBSD to use the TGA card:  */
			/*  Actually class COMPONENT_CLASS_ControllerClass, type COMPONENT_TYPE_DisplayController  */
			if (emul->use_x11)
				arcbios_addchild_manual(cpu, 4, 19,  0, 1, 2, 0, 0x0, "10110004", system);
		}

		if (emul->emulation_type == EMULTYPE_ARC &&
		    emul->machine == MACHINE_ARC_PICA) {
			uint32_t jazzbus;
			jazzbus = arcbios_addchild_manual(cpu,
			    3 /*  Adapter  */,
			    12 /* MultiFunctionAdapter */,
			    0, 1, 2, 0, 0xffffffff, "Jazz-Internal Bus",
			    system);

			/*
			 *  DisplayController, needed by NetBSD:
			 *  TODO: NetBSD still doesn't use it :(
			 */
			if (emul->use_x11)
				arcbios_addchild_manual(cpu,
				    4  /*  Controller  */,
				    19  /* Display controller */,
				    COMPONENT_FLAG_ConsoleOut |
					COMPONENT_FLAG_Output,
				    1, 2, 0, 0xffffffff, "ALI_S3",
				    jazzbus);
		}


		add_symbol_name(&emul->symbol_context,
		    ARC_FIRMWARE_ENTRIES, 0x10000, "[ARCBIOS entry]", 0);

		switch (arc_wordlen) {
		case sizeof(uint64_t):
			for (i=0; i<100; i++)
				store_64bit_word(cpu, ARC_FIRMWARE_VECTORS + i*8,
				    ARC_FIRMWARE_ENTRIES + i*8);
			for (i=0; i<100; i++)
				store_64bit_word(cpu, ARC_PRIVATE_VECTORS + i*8,
				    ARC_PRIVATE_ENTRIES + i*8);
			break;
		default:
			for (i=0; i<100; i++)
				store_32bit_word(cpu, ARC_FIRMWARE_VECTORS + i*4,
				    ARC_FIRMWARE_ENTRIES + i*4);
			for (i=0; i<100; i++)
				store_32bit_word(cpu, ARC_PRIVATE_VECTORS + i*4,
				    ARC_PRIVATE_ENTRIES + i*4);
		}

		/*  Hahaha, this is so ugly.  */
		/*  TODO: Make it less ugly by not hardcoding everything.  */

		switch (arc_wordlen) {
		case sizeof(uint64_t):
			/*  64-bit ARCBIOS SPB:  (TODO: This is just a guess)  */
			memset(&arcbios_spb_64, 0, sizeof(arcbios_spb_64));
			store_64bit_word_in_host(cpu, (unsigned char *)&arcbios_spb_64.SPBSignature, ARCBIOS_SPB_SIGNATURE);
			store_16bit_word_in_host(cpu, (unsigned char *)&arcbios_spb_64.Version, 1);
			store_16bit_word_in_host(cpu, (unsigned char *)&arcbios_spb_64.Revision, emul->emulation_type == EMULTYPE_SGI? 10 : 2);
			store_64bit_word_in_host(cpu, (unsigned char *)&arcbios_spb_64.FirmwareVector, ARC_FIRMWARE_VECTORS);
			/*  FirmwareVectorLength is a pointer to something?  */
			store_64bit_word_in_host(cpu, (unsigned char *)&arcbios_spb_64.FirmwareVectorLength, 0xa0000000ULL);	/*  ?  */
			store_64bit_word_in_host(cpu, (unsigned char *)&arcbios_spb_64.PrivateVector, ARC_PRIVATE_VECTORS);
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_spb_64.PrivateVectorLength, 100 * 4);	/*  ?  */
			store_buf(cpu, SGI_SPB_ADDR, (char *)&arcbios_spb_64,
			    sizeof(arcbios_spb_64));

			/*
			 *  Super-ugly test hack, to fool arcs_getenv() in
			 *  64-bit Irix:
			 *
			 *  The SPB is a bit less than 0x40 bytes long, but
			 *  Irix seems to use offsets after the standard SPB
			 *  data.  (TODO)
			 *
			 *  Then, at offset 0xf0 in this new structure, there
			 *  is a pointer to a function which Irix calls.  It
			 *  seems that this is a getenv-like function, so let's
			 *  call (32-bit) arcbios, and then return.
			 *
			 *  This is ugly.
			 */
			store_64bit_word(cpu, SGI_SPB_ADDR + 0x40,
			    0xffffffff80001400ULL);

			/*  0x50 is used by Linux/IP30:  */
			store_64bit_word(cpu, SGI_SPB_ADDR + 0x400 + 0x50, SGI_SPB_ADDR + 0x700);
			/*  0x90 is used by Linux/IP30:  */
			store_64bit_word(cpu, SGI_SPB_ADDR + 0x400 + 0x90, SGI_SPB_ADDR + 0x740);
			/*  0xd8 is used by Linux/IP30:  */
			store_64bit_word(cpu, SGI_SPB_ADDR + 0x400 + 0xd8, SGI_SPB_ADDR + 0x780);
			/*  0xf0 is used by Irix:  */
			store_64bit_word(cpu, SGI_SPB_ADDR + 0x400 + 0xf0, SGI_SPB_ADDR + 0x800);

			/*  getchild  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x700 +  0, (HI6_LUI << 26) + (2 << 16) + ((ARC_FIRMWARE_ENTRIES >> 16) & 0xffff));	/*  lui  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x700 +  4, (HI6_ORI << 26) + (2 << 21) + (2 << 16) + (ARC_FIRMWARE_ENTRIES & 0xffff));  /*  ori  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x700 +  8, (HI6_ADDIU << 26) + (2 << 21) + (2 << 16) + 0x28);  /*  addiu, 0x28 = getchild  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x700 + 12, SPECIAL_JR + (2 << 21));	/*  jr  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x700 + 16, 0);				/*  nop  */

			/*  getmemorydescriptor  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x740 +  0, (HI6_LUI << 26) + (2 << 16) + ((ARC_FIRMWARE_ENTRIES >> 16) & 0xffff));	/*  lui  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x740 +  4, (HI6_ORI << 26) + (2 << 21) + (2 << 16) + (ARC_FIRMWARE_ENTRIES & 0xffff));  /*  ori  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x740 +  8, (HI6_ADDIU << 26) + (2 << 21) + (2 << 16) + 0x48);  /*  addiu, 0x48 = getmemorydescriptor  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x740 + 12, SPECIAL_JR + (2 << 21));	/*  jr  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x740 + 16, 0);				/*  nop  */

			/*  write  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x780 +  0, (HI6_LUI << 26) + (2 << 16) + ((ARC_FIRMWARE_ENTRIES >> 16) & 0xffff));	/*  lui  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x780 +  4, (HI6_ORI << 26) + (2 << 21) + (2 << 16) + (ARC_FIRMWARE_ENTRIES & 0xffff));  /*  ori  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x780 +  8, (HI6_ADDIU << 26) + (2 << 21) + (2 << 16) + 0x6c);  /*  addiu, 0x6c = write  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x780 + 12, SPECIAL_JR + (2 << 21));	/*  jr  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x780 + 16, 0);				/*  nop  */

			/*  getenv  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x800 +  0, (HI6_LUI << 26) + (2 << 16) + ((ARC_FIRMWARE_ENTRIES >> 16) & 0xffff));	/*  lui  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x800 +  4, (HI6_ORI << 26) + (2 << 21) + (2 << 16) + (ARC_FIRMWARE_ENTRIES & 0xffff));  /*  ori  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x800 +  8, (HI6_ADDIU << 26) + (2 << 21) + (2 << 16) + 0x78);  /*  addiu, 0x78 = getenv  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x800 + 12, SPECIAL_JR + (2 << 21));	/*  jr  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0x800 + 16, 0);				/*  nop  */

			/*  This is similar, but used by Irix' arcs_nvram_tab() instead of arcs_getenv():  */
			store_64bit_word(cpu, SGI_SPB_ADDR + 0x50,
			    0xffffffff80001900ULL);
			store_64bit_word(cpu, SGI_SPB_ADDR + 0x900 + 0x8, SGI_SPB_ADDR + 0xa00);

			store_32bit_word(cpu, SGI_SPB_ADDR + 0xa00 +  0, (HI6_LUI << 26) + (2 << 16) + ((ARC_PRIVATE_ENTRIES >> 16) & 0xffff));	/*  lui  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0xa00 +  4, (HI6_ORI << 26) + (2 << 21) + (2 << 16) + (ARC_PRIVATE_ENTRIES & 0xffff));  /*  ori  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0xa00 +  8, (HI6_ADDIU << 26) + (2 << 21) + (2 << 16) + 0x04);  /*  addiu, 0x04 = get nvram  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0xa00 + 12, SPECIAL_JR + (2 << 21));	/*  jr  */
			store_32bit_word(cpu, SGI_SPB_ADDR + 0xa00 + 16, 0);				/*  nop  */

			break;
		default:	/*  32-bit  */
			/*  ARCBIOS SPB:  */
			memset(&arcbios_spb, 0, sizeof(arcbios_spb));
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_spb.SPBSignature, ARCBIOS_SPB_SIGNATURE);
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_spb.SPBLength, sizeof(arcbios_spb));
			store_16bit_word_in_host(cpu, (unsigned char *)&arcbios_spb.Version, 1);
			store_16bit_word_in_host(cpu, (unsigned char *)&arcbios_spb.Revision, emul->emulation_type == EMULTYPE_SGI? 10 : 2);
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_spb.FirmwareVector, ARC_FIRMWARE_VECTORS);
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_spb.FirmwareVectorLength, 100 * 4);	/*  ?  */
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_spb.PrivateVector, ARC_PRIVATE_VECTORS);
			store_32bit_word_in_host(cpu, (unsigned char *)&arcbios_spb.PrivateVectorLength, 100 * 4);	/*  ?  */
			store_buf(cpu, SGI_SPB_ADDR, (char *)&arcbios_spb, sizeof(arcbios_spb));
		}

		/*  Boot string in ARC format:  */
		/*  TODO: use actual SCSI id number  */
		if (emul->emulation_type == EMULTYPE_SGI)
			init_bootpath = "scsi(0)disk(0)rdisk(0)partition(0)\\";
		else
			init_bootpath = "scsi(0)disk(0)rdisk(0)partition(1)";

#if 0
			/*  Floppy?  */
			init_bootpath = "multi()disk()fdisk()";
#endif

		bootstr = malloc(strlen(init_bootpath) +
		    strlen(emul->boot_kernel_filename) + 1);
		strcpy(bootstr, init_bootpath);
		strcat(bootstr, emul->boot_kernel_filename);

		/*  Boot args., eg "-a"  */
		bootarg = emul->boot_string_argument;

		/*  argc, argv, envp in a0, a1, a2:  */
		cpu->gpr[GPR_A0] = 0;	/*  note: argc is increased later  */
		cpu->gpr[GPR_A1] = ARC_ARGV_START;
		cpu->gpr[GPR_A2] = ARC_ENV_POINTERS;

		/*  TODO:  not needed?  */
		cpu->gpr[GPR_SP] = emul->physical_ram_in_mb * 1048576 + 0x80000000 - 0x2080;

		/*
		 *  Add environment variables.  For each variable, add it
		 *  as a string using add_environment_string(), and add a
		 *  pointer to it to the ARC_ENV_POINTERS array.
		 */
		addr = ARC_ENV_STRINGS;
		addr2 = ARC_ENV_POINTERS;

		if (emul->use_x11) {
			if (emul->emulation_type == EMULTYPE_ARC) {
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "CONSOLEIN=multi()key()keyboard()console()", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "CONSOLEOUT=multi()video()monitor()console()", &addr);
			} else {
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "ConsoleIn=keyboard()", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "ConsoleOut=video()", &addr);

				/*  g for graphical mode. G for graphical mode
				    with SGI logo visible on Irix?  */
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "console=g", &addr);
			}
		} else {
			if (emul->emulation_type == EMULTYPE_ARC) {
				/*  TODO: serial console for ARC?  */
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "CONSOLEIN=multi()serial(0)", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "CONSOLEOUT=multi()serial(0)", &addr);
			} else {
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "ConsoleIn=serial(0)", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "ConsoleOut=serial(0)", &addr);

				/*  'd' or 'd2' in Irix, 'ttyS0' in Linux?  */
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "console=d", &addr);		/*  d2 = serial?  */
			}
		}

		if (emul->emulation_type == EMULTYPE_SGI) {
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "cpufreq=3", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "dbaud=9600", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "rbaud=9600", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "nogfxkbd=1", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "eaddr=10:20:30:40:50:60", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "verbose=istrue", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "showconfig=istrue", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "diagmode=v", &addr);

			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "SystemPartition=dksc (0,0,8)", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "OSLoadPartition=dksc (0,0,0)", &addr);
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "root=dks0d0s0", &addr);

			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "netaddr=10.0.0.1", &addr);
		} else {
			/*  ARC:  */
			char *tmp;
			tmp = malloc(strlen(bootarg) +
			    strlen("OSLOADOPTIONS=") + 2);
			sprintf(tmp, "OSLOADOPTIONS=%s", bootarg);
			store_pointer_and_advance(cpu, &addr2, addr,
			    arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, tmp, &addr);

			/*  TODO:  */
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, "OSLOADPARTITION=scsi(0)disk(0)rdisk(0)partition(1)", &addr);
		}


		/*  End the environment strings with an empty zero-terminated
		    string, and the envp array with a NULL pointer.  */
		add_environment_string(cpu, "", &addr);	/*  the end  */
		store_pointer_and_advance(cpu, &addr2,
		    0, arc_wordlen==sizeof(uint64_t));

		/*  Set up argc/argv:  */
		addr = ARC_ARGV_START;
		addr2 = ARC_ENV_POINTERS;

		/*  bootstr:  */
		store_string(cpu, ARC_ARGV_START + 0x100, bootstr);
		cpu->gpr[GPR_A0] ++;
		store_pointer_and_advance(cpu, &addr,
		    ARC_ARGV_START + 0x100, arc_wordlen==sizeof(uint64_t));

		/*  bootarg:  */
		if (bootarg[0] != '\0') {
			store_string(cpu, ARC_ARGV_START + 0x200, bootarg);
			cpu->gpr[GPR_A0] ++;
			store_pointer_and_advance(cpu, &addr,
			    ARC_ARGV_START + 0x200,
			    arc_wordlen==sizeof(uint64_t));
		}

		if (emul->emulation_type == EMULTYPE_SGI) {
			/*
			 *  See http://guinness.cs.stevens-tech.edu/sgidocs/SGI_EndUser/books/IRIX_EnvVar/sgi_html/ch02.html
			 *  for more options.  It seems that on SGI machines,
			 *  _ALL_ environment variables are passed on the
			 *  command line, but NOT on generic ARC.
			 */

			/*  Copy envp into end of argv:  */
			/*  TODO: Is this actually correct? Maybe not.  */
		}

		/*  End of arguments, an extra NULL pointer:  */
		store_pointer_and_advance(cpu, &addr,
		    0, arc_wordlen==sizeof(uint64_t));

		break;

	case EMULTYPE_MESHCUBE:
		emul->machine_name = "MeshCube";

		if (emul->physical_ram_in_mb != 64)
			fprintf(stderr, "WARNING! MeshCubes are supposed to have exactly 64 MB RAM. Continuing anyway.\n");
		if (emul->use_x11)
			fprintf(stderr, "WARNING! MeshCube with -X is meaningless. Continuing anyway.\n");

		/*  First of all, the MeshCube has an Au1500 in it:  */
		cpu->md_interrupt = au1x00_interrupt;
		au1x00_ic_data = dev_au1x00_init(cpu, mem);

		/*
		 *  TODO:  Which non-Au1500 devices, and at what addresses?
		 *
		 *  "4G Systems MTX-1 Board" at ?
		 *	1017fffc, 14005004, 11700000, 11700008, 11900014,
		 *	1190002c, 11900100, 11900108, 1190010c,
		 *	10400040 - 10400074,
		 *	14001000 (possibly LCD?)
		 *	11100028 (possibly ttySx?)
		 *
		 *  "usb_ohci=base:0x10100000,len:0x100000,irq:26"
		 */

		dev_random_init(mem, 0x1017fffc, 4);

		/*
		 *  TODO:  A Linux kernel wants "memsize" from somewhere... I
		 *  haven't found any docs on how it is used though.
		 */

		cpu->gpr[GPR_A0] = 1;
		cpu->gpr[GPR_A1] = 0xa0001000ULL;
		store_32bit_word(cpu, cpu->gpr[GPR_A1],
		    0xa0002000ULL);
		store_string(cpu, 0xa0002000ULL, "something=somethingelse");

		cpu->gpr[GPR_A2] = 0xa0003000ULL;
		store_string(cpu, 0xa0002000ULL, "hello=world\n");

		break;

	case EMULTYPE_NETGEAR:
		emul->machine_name = "NetGear WG602";

		if (emul->use_x11)
			fprintf(stderr, "WARNING! NetGear with -X is meaningless. Continuing anyway.\n");
		if (emul->physical_ram_in_mb != 16)
			fprintf(stderr, "WARNING! Real NetGear WG602 boxes have exactly 16 MB RAM. Continuing anyway.\n");

		/*
		 *  Lots of info about the IDT 79RC 32334
		 *  http://www.idt.com/products/pages/Integrated_Processors-79RC32334.html
		 */

		dev_8250_init(cpu, mem, 0x18000800, 0, 4);

		break;

	case EMULTYPE_WRT54G:
		emul->machine_name = "Linksys WRT54G";

		if (emul->use_x11)
			fprintf(stderr, "WARNING! Linksys WRT54G with -X is meaningless. Continuing anyway.\n");

		/*  200 MHz default  */
		if (emul->emulated_hz == 0)
			emul->emulated_hz = 200000000;

		/*
		 *  Linux should be loaded at 0x80001000.
		 *  RAM: 16 or 32 MB, Flash RAM: 4 or 8 MB.
		 *  http://www.bumpclub.ee/~jaanus/wrt54g/vana/minicom.cap:
		 *
		 *  Starting program at 0x80001000
		 *  CPU revision is: 00029007
		 *  Primary instruction cache 8kb, linesize 16 bytes (2 ways)
		 *  Primary data cache 4kb, linesize 16 bytes (2 ways)
		 *   memory: 01000000 @ 00000000 (usable)
		 *  Kernel command line: root=/dev/mtdblock2 rootfstype=squashfs init=/etc/preinit noinitrd console=ttyS0,115200
		 *  CPU: BCM4712 rev 1 at 200 MHz
		 *  Calibrating delay loop... 199.47 BogoMIPS
		 *  ttyS00 at 0xb8000300 (irq = 3) is a 16550A
		 *  ttyS01 at 0xb8000400 (irq = 0) is a 16550A
		 *  Flash device: 0x400000 at 0x1c000000
		 *  ..
		 */

		/*  TODO: What should the initial register contents be?  */
#if 1
{
int i;
for (i=0; i<32; i++)
		cpu->gpr[i] = 0x01230000 + (i << 8) + 0x55;
}
#endif

		break;

	default:
		fatal("Unknown emulation type %i\n", emul->emulation_type);
		exit(1);
	}

	if (emul->machine_name != NULL)
		debug("machine: %s", emul->machine_name);

	if (emul->emulated_hz > 0)
		debug(" (%.2f MHz)", (float)emul->emulated_hz / 1000000);
	debug("\n");

	if (emul->emulated_hz < 1)
		emul->emulated_hz = 1500000;

	if (bootstr != NULL) {
		debug("bootstring%s: %s", bootarg==NULL? "": "(+bootarg)",
		    bootstr);
		if (bootarg != NULL)
			debug(" %s", bootarg);
		debug("\n");
	}
}

