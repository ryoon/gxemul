/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 *  $Id: machine.c,v 1.19 2003-12-29 00:52:14 debug Exp $
 *
 *  Emulation of specific machines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "misc.h"
#include "devices.h"

#include "dec_5100.h"
#include "dec_kn01.h"
#include "dec_kn02.h"
#include "dec_kn03.h"
#include "dec_kmin.h"


extern int emulation_type;
extern char *machine_name;

extern int bootstrap_cpu;
extern int ncpus;
extern struct cpu **cpus;
extern int emulation_type;
extern int emulated_ips;
extern int machine;
extern char *machine_name;
extern int physical_ram_in_mb;

extern int use_x11;

extern char *last_filename;

uint64_t file_loaded_end_addr = 0;

extern struct memory *GLOBAL_gif_mem;

struct kn230_csr *kn230_csr;
struct kn02_csr *kn02_csr;
/*  TODO:  struct kmin_csr *kmin_csr;  */


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
		char ch = '\0';
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
	while (len-- > 0)
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

	/*  Generic bootstring stuff:  */
	char *bootstr = NULL;
	char *bootarg = NULL;
	char *tmp_ptr, *init_bootpath;

	/*  Framebuffer stuff:  */
	struct vfb_data *fb;

	/*  Playstation:  */
	struct cpu *ps1_subcpu;
	struct memory *ps1_mem;

	machine_name = NULL;

	switch (emulation_type) {
	case EMULTYPE_NONE:
		dev_cons_init(mem);
		dev_mp_init(mem, cpus);
		break;

	case EMULTYPE_DEC:
		/*  An R2020 or R3220 memory thingy:  */
		cpus[0]->coproc[3] = coproc_new(cpus[0], 3);

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
			fb = dev_fb_init(cpus[0], mem, KN01_PHYS_FBUF_START, color_fb_flag? VFB_DEC_VFB02 : VFB_DEC_VFB01,
			    0,0,0,0,0, color_fb_flag? "VFB02":"VFB01");
			dev_colorplanemask_init(mem, KN01_PHYS_COLMASK_START, &fb->color_plane_mask);
			dev_vdac_init(mem, KN01_SYS_VDAC, fb->rgb_palette, color_fb_flag);
			dev_le_init(mem, KN01_SYS_LANCE, KN01_SYS_LANCE_B_START, KN01_SYS_LANCE_B_END, KN01_INT_LANCE);
			dev_sii_init(cpus[0], mem, KN01_SYS_SII, KN01_SYS_SII_B_START, KN01_SYS_SII_B_END, KN01_INT_SII);
			dev_dc7085_init(cpus[0], mem, KN01_SYS_DZ, KN01_INT_DZ, use_x11);
			dev_mc146818_init(cpus[0], mem, KN01_SYS_CLOCK, KN01_INT_CLOCK, 0, 1, emulated_ips);
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
			cpus[0]->coproc[3] = coproc_new(cpus[0], 3);

			/*
			 *  According to NetBSD/pmax:
			 *  asc0 at tc0 slot 5 offset 0x0
			 *  le0 at tc0 slot 6 offset 0x0
			 *  ibus0 at tc0 slot 7 offset 0x0
			 *  dc0 at ibus0 addr 0x1fe00000
			 *  mcclock0 at ibus0 addr 0x1fe80000: mc146818 or compatible
			 */

			/*  TURBOchannel slots 0, 1, and 2 are free for option cards.  */
			dev_turbochannel_init(cpus[0], mem, 0, KN02_PHYS_TC_0_START, KN02_PHYS_TC_0_END, "PMAG-AA", KN02_IP_SLOT0 +8);
			dev_turbochannel_init(cpus[0], mem, 1, KN02_PHYS_TC_1_START, KN02_PHYS_TC_1_END, "", KN02_IP_SLOT1 +8);
			dev_turbochannel_init(cpus[0], mem, 2, KN02_PHYS_TC_2_START, KN02_PHYS_TC_2_END, "", KN02_IP_SLOT2 +8);

			/*  TURBOchannel slots 3 and 4 are reserved.  */

			/*  TURBOchannel slot 5 is PMAZ-AA (asc SCSI), 6 is PMAD-AA (LANCE ethernet).  */
			dev_turbochannel_init(cpus[0], mem, 5, KN02_PHYS_TC_5_START, KN02_PHYS_TC_5_END, "PMAZ-AA", KN02_IP_SCSI +8);
			dev_turbochannel_init(cpus[0], mem, 6, KN02_PHYS_TC_6_START, KN02_PHYS_TC_6_END, "PMAD-AA", KN02_IP_LANCE +8);

			/*  TURBOchannel slot 7 is system stuff.  */
			dev_dc7085_init(cpus[0], mem, KN02_SYS_DZ, KN02_IP_DZ +8, use_x11);
			dev_mc146818_init(cpus[0], mem, KN02_SYS_CLOCK, KN02_INT_CLOCK, 0, 1, emulated_ips);

			/*  (kn02 shared irq numbers (IP) are offset by +8 in the emulator)  */
			kn02_csr = dev_kn02_init(cpus[0], mem, KN02_SYS_CSR);

			framebuffer_console_name = "osconsole=0,7";	/*  fb,keyb  */
			serial_console_name      = "osconsole=2";
			break;

		case MACHINE_3MIN_5000:		/*  type 3, KN02BA  */
			machine_name = "DECstation 5000/112 or 145 (3MIN, KN02BA)";
			if (emulated_ips == 0)
				emulated_ips = 33000000;
			if (physical_ram_in_mb > 128)
				fprintf(stderr, "WARNING! Real 3MIN machines cannot have more than 128MB RAM. Continuing anyway.\n");

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
			dev_scc_init(cpus[0], mem, 0x1c180000, KMIN_INTR_SCC_1 +8, use_x11);
			dev_mc146818_init(cpus[0], mem, 0x1c200000, KMIN_INTR_CLOCK +8, 0, 1, emulated_ips);
			dev_asc_init(cpus[0], mem, 0x1c300000, KMIN_INTR_SCSI +8);

			/*  TURBOchannel slots 0, 1, and 2 are free for option cards. TODO: irqs  */
			dev_turbochannel_init(cpus[0], mem, 0, 0x10000000, 0x103fffff, "PMAG-BA", KMIN_INT_TC0);
			dev_turbochannel_init(cpus[0], mem, 1, 0x14000000, 0x143fffff, "", KMIN_INT_TC1);
			dev_turbochannel_init(cpus[0], mem, 2, 0x18000000, 0x183fffff, "", KMIN_INT_TC2);

			/*  (kmin shared irq numbers (IP) are offset by +8 in the emulator)  */
			/*  TODO:  kmin_csr = dev_kmin_init(cpus[0], mem, KMIN_REG_INTR);  */

			framebuffer_console_name = "osconsole=0,3";	/*  fb, keyb (?)  */
			serial_console_name      = "osconsole=3";	/*  ?  */
			break;

		case MACHINE_3MAXPLUS_5000:	/*  type 4, KN03  */
			machine_name = "DECsystem 5900 or 5000 (3MAX+) (KN03)";
			if (emulated_ips == 0)
				emulated_ips = 40000000;
			if (physical_ram_in_mb > 480)
				fprintf(stderr, "WARNING! Real KN03 machines cannot have more than 480MB RAM. Continuing anyway.\n");

			/*
			 *  tc0 at mainbus0: 25 MHz clock (slot 0)			(0x1e000000)
			 *  tc0 slot 1							(0x1e800000)
			 *  tc0 slot 2							(0x1f000000)
			 *  ioasic0 at tc0 slot 3 offset 0x0				(0x1f800000)
			 *  le0 at ioasic0 offset 0xc0000				(0x1f8c0000)
			 *  scc0 at ioasic0 offset 0x100000				(0x1f900000)
			 *  scc1 at ioasic0 offset 0x180000: console			(0x1f980000)
			 *  mcclock0 at ioasic0 offset 0x200000: mc146818 or compatible	(0x1fa00000)
			 *  asc0 at ioasic0 offset 0x300000: NCR53C94, 25MHz, SCSI ID 7	(0x1fb00000)
			 */
			dev_le_init(mem, KN03_SYS_LANCE, 0, 0, KN03_INTR_LANCE +8);
			dev_scc_init(cpus[0], mem, KN03_SYS_SCC_1, KN03_INTR_SCC_1 +8, use_x11);
			dev_mc146818_init(cpus[0], mem, KN03_SYS_CLOCK, KN03_INT_RTC, 0, 1, emulated_ips);
			dev_asc_init(cpus[0], mem, KN03_SYS_SCSI, KN03_INTR_SCSI +8);

			/*  TURBOchannel slots 0, 1, and 2 are free for option cards.  TODO: irqs */
			dev_turbochannel_init(cpus[0], mem, 0, KN03_PHYS_TC_0_START, KN03_PHYS_TC_0_END, "PMAG-AA", KN03_INTR_TC_0 +8);
			dev_turbochannel_init(cpus[0], mem, 1, KN03_PHYS_TC_1_START, KN03_PHYS_TC_1_END, "", KN03_INTR_TC_1 +8);
			dev_turbochannel_init(cpus[0], mem, 2, KN03_PHYS_TC_2_START, KN03_PHYS_TC_2_END, "", KN03_INTR_TC_2 +8);

			/*  TODO: interrupts  */
			/*  shared (turbochannel) interrupts are +8  */

			framebuffer_console_name = "osconsole=0,3";	/*  fb, keyb (?)  */
			serial_console_name      = "osconsole=3";	/*  ?  */
			break;

		case MACHINE_5800:		/*  type 5, KN5800  */
			/*  KN5800  */
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
			dev_kn210_init(cpus[0], mem, 0x10080000);
			dev_ssc_init(cpus[0], mem, 0x10140000, 0, use_x11);	/*  TODO:  not irq 0  */
			break;

		case MACHINE_MAXINE_5000:	/*  type 7, KN02CA  */
			machine_name = "Personal DECstation 5000/xxx (MAXINE) (KN02CA)";
			if (emulated_ips == 0)
				emulated_ips = 33000000;

			if (physical_ram_in_mb < 8)
				fprintf(stderr, "WARNING! Real KN02CA machines do not have less than 8MB RAM. Continuing anyway.\n");
			if (physical_ram_in_mb > 40)
				fprintf(stderr, "WARNING! Real KN02CA machines cannot have more than 40MB RAM. Continuing anyway.\n");

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

			/*  TURBOchannel slots (0 and 1). TODO: irqs  */
			dev_turbochannel_init(cpus[0], mem, 0, 0x10000000, 0x103fffff, "", 0);
			dev_turbochannel_init(cpus[0], mem, 1, 0x14000000, 0x143fffff, "", 0);

			/*  TURBOchannel slot 2 is hardwired to be used by the framebuffer: (NOTE: 0x8000000, not 0x18000000)  */
			dev_turbochannel_init(cpus[0], mem, 2, 0x8000000, 0xbffffff, "PMAG-DV", 0);
/*			fb = dev_fb_init(cpus[0], mem, 0xa000000, VFB_DEC_MAXINE, 0,0,0,0,0, "Maxine");  */

			/*  TURBOchannel slot 3: fixed, ioasic (the system stuff), 0x1c000000  */
			dev_scc_init(cpus[0], mem, 0x1c100000, 0, use_x11);
			dev_mc146818_init(cpus[0], mem, 0x1c200000, 3, 0, 1, emulated_ips);
			dev_asc_init(cpus[0], mem, 0x1c300000, 0);	/*  (?)  SCSI  */

			framebuffer_console_name = "osconsole=3,2";	/*  keyb,fb ??  */
			serial_console_name      = "osconsole=2";
			break;

		case MACHINE_5500:	/*  type 11, KN220  */
			machine_name = "DECsystem 5500 (KN220)";
			dev_ssc_init(cpus[0], mem, 0x10140000, 0, use_x11);	/*  A wild guess. TODO:  not irq 0  */
			break;

		case MACHINE_MIPSMATE_5100:	/*  type 12  */
			machine_name = "DEC MIPSMATE 5100";
			if (emulated_ips == 0)
				emulated_ips = 20000000;
			if (physical_ram_in_mb > 128)
				fprintf(stderr, "WARNING! Real MIPSMATE 5100 machines cannot have more than 128MB RAM. Continuing anyway.\n");

			if (use_x11)
				fprintf(stderr, "WARNING! Real MIPSMATE 5100 machines cannot have a graphical framebuffer. Continuing anyway.\n");

			/*
			 *  According to NetBSD/pmax:
			 *  dc0 at ibus0 addr 0x1c000000
			 *  le0 at ibus0 addr 0x18000000: address 00:00:00:00:00:00
			 *  sii0 at ibus0 addr 0x1a000000
			 *
			 *  The KN230 cpu board has several devices sharing the same IRQ, so
			 *  the kn230 CSR contains info about which devices have caused interrupts.
			 */
			dev_mc146818_init(cpus[0], mem, KN230_SYS_CLOCK, 4, 0, 1, emulated_ips);
			dev_dc7085_init(cpus[0], mem, KN230_SYS_DZ0, KN230_CSR_INTR_DZ0, use_x11);		/*  NOTE: CSR_INTR  */
			/* dev_dc7085_init(cpus[0], mem, KN230_SYS_DZ1, KN230_CSR_INTR_OPT0, use_x11); */	/*  NOTE: CSR_INTR  */
			/* dev_dc7085_init(cpus[0], mem, KN230_SYS_DZ2, KN230_CSR_INTR_OPT1, use_x11); */	/*  NOTE: CSR_INTR  */
			dev_le_init(mem, KN230_SYS_LANCE, KN230_SYS_LANCE_B_START, KN230_SYS_LANCE_B_END, KN230_CSR_INTR_LANCE);
			dev_sii_init(cpus[0], mem, KN230_SYS_SII, KN230_SYS_SII_B_START, KN230_SYS_SII_B_END, KN230_CSR_INTR_SII);
			kn230_csr = dev_kn230_init(cpus[0], mem, KN230_SYS_ICSR);

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
		store_32bit_word(DEC_PROM_INITIAL_ARGV+4, (uint32_t)(DEC_PROM_INITIAL_ARGV + 0x40));
		store_32bit_word(DEC_PROM_INITIAL_ARGV+8, 0);

		/*  For ultrixboot, these might work:  */
		bootstr = "boot"; bootarg = "0/tftp/vmunix";

		/*
		 *  For booting NetBSD or Ultrix immediately, the following might work:
		 *     "rz(0,0,0)netbsd" for 3100/2100, "5/rz0a/netbsd" for others
		 *  where netbsd is the name of the kernel.  This fakes the bootstring
		 *  as given to the kernel by ultrixboot.
		 */
		if (machine == MACHINE_PMAX_3100)
			init_bootpath = "rz(0,0,0)";
		else
			init_bootpath = "5/rz0/";

		tmp_ptr = rindex(last_filename, '/');
		if (tmp_ptr == NULL)
			tmp_ptr = last_filename;
		else
			tmp_ptr ++;

		bootstr = malloc(strlen(init_bootpath) + strlen(tmp_ptr) + 1);
		strcpy(bootstr, init_bootpath);
		strcat(bootstr, tmp_ptr);
		bootarg = "-a";

		store_string(DEC_PROM_INITIAL_ARGV+0x10, bootstr);
		store_string(DEC_PROM_INITIAL_ARGV+0x40, bootarg);

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

		add_environment_string("scsiid0=7", &addr);
		add_environment_string("", &addr);	/*  the end  */

/*  cpus[0]->gpr[GPR_SP] = physical_ram_in_mb*1048576 + 0x80000000 - 0x2100;  */

		break;

	case EMULTYPE_COBALT:
		machine_name = "Cobalt";
		if (emulated_ips == 0)
			emulated_ips = 1000000;		/*  TODO: how fast are Cobalt machines?  */

		dev_mc146818_init(cpus[0], mem, 0x10000000, 4, 1, 1, emulated_ips);	/*  ???  */
		dev_gt_init(mem, 0x14000000, 0);			/*  ???  */
		dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x1c800000, 5, 1);		/*  ???  */

		/*
		 *  NetBSD/cobalt expects memsize in a0, but it seems that what
		 *  it really wants is the end of memory + 0x80000000.
		 *
		 *  The bootstring should be stored starting 512 bytes before end
		 *  of physical ram.
		 */
		cpus[bootstrap_cpu]->gpr[GPR_A0] = physical_ram_in_mb * 1048576 + 0x80000000;
		/*  bootstr = "root=/dev/hda1 ro nfsroot=/usr/cobalt/";  */
		bootstr = "nfsroot=/usr/cobalt/";
		store_string(0x80000000 + physical_ram_in_mb * 1048576 - 512, bootstr);
		break;

	case EMULTYPE_HPCMIPS:
		machine_name = "hpcmips";
		dev_fb_init(cpus[0], mem, HPCMIPS_FB_ADDR, VFB_HPCMIPS, HPCMIPS_FB_XSIZE, HPCMIPS_FB_YSIZE,
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
		hpc_bootinfo.platid_cpu = random();
		hpc_bootinfo.platid_machine = random();
		printf("hpc_bootinfo.platid_cpu = 0x%x\n", hpc_bootinfo.platid_cpu);
		printf("hpc_bootinfo.platid_machine = 0x%x\n", hpc_bootinfo.platid_machine);
		hpc_bootinfo.timezone = 0;
		store_buf(0x80000000 + physical_ram_in_mb * 1048576 - 256, (char *)&hpc_bootinfo, sizeof(hpc_bootinfo));
		break;

	case EMULTYPE_PS2:
		machine_name = "Playstation 2";

		if (physical_ram_in_mb != 32)
			fprintf(stderr, "WARNING! Playstation 2 machines are supposed to have exactly 32 MB RAM. Continuing anyway.\n");

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

		if (emulation_type == EMULTYPE_SGI)
			sprintf(machine_name, "SGI-IP%i", machine);
		else
			machine_name = "ARC";

		if (physical_ram_in_mb < 16)
			fprintf(stderr, "WARNING! The ARC platform specification doesn't allow less than 16 MB of RAM. Continuing anyway.\n");

		/*  ARCBIOS:  */
		memset(&arcbios_spb, 0, sizeof(arcbios_spb));
		store_32bit_word_in_host((unsigned char *)&arcbios_spb.SPBSignature, ARCBIOS_SPB_SIGNATURE);
		store_32bit_word_in_host((unsigned char *)&arcbios_spb.FirmwareVector, 0xbfc00000);
		store_buf(SGI_SPB_ADDR, (char *)&arcbios_spb, sizeof(arcbios_spb));

		memset(&arcbios_sysid, 0, sizeof(arcbios_sysid));
		if (emulation_type == EMULTYPE_SGI) {
			strncpy(arcbios_sysid.VendorId,  "SGI", 3);		/*  NOTE: max 8 chars  */
			sprintf(arcbios_sysid.ProductId, "IP%i", machine);	/*  NOTE: max 8 chars  */
		} else {
			/*  NEC-RD94 = NEC RISCstation 2250  */
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

		memset(&arcbios_mem, 0, sizeof(arcbios_mem));
		store_32bit_word_in_host((unsigned char *)&arcbios_mem.Type, emulation_type == EMULTYPE_SGI? 3 : 2);	/*  FreeMemory  */
		store_32bit_word_in_host((unsigned char *)&arcbios_mem.BasePage, 8 * 1048576 / 4096);
		store_32bit_word_in_host((unsigned char *)&arcbios_mem.PageCount, (physical_ram_in_mb - 8) * 1048576 / 4096);
		store_buf(ARC_MEMDESC_ADDR, (char *)&arcbios_mem, sizeof(arcbios_mem));

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
		 */

		{
			uint32_t system;
			int i;

			if (emulation_type == EMULTYPE_SGI) {
				system = arcbios_addchild_manual(COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
				    0, 1, 20, 0, 0x0, machine_name, 0  /*  ROOT  */);

				/*  TODO:  sync devices and component tree  */
				/*  TODO 2: These are model dependant!!!  */
				dev_crime_init(mem, 0x14000000);		/*  crime0  */
				dev_macepci_init(mem, 0x1f080000);		/*  macepci0  */
				/*  mec0 (ethernet) at 0x1f280000  */
				dev_mace_init(mem, 0x1f310000);			/*  mace0  */
				dev_pckbc_init(mem, 0x1f320000, 0);		/*  ???  */
				dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x1f390000, 2, 0x100);	/*  com0  */
				dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x1f398000, 8, 0x100);	/*  com1  */
				dev_mc146818_init(cpus[0], mem, 0x1f3a0000, 0, 0, 0x40, emulated_ips);  /*  mcclock0  */
			} else {
				system = arcbios_addchild_manual(COMPONENT_CLASS_SystemClass, COMPONENT_TYPE_ARC,
				    0, 1, 20, 0, 0x0, "NEC-RD94", 0  /*  ROOT  */);

				/*  TODO:  sync devices and component tree  */
				/*  TODO 2: These are model dependant!!!  */
				dev_mc146818_init(cpus[0], mem, 0x2000004000, 0, 0, 1, emulated_ips);	/*  ???  */
				dev_pckbc_init(mem, 0x2000005000, 0);					/*  ???  */
				dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x2000006000, 2, 1);		/*  com0  */
				/* dev_ns16550_init(cpus[bootstrap_cpu], mem, 0x2000007000, 0, 1); */	/*  com1  */
			}

			/*  Common stuff for both SGI and ARC:  */
			debug("system = 0x%x\n", system);

			for (i=0; i<ncpus; i++) {
				uint32_t cpu, fpu;
				cpu = arcbios_addchild_manual(COMPONENT_CLASS_ProcessorClass, COMPONENT_TYPE_CPU,
				    0, 1, 20, 0, 0x0, "R5000", system);
				fpu = arcbios_addchild_manual(COMPONENT_CLASS_ProcessorClass, COMPONENT_TYPE_FPU,
				    0, 1, 20, 0, 0x0, "R5000FPC", cpu);

				/*  TODO:  cache (per cpu)  */
				debug("cpu%i = 0x%x  (fpu = 0x%x)\n", i, cpu, fpu);
			}
		}

		add_symbol_name(0xbfc10000, 0x10000, "[ARCBIOS entry]", 0);

		for (i=0; i<100; i++)
			store_32bit_word(0xbfc00000 + i*4, 0xbfc10000 + i*4);

		cpus[bootstrap_cpu]->gpr[GPR_A0] = 2;
		cpus[bootstrap_cpu]->gpr[GPR_A1] = 0x80018000;

		store_32bit_word(0x80018000, 0x80018100);
		store_32bit_word(0x80018004, 0x80018200);
		store_32bit_word(0x80018010, 0);

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

		store_string(0x80018100, bootstr);
		store_string(0x80018200, bootarg);

		cpus[0]->gpr[GPR_SP] = physical_ram_in_mb * 1048576 + 0x80000000 - 0x2080;

		addr = SGI_ENV_STRINGS;
		/*  add_environment_string("ConsoleIn=serial(0)", &addr);
		    add_environment_string("ConsoleOut=serial(0)", &addr);  */
		add_environment_string("ConsoleOut=arcs", &addr);
		add_environment_string("cpufreq=3", &addr);
		add_environment_string("dbaud=9600", &addr);
		add_environment_string("", &addr);	/*  the end  */

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

