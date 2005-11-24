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
 *  $Id: dev_dec21143.c,v 1.13 2005-11-24 18:52:15 debug Exp $
 *
 *  DEC 21143 ("Tulip") ethernet controller. Implemented from Intel document
 *  278074-001 ("21143 PC/CardBus 10/100Mb/s Ethernet LAN Controller") and by
 *  reverse-engineering OpenBSD and NetBSD sources.
 *
 *
 *  TODO: Lots of stuff.
 *
 *	o)  Endianness for descriptors...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"

#include "tulipreg.h"


/*  #define debug fatal  */

#define	DEC21143_TICK_SHIFT		16

#define	N_REGS			32
#define	ROM_WIDTH		6

struct dec21143_data {
	int		irq_nr;
	int		irq_asserted;

	/*  PCI:  */
	int		pci_little_endian;

	/*  Ethernet address, and a network which we are connected to:  */
	uint8_t		mac[6];
	struct net	*net;

	/*  ROM emulation:  */
	uint16_t	rom[1 << ROM_WIDTH];
	int		srom_curbit;
	int		srom_opcode;
	int		srom_opcode_has_started;
	int		rom_addr;

	/*  21143 registers:  */
	uint32_t	reg[N_REGS];

	/*  Internal (emulated) state:  */
	uint64_t	cur_tx_addr;
	unsigned char	*cur_tx_buf;
	int		cur_tx_buf_len;
	int		tx_idling;

	uint64_t	cur_rx_addr;
	unsigned char	*cur_rx_buf;
	int		cur_rx_buf_len;
	int		cur_rx_offset;
};


/*
 *  dec21143_rx():
 */
int dec21143_rx(struct cpu *cpu, struct dec21143_data *d)
{
	uint64_t addr = d->cur_rx_addr, bufaddr;
	unsigned char descr[16];
	uint32_t rdes0, rdes1, rdes2, rdes3;
	int bufsize, buf1_size, buf2_size, i, writeback_len = 4, to_xfer;

	/*  No current packet? Then check for new ones.  */
	if (d->cur_rx_buf == NULL) {
		/*  Nothing available? Then abort.  */
		if (!net_ethernet_rx_avail(d->net, d))
			return 0;

		/*  Get the next packet into our buffer:  */
		net_ethernet_rx(d->net, d, &d->cur_rx_buf,
		    &d->cur_rx_buf_len);

		d->cur_rx_offset = 0;
	}

	fatal("{ dec21143_rx: base = 0x%08x }\n", (int)addr);


	if (!cpu->memory_rw(cpu, cpu->mem, addr, descr, sizeof(uint32_t),
	    MEM_READ, PHYSICAL | NO_EXCEPTIONS)) {
		fatal("[ dec21143_tx: memory_rw failed! ]\n");
		return 0;
	}

	rdes0 = descr[0] + (descr[1]<<8) + (descr[2]<<16) + (descr[3]<<24);

	/*  Only use descriptors owned by the 21143:  */
	if (!(rdes0 & TDSTAT_OWN)) {
		d->reg[CSR_STATUS/8] |= STATUS_RU;
		return 0;
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr + sizeof(uint32_t), descr +
	    sizeof(uint32_t), sizeof(uint32_t) * 3, MEM_READ, PHYSICAL |
	    NO_EXCEPTIONS)) {
		fatal("[ dec21143_tx: memory_rw failed! ]\n");
		return 0;
	}

	rdes1 = descr[4] + (descr[5]<<8) + (descr[6]<<16) + (descr[7]<<24);
	rdes2 = descr[8] + (descr[9]<<8) + (descr[10]<<16) + (descr[11]<<24);
	rdes3 = descr[12] + (descr[13]<<8) + (descr[14]<<16) + (descr[15]<<24);

	buf1_size = rdes1 & TDCTL_SIZE1;
	buf2_size = (rdes1 & TDCTL_SIZE2) >> TDCTL_SIZE2_SHIFT;
	bufaddr = buf1_size? rdes2 : rdes3;
	bufsize = buf1_size? buf1_size : buf2_size;

	d->reg[CSR_STATUS/8] &= ~STATUS_RS;

	if (rdes1 & TDCTL_ER)
		d->cur_rx_addr = d->reg[CSR_RXLIST / 8];
	else {
		if (rdes1 & TDCTL_CH)
			d->cur_rx_addr = rdes3;
		else
			d->cur_rx_addr += 4 * sizeof(uint32_t);
	}

	fatal("{ RX (%llx): %08x %08x %x %x: buf %i bytes at 0x%x }\n",
	    (long long)addr, rdes0, rdes1, rdes2, rdes3, bufsize, (int)bufaddr);

	/*  Turn off all status bits, and give up ownership:  */
	rdes0 = 0x00000000;

	to_xfer = d->cur_rx_buf_len - d->cur_rx_offset;
	if (to_xfer > bufsize)
		to_xfer = bufsize;

	/*  DMA bytes from the packet into emulated physical memory:  */
	for (i=0; i<to_xfer; i++) {
		cpu->memory_rw(cpu, cpu->mem, bufaddr + i,
		    d->cur_rx_buf + d->cur_rx_offset + i, 1, MEM_WRITE,
		    PHYSICAL | NO_EXCEPTIONS);
		/*  fatal(" %02x", d->cur_rx_buf[d->cur_rx_offset + i]);  */
	}

	/*  Was this the first buffer in a frame? Then mark it as such.  */
	if (d->cur_rx_offset == 0)
		rdes0 |= TDSTAT_Rx_FS;

	d->cur_rx_offset += to_xfer;

	/*  Frame completed?  */
	if (d->cur_rx_offset >= d->cur_rx_buf_len) {
		rdes0 |= TDSTAT_Rx_LS;

		/*  Frame len, which includes the size of a 4-byte CRC:  */
		rdes0 |= ((d->cur_rx_buf_len + 4) << 16) & TDSTAT_Rx_FL;

		/*  Too long frame?  */
		if (d->cur_rx_buf_len > 1518)
			rdes0 |= TDSTAT_Rx_TL;

		/*  Cause a receiver interrupt:  */
		d->reg[CSR_STATUS/8] |= STATUS_RI;

		free(d->cur_rx_buf);
		d->cur_rx_buf = NULL;
		d->cur_rx_buf_len = 0;
	}

	/*  Descriptor writeback:  */
	descr[ 0] = rdes0;       descr[ 1] = rdes0 >> 8;
	descr[ 2] = rdes0 >> 16; descr[ 3] = rdes0 >> 24;
	if (writeback_len > 1) {
		descr[ 4] = rdes1;       descr[ 5] = rdes1 >> 8;
		descr[ 6] = rdes1 >> 16; descr[ 7] = rdes1 >> 24;
		descr[ 8] = rdes2;       descr[ 9] = rdes2 >> 8;
		descr[10] = rdes2 >> 16; descr[11] = rdes2 >> 24;
		descr[12] = rdes3;       descr[13] = rdes3 >> 8;
		descr[14] = rdes3 >> 16; descr[15] = rdes3 >> 24;
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr, descr, sizeof(uint32_t)
	    * writeback_len, MEM_WRITE, PHYSICAL | NO_EXCEPTIONS)) {
		fatal("[ dec21143_rx: memory_rw failed! ]\n");
		return 0;
	}

	return 1;
}


/*
 *  dec21143_tx():
 */
int dec21143_tx(struct cpu *cpu, struct dec21143_data *d)
{
	uint64_t addr = d->cur_tx_addr, bufaddr;
	unsigned char descr[16];
	uint32_t tdes0, tdes1, tdes2, tdes3;
	int bufsize, buf1_size, buf2_size, i, writeback_len = 4;

	/*  fatal("{ dec21143_tx: base = 0x%08x }\n", (int)addr);  */

	if (!cpu->memory_rw(cpu, cpu->mem, addr, descr, sizeof(uint32_t),
	    MEM_READ, PHYSICAL | NO_EXCEPTIONS)) {
		fatal("[ dec21143_tx: memory_rw failed! ]\n");
		return 0;
	}

	tdes0 = descr[0] + (descr[1]<<8) + (descr[2]<<16) + (descr[3]<<24);

	/*  Only process packets owned by the 21143:  */
	if (!(tdes0 & TDSTAT_OWN)) {
		if (!d->tx_idling) {
			d->reg[CSR_STATUS/8] |= STATUS_TU;
			d->tx_idling = 1;
		}
		return 0;
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr + sizeof(uint32_t), descr +
	    sizeof(uint32_t), sizeof(uint32_t) * 3, MEM_READ, PHYSICAL |
	    NO_EXCEPTIONS)) {
		fatal("[ dec21143_tx: memory_rw failed! ]\n");
		return 0;
	}

	tdes1 = descr[4] + (descr[5]<<8) + (descr[6]<<16) + (descr[7]<<24);
	tdes2 = descr[8] + (descr[9]<<8) + (descr[10]<<16) + (descr[11]<<24);
	tdes3 = descr[12] + (descr[13]<<8) + (descr[14]<<16) + (descr[15]<<24);

	buf1_size = tdes1 & TDCTL_SIZE1;
	buf2_size = (tdes1 & TDCTL_SIZE2) >> TDCTL_SIZE2_SHIFT;
	bufaddr = buf1_size? tdes2 : tdes3;
	bufsize = buf1_size? buf1_size : buf2_size;

	d->reg[CSR_STATUS/8] &= ~STATUS_TS;

	if (tdes1 & TDCTL_ER)
		d->cur_tx_addr = d->reg[CSR_TXLIST / 8];
	else {
		if (tdes1 & TDCTL_CH)
			d->cur_tx_addr = tdes3;
		else
			d->cur_tx_addr += 4 * sizeof(uint32_t);
	}

	fatal("{ TX (%llx): %08x %08x %x %x: buf %i bytes at 0x%x }\n",
	    (long long)addr, tdes0, tdes1, tdes2, tdes3, bufsize, (int)bufaddr);

	/*  Assume no error:  */
	tdes0 &= ~ (TDSTAT_Tx_UF | TDSTAT_Tx_EC | TDSTAT_Tx_LC
	    | TDSTAT_Tx_NC | TDSTAT_Tx_LO | TDSTAT_Tx_TO | TDSTAT_ES);

	if (tdes1 & TDCTL_Tx_SET) {
		/*
		 *  Setup Packet.
		 *
		 *  TODO. For now, just ignore it, and pretend it worked.
		 */
		fatal("{ TX: setup packet }\n");
		if (bufsize != 192)
			fatal("[ dec21143: setup packet len = %i, should be"
			    " 192! ]\n", (int)bufsize);
		if (tdes1 & TDCTL_Tx_IC)
			d->reg[CSR_STATUS/8] |= STATUS_TI;
		/*  New descriptor values, according to the docs:  */
		tdes0 = 0x7fffffff; tdes1 = 0xffffffff;
		tdes2 = 0xffffffff; tdes3 = 0xffffffff;
	} else {
		/*
		 *  Data Packet.
		 */
		fatal("{ TX: data packet: ");
		if (tdes1 & TDCTL_Tx_FS) {
			/*  First segment. Let's allocate a new buffer:  */
			fatal("new frame }\n");
			d->cur_tx_buf = malloc(bufsize);
			d->cur_tx_buf_len = 0;
		} else {
			/*  Not first segment. Increase the length of
			    the current buffer:  */
			fatal("continuing last frame }\n");
			d->cur_tx_buf = realloc(d->cur_tx_buf,
			    d->cur_tx_buf_len + bufsize);
		}

		if (d->cur_tx_buf == NULL) {
			fatal("dec21143_tx(): out of memory\n");
			exit(1);
		}

		/*  "DMA" data from emulated physical memory into the buf:  */
		for (i=0; i<bufsize; i++) {
			cpu->memory_rw(cpu, cpu->mem, bufaddr + i,
			    d->cur_tx_buf + d->cur_tx_buf_len + i, 1, MEM_READ,
			    PHYSICAL | NO_EXCEPTIONS);
			/*  fatal(" %02x", d->cur_tx_buf[
			    d->cur_tx_buf_len + i]);  */
		}

		d->cur_tx_buf_len += bufsize;

		/*  Last segment? Then actually transmit it:  */
		if (tdes1 & TDCTL_Tx_LS) {
			fatal("{ TX: data frame complete. }\n");
			if (d->net != NULL) {
				net_ethernet_tx(d->net, d, d->cur_tx_buf,
				    d->cur_tx_buf_len);
			} else {
				static int warn = 0;
				if (!warn)
					fatal("[ dec21143: WARNING! Not "
					    "connected to a network! ]\n");
				warn = 1;
			}

			free(d->cur_tx_buf);
			d->cur_tx_buf = NULL;
			d->cur_tx_buf_len = 0;

			/*  We are done.  */
			tdes0 &= ~TDSTAT_OWN;
			writeback_len = 1;

			/*  Interrupt, if Tx_IC is set:  */
/*			if (tdes1 & TDCTL_Tx_IC)  */
				d->reg[CSR_STATUS/8] |= STATUS_TI;
		}
	}

	/*  Error summary:  */
	if (tdes0 & (TDSTAT_Tx_UF | TDSTAT_Tx_EC | TDSTAT_Tx_LC
	    | TDSTAT_Tx_NC | TDSTAT_Tx_LO | TDSTAT_Tx_TO))
		tdes0 |= TDSTAT_ES;

	/*  Descriptor writeback:  */
	descr[ 0] = tdes0;       descr[ 1] = tdes0 >> 8;
	descr[ 2] = tdes0 >> 16; descr[ 3] = tdes0 >> 24;
	if (writeback_len > 1) {
		descr[ 4] = tdes1;       descr[ 5] = tdes1 >> 8;
		descr[ 6] = tdes1 >> 16; descr[ 7] = tdes1 >> 24;
		descr[ 8] = tdes2;       descr[ 9] = tdes2 >> 8;
		descr[10] = tdes2 >> 16; descr[11] = tdes2 >> 24;
		descr[12] = tdes3;       descr[13] = tdes3 >> 8;
		descr[14] = tdes3 >> 16; descr[15] = tdes3 >> 24;
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr, descr, sizeof(uint32_t)
	    * writeback_len, MEM_WRITE, PHYSICAL | NO_EXCEPTIONS)) {
		fatal("[ dec21143_tx: memory_rw failed! ]\n");
		return 0;
	}

	return 1;
}


/*
 *  dev_dec21143_tick():
 */
void dev_dec21143_tick(struct cpu *cpu, void *extra)
{
	struct dec21143_data *d = extra;
	int asserted;

	if (d->reg[CSR_OPMODE / 8] & OPMODE_SR)
		while (dec21143_rx(cpu, d))
			;

	if (d->reg[CSR_OPMODE / 8] & OPMODE_ST)
		while (dec21143_tx(cpu, d))
			;

	/*  Normal and Abnormal interrupt summary:  */
	d->reg[CSR_STATUS / 8] &= ~(STATUS_NIS | STATUS_AIS);
	if (d->reg[CSR_STATUS / 8] & 0x00004845)
		d->reg[CSR_STATUS / 8] |= STATUS_NIS;
	if (d->reg[CSR_STATUS / 8] & 0x0c0037ba)
		d->reg[CSR_STATUS / 8] |= STATUS_AIS;

	asserted = d->reg[CSR_STATUS / 8] & d->reg[CSR_INTEN / 8] & 0x0c01ffff;
	if (asserted) {
		cpu_interrupt(cpu, d->irq_nr);
	} else {
		if (d->irq_asserted)
			cpu_interrupt_ack(cpu, d->irq_nr);
	}

	/*  Remember assertion flag:  */
	d->irq_asserted = asserted;
}


/*
 *  srom_access():
 *
 *  This function handles reads from the Ethernet Address ROM. This is not a
 *  100% correct implementation, as it was reverse-engineered from OpenBSD
 *  sources; it seems to work with OpenBSD, NetBSD, and Linux.
 *
 *  Each transfer (if I understood this correctly) is of the following format:
 *
 *	1xx yyyyyy zzzzzzzzzzzzzzzz
 *
 *  where 1xx    = operation (6 means a Read),
 *        yyyyyy = ROM address
 *        zz...z = data
 *
 *  y and z are _both_ read and written to at the same time; this enables the
 *  operating system to sense the number of bits in y (when reading, all y bits
 *  are 1 except the last one).
 */
static void srom_access(struct cpu *cpu, struct dec21143_data *d,
	uint32_t oldreg, uint32_t idata)
{
	int obit, ibit;

	/*  debug("CSR9 WRITE! 0x%08x\n", (int)idata);  */

	/*  New selection? Then reset internal state.  */
	if (idata & MIIROM_SR && !(oldreg & MIIROM_SR)) {
		d->srom_curbit = 0;
		d->srom_opcode = 0;
		d->srom_opcode_has_started = 0;
		d->rom_addr = 0;
	}

	/*  Only care about data during clock cycles:  */
	if (!(idata & MIIROM_SROMSK))
		return;

	obit = 0;
	ibit = idata & MIIROM_SROMDI? 1 : 0;
	/*  debug("CLOCK CYCLE! (bit %i): ", d->srom_curbit);  */

	/*
	 *  Linux sends more zeroes before starting the actual opcode, than
	 *  OpenBSD and NetBSD. Hopefully this is correct. (I'm just guessing
	 *  that all opcodes should start with a 1, perhaps that's not really
	 *  the case.)
	 */
	if (!ibit && !d->srom_opcode_has_started)
		return;

	if (d->srom_curbit < 3) {
		d->srom_opcode_has_started = 1;
		d->srom_opcode <<= 1;
		d->srom_opcode |= ibit;
		/*  debug("opcode input '%i'\n", ibit);  */
	} else {
		switch (d->srom_opcode) {
		case TULIP_SROM_OPC_READ:
			if (d->srom_curbit < ROM_WIDTH + 3) {
				obit = d->srom_curbit < ROM_WIDTH + 2;
				d->rom_addr <<= 1;
				d->rom_addr |= ibit;
			} else {
				if (d->srom_curbit == ROM_WIDTH + 3)
					debug("[ dec21143: ROM read from offset"
					    " 0x%03x: 0x%04x ]\n", d->rom_addr,
					    d->rom[d->rom_addr]);
				obit = d->rom[d->rom_addr] & (0x8000 >>
				    (d->srom_curbit - ROM_WIDTH - 3))? 1 : 0;
			}
			break;
		default:fatal("[ dec21243: unimplemented SROM/EEPROM "
			    "opcode %i ]\n", d->srom_opcode);
		}
		d->reg[CSR_MIIROM / 8] &= ~MIIROM_SROMDO;
		if (obit)
			d->reg[CSR_MIIROM / 8] |= MIIROM_SROMDO;
		/*  debug("input '%i', output '%i'\n", ibit, obit);  */
	}

	d->srom_curbit ++;

	/*
	 *  Done opcode + addr + data? Then restart. (At least NetBSD does
	 *  sequential reads without turning selection off and then on.)
	 */
	if (d->srom_curbit >= 3 + ROM_WIDTH + 16) {
		d->srom_curbit = 0;
		d->srom_opcode = 0;
		d->srom_opcode_has_started = 0;
		d->rom_addr = 0;
	}
}


/*
 *  dec21143_reset():
 *
 *  Set the 21143 registers to reasonable values (according to the 21143
 *  manual).
 */
static void dec21143_reset(struct cpu *cpu, struct dec21143_data *d)
{
	memset(d->reg, 0, sizeof(uint32_t) * N_REGS);

	d->reg[CSR_BUSMODE / 8] = 0xfe000000;	/*  csr0   */
	d->reg[CSR_MIIROM  / 8] = 0xfff483ff;	/*  csr9   */
	d->reg[CSR_SIACONN / 8] = 0xffff0000;	/*  csr13  */
	d->reg[CSR_SIATXRX / 8] = 0xffffffff;	/*  csr14  */
	d->reg[CSR_SIAGEN  / 8] = 0x8ff00000;	/*  csr15  */

	d->cur_rx_addr = d->cur_tx_addr = 0;
}


/*
 *  dev_dec21143_access():
 */
int dev_dec21143_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct dec21143_data *d = extra;
	uint64_t idata = 0, odata = 0;
	uint32_t oldreg = 0;
	int regnr = relative_addr >> 3;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len | d->pci_little_endian);

	if ((relative_addr & 7) == 0 && regnr < N_REGS) {
		if (writeflag == MEM_READ) {
			odata = d->reg[regnr];
		} else {
			oldreg = d->reg[regnr];
			switch (regnr) {
			case CSR_STATUS / 8:	/*  Zero-on-write  */
				d->reg[regnr] &= ~(idata & 0x0c01ffff);
				break;
			case CSR_MISSED / 8:	/*  Read only  */
				break;
			default:d->reg[regnr] = idata;
			}
		}
	} else
		fatal("[ dec21143: WARNING! unaligned access (0x%x) ]\n",
		    (int)relative_addr);

	switch (relative_addr) {

	case CSR_BUSMODE:	/*  csr0  */
		if (writeflag == MEM_WRITE) {
			/*  Software reset takes effect immediately.  */
			if (idata & BUSMODE_SWR) {
				dec21143_reset(cpu, d);
				idata &= ~BUSMODE_SWR;
			}
		}
		break;

	case CSR_TXPOLL:	/*  csr1  */
		if (writeflag == MEM_READ)
			fatal("[ dec21143: UNIMPLEMENTED READ from "
			    "txpoll ]\n");
		d->tx_idling = 0;
		dev_dec21143_tick(cpu, extra);
		break;

	case CSR_RXPOLL:	/*  csr2  */
		if (writeflag == MEM_READ)
			fatal("[ dec21143: UNIMPLEMENTED READ from "
			    "rxpoll ]\n");
		dev_dec21143_tick(cpu, extra);
		break;

	case CSR_RXLIST:	/*  csr3  */
		if (writeflag == MEM_WRITE) {
			debug("[ dec21143: setting RXLIST to 0x%x ]\n",
			    (int)idata);
			if (idata & 0x3)
				fatal("[ dec21143: WARNING! RXLIST not aligned"
				    "? (0x%llx) ]\n", (long long)idata);
			idata &= ~0x3;
			d->cur_rx_addr = idata;
		}
		break;

	case CSR_TXLIST:	/*  csr4  */
		if (writeflag == MEM_WRITE) {
			debug("[ dec21143: setting TXLIST to 0x%x ]\n",
			    (int)idata);
			if (idata & 0x3)
				fatal("[ dec21143: WARNING! TXLIST not aligned"
				    "? (0x%llx) ]\n", (long long)idata);
			idata &= ~0x3;
			d->cur_tx_addr = idata;
		}
		break;

	case CSR_STATUS:	/*  csr5  */
	case CSR_INTEN:		/*  csr7  */
		if (writeflag == MEM_WRITE) {
			/*  Recalculate interrupt assertion.  */
			dev_dec21143_tick(cpu, extra);
		}
		break;

	case CSR_OPMODE:	/*  csr6:  */
		if (writeflag == MEM_WRITE) {
			if (idata & 0x02000000) {
				/*  A must-be-one bit.  */
				idata &= ~0x02000000;
			}
			if (idata & OPMODE_ST)
				idata &= ~OPMODE_ST;
			if (idata & OPMODE_SR)
				idata &= ~OPMODE_SR;
			if (idata & OPMODE_SF)
				idata &= ~OPMODE_SF;
			if (idata != 0) {
				fatal("[ dec21143: UNIMPLEMENTED OPMODE bits"
				    ": 0x%08x ]\n", (int)idata);
			}
			dev_dec21143_tick(cpu, extra);
		}
		break;

	case CSR_MISSED:	/*  csr8  */
		break;

	case CSR_MIIROM:	/*  csr9  */
		if (writeflag == MEM_WRITE)
			srom_access(cpu, d, oldreg, idata);
		break;

	case CSR_SIASTAT:	/*  csr12  */
		/*  TODO  */
		odata = 0xffffffff;
		break;

	default:if (writeflag == MEM_READ)
			fatal("[ dec21143: read from unimplemented 0x%02x ]\n",
			    (int)relative_addr);
		else
			fatal("[ dec21143: write to unimplemented 0x%02x: "
			    "0x%02x ]\n", (int)relative_addr, (int)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len | d->pci_little_endian, odata);

	return 1;
}


/*
 *  devinit_dec21143():
 */
int devinit_dec21143(struct devinit *devinit)
{
	struct dec21143_data *d;
	char name2[100];

	d = malloc(sizeof(struct dec21143_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec21143_data));

	d->irq_nr = devinit->irq_nr;
	d->pci_little_endian = devinit->pci_little_endian;

	net_generate_unique_mac(devinit->machine, d->mac);
	net_add_nic(devinit->machine->emul->net, d, d->mac);
	d->net = devinit->machine->emul->net;

	/*  Version (= 1) and Chip count (= 1):  */
	d->rom[TULIP_ROM_SROM_FORMAT_VERION / 2] = 0x101;

	/*  MAC:  */
	d->rom[10] = (d->mac[1] << 8) + d->mac[0];
	d->rom[11] = (d->mac[3] << 8) + d->mac[2];
	d->rom[12] = (d->mac[5] << 8) + d->mac[4];

	dec21143_reset(devinit->machine->cpus[0], d);

	snprintf(name2, sizeof(name2), "%s [%02x:%02x:%02x:%02x:%02x:%02x]",
	    devinit->name, d->mac[0], d->mac[1], d->mac[2], d->mac[3],
	    d->mac[4], d->mac[5]);

	memory_device_register(devinit->machine->memory, name2,
	    devinit->addr, 0x100, dev_dec21143_access, d, DM_DEFAULT, NULL);

	machine_add_tickfunction(devinit->machine,
	    dev_dec21143_tick, d, DEC21143_TICK_SHIFT);

	/*
	 *  NetBSD/cats uses memory accesses, OpenBSD/cats uses I/O registers.
	 *  Let's make a mirror from the memory range to the I/O range:
	 */
	dev_ram_init(devinit->machine, devinit->addr2, 0x100, DEV_RAM_MIRROR
	    | DEV_RAM_MIGHT_POINT_TO_DEVICES, devinit->addr);

	return 1;
}

