/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_sgi_ip32.c,v 1.18 2005-02-03 20:57:20 debug Exp $
 *  
 *  SGI IP32 devices.
 *
 *	o)  CRIME
 *	o)  MACE
 *	o)  MACE PCI bus
 *	o)  mec (ethernet)
 *	o)  ust (unknown device)
 *	o)  mte (memory transfer engine? details unknown)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "console.h"
#include "cpu.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"

#include "crimereg.h"

#include "if_mecreg.h"


#define	CRIME_TICKSHIFT			14
#define	CRIME_SPEED_MUL_FACTOR		1
#define	CRIME_SPEED_DIV_FACTOR		1

struct macepci_data {
	struct pci_data *pci_data;
	uint32_t	reg[DEV_MACEPCI_LENGTH / 4];
};


/*
 *  dev_crime_tick():
 *
 *  This function simply updates CRIME_TIME each tick.
 *
 *  The names DIV and MUL may be a bit confusing. Increasing the
 *  MUL factor will result in an OS running on the emulated machine
 *  detecting a faster CPU. Increasing the DIV factor will result
 *  in a slower detected CPU.
 *
 *  A R10000 is detected as running at
 *  CRIME_SPEED_FACTOR * 66 MHz. (TODO: this is not correct anymore)
 */
void dev_crime_tick(struct cpu *cpu, void *extra)
{
	int j, carry, old, new, add_byte;
	uint64_t what_to_add = (1<<CRIME_TICKSHIFT)
	    * CRIME_SPEED_DIV_FACTOR / CRIME_SPEED_MUL_FACTOR;
	struct crime_data *d = extra;

	j = 0;
	carry = 0;
	while (j < 8) {
		old = d->reg[CRIME_TIME + 7 - j];
		add_byte = what_to_add >> ((int64_t)j * 8);
		add_byte &= 255;
		new = old + add_byte + carry;
		d->reg[CRIME_TIME + 7 - j] = new & 255;
		if (new >= 256)
			carry = 1;
		else
			carry = 0;
		j++;
	}
}


/*
 *  dev_crime_access():
 */
int dev_crime_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	int i;
	struct crime_data *d = extra;
	uint64_t idata;

	idata = memory_readmax64(cpu, data, len);

	/*
	 *  Set crime version/revision:
	 *
	 *  This might not be the most elegant or correct solution,
	 *  but it seems that the IP32 PROM likes 0x11 for machines
	 *  without graphics, and 0xa1 for machines with graphics.
	 *
	 *  NetBSD 2.0 complains about "unknown" crime for 0x11,
	 *  but I guess that's something one has to live with.
	 *
	 *  (TODO?)
	 */
	d->reg[4] = 0x00; d->reg[5] = 0x00; d->reg[6] = 0x00;
	d->reg[7] = d->use_fb? 0xa1 : 0x11;

	/*
	 *  Amount of memory.  Bit 8 of bank control set ==> 128MB instead
	 *  of 32MB per bank (?)
	 *
	 *  When the bank control registers contain the same value as the
	 *  previous one, that bank is not valid. (?)
	 */
	d->reg[CRM_MEM_BANK_CTRL0 + 6] = 0;  /* lowbit set=128MB, clear=32MB */
	d->reg[CRM_MEM_BANK_CTRL0 + 7] = 0;  /* address * 32MB  */
	d->reg[CRM_MEM_BANK_CTRL1 + 6] = 0;  /* lowbit set=128MB, clear=32MB */
	d->reg[CRM_MEM_BANK_CTRL1 + 7] = 1;  /* address * 32MB  */

	if (relative_addr >= CRIME_TIME && relative_addr < CRIME_TIME+8) {
		if (writeflag == MEM_READ)
			memcpy(data, &d->reg[relative_addr], len);
		return 1;
	}

	if (writeflag == MEM_WRITE)
		memcpy(&d->reg[relative_addr], data, len);
	else
		memcpy(data, &d->reg[relative_addr], len);

	if (relative_addr == 0x18 || relative_addr == 0x1c) {
		/*
		 *  Force interrupt re-assertion:
		 */
		cpu_interrupt_ack(cpu, 8);
	}

	switch (relative_addr) {
	case CRIME_CONTROL:	/*  0x008  */
		/*  TODO: 64-bit write to CRIME_CONTROL, but some things
		    (such as NetBSD 1.6.2) write to 0x00c!  */
		if (writeflag == MEM_WRITE) {
			/*
			 *  0x200 = watchdog timer (according to NetBSD)
			 *  0x800 = "reboot" used by the IP32 PROM
			 */
			if (idata & 0x200) {
				idata &= ~0x200;
			}
			if (idata & 0x800) {
				/*  This is used by the IP32 PROM's
				    "reboot" command:  */
				for (i=0; i<cpu->machine->ncpus; i++)
					cpu->machine->cpus[i]->running = 0;
				cpu->machine->
				    exit_without_entering_debugger = 1;
				idata &= ~0x800;
			}
			if (idata != 0)
				fatal("[ CRIME_CONTROL: unimplemented "
				    "control 0x%016llx ]\n", (long long)idata);
		}
		break;
#if 1
	case CRIME_INTSTAT:	/*  0x010, Current interrupt status  */
	case 0x14:
	case CRIME_INTMASK:	/*  0x018,  Current interrupt mask  */
	case 0x1c:
	case 0x34:
#endif
		/*  don't dump debug info for these  */
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

	return 1;
}


/*
 *  dev_crime_init():
 */
struct crime_data *dev_crime_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int irq_nr, int use_fb)
{
	struct crime_data *d;

	d = malloc(sizeof(struct crime_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct crime_data));
	d->irq_nr = irq_nr;
	d->use_fb = use_fb;

	memory_device_register(mem, "crime", baseaddr, DEV_CRIME_LENGTH,
	    dev_crime_access, d, MEM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_crime_tick, d, CRIME_TICKSHIFT);

	return d;
}


/****************************************************************************/


/*
 *  dev_mace_access():
 */
int dev_mace_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	int i;
	struct mace_data *d = extra;

	if (writeflag == MEM_WRITE)
		memcpy(&d->reg[relative_addr], data, len);
	else
		memcpy(data, &d->reg[relative_addr], len);

	switch (relative_addr) {
#if 0
	case 0x14:	/*  Current interrupt assertions  */
	case 0x18:	/*  ???  */
	case 0x1c:	/*  Interrupt mask  */
		/*  don't dump debug info for these  */
		break;
#endif
	default:
		if (writeflag==MEM_READ) {
			debug("[ mace: read from 0x%x, len=%i ]\n",
			    (int)relative_addr, len);
		} else {
			debug("[ mace: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		}
	}

	return 1;
}


/*
 *  dev_mace_init():
 */
struct mace_data *dev_mace_init(struct memory *mem, uint64_t baseaddr,
	int irqnr)
{
	struct mace_data *d;

	d = malloc(sizeof(struct mace_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct mace_data));
	d->irqnr = irqnr;

	memory_device_register(mem, "mace", baseaddr, DEV_MACE_LENGTH,
	    dev_mace_access, d, MEM_DEFAULT, NULL);

	return d;
}


/****************************************************************************/


/*
 *  dev_macepci_access():
 */
int dev_macepci_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct macepci_data *d = (struct macepci_data *) extra;
	uint64_t idata = 0, odata=0;
	int regnr, res = 1;

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
			res = bus_pci_access(cpu, mem, relative_addr,
			    &idata, writeflag, d->pci_data);
		} else {
			res = bus_pci_access(cpu, mem, relative_addr,
			    &odata, writeflag, d->pci_data);
			/*  odata = 0;  */
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ macepci: unimplemented write to address "
			    "0x%x, data=0x%02x ]\n",
			    (int)relative_addr, (int)idata);
		} else {
			debug("[ macepci: unimplemented read from address "
			    "0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return res;
}


/*
 *  dev_macepci_init():
 */
struct pci_data *dev_macepci_init(struct memory *mem, uint64_t baseaddr,
	int pciirq)
{
	struct macepci_data *d = malloc(sizeof(struct macepci_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct macepci_data));

	d->pci_data = bus_pci_init(mem, pciirq);

	memory_device_register(mem, "macepci", baseaddr, DEV_MACEPCI_LENGTH,
	    dev_macepci_access, (void *)d, MEM_DEFAULT, NULL);

	return d->pci_data;
}


/****************************************************************************/


/*
 *  SGI "mec" ethernet. Used in SGI-IP32.
 *
 *  Study http://www.openbsd.org/cgi-bin/cvsweb/src/sys/arch/sgi/dev/if_mec.c
 *  and/or NetBSD.  TODO:
 *
 *	x)  tx and rx interrupts/ring/slot stuff
 */

#define	MEC_TICK_SHIFT		14

#define	MAX_TX_PACKET_LEN	1700
#define	N_RX_ADDRESSES		16

struct sgi_mec_data {
	uint64_t	reg[DEV_SGI_MEC_LENGTH / sizeof(uint64_t)];

	int		irq_nr;

	unsigned char	cur_tx_packet[MAX_TX_PACKET_LEN];
	int		cur_tx_packet_len;

	unsigned char	*cur_rx_packet;
	int		cur_rx_packet_len;

	uint64_t	rx_addr[N_RX_ADDRESSES];
	int		cur_rx_addr_index_write;
	int		cur_rx_addr_index;
};


/*
 *  mec_reset():
 */
static void mec_reset(struct sgi_mec_data *d)
{
	if (d->cur_rx_packet != NULL)
		free(d->cur_rx_packet);

	memset(d->reg, 0, sizeof(d->reg));
}


/*
 *  mec_control_write():
 */
static void mec_control_write(struct cpu *cpu, struct sgi_mec_data *d,
	uint64_t x)
{
	if (x & MEC_MAC_CORE_RESET) {
		debug("[ sgi_mec: CORE RESET ]\n");
		mec_reset(d);
	}
}


/*
 *  mec_try_rx():
 */
static int mec_try_rx(struct cpu *cpu, struct sgi_mec_data *d)
{
	uint64_t base;
	unsigned char data[8];
	int i, res, retval = 0;

	base = d->rx_addr[d->cur_rx_addr_index];
	if (base & 0xfff)
		fatal("[ mec_try_rx(): WARNING! lowest bits of base are "
		    "non-zero (0x%3x). TODO ]\n", (int)(base & 0xfff));
	base &= 0xfffff000ULL;
	if (base == 0)
		goto skip_but_goto_next;

	/*  printf("rx base = 0x%016llx\n", (long long)base);  */

	/*  Read an rx descriptor from memory:  */
	res = memory_rw(cpu, cpu->mem, base,
	    &data[0], sizeof(data), MEM_READ, PHYSICAL);
	if (!res)
		return 0;

#if 0
	printf("{ mec: rxdesc %i: ", d->cur_rx_addr_index);
	for (i=0; i<sizeof(data); i++) {
		if ((i & 3) == 0)
			printf(" ");
		printf("%02x", data[i]);
	}
	printf(" }\n");
#endif

	/*  Is this descriptor already in use?  */
	if (data[0] & 0x80) {
printf("INTERRUPT for base = 0x%x\n", (int)base);
		goto skip_but_interrupt;
	}

	if (d->cur_rx_packet == NULL &&
	    net_ethernet_rx_avail(cpu->machine->emul->net, d))
		net_ethernet_rx(cpu->machine->emul->net, d,
		    &d->cur_rx_packet, &d->cur_rx_packet_len);

	if (d->cur_rx_packet == NULL)
		goto skip;

	/*  Copy the packet data:  */
	/*  printf("RX: ");  */
	for (i=0; i<d->cur_rx_packet_len; i++) {
		res = memory_rw(cpu, cpu->mem, base + 32 + i + 2,
		    d->cur_rx_packet + i, 1, MEM_WRITE, PHYSICAL);
		/*  printf(" %02x", d->cur_rx_packet[i]);  */
	}
	/*  printf("\n");  */

	printf("RX: %i bytes, index %i, base = 0x%x\n",
	    d->cur_rx_packet_len, d->cur_rx_addr_index, (int)base);

	/*  4 bytes of CRC at the end. Hm. TODO  */
	d->cur_rx_packet_len += 4;

	memset(data, 0, sizeof(data));
	data[6] = (d->cur_rx_packet_len >> 8) & 255;
	data[7] = d->cur_rx_packet_len & 255;
	/*  TODO: lots of bits :-)  */
	data[4] = 0x04;		/*  match MAC  */
	data[0] = 0x80;		/*  0x80 = received.  */
	res = memory_rw(cpu, cpu->mem, base,
	    &data[0], sizeof(data), MEM_WRITE, PHYSICAL);

	/*  Free the packet from memory:  */
	free(d->cur_rx_packet);
	d->cur_rx_packet = NULL;

skip_but_interrupt:
	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |= MEC_INT_RX_THRESHOLD;
	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] &= ~MEC_INT_RX_MCL_FIFO_ALIAS;
	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |= ((d->cur_rx_addr_index + 1) & 0x1f) << 8;

	retval = 1;

skip_but_goto_next:
	d->cur_rx_addr_index ++;
	d->cur_rx_addr_index %= N_RX_ADDRESSES;

skip:
	return retval;
}


/*
 *  mec_try_tx():
 */
static int mec_try_tx(struct cpu *cpu, struct sgi_mec_data *d)
{
	uint64_t base, addr, dma_base;
	int tx_ring_ptr, ringread, ringwrite, res, i, j;
	unsigned char data[32];
	int len, start_offset, dma_ptr_nr, dma_len;

	base = d->reg[MEC_TX_RING_BASE / sizeof(uint64_t)];
	tx_ring_ptr = d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)];

	if (base == 0)
		return 0;

	/*  printf("base = 0x%016llx\n", base);  */

	tx_ring_ptr &= MEC_TX_RING_READ_PTR;
	tx_ring_ptr >>= 16;

	/*  Each tx descriptor (+ buffer) is 128 bytes:  */
	addr = base + tx_ring_ptr*128;
	res = memory_rw(cpu, cpu->mem, addr,
	    &data[0], sizeof(data), MEM_READ, PHYSICAL);
	if (!res)
		return 0;

	/*  Is this packet transmitted already?  */
	if (data[0] & 0x80) {
		/*  fatal("[ mec_try_tx: tx_ring_ptr = %i, already"
		    " transmitted? ]\n", tx_ring_ptr);  */
		return 0;
	}

	len = data[6] * 256 + data[7];
	start_offset = data[5] & 0x7f;

	/*  Is this packet empty? Then don't transmit.  */
	if (len == 0)
		return 0;

	/*  Hm. Is len one too little?  TODO  */
	len ++;

	printf("{ mec: txdesc %i: ", tx_ring_ptr);
	for (i=0; i<sizeof(data); i++) {
		if ((i & 3) == 0)
			printf(" ");
		printf("%02x", data[i]);
	}
	printf(" }\n");

	dma_ptr_nr = 0;

	j = 0;
	d->cur_tx_packet_len = len;

	for (i=start_offset; i<start_offset+len; i++) {
		unsigned char ch;

		if ((i & 0x7f) == 0x00)
			break;

		res = memory_rw(cpu, cpu->mem, addr + i,
		    &ch, sizeof(ch), MEM_READ, PHYSICAL);
		/*  printf(" %02x", ch);  */

		d->cur_tx_packet[j++] = ch;
		if (j >= MAX_TX_PACKET_LEN) {
			fatal("[ mec_try_tx: packet too large? ]\n");
			break;
		}
	}
	/*  printf("\n");  */

	if (j < len) {
		/*  Continue with DMA:  */
		for (;;) {
			dma_ptr_nr ++;
			if (dma_ptr_nr >= 4)
				break;
			if (!(data[4] & (0x01 << dma_ptr_nr)))
				break;
			dma_base = (data[dma_ptr_nr * 8 + 4] << 24)
			         + (data[dma_ptr_nr * 8 + 5] << 16)
			         + (data[dma_ptr_nr * 8 + 6] <<  8)
			         + (data[dma_ptr_nr * 8 + 7]);
			dma_base &= 0xfffffff8ULL;
			dma_len = (data[dma_ptr_nr * 8 + 2] << 8)
			        + (data[dma_ptr_nr * 8 + 3]) + 1;

			/*  printf("dma_base = %08x, dma_len = %i\n",
			    (int)dma_base, dma_len);  */

			while (dma_len > 0) {
				unsigned char ch;
				res = memory_rw(cpu, cpu->mem, dma_base,
				    &ch, sizeof(ch), MEM_READ, PHYSICAL);
				/*  printf(" %02x", ch);  */

				d->cur_tx_packet[j++] = ch;
				if (j >= MAX_TX_PACKET_LEN) {
					fatal("[ mec_try_tx: packet too large?"
					    " ]\n");
					break;
				}
				dma_base ++;
				dma_len --;
			}
		}
	}

	if (j < len)
		fatal("[ mec_try_tx: not enough data? ]\n");

	net_ethernet_tx(cpu->machine->emul->net, d,
	    d->cur_tx_packet, d->cur_tx_packet_len);

	/*  see openbsd's if_mec.c for details  */
	if (data[4] & 0x01) {
		d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |=
		    MEC_INT_TX_PACKET_SENT;
	}
	data[0] = 0x80;
	data[5] = 0x80;
	data[4] = 0x00;

	res = memory_rw(cpu, cpu->mem, addr,
	    &data[0], sizeof(data), MEM_WRITE, PHYSICAL);

	/*  Advance the ring Read ptr.  */
	tx_ring_ptr = d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)];
	ringread = tx_ring_ptr & MEC_TX_RING_READ_PTR;
	ringwrite = tx_ring_ptr & MEC_TX_RING_WRITE_PTR;

	ringread = (ringread >> 16) + 1;
	ringread &= 63;
	ringread <<= 16;

	d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)] =
	    (ringwrite & MEC_TX_RING_WRITE_PTR) |
	    (ringread & MEC_TX_RING_READ_PTR);

/*	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |= MEC_INT_TX_EMPTY;  */

	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] &=
	    ~MEC_INT_TX_RING_BUFFER_ALIAS;
	d->reg[MEC_INT_STATUS / sizeof(uint64_t)] |=
	    (d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)] &
	    MEC_INT_TX_RING_BUFFER_ALIAS);

	return 1;
}


/*
 *  dev_sgi_mec_tick():
 */
void dev_sgi_mec_tick(struct cpu *cpu, void *extra)
{
	struct sgi_mec_data *d = (struct sgi_mec_data *) extra;

	while (mec_try_tx(cpu, d))
		;

	while (mec_try_rx(cpu, d))
		;

	/*  Interrupts:  (TODO: only when enabled)  */
	if (d->reg[MEC_INT_STATUS / sizeof(uint64_t)] & MEC_INT_STATUS_MASK)
		cpu_interrupt(cpu, d->irq_nr);
	else
		cpu_interrupt_ack(cpu, d->irq_nr);
}


/*
 *  dev_sgi_mec_access():
 */
int dev_sgi_mec_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct sgi_mec_data *d = (struct sgi_mec_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint64_t);

	/*  Treat most registers as read/write, by default.  */
	if (writeflag == MEM_WRITE) {
		switch (relative_addr) {
		case MEC_INT_STATUS:	/*  0x08  */
			/*  Clear bits on write:  (This is just a guess)  */
			d->reg[regnr] = (d->reg[regnr] & ~0xff)
			    | ((d->reg[regnr] & ~idata) & 0xff);
			break;
		case MEC_TX_RING_PTR:	/*  0x30  */
			idata &= MEC_TX_RING_WRITE_PTR;
			d->reg[regnr] = (d->reg[regnr] &
			    ~MEC_TX_RING_WRITE_PTR) | idata;
			/*  TODO  */
			break;
		default:
			d->reg[regnr] = idata;
		}
	} else
		odata = d->reg[regnr];

	switch (relative_addr) {
	case MEC_MAC_CONTROL:	/*  0x00  */
		if (writeflag)
			mec_control_write(cpu, d, idata);
		else {
			/*  Fake "revision 1":  */
			odata &= ~MEC_MAC_REVISION;
			odata |= 1 << MEC_MAC_REVISION_SHIFT;
		}
		break;
	case MEC_INT_STATUS:	/*  0x08  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_INT_STATUS: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_DMA_CONTROL:	/*  0x10  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_DMA_CONTROL: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_TX_ALIAS:	/*  0x20  */
		if (writeflag) {
			debug("[ sgi_mec: write to MEC_TX_ALIAS: "
			    "0x%016llx ]\n", (long long)idata);
		} else {
			debug("[ sgi_mec: read from MEC_TX_ALIAS: "
			    "0x%016llx ]\n", (long long)idata);
			odata = d->reg[MEC_TX_RING_PTR / sizeof(uint64_t)];
		}
		break;
	case MEC_RX_ALIAS:	/*  0x28  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_RX_ALIAS: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_TX_RING_PTR:	/*  0x30  */
		if (writeflag)
			fatal("[ sgi_mec: write to MEC_TX_RING_PTR: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_PHY_DATA:	/*  0x64  */
		if (writeflag)
			fatal("[ sgi_mec: write to MEC_PHY_DATA: "
			    "0x%016llx ]\n", (long long)idata);
		else
			odata = 0;	/*  ?  */
		break;
	case MEC_PHY_ADDRESS:	/*  0x6c  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_PHY_ADDRESS: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_PHY_READ_INITIATE:	/*  0x70  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_PHY_READ_INITIATE: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case 0x74:
		if (writeflag)
			debug("[ sgi_mec: write to 0x74: 0x%016llx ]\n",
			    (long long)idata);
		else
			debug("[ sgi_mec: read from 0x74 ]\n");
		break;
	case MEC_STATION:	/*  0xa0  */
		if (writeflag)
			debug("[ sgi_mec: setting the MAC address to "
			    "%02x:%02x:%02x:%02x:%02x:%02x ]\n",
			    (idata >> 40) & 255, (idata >> 32) & 255,
			    (idata >> 24) & 255, (idata >> 16) & 255,
			    (idata >>  8) & 255, (idata >>  0) & 255);
		break;
	case MEC_STATION_ALT:	/*  0xa8  */
		if (writeflag)
			debug("[ sgi_mec: setting the ALTERNATIVE MAC address"
			    " to %02x:%02x:%02x:%02x:%02x:%02x ]\n",
			    (idata >> 40) & 255, (idata >> 32) & 255,
			    (idata >> 24) & 255, (idata >> 16) & 255,
			    (idata >>  8) & 255, (idata >>  0) & 255);
		break;
	case MEC_MULTICAST:	/*  0xb0  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_MULTICAST: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_TX_RING_BASE:	/*  0xb8  */
		if (writeflag)
			debug("[ sgi_mec: write to MEC_TX_RING_BASE: "
			    "0x%016llx ]\n", (long long)idata);
		break;
	case MEC_MCL_RX_FIFO:	/*  0x100  */
		if (writeflag) {
			debug("[ sgi_mec: write to MEC_MCL_RX_FIFO: 0x"
			    "%016llx ]\n", (long long)idata);
			d->rx_addr[d->cur_rx_addr_index_write] = idata;
			d->cur_rx_addr_index_write ++;
			d->cur_rx_addr_index_write %= N_RX_ADDRESSES;
		}
		break;
	default:
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_mec: unimplemented write to address"
			    " 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_mec: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	dev_sgi_mec_tick(cpu, extra);

	return 1;
}


/*
 *  dev_sgi_mec_init():
 */
void dev_sgi_mec_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int irq_nr)
{
	struct sgi_mec_data *d = malloc(sizeof(struct sgi_mec_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_mec_data));
	d->irq_nr = irq_nr;

	mec_reset(d);

	memory_device_register(mem, "sgi_mec", baseaddr,
	    DEV_SGI_MEC_LENGTH, dev_sgi_mec_access, (void *)d,
	    MEM_DEFAULT, NULL);

	machine_add_tickfunction(machine, dev_sgi_mec_tick, d, MEC_TICK_SHIFT);
}


/****************************************************************************/


struct sgi_ust_data {
	uint64_t	reg[DEV_SGI_UST_LENGTH / sizeof(uint64_t)];
};


/*
 *  dev_sgi_ust_access():
 */
int dev_sgi_ust_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
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
		d->reg[regnr] += 0x2710;
		break;
	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_ust: unimplemented write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
		else
			debug("[ sgi_ust: unimplemented read from address 0x%llx ]\n", (long long)relative_addr);
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
	struct sgi_ust_data *d = malloc(sizeof(struct sgi_ust_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_ust_data));

	memory_device_register(mem, "sgi_ust", baseaddr,
	    DEV_SGI_UST_LENGTH, dev_sgi_ust_access, (void *)d, MEM_DEFAULT, NULL);
}


/****************************************************************************/


/*
 *  SGI "mte". This device seems to be an accelerator for copying/clearing
 *  memory.  Used in SGI-IP32.
 */

#define	ZERO_CHUNK_LEN		4096

struct sgi_mte_data {
	uint64_t	reg[DEV_SGI_MTE_LENGTH / sizeof(uint64_t)];
};

/*
 *  dev_sgi_mte_access():
 */
int dev_sgi_mte_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct sgi_mte_data *d = (struct sgi_mte_data *) extra;
	uint64_t first_addr, last_addr, zerobuflen, fill_addr, fill_len;
	unsigned char zerobuf[ZERO_CHUNK_LEN];
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint64_t);

	/*  Treat all registers as read/write, by default.  */
	if (writeflag == MEM_WRITE)
		d->reg[regnr] = idata;
	else
		odata = d->reg[regnr];

	/*
	 *  I've not found any docs about this 'mte' device at all, so this is just
	 *  a guess. The mte seems to be used for copying and zeroing chunks of
	 *  memory.
	 *
	 *  [ sgi_mte: unimplemented write to address 0x3030, data=0x00000000003da000 ]  <-- first address
	 *  [ sgi_mte: unimplemented write to address 0x3038, data=0x00000000003f9fff ]  <-- last address
	 *  [ sgi_mte: unimplemented write to address 0x3018, data=0x0000000000000000 ]  <-- what to fill?
	 *  [ sgi_mte: unimplemented write to address 0x3008, data=0x00000000ffffffff ]  <-- ?
	 *  [ sgi_mte: unimplemented write to address 0x3800, data=0x0000000000000011 ]  <-- operation (0x11 = zerofill)
	 *
	 *  [ sgi_mte: unimplemented write to address 0x1700, data=0x80001ea080001ea1 ]  <-- also containing the address to fill (?)
	 *  [ sgi_mte: unimplemented write to address 0x1708, data=0x80001ea280001ea3 ]
	 *  [ sgi_mte: unimplemented write to address 0x1710, data=0x80001ea480001ea5 ]
	 *  ...
	 *  [ sgi_mte: unimplemented write to address 0x1770, data=0x80001e9c80001e9d ]
	 *  [ sgi_mte: unimplemented write to address 0x1778, data=0x80001e9e80001e9f ]
	 */
	switch (relative_addr) {

	/*  No warnings for these:  */
	case 0x3030:
	case 0x3038:
		break;

	/*  Unknown, but no warning:  */
	case 0x4000:
	case 0x3018:
	case 0x3008:
	case 0x1700:
	case 0x1708:
	case 0x1710:
	case 0x1718:
	case 0x1720:
	case 0x1728:
	case 0x1730:
	case 0x1738:
	case 0x1740:
	case 0x1748:
	case 0x1750:
	case 0x1758:
	case 0x1760:
	case 0x1768:
	case 0x1770:
	case 0x1778:
		break;

	/*  Operations:  */
	case 0x3800:
		if (writeflag == MEM_WRITE) {
			switch (idata) {
			case 0x11:		/*  zerofill  */
				first_addr = d->reg[0x3030 / sizeof(uint64_t)];
				last_addr  = d->reg[0x3038 / sizeof(uint64_t)];
				zerobuflen = last_addr - first_addr + 1;
				debug("[ sgi_mte: zerofill: first = 0x%016llx, last = 0x%016llx, length = 0x%llx ]\n",
				    (long long)first_addr, (long long)last_addr, (long long)zerobuflen);

				/*  TODO:  is there a better way to implement this?  */
				memset(zerobuf, 0, sizeof(zerobuf));
				fill_addr = first_addr;
				while (zerobuflen != 0) {
					if (zerobuflen > sizeof(zerobuf))
						fill_len = sizeof(zerobuf);
					else
						fill_len = zerobuflen;
					memory_rw(cpu, mem, fill_addr, zerobuf, fill_len, MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);
					fill_addr += fill_len;
					zerobuflen -= sizeof(zerobuf);
				}

				break;
			default:
				fatal("[ sgi_mte: UNKNOWN operation 0x%x ]\n", idata);
			}
		}
		break;
	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_mte: unimplemented write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
		else
			debug("[ sgi_mte: unimplemented read from address 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_mte_init():
 */
void dev_sgi_mte_init(struct memory *mem, uint64_t baseaddr)
{
	struct sgi_mte_data *d = malloc(sizeof(struct sgi_mte_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_mte_data));

	memory_device_register(mem, "sgi_mte", baseaddr, DEV_SGI_MTE_LENGTH,
	    dev_sgi_mte_access, (void *)d, MEM_DEFAULT, NULL);
}

