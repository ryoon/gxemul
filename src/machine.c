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
 *  $Id: machine.c,v 1.57 2004-03-04 06:12:40 debug Exp $
 *
 *  Emulation of specific machines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "bus_pci.h"

#include "dec_5100.h"
#include "dec_kn01.h"
#include "dec_kn02.h"
#include "dec_kn03.h"
#include "dec_kmin.h"
#include "dec_maxine.h"


extern int emulation_type;
extern char *machine_name;
extern char emul_cpu_name[];

extern int bootstrap_cpu;
extern int ncpus;
extern struct cpu **cpus;
extern int emulation_type;
extern int emulated_ips;
extern int machine;
extern char *machine_name;
extern int physical_ram_in_mb;
extern int ultrixboot_emul;
extern int use_x11;

extern char *last_filename;

uint64_t file_loaded_end_addr = 0;

extern struct memory *GLOBAL_gif_mem;

struct kn230_csr *kn230_csr;
struct kn02_csr *kn02_csr;
struct dec_ioasic_data *dec_ioasic_data;


/********************** Helper functions **********************/


/*
 *  read_char_from_memory():
 */
unsigned char read_char_from_memory(struct cpu *cpu, int regbase, int offset)
{
	unsigned char ch;
	memory_rw(cpu, cpu->mem, cpu->gpr[regbase] + offset, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
	return ch;
}


/*
 *  dump_mem_string():
 */
void dump_mem_string(struct cpu *cpu, uint64_t addr)
{
	int i;
	for (i=0; i<40; i++) {
		unsigned char ch = '\0';
		memory_rw(cpu, cpu->mem, addr + i, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
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
 *  Helper function.
 */
void store_byte(uint64_t addr, uint8_t data)
{
	memory_rw(cpus[bootstrap_cpu], cpus[bootstrap_cpu]->mem, addr, &data, sizeof(data), MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
}


/*
 *  store_string():
 *
 *  Helper function.
 */
void store_string(uint64_t addr, char *s)
{
	do {
		store_byte(addr++, *s);
	} while (*s++);
}


/*
 *  Like store_string(), but advances the pointer afterwards.
 */
void add_environment_string(char *s, uint64_t *addr)
{
	store_string(*addr, s);
	(*addr) += strlen(s) + 1;
}


/*
 *  store_buf():
 *
 *  Helper function.
 */
void store_buf(uint64_t addr, char *s, size_t len)
{
	while (len-- != 0)
		store_byte(addr++, *s++);
}


/*
 *  store_32bit_word():
 *
 *  Helper function.
 */
void store_32bit_word(uint64_t addr, uint32_t data32)
{
	unsigned char data[4];
	data[0] = (data32 >> 24) & 255;
	data[1] = (data32 >> 16) & 255;
	data[2] = (data32 >> 8) & 255;
	data[3] = (data32) & 255;
	if (cpus[bootstrap_cpu]->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}
	memory_rw(cpus[bootstrap_cpu], cpus[bootstrap_cpu]->mem, addr, data, sizeof(data), MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
}


/*
 *  load_32bit_word():
 *
 *  Helper function.  Prints a warning and returns 0, if
 *  the read failed.
 */
uint32_t load_32bit_word(uint64_t addr)
{
	unsigned char data[4];

	memory_rw(cpus[bootstrap_cpu], cpus[bootstrap_cpu]->mem, addr, data, sizeof(data),
	    MEM_READ, CACHE_NONE | NO_EXCEPTIONS);

	if (cpus[bootstrap_cpu]->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}

	return (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + data[3];
}


/*
 *  store_32bit_word_in_host():
 *
 *  Helper function.
 */
void store_32bit_word_in_host(unsigned char *data, uint32_t data32)
{
	data[0] = (data32 >> 24) & 255;
	data[1] = (data32 >> 16) & 255;
	data[2] = (data32 >> 8) & 255;
	data[3] = (data32) & 255;
	if (cpus[bootstrap_cpu]->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}
}


/*
 *  store_16bit_word_in_host():
 *
 *  Helper function.
 */
void store_16bit_word_in_host(unsigned char *data, uint16_t data16)
{
	data[0] = (data16 >> 8) & 255;
	data[1] = (data16) & 255;
	if (cpus[bootstrap_cpu]->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[1]; data[1] = tmp;
	}
}


/**************************************************************/


void kn02_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;

	if (assrt) {
		/*  OR in the irq_nr mask into the CSR:  */
		kn02_csr->csr |= irq_nr;

		/*  Assert MIPS interrupt 2:  */
		cpu_interrupt(cpu, 2);
	} else {
		/*  AND out the irq_nr mask from the CSR:  */
		kn02_csr->csr &= ~irq_nr;

		if ((kn02_csr->csr & KN02_CSR_IOINT) == 0)
			cpu_interrupt_ack(cpu, 2);
	}
}


void kmin_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	debug("kmin_interrupt(): irq_nr=%i assrt=%i\n", irq_nr, assrt);

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


void kn230_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	int r2;

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


void sgi_mace_interrupt(struct cpu *cpu, int irq_nr, int assrt)
{
	/*  TODO:  how should all this be done nicely?  */
	static uint32_t mace_interrupts = 0;

	if (assrt) {
		mace_interrupts |= irq_nr;
		cpu_interrupt(cpu, 2);
	} else {
		mace_interrupts &= ~irq_nr;
		if (mace_interrupts == 0)
			cpu_interrupt_ack(cpu, 2);
	}

	/*  printf("sgi_mace_machine_irq(%i,%i): new interrupts = 0x%08x\n", assrt, irq_nr, mace_interrupts);  */
}


/**************************************************************/


/*
 *  machine_init():
 *
 *  Initialize memory and/or devices required by specific
 *  machine emulations.
 */
void machine_init(struct memory *mem)
{
	uint64_t addr;
	int i;

	/*  DECstation:  */
	char *framebuffer_console_name, *serial_console_name;
	int color_fb_flag;

	/*  HPCmips:  */
	struct xx {
		struct btinfo_magic a;
		struct btinfo_bootpath b;
		struct btinfo_symtab c;
	} xx;
	struct hpc_bootinfo hpc_bootinfo;

	/*  ARCBIOS stuff:  */
	struct arcbios_spb arcbios_spb;
	struct arcbios_sysid arcbios_sysid;
	struct arcbios_dsp_stat arcbios_dsp_stat;
	struct arcbios_mem arcbios_mem;
	uint64_t mem_base, mem_count, mem_bufaddr;
	int mem_mb_left;
	uint32_t system;

	/*  Generic bootstring stuff:  */
	char *bootstr = NULL;
	char *bootarg = NULL;
	char *tmp_ptr;
	char *init_bootpath;

	/*  PCI stuff:  */
	struct pci_data *pci_data;

	/*  Framebuffer stuff:  */
	struct vfb_data *fb;

	/*  Playstation:  */
	struct cpu *ps1_subcpu;
	struct memory *ps1_mem;

	machine_name = NULL;

	switch (emulation_type) {
	case EMULTYPE_NONE:
		/*
		 *  This "none" type is used if no type is specified.
		 *  It is used for testing.
		 */

		dev_cons_init(mem);		/*  TODO: include address here?  */
		dev_mp_init(mem, cpus);
		fb = dev_fb_init(cpus[bootstrap_cpu], mem, 0x12000000, VFB_GENERIC, 640,480, 640,480, 24, "generic");

		break;

	case EMULTYPE_DEC:
		/*  An R2020 or R3220 memory thingy:  */
		cpus[bootstrap_cpu]->coproc[3] = coproc_new(cpus[bootstrap_cpu], 3);

		/*  There aren't really any good standard values...  */
		framebuffer_console_name = "osconsole=0,3";
		serial_console_name      = "osconsole=1";

		switch (machine) {
		case MACHINE_PMAX_3100:		/*  type  1, KN01  */
			/*  Supposed to have 12MHz or 16.67MHz R2000 CPU, R2010 FPC, R2020 Memory coprocessor  */
			machine_name = "DEC PMAX 3100 (KN01)";

			if (emulated_ips == 0)
				emulated_ips = 16670000;	/*  12 MHz for 2100, 16.67 MHz for 3100  */

			if (physical_ram_in_mb > 24)
				fprintf(stderr, "WARNING! Real DECstation 3100 machines cannot have more than 24MB RAM. Continuing anyway.\n");

			if ((physical_ram_in_mb % 4) != 0)
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
			fb = dev_fb_init(cpus[bootstrap_cpu], mem, KN01_PHYS_FBUF_START, color_fb_flag? VFB_DEC_VFB02 : VFB_DEC_VFB01,
			    0,0,0,0,0, color_fb_flag? "VFB02":"VFB01");
			dev_colorplanemask_init(mem, KN01_PHYS_COLMASK_START, &fb->color_plane_mask);
			dev_vdac_init(mem, KN01_SYS_VDAC, fb->rgb_palette, color_fb_flag);
			dev_le_init(mem, KN01_SYS_LANCE, KN01_SYS_LANCE_B_START, KN01_SYS_LANCE_B_END, KN01_INT_LANCE, 4*1048576);
			dev_sii_init(cpus[bootstrap_cpu], mem, KN01_SYS_SII, KN01_SYS_SII_B_START, KN01_SYS_SII_B_END, KN01_INT_SII);
			dev_dc7085_init(cpus[bootstrap_cpu], mem, KN01_SYS_DZ, KN01_INT_DZ, use_x11);
			dev_mc146818_init(cpus[bootstrap_cpu], mem, KN01_SYS_CLOCK, KN01_INT_CLOCK, MC146818_DEC, 1, emulated_ips);
			dev_kn01_csr_init(mem, KN01_SYS_CSR, color_fb_flag);

			framebuffer_console_name = "osconsole=0,3";	/*  fb,keyb  */
			serial_console_name      = "osconsole=3";	/*  3  */
			break;

		case MACHINE_3MAX_5000:		/*  type  2, KN02  */
			/*  Supposed to have 25MHz R3000 CPU, R3010 FPC, R3220 Memory coprocessor  */
			machine_name = "DECstation 5000/200 (3MAX, KN02)";

			if (emulated_ips == 0)
				emulated_ips = 25000000;

			if (physical_ram_in_mb < 8)
				fprintf(stderr, "WARNING! Real KN02 machines do not have less than 8MB RAM. Continuing anyway.\n");
			if (physical_ram_in_mb > 480)
				fprintf(stderr, "WARNING! Real KN02 machines cannot have more than 480MB RAM. Continuing anyway.\n");

			/*  An R3220 memory thingy:  */
			cpus[bootstrap_cpu]->coproc[3] = coproc_new(cpus[bootstrap_cpu], 3);

			/*  KN02 interrupts:  */
			cpus[bootstrap_cpu]->md_interrupt = kn02_interrupt;

			/*
			 *  According to NetBSD/pmax:
			 *  asc0 at tc0 slot 5 offset 0x0
			 *  le0 at tc0 slot 6 offset 0x0
			 *  ibus0 at tc0 slot 7 offset 0x0
			 *  dc0 at ibus0 addr 0x1fe00000
			 *  mcclock0 at ibus0 addr 0x1fe80000: mc146818 or compatible
			 */

			/*  TURBOchannel slots 0, 1, and 2 are free for option cards.  */
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 0, KN02_PHYS_TC_0_START, KN02_PHYS_TC_0_END, "PMAG-AA", KN02_IP_SLOT0 +8);
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 1, KN02_PHYS_TC_1_START, KN02_PHYS_TC_1_END, "", KN02_IP_SLOT1 +8);
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 2, KN02_PHYS_TC_2_START, KN02_PHYS_TC_2_END, "", KN02_IP_SLOT2 +8);

			/*  TURBOchannel slots 3 and 4 are reserved.  */

			/*  TURBOchannel slot 5 is PMAZ-AA (asc SCSI), 6 is PMAD-AA (LANCE ethernet).  */
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 5, KN02_PHYS_TC_5_START, KN02_PHYS_TC_5_END, "PMAZ-AA", KN02_IP_SCSI +8);
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 6, KN02_PHYS_TC_6_START, KN02_PHYS_TC_6_END, "PMAD-AA", KN02_IP_LANCE +8);

			/*  TURBOchannel slot 7 is system stuff.  */
			dev_dc7085_init(cpus[bootstrap_cpu], mem, KN02_SYS_DZ, KN02_IP_DZ +8, use_x11);
			dev_mc146818_init(cpus[bootstrap_cpu], mem, KN02_SYS_CLOCK, KN02_INT_CLOCK, MC146818_DEC, 1, emulated_ips);

			/*  (kn02 shared irq numbers (IP) are offset by +8 in the emulator)  */
			kn02_csr = dev_kn02_init(cpus[bootstrap_cpu], mem, KN02_SYS_CSR);

			framebuffer_console_name = "osconsole=0,7";	/*  fb,keyb  */
			serial_console_name      = "osconsole=2";
			break;

		case MACHINE_3MIN_5000:		/*  type 3, KN02BA  */
			machine_name = "DECstation 5000/112 or 145 (3MIN, KN02BA)";
			if (emulated_ips == 0)
				emulated_ips = 33000000;
			if (physical_ram_in_mb > 128)
				fprintf(stderr, "WARNING! Real 3MIN machines cannot have more than 128MB RAM. Continuing anyway.\n");

			/*  KMIN interrupts:  */
			cpus[bootstrap_cpu]->md_interrupt = kmin_interrupt;

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
			dev_scc_init(cpus[bootstrap_cpu], mem, 0x1c100000, KMIN_INTR_SCC_0 +8, use_x11, 0);
			dev_scc_init(cpus[bootstrap_cpu], mem, 0x1c180000, KMIN_INTR_SCC_1 +8, use_x11, 1);
			dev_mc146818_init(cpus[bootstrap_cpu], mem, 0x1c200000, KMIN_INTR_CLOCK +8, MC146818_DEC, 1, emulated_ips);
			dev_asc_init(cpus[bootstrap_cpu], mem, 0x1c300000, KMIN_INTR_SCSI +8);

			/*  TURBOchannel slots 0, 1, and 2 are free for option cards. TODO: irqs  */
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 0, 0x10000000, 0x103fffff, "PMAG-BA", KMIN_INT_TC0);
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 1, 0x14000000, 0x143fffff, "", KMIN_INT_TC1);
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 2, 0x18000000, 0x183fffff, "", KMIN_INT_TC2);

			/*  (kmin shared irq numbers (IP) are offset by +8 in the emulator)  */
			/*  kmin_csr = dev_kmin_init(cpus[bootstrap_cpu], mem, KMIN_REG_INTR);  */

			framebuffer_console_name = "osconsole=0,3";	/*  fb, keyb (?)  */
			serial_console_name      = "osconsole=3";	/*  ?  */
			break;

		case MACHINE_3MAXPLUS_5000:	/*  type 4, KN03  */
			machine_name = "DECsystem 5900 or 5000 (3MAX+) (KN03)";

			/*  5000/240 (KN03-GA, R3000): 40 MHz  */
			/*  5000/260 (KN05-NB, R4000): 60 MHz  */
			/*  TODO: are both these type 4?  */
			if (emulated_ips == 0)
				emulated_ips = 40000000;
			if (physical_ram_in_mb > 480)
				fprintf(stderr, "WARNING! Real KN03 machines cannot have more than 480MB RAM. Continuing anyway.\n");

			/*  KN03 interrupts:  */
			cpus[bootstrap_cpu]->md_interrupt = kn03_interrupt;

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

			dev_le_init(mem, KN03_SYS_LANCE, 0, 0, KN03_INTR_LANCE +8, 4*65536);
			dev_scc_init(cpus[bootstrap_cpu], mem, KN03_SYS_SCC_0, KN03_INTR_SCC_0 +8, use_x11, 0);
			dev_scc_init(cpus[bootstrap_cpu], mem, KN03_SYS_SCC_1, KN03_INTR_SCC_1 +8, use_x11, 1);
			dev_mc146818_init(cpus[bootstrap_cpu], mem, KN03_SYS_CLOCK, KN03_INT_RTC, MC146818_DEC, 1, emulated_ips);
			dev_asc_init(cpus[bootstrap_cpu], mem, KN03_SYS_SCSI, KN03_INTR_SCSI +8);

			/*  TURBOchannel slots 0, 1, and 2 are free for option cards.  TODO: irqs */
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 0, KN03_PHYS_TC_0_START, KN03_PHYS_TC_0_END, "PMAG-AA", KN03_INTR_TC_0 +8);
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 1, KN03_PHYS_TC_1_START, KN03_PHYS_TC_1_END, "", KN03_INTR_TC_1 +8);
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 2, KN03_PHYS_TC_2_START, KN03_PHYS_TC_2_END, "", KN03_INTR_TC_2 +8);

			/*  TODO: interrupts  */
			/*  shared (turbochannel) interrupts are +8  */

			framebuffer_console_name = "osconsole=0,3";	/*  fb, keyb (?)  */
			serial_console_name      = "osconsole=3";	/*  ?  */
			break;

		case MACHINE_5800:		/*  type 5, KN5800  */
			machine_name = "DECsystem 5800";
			if (physical_ram_in_mb < 48)
				fprintf(stderr, "WARNING! 5800 will probably not run with less than 48MB RAM. Continuing anyway.\n");

			/*
			 *  Ultrix might support SMP on this machine type.
			 *
			 *  Something at 0x10000000.
			 *  ssc serial console at 0x10140000, interrupt 2 (shared with XMI?).
			 *  xmi 0 at address 0x11800000   (node x at offset x*0x80000)
			 *  Clock uses interrupt 3 (shared with XMI?).
			 */

			dev_dec5800_init(cpus[bootstrap_cpu], mem, 0x10000000);
			dev_ssc_init(cpus[bootstrap_cpu], mem, 0x10140000, 2, use_x11);
			dev_decxmi_init(cpus[bootstrap_cpu], mem, 0x11800000);

			break;

		case MACHINE_5400:		/*  type 6, KN210  */
			machine_name = "DECsystem 5400 (KN210)";
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
			dev_kn210_init(cpus[bootstrap_cpu], mem, 0x10080000);
			dev_ssc_init(cpus[bootstrap_cpu], mem, 0x10140000, 0, use_x11);	/*  TODO:  not irq 0  */
			break;

		case MACHINE_MAXINE_5000:	/*  type 7, KN02CA  */
			machine_name = "Personal DECstation 5000/xxx (MAXINE) (KN02CA)";
			if (emulated_ips == 0)
				emulated_ips = 33000000;

			if (physical_ram_in_mb < 8)
				fprintf(stderr, "WARNING! Real KN02CA machines do not have less than 8MB RAM. Continuing anyway.\n");
			if (physical_ram_in_mb > 40)
				fprintf(stderr, "WARNING! Real KN02CA machines cannot have more than 40MB RAM. Continuing anyway.\n");

			/*  Maxine interrupts:  */
			cpus[bootstrap_cpu]->md_interrupt = maxine_interrupt;

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
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 0, 0x10000000, 0x103fffff, "", XINE_INTR_TC_0 +8);
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 1, 0x14000000, 0x143fffff, "", XINE_INTR_TC_1 +8);

			/*  TURBOchannel slot 2 is hardwired to be used by the framebuffer: (NOTE: 0x8000000, not 0x18000000)  */
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 2, 0x8000000, 0xbffffff, "PMAG-DV", 0);

			/*  TURBOchannel slot 3: fixed, ioasic (the system stuff), 0x1c000000  */
			dev_scc_init(cpus[bootstrap_cpu], mem, 0x1c100000, XINE_INTR_SCC_0 +8, use_x11, 0);
			dev_mc146818_init(cpus[bootstrap_cpu], mem, 0x1c200000, XINE_INT_TOY, MC146818_DEC, 1, emulated_ips);
			dev_asc_init(cpus[bootstrap_cpu], mem, 0x1c300000, XINE_INTR_SCSI +8);

			framebuffer_console_name = "osconsole=3,2";	/*  keyb,fb ??  */
			serial_console_name      = "osconsole=3";
			break;

		case MACHINE_5500:	/*  type 11, KN220  */
			machine_name = "DECsystem 5500 (KN220)";

			/*
			 *  See KN220 docs for more info.
			 *
			 *  scc at 0x10140000
			 *  qbus at (or around) 0x10080000
			 *  dssi (disk controller) buffers at 0x10100000, registers at 0x10160000.
			 *  sgec (ethernet) registers at 0x10008000, station addresss at 0x10120000.
			 *  asc (scsi) at 0x17100000.
			 */

			dev_ssc_init(cpus[bootstrap_cpu], mem, 0x10140000, 0, use_x11);		/*  TODO:  not irq 0  */

			/*  something at 0x17000000, ultrix says "cpu 0 panic: DS5500 I/O Board is missing" if this is not here  */
			dev_dec5500_ioboard_init(cpus[bootstrap_cpu], mem, 0x17000000);

			dev_sgec_init(mem, 0x10008000, 0);		/*  irq?  */

			/*  The asc controller might be TURBOchannel-ish?  */
#if 0
			dev_turbochannel_init(cpus[bootstrap_cpu], mem, 0, 0x17100000, 0x171fffff, "PMAZ-AA", 0);	/*  irq?  */
#else
			dev_asc_init(cpus[bootstrap_cpu], mem, 0x17100000, 0);		/*  irq?  */
#endif

			framebuffer_console_name = "osconsole=0,0";	/*  TODO (?)  */
			serial_console_name      = "osconsole=0";
			break;

		case MACHINE_MIPSMATE_5100:	/*  type 12  */
			machine_name = "DEC MIPSMATE 5100 (KN230)";
			if (emulated_ips == 0)
				emulated_ips = 20000000;
			if (physical_ram_in_mb > 128)
				fprintf(stderr, "WARNING! Real MIPSMATE 5100 machines cannot have more than 128MB RAM. Continuing anyway.\n");

			if (use_x11)
				fprintf(stderr, "WARNING! Real MIPSMATE 5100 machines cannot have a graphical framebuffer. Continuing anyway.\n");

			/*  KN230 interrupts:  */
			cpus[bootstrap_cpu]->md_interrupt = kn230_interrupt;

			/*
			 *  According to NetBSD/pmax:
			 *  dc0 at ibus0 addr 0x1c000000
			 *  le0 at ibus0 addr 0x18000000: address 00:00:00:00:00:00
			 *  sii0 at ibus0 addr 0x1a000000
			 */
			dev_mc146818_init(cpus[bootstrap_cpu], mem, KN230_SYS_CLOCK, 4, MC146818_DEC, 1, emulated_ips);
			dev_dc7085_init(cpus[bootstrap_cpu], mem, KN230_SYS_DZ0, KN230_CSR_INTR_DZ0, use_x11);		/*  NOTE: CSR_INTR  */
			/* dev_dc7085_init(cpus[bootstrap_cpu], mem, KN230_SYS_DZ1, KN230_CSR_INTR_OPT0, use_x11); */	/*  NOTE: CSR_INTR  */
			/* dev_dc7085_init(cpus[bootstrap_cpu], mem, KN230_SYS_DZ2, KN230_CSR_INTR_OPT1, use_x11); */	/*  NOTE: CSR_INTR  */
			dev_le_init(mem, KN230_SYS_LANCE, KN230_SYS_LANCE_B_START, KN230_SYS_LANCE_B_END, KN230_CSR_INTR_LANCE, 4*1048576);
			dev_sii_init(cpus[bootstrap_cpu], mem, KN230_SYS_SII, KN230_SYS_SII_B_START, KN230_SYS_SII_B_END, KN230_CSR_INTR_SII);
			kn230_csr = dev_kn230_init(cpus[bootstrap_cpu], mem, KN230_SYS_ICSR);

			serial_console_name = "osconsole=0";
			break;

		default:
			;
		}

		/*  DECstation PROM stuff:  (TODO: endianness)  */
		for (i=0; i<100; i++)
			store_32bit_word(DEC_PROM_CALLBACK_STRUCT + i*4, DEC_PROM_EMULATION + i*4);

		/*  Fill PROM with dummy return instructions:  (TODO: make this nicer)  */
		for (i=0; i<100; i++) {
			store_32bit_word(0xbfc00000 + i*8,     0x03e00008);	/*  return  */
			store_32bit_word(0xbfc00000 + i*8 + 4, 0x00000000);	/*  nop  */
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

		cpus[bootstrap_cpu]->gpr[GPR_A0] = 2;
		cpus[bootstrap_cpu]->gpr[GPR_A1] = DEC_PROM_INITIAL_ARGV;
		cpus[bootstrap_cpu]->gpr[GPR_A2] = DEC_PROM_MAGIC;
		cpus[bootstrap_cpu]->gpr[GPR_A3] = DEC_PROM_CALLBACK_STRUCT;

		store_32bit_word(INITIAL_STACK_POINTER + 0x10, BOOTINFO_MAGIC);
		store_32bit_word(INITIAL_STACK_POINTER + 0x14, BOOTINFO_ADDR);

		store_32bit_word(DEC_PROM_INITIAL_ARGV, (uint32_t)(DEC_PROM_INITIAL_ARGV + 0x10));
		store_32bit_word(DEC_PROM_INITIAL_ARGV+4, (uint32_t)(DEC_PROM_INITIAL_ARGV + 0x70));
		store_32bit_word(DEC_PROM_INITIAL_ARGV+8, 0);

		/*
		 *  NOTE:  NetBSD and Ultrix expect their bootargs in different ways.
		 *
		 *	NetBSD:  "bootdev" "-a"
		 *	Ultrix:  "ultrixboot" [args?] "bootdev" [args?]
		 *
		 *  where bootdev is supposed to be "rz(0,0,0)netbsd" for 3100/2100
		 *  (although that crashes Ultrix :-/), and "5/rz0a/netbsd" for alll
		 *  others.  The number '5' is the slot number of the boot device.
		 *
		 *  TODO:  Make this nicer.
		 */
#if 0
		if (machine == MACHINE_PMAX_3100)
			init_bootpath = "rz(0,0,0)";
		else
#endif
			init_bootpath = "3/rz0/";

		tmp_ptr = rindex(last_filename, '/');
		if (tmp_ptr == NULL)
			tmp_ptr = last_filename;
		else
			tmp_ptr ++;

		bootstr = malloc(strlen(init_bootpath) + strlen(tmp_ptr) + 1);
		strcpy(bootstr, init_bootpath);
		strcat(bootstr, tmp_ptr);

		if (ultrixboot_emul) {
			/*  For Ultrixboot emulation:  */
			bootarg = bootstr;
			bootstr = "ultrixboot";
		} else {
			/*  NetBSD's second stage bootloader:  */
			bootarg = "-a";
		}

		store_string(DEC_PROM_INITIAL_ARGV+0x10, bootstr);
		store_string(DEC_PROM_INITIAL_ARGV+0x70, bootarg);

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

		store_buf(BOOTINFO_ADDR, (char *)&xx, sizeof(xx));

		/*  The system's memmap:  (memmap is a global variable, in dec_prom.h)  */
		store_32bit_word_in_host((unsigned char *)&memmap.pagesize, 4096);
		for (i=0; i<sizeof(memmap.bitmap); i++)
			memmap.bitmap[i] = (i * 4096*8 < 1048576*physical_ram_in_mb)? 0xff : 0x00;
		store_buf(DEC_MEMMAP_ADDR, (char *)&memmap, sizeof(memmap));

		/*  Environment variables:  */
		addr = DEC_PROM_STRINGS;

		if (use_x11)
			add_environment_string(framebuffer_console_name, &addr);	/*  (0,3)  Keyboard and Framebuffer  */
		else
			add_environment_string(serial_console_name, &addr);	/*  Serial console  */

		/*
		 *  The KN5800 (SMP system) uses a CCA (console communications area):
		 *  (See VAX 6000 documentation for details.)
		 */
		{
			char tmps[300];
			sprintf(tmps, "cca=%x", (int)DEC_PROM_CCA);
			add_environment_string(tmps, &addr);

			/*  These are needed, or Ultrix complains:  */
			store_byte(DEC_PROM_CCA + 6, 67);
			store_byte(DEC_PROM_CCA + 7, 67);

			store_byte(DEC_PROM_CCA + 8, ncpus);
			store_32bit_word(DEC_PROM_CCA + 20, (1 << ncpus) - 1);	/*  one bit for each cpu  */
		}

		add_environment_string("scsiid0=7", &addr);
		add_environment_string("bootmode=a", &addr);
		add_environment_string("testaction=q", &addr);
		add_environment_string("haltaction=h", &addr);
		add_environment_string("more=24", &addr);

		/*  Used in at least Ultrix on the 5100:  */
		add_environment_string("scsiid=7", &addr);
		add_environment_string("baud0=9600", &addr);
		add_environment_string("baud1=9600", &addr);
		add_environment_string("baud2=9600", &addr);
		add_environment_string("baud3=9600", &addr);
		add_environment_string("iooption=0x1", &addr);

		/*  The end:  */
		add_environment_string("", &addr);

		break;

	case EMULTYPE_COBALT:
		machine_name = "Cobalt";
		if (emulated_ips == 0)
			emulated_ips = 1000000;		/*  TODO: how fast are Cobalt machines?  */

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
/*		dev_XXX_init(cpus[bootstrap_cpu], mem, 0x10000000, emulated_ips);	*/
		dev_mc146818_init(cpus[bootstrap_cpu], mem, 0x10000070, 0, MC146818_PC_CMOS, 0x4, emulated_ips);  	/*  mcclock0  */
		dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x1c800000, 5, 1);				/*  com0  */

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
		pci_data = dev_gt_init(cpus[bootstrap_cpu], mem, 0x14000000, 2, 6);	/*  7 for PCI, not 6?  */
		/*  bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0,  7, 0, pci_dec21143_init, pci_dec21143_rr);  */
		bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0,  8, 0, NULL, NULL);  /*  PCI_VENDOR_SYMBIOS, PCI_PRODUCT_SYMBIOS_860  */
		bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0,  9, 0, pci_vt82c586_isa_init, pci_vt82c586_isa_rr);
		bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0,  9, 1, pci_vt82c586_ide_init, pci_vt82c586_ide_rr);
		/*  bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0, 12, 0, pci_dec21143_init, pci_dec21143_rr);  */

		/*
		 *  NetBSD/cobalt expects memsize in a0, but it seems that what
		 *  it really wants is the end of memory + 0x80000000.
		 *
		 *  The bootstring should be stored starting 512 bytes before end
		 *  of physical ram.
		 */
		cpus[bootstrap_cpu]->gpr[GPR_A0] = physical_ram_in_mb * 1048576 + 0x80000000;
		bootstr = "root=/dev/hda1 ro";
		/*  bootstr = "nfsroot=/usr/cobalt/";  */
		store_string(0x80000000 + physical_ram_in_mb * 1048576 - 512, bootstr);
		break;

	case EMULTYPE_HPCMIPS:
		machine_name = "hpcmips";
		dev_fb_init(cpus[bootstrap_cpu], mem, HPCMIPS_FB_ADDR, VFB_HPCMIPS, HPCMIPS_FB_XSIZE, HPCMIPS_FB_YSIZE,
		    HPCMIPS_FB_XSIZE, HPCMIPS_FB_YSIZE, 2, "HPCmips");

		/*
		 *  NetBSD/hpcmips expects the following:
		 */
		cpus[bootstrap_cpu]->gpr[GPR_A0] = 1;	/*  argc  */
		cpus[bootstrap_cpu]->gpr[GPR_A1] = physical_ram_in_mb * 1048576 + 0x80000000 - 512;	/*  argv  */
		cpus[bootstrap_cpu]->gpr[GPR_A2] = physical_ram_in_mb * 1048576 + 0x80000000 - 256;	/*  ptr to hpc_bootinfo  */
		bootstr = "netbsd";
		store_32bit_word(0x80000000 + physical_ram_in_mb * 1048576 - 512, 0x80000000 + physical_ram_in_mb * 1048576 - 512 + 8);
		store_32bit_word(0x80000000 + physical_ram_in_mb * 1048576 - 512 + 4, 0);
		store_string(0x80000000 + physical_ram_in_mb * 1048576 - 512 + 8, bootstr);
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
		store_buf(0x80000000 + physical_ram_in_mb * 1048576 - 256, (char *)&hpc_bootinfo, sizeof(hpc_bootinfo));
		break;

	case EMULTYPE_PS2:
		machine_name = "Playstation 2";

		if (physical_ram_in_mb != 32)
			fprintf(stderr, "WARNING! Playstation 2 machines are supposed to have exactly 32 MB RAM. Continuing anyway.\n");
		if (!use_x11)
			fprintf(stderr, "WARNING! Playstation 2 without -X is pretty meaningless. Continuing anyway.\n");

		dev_ps2_gs_init(mem, 0x12000000);
		dev_ps2_dmac_init(mem, 0x10008000, GLOBAL_gif_mem);

		add_symbol_name(PLAYSTATION2_SIFBIOS, 0x10000, "[SIFBIOS entry]", 0);
		store_32bit_word(PLAYSTATION2_BDA + 0, PLAYSTATION2_SIFBIOS);
		store_buf(PLAYSTATION2_BDA + 4, "PS2b", 4);

		cpus[bootstrap_cpu]->gpr[GPR_SP] = 0x80007f00;

		debug("adding playstation 1 memory: 4 MB\n");
		ps1_mem = memory_new(DEFAULT_BITS_PER_PAGETABLE, DEFAULT_BITS_PER_MEMBLOCK, 4 * 1048576, DEFAULT_MAX_BITS);
		if (ps1_mem == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}

		debug("adding playstation 1 cpu: R3000A\n");
		ps1_subcpu = cpu_new(ps1_mem, -1, "R3000A");

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
		machine_name = malloc(500);
		if (machine_name == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}

		if (emulation_type == EMULTYPE_SGI) {
			cpus[bootstrap_cpu]->byte_order = EMUL_BIG_ENDIAN;
			sprintf(machine_name, "SGI-IP%i", machine);
		} else {
			cpus[bootstrap_cpu]->byte_order = EMUL_LITTLE_ENDIAN;
			machine_name = "ARC";
		}

		if (physical_ram_in_mb < 16)
			fprintf(stderr, "WARNING! The ARC platform specification doesn't allow less than 16 MB of RAM. Continuing anyway.\n");

		/*  ARCBIOS:  */
		memset(&arcbios_spb, 0, sizeof(arcbios_spb));
		store_32bit_word_in_host((unsigned char *)&arcbios_spb.SPBSignature, ARCBIOS_SPB_SIGNATURE);
		store_16bit_word_in_host((unsigned char *)&arcbios_spb.Version, 1);
		store_16bit_word_in_host((unsigned char *)&arcbios_spb.Revision, emulation_type == EMULTYPE_SGI? 10 : 2);
		store_32bit_word_in_host((unsigned char *)&arcbios_spb.FirmwareVector, (uint32_t)ARC_FIRMWARE_VECTORS);
		store_32bit_word_in_host((unsigned char *)&arcbios_spb.FirmwareVectorLength, 100 * 4);	/*  ?  */
		store_buf(SGI_SPB_ADDR, (char *)&arcbios_spb, sizeof(arcbios_spb));

		memset(&arcbios_sysid, 0, sizeof(arcbios_sysid));
		if (emulation_type == EMULTYPE_SGI) {
			strncpy(arcbios_sysid.VendorId,  "SGI", 3);		/*  NOTE: max 8 chars  */
			switch (machine) {
			case 22:
				strncpy(arcbios_sysid.ProductId, "87654321", 8);	/*  some kind of ID?  */
				break;
			case 32:
				strncpy(arcbios_sysid.ProductId, "8", 1);		/*  6 or 8 (?)  */
				break;
			default:
				snprintf(arcbios_sysid.ProductId, 8, "IP%i", machine);
			}
		} else {
			/*
			 *  ARC:  TODO:  Support other machine types. Right now,
			 *  the "NEC-RD94" (NEC RISCstation 2250) is the only supported one.
			 */
			strncpy(arcbios_sysid.VendorId,  "NEC W&S", 8);	/*  NOTE: max 8 chars  */
			strncpy(arcbios_sysid.ProductId, "RD94", 4);	/*  NOTE: max 8 chars  */
		}
		store_buf(SGI_SYSID_ADDR, (char *)&arcbios_sysid, sizeof(arcbios_sysid));

		memset(&arcbios_dsp_stat, 0, sizeof(arcbios_dsp_stat));
		/*  TODO:  get 79 and 24 from the current terminal settings?  */
		store_16bit_word_in_host((unsigned char *)&arcbios_dsp_stat.CursorMaxXPosition, 79);
		store_16bit_word_in_host((unsigned char *)&arcbios_dsp_stat.CursorMaxYPosition, 24);
		arcbios_dsp_stat.ForegroundColor = 7;
		arcbios_dsp_stat.HighIntensity = 15;
		store_buf(ARC_DSPSTAT_ADDR, (char *)&arcbios_dsp_stat, sizeof(arcbios_dsp_stat));

		/*
		 *  The first 16 MBs of RAM are simply reserved... this simplifies things a lot.
		 *  If there's more than 512MB of RAM, it has to be split in two, according to
		 *  the ARC spec.  This code creates a number of chunks of at most 512MB each.
		 *
		 *  NOTE:  The region of physical address space between 0x10000000 and 0x1fffffff
		 *  (256 - 512 MB) is usually occupied by memory mapped devices, so that portion is "lost".
		 */
		mem_base = 16 * 1048576 / 4096;
		mem_count = physical_ram_in_mb <= 256? physical_ram_in_mb : 256;
		mem_count = (mem_count - 16) * 1048576 / 4096;

		/*  SUPER-special case:   SGI-IP22, ignore the lowest 128MB of RAM:  */
		if (emulation_type == EMULTYPE_SGI && machine == 22) {
			if (physical_ram_in_mb <= 128) {
				fatal("weird physical_ram_in_mb setting for SGI-IP22\n");
				exit(1);
			}
			mem_base  += (128 * 1048576) / 4096;
			mem_count -= (128 * 1048576) / 4096;
		}

		memset(&arcbios_mem, 0, sizeof(arcbios_mem));
		store_32bit_word_in_host((unsigned char *)&arcbios_mem.Type, emulation_type == EMULTYPE_SGI? 2 : 7);
		store_32bit_word_in_host((unsigned char *)&arcbios_mem.BasePage, mem_base);
		store_32bit_word_in_host((unsigned char *)&arcbios_mem.PageCount, mem_count);
		store_buf(ARC_MEMDESC_ADDR, (char *)&arcbios_mem, sizeof(arcbios_mem));

		mem_mb_left = physical_ram_in_mb - 512;
		mem_base = 512 * (1048576 / 4096);
		mem_bufaddr = ARC_MEMDESC_ADDR + sizeof(arcbios_mem);
		while (mem_mb_left > 0) {
			mem_count = (mem_mb_left <= 512? mem_mb_left : 512) * (1048576 / 4096);

			memset(&arcbios_mem, 0, sizeof(arcbios_mem));
			store_32bit_word_in_host((unsigned char *)&arcbios_mem.Type, emulation_type == EMULTYPE_SGI? 2 : 7);
			store_32bit_word_in_host((unsigned char *)&arcbios_mem.BasePage, mem_base);
			store_32bit_word_in_host((unsigned char *)&arcbios_mem.PageCount, mem_count);

			store_buf(mem_bufaddr, (char *)&arcbios_mem, sizeof(arcbios_mem));
			mem_bufaddr += sizeof(arcbios_mem);

			mem_mb_left -= 512;
			mem_base += 512 * (1048576 / 4096);
		}

		/*  End of memory descriptors:  (pagecount = zero)  */
		memset(&arcbios_mem, 0, sizeof(arcbios_mem));
		store_buf(mem_bufaddr, (char *)&arcbios_mem, sizeof(arcbios_mem));

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

		if (emulation_type == EMULTYPE_SGI) {
			system = arcbios_addchild_manual(COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
			    0, 1, 20, 0, 0x0, machine_name, 0  /*  ROOT  */);

			/*  TODO:  sync devices and component tree  */

			/*  TODO:  Other machine types?  */
			switch (machine) {
			case 20:
				strcat(machine_name, " (Indigo2)");
				dev_zs_init(cpus[bootstrap_cpu], mem, 0x1fbd9830, 8, 1);	/*  serial??  */
				break;
			case 22:
				strcat(machine_name, " (Indy, Indigo2, Challenge S)");

				/*
				 *  This would be one possible implementation of the 128MB mirroring
				 *  on IP22 machines.  However, an optimization in memory.c when
				 *  reading instructions assumes that instructions are read from
				 *  physical RAM.   (TODO)
				 */
				/*  dev_ram_init(mem, 128 * 1048576, 128 * 1048576, DEV_RAM_MIRROR, 0xa0000000);  */

				/*
				 *  According to NetBSD:
				 *
				 *  imc0 at mainbus0 addr 0x1fa00000, Revision 0
				 *  gio0 at imc0
				 *  hpc0 at gio0 addr 0x1fb80000: SGI HPC3
				 *  zsc0 at hpc0 offset 0x59830
				 *  zstty0 at zsc0 channel 1 (console i/o)
				 *  zstty1 at zsc0 channel 0
				 *  sq0 at hpc0 offset 0x54000: SGI Seeq 80c03	(Ethernet)
				 *  wdsc0 at hpc0 offset 0x44000: UNKNOWN SCSI, rev=12, target 7
				 *  scsibus2 at wdsc0: 8 targets, 8 luns per target
				 *  dsclock0 at hpc0 offset 0x60000
				 *  hpc1 at gio0 addr 0x1fb00000: SGI HPC3
				 *  zsc at hpc1 offset 0x59830 not configured
				 *  sq at hpc1 offset 0x54000 not configured
				 *  wdsc at hpc1 offset 0x44000 not configured
				 *  dsclock at hpc1 offset 0x60000 not configured
				 *  hpc2 at gio0 addr 0x1f980000: SGI HPC3
				 *  zsc at hpc2 offset 0x59830 not configured
				 *  sq at hpc2 offset 0x54000 not configured
				 *  wdsc at hpc2 offset 0x44000 not configured
				 *  dsclock at hpc2 offset 0x60000 not configured
				 */

				dev_zs_init(cpus[bootstrap_cpu], mem, 0x1fbd9830, 8, 1);	/*  zsc0 serial console  */
				dev_wdsc_init(mem, 0x1fbc4000); 	 			/*  wdsc0  */

				/*  How about these?  They are not detected by NetBSD:  */
				dev_zs_init(cpus[bootstrap_cpu], mem, 0x1fb59830, 8, 1);	/*  zsc1 serial  */
				dev_wdsc_init(mem, 0x1fb44000); 	 			/*  wdsc1  */
				dev_zs_init(cpus[bootstrap_cpu], mem, 0x1f9d9830, 8, 1);	/*  zsc2 serial  */
				dev_wdsc_init(mem, 0x1f9c4000); 	 			/*  wdsc2  */

				dev_sgi_ip22_init(cpus[bootstrap_cpu], mem, 0x1fbd9880);	/*  or 0x1fbd9000 on "fullhouse" machines?  */
				break;
			case 27:
				strcat(machine_name, " (Origin 200/2000, Onyx2)");
				/*  2 cpus per node  */

				/*
				 *  IRIX reads from the following addresses, so there's probably
				 *  something interesting there:
				 *
				 *  0x1fcffff0 <.MIPS.options+0x30>
				 *  0x19600000 <get_nasid+0x4>
				 *  0x190020d0 <get_cpuinfo+0x34>
				 */
				dev_zs_init(cpus[bootstrap_cpu], mem, 0x1fbd9830, 8, 1);	/*  serial??  */
				dev_sgi_nasid_init(mem, DEV_SGI_NASID_BASE);
				dev_sgi_cpuinfo_init(mem, DEV_SGI_CPUINFO_BASE);
				break;
			case 28:
				strcat(machine_name, " (Impact Indigo2 ?)");
				break;
			case 30:
				strcat(machine_name, " (Octane)");
				break;
			case 32:
				strcat(machine_name, " (O2)");

dev_ram_init(mem, 0x40000000000, 32 * 1048576, DEV_RAM_RAM, 0);
dev_ram_init(mem, 0x41000000000, 32 * 1048576, DEV_RAM_RAM, 0);
dev_ram_init(mem,    0x20000000, 32 * 1048576, DEV_RAM_RAM, 0);
dev_ram_init(mem,    0x40000000, 32 * 1048576, DEV_RAM_RAM, 0);

/*
dev_ram_init(mem, 0x40000000000, 128 * 1048576, DEV_RAM_MIRROR, 0xa0000000);
dev_ram_init(mem, 0x41000000000, 128 * 1048576, DEV_RAM_MIRROR, 0xa0000000);

dev_ram_init(mem, 0x42000000000, 128 * 1048576, DEV_RAM_MIRROR, 0xa0000000);
dev_ram_init(mem, 0x47000000000, 128 * 1048576, DEV_RAM_MIRROR, 0xa0000000);
dev_ram_init(mem,    0x20000000, 128 * 1048576, DEV_RAM_MIRROR, 0xa0000000);
dev_ram_init(mem,    0x40000000, 128 * 1048576, DEV_RAM_MIRROR, 0xb0000000);
*/
				dev_crime_init(cpus[bootstrap_cpu], mem, 0x14000000);	/*  crime0  */
				dev_sgi_mte_init(mem, 0x15000000);			/*  mte ??? memory thing  */
				dev_sgi_gbe_init(cpus[bootstrap_cpu], mem, 0x16000000);	/*  gbe?  framebuffer?  */
				/*  0x17000000: something called 'VICE' in linux  */
				dev_8250_init(cpus[bootstrap_cpu], mem, 0x18000300, 0, 0x1);	/*  serial??  */
				pci_data = dev_macepci_init(mem, 0x1f080000, 0);	/*  macepci0  */
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
				dev_mace_init(mem, 0x1f310000, 2);					/*  mace0  */
				cpus[bootstrap_cpu]->md_interrupt = sgi_mace_interrupt;
				dev_pckbc_init(cpus[bootstrap_cpu], mem, 0x1f320000, PCKBC_8242, 0x200, 0x800);	/*  keyb+mouse (mace irq numbers)  */
				dev_sgi_ust_init(mem, 0x1f340000);					/*  ust?  */
				dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x1f390000, 2, 0x100);	/*  com0  */
				dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x1f398000, 0, 0x100);	/*  com1  */
				dev_mc146818_init(cpus[bootstrap_cpu], mem, 0x1f3a0000, 0, MC146818_SGI, 0x40, emulated_ips);  /*  mcclock0  */
				dev_zs_init(cpus[bootstrap_cpu], mem, 0x1fbd9830, 0, 1);	/*  serial??  */

				/*
				 *  PCI devices:   (according to NetBSD's GENERIC config file for sgimips)
				 *
				 *	ne*             at pci? dev ? function ?
				 *	ahc0            at pci0 dev 1 function ?
				 *	ahc1            at pci0 dev 2 function ?
				 */

				/*  bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0, 0, 0, pci_ne2000_init, pci_ne2000_rr);  TODO  */
/*				bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0, 1, 0, pci_ahc_init, pci_ahc_rr);  */
				/*  bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0, 2, 0, pci_ahc_init, pci_ahc_rr);  */

				break;
			case 35:
				strcat(machine_name, " (Origin 3000)");
				/*  4 cpus per node  */
				break;
			default:
				fatal("unimplemented SGI machine type IP%i\n", machine);
				exit(1);
			}
		} else {
			system = arcbios_addchild_manual(COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
			    0, 1, 20, 0, 0x0, "NEC-RD94", 0  /*  ROOT  */);

			/*  TODO:  sync devices and component tree  */
			/*  TODO 2: These are model dependant!!!  */
			pci_data = dev_rd94_init(cpus[bootstrap_cpu], mem, 0x2000000000, 0);
			dev_mc146818_init(cpus[bootstrap_cpu], mem, 0x2000004000, 0, MC146818_ARC_NEC, 1, emulated_ips);	/*  ???  */
			dev_pckbc_init(cpus[bootstrap_cpu], mem, 0x2000005000, PCKBC_8042, 0, 0);		/*  ???  */
			dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x2000006000, 3, 1);		/*  com0  */
			dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x2000007000, 0, 1);		/*  com1  */
			/*  lpt at 0x2000008000  */
			dev_fdc_init(mem, 0x200000c000, 0);					/*  fdc  */

			/*  This DisplayController needs to be here, to allow NetBSD to use the TGA card:  */
			/*  Actually class COMPONENT_CLASS_ControllerClass, type COMPONENT_TYPE_DisplayController  */
			if (use_x11)
				arcbios_addchild_manual(4, 19,  0, 1, 20, 0, 0x0, "10110004", system);

			/*  PCI devices:  (NOTE: bus must be 0, device must be 3, 4, or 5, for NetBSD to accept interrupts)  */
			bus_pci_add(cpus[bootstrap_cpu], pci_data, mem, 0, 3, 0, pci_dec21030_init, pci_dec21030_rr);	/*  tga graphics  */
		}

		/*
		 *  Common stuff for both SGI and ARC:
		 */
		debug("system = 0x%x\n", system);

		for (i=0; i<ncpus; i++) {
			uint32_t cpu, fpu;
			int jj;
			char arc_cpu_name[100];
			char arc_fpc_name[105];

			strncpy(arc_cpu_name, emul_cpu_name, sizeof(arc_cpu_name));
			arc_cpu_name[sizeof(arc_cpu_name)-1] = 0;
			for (jj=0; jj<strlen(arc_cpu_name); jj++)
				if (arc_cpu_name[jj] >= 'a' && arc_cpu_name[jj] <= 'z')
					arc_cpu_name[jj] += ('A' - 'a');

			strcpy(arc_fpc_name, arc_cpu_name);
			strcat(arc_fpc_name, "FPC");

			cpu = arcbios_addchild_manual(COMPONENT_CLASS_ProcessorClass, COMPONENT_TYPE_CPU,
			    0, 1, 20, 0, 0x0, arc_cpu_name, system);
			fpu = arcbios_addchild_manual(COMPONENT_CLASS_ProcessorClass, COMPONENT_TYPE_FPU,
			    0, 1, 20, 0, 0x0, arc_fpc_name, cpu);

			/*  TODO:  cache (per cpu)  */
			debug("cpu%i = 0x%x  (fpu = 0x%x)\n", i, cpu, fpu);
/*  NetBSD:
case arc_CacheClass:
                if (cf->type == arc_SecondaryDcache)
                        arc_cpu_l2cache_size = 4096 << (cf->key & 0xffff);
*/
		}


		add_symbol_name(ARC_FIRMWARE_ENTRIES, 0x10000, "[ARCBIOS entry]", 0);

		for (i=0; i<100; i++)
			store_32bit_word(ARC_FIRMWARE_VECTORS + i*4, ARC_FIRMWARE_ENTRIES + i*4);

		cpus[bootstrap_cpu]->gpr[GPR_A0] = 9;
		cpus[bootstrap_cpu]->gpr[GPR_A1] = ARC_ARGV_START;

		store_32bit_word(ARC_ARGV_START, ARC_ARGV_START + 0x100);
		store_32bit_word(ARC_ARGV_START + 0x4, ARC_ARGV_START + 0x180);
		store_32bit_word(ARC_ARGV_START + 0x8, ARC_ARGV_START + 0x200);
		store_32bit_word(ARC_ARGV_START + 0xc, ARC_ARGV_START + 0x220);
		store_32bit_word(ARC_ARGV_START + 0x10, ARC_ARGV_START + 0x240);
		store_32bit_word(ARC_ARGV_START + 0x14, ARC_ARGV_START + 0x260);
		store_32bit_word(ARC_ARGV_START + 0x18, ARC_ARGV_START + 0x280);
		store_32bit_word(ARC_ARGV_START + 0x1c, ARC_ARGV_START + 0x2a0);
		store_32bit_word(ARC_ARGV_START + 0x20, ARC_ARGV_START + 0x2c0);
		store_32bit_word(ARC_ARGV_START + 0x24, 0);

		/*  Boot string in ARC format:  */
		init_bootpath = "scsi(0)disk(0)rdisk(0)partition(0)\\";
		tmp_ptr = rindex(last_filename, '/');
		if (tmp_ptr == NULL)
			tmp_ptr = last_filename;
		else
			tmp_ptr ++;
		bootstr = malloc(strlen(init_bootpath) + strlen(tmp_ptr) + 1);
		strcpy(bootstr, init_bootpath);
		strcat(bootstr, tmp_ptr);

		bootarg = "-a";

		/*
		 *  See http://guinness.cs.stevens-tech.edu/sgidocs/SGI_EndUser/books/IRIX_EnvVar/sgi_html/ch02.html
		 *  for more options.  It seems that on SGI machines, _ALL_ environment
		 *  variables are passed on the command line.  (This is not true for ARC? TODO)
		 */

		store_string(ARC_ARGV_START + 0x100, bootstr);

		if (use_x11) {
			store_string(ARC_ARGV_START + 0x180, "console=g");
			store_string(ARC_ARGV_START + 0x200, "ConsoleIn=keyboard()");
			store_string(ARC_ARGV_START + 0x220, "ConsoleOut=video()");
		} else {
#if 1
			store_string(ARC_ARGV_START + 0x180, "console=ttyS0");	/*  Linux  */
#else
			store_string(ARC_ARGV_START + 0x180, "console=d2");	/*  Irix  */
#endif
			store_string(ARC_ARGV_START + 0x200, "ConsoleIn=serial(0)");
			store_string(ARC_ARGV_START + 0x220, "ConsoleOut=serial(0)");
		}

		store_string(ARC_ARGV_START + 0x240, "cpufreq=3");
		store_string(ARC_ARGV_START + 0x260, "dbaud=9600");
		store_string(ARC_ARGV_START + 0x280, "verbose=istrue");
		store_string(ARC_ARGV_START + 0x2a0, "showconfig=istrue");
		store_string(ARC_ARGV_START + 0x2c0, bootarg);

		/*  TODO:  not needed?  */
		cpus[bootstrap_cpu]->gpr[GPR_SP] = physical_ram_in_mb * 1048576 + 0x80000000 - 0x2080;

		addr = SGI_ENV_STRINGS;

		if (use_x11) {
			if (emulation_type == EMULTYPE_ARC) {
				add_environment_string("ConsoleIn=multi()key()keyboard()console()", &addr);
				add_environment_string("ConsoleOut=multi()video()monitor()console()", &addr);
			} else {
				add_environment_string("ConsoleIn=keyboard()", &addr);
				add_environment_string("ConsoleOut=video()", &addr);
			}

			add_environment_string("console=g", &addr);
		} else {
			add_environment_string("ConsoleIn=serial(0)", &addr);
			add_environment_string("ConsoleOut=serial(0)", &addr);
			add_environment_string("console=d2", &addr);		/*  d2 = serial?  */
		}
		add_environment_string("cpufreq=3", &addr);
		add_environment_string("dbaud=9600", &addr);
		add_environment_string("eaddr=00:00:00:00:00:00", &addr);
		add_environment_string("verbose=istrue", &addr);
		add_environment_string("showconfig=istrue", &addr);
		add_environment_string("", &addr);	/*  the end  */

		break;

	case EMULTYPE_NINTENDO64:
		machine_name = "Nintendo 64";
		cpus[bootstrap_cpu]->byte_order = EMUL_BIG_ENDIAN;

		/*
		 *  Nintendo 64 emulation is not very important:
		 *
		 *	o)  There isn't that much software out there to run
		 *	    in the emulator, except for games. :-/
		 *
		 *	o)  There are already other emulators out there,
		 *	    specific for Nintendo 64, which are capable of
		 *	    playing games (which is what the N64 is supposed to do)
		 *
		 *  The N64 is supposed to have 4MB SDRAM, some kind of ROM,
		 *  and some surrounding chips.  Games are loaded via cartridges.
		 *
		 *	List according to http://n64.icequake.net/mirror/www.cd64.com/cd64_circuit.htm
		 *
		 *	0xb0??????	bios
		 *	0xb1??????	reserved
		 *	0xb2??????	cartridge
		 *	0xb3??????	cartridge
		 *	0xb4??????	dram
		 *	0xb5??????	dram
		 *	0xb6??????	reserved
		 *	0xb7??????	srom i/o, reg
		 *
		 *  TODO:  This emulation is 99.999% non-functional.
		 */

		/*  Numbers from Mupen64:  */
		cpus[bootstrap_cpu]->gpr[ 1] = 0x0000000000000001;
		cpus[bootstrap_cpu]->gpr[ 2] = 0x000000000EBDA536;
		cpus[bootstrap_cpu]->gpr[ 3] = 0x000000000EBDA536;
		cpus[bootstrap_cpu]->gpr[ 4] = 0x000000000000A536;
		cpus[bootstrap_cpu]->gpr[ 5] = 0xFFFFFFFFC0F1D859;
		cpus[bootstrap_cpu]->gpr[ 6] = 0xFFFFFFFFA4001F0C;
		cpus[bootstrap_cpu]->gpr[ 7] = 0xFFFFFFFFA4001F08;
		cpus[bootstrap_cpu]->gpr[ 8] = 0x00000000000000C0;
		cpus[bootstrap_cpu]->gpr[10] = 0x0000000000000040;
		cpus[bootstrap_cpu]->gpr[11] = 0xFFFFFFFFA4000040;
		cpus[bootstrap_cpu]->gpr[12] = 0xFFFFFFFFED10D0B3;
		cpus[bootstrap_cpu]->gpr[13] = 0x000000001402A4CC;
		cpus[bootstrap_cpu]->gpr[14] = 0x000000002DE108EA;
		cpus[bootstrap_cpu]->gpr[15] = 0x000000003103E121;
		cpus[bootstrap_cpu]->gpr[22] = 0x000000000000003F;
		cpus[bootstrap_cpu]->gpr[23] = 0x0000000000000006;
		cpus[bootstrap_cpu]->gpr[25] = 0xFFFFFFFF9DEBB54F;
		cpus[bootstrap_cpu]->gpr[29] = 0xFFFFFFFFA4001FF0;
		cpus[bootstrap_cpu]->gpr[31] = 0xFFFFFFFFA4001554;

		/*  TODO:  DEV_RAM_MIRROR instead of copying  */
		dev_ram_init(mem, 0x04000000, 2048, DEV_RAM_RAM, 0);
		dev_ram_init(mem, 0x10000000, 2048, DEV_RAM_RAM, 0);

		/*  Copy 1KB from 0x80000000 to 0xa4000000 and to 0xb0000000.  */

		for (i=0; i<1024; i++) {
			unsigned char byte;
			byte = read_char_from_memory(cpus[bootstrap_cpu], 0, 0x80000000 + i);
			store_byte(0xa4000000 + i, byte);
			store_byte(0xb0000000 + i, byte);
		}

		cpus[bootstrap_cpu]->pc = 0xa4000040;

		dev_n64_bios_init(mem, 0x03f00000);

		break;

	default:
		;
	}

	if (machine_name != NULL)
		debug("machine: %s", machine_name);

	if (emulated_ips != 0)
		debug(" (%.2f MHz)\n", (float)emulated_ips / 1000000);
	else
		debug("\n");

	if (bootstr != NULL) {
		debug("bootstring%s: %s", bootarg==NULL? "": "(+bootarg)", bootstr);
		if (bootarg != NULL)
			debug(" %s", bootarg);
		debug("\n");
	}
}

