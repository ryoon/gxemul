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
 *  $Id: machine.c,v 1.628 2005-12-03 04:14:11 debug Exp $
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

#include "arcbios.h"
#include "bus_isa.h"
#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "diskimage.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "mp.h"
#include "net.h"
#include "of.h"
#include "symbol.h"

/*  For Alpha emulation:  */
#include "alpha_rpb.h"

/*  For CATS emulation:  */
#include "cyclone_boot.h"

/*  For SGI and ARC emulation:  */
#include "sgi_arcbios.h"
#include "crimereg.h"

/*  For evbmips emulation:  */
#include "maltareg.h"

/*  For DECstation emulation:  */
#include "dec_prom.h"
#include "dec_bootinfo.h"
#include "dec_5100.h"
#include "dec_kn01.h"
#include "dec_kn02.h"
#include "dec_kn03.h"
#include "dec_kmin.h"
#include "dec_maxine.h"

/*  HPC:  */
#include "hpc_bootinfo.h"
#include "vripreg.h"

#define	BOOTSTR_BUFLEN		1000
#define	BOOTARG_BUFLEN		2000
#define	ETHERNET_STRING_MAXLEN	40

struct machine_entry_subtype {
	int			machine_subtype;/*  Old-style subtype  */
	const char		*name;		/*  Official name  */
	int			n_aliases;
	char			**aliases;	/*  Aliases  */
};

struct machine_entry {
	struct machine_entry	*next;

	/*  Machine type:  */
	int			arch;
	int			machine_type;	/*  Old-style type  */
	const char		*name;		/*  Official name  */
	int			n_aliases;
	char			**aliases;	/*  Aliases  */

	/*  Machine subtypes:  */
	int			n_subtypes;
	struct machine_entry_subtype **subtype;
};


/*  See main.c:  */
extern int quiet_mode;
extern int verbose;


/*  This is initialized by machine_init():  */
static struct machine_entry *first_machine_entry = NULL;


/*
 *  machine_new():
 *
 *  Returns a reasonably initialized struct machine.
 */
struct machine *machine_new(char *name, struct emul *emul)
{
	struct machine *m;
	m = malloc(sizeof(struct machine));
	if (m == NULL) {
		fprintf(stderr, "machine_new(): out of memory\n");
		exit(1);
	}

	memset(m, 0, sizeof(struct machine));

	/*  Back pointer:  */
	m->emul = emul;

	m->name = strdup(name);

	/*  Sane default values:  */
	m->serial_nr = 1;
	m->machine_type = MACHINE_NONE;
	m->machine_subtype = MACHINE_NONE;
#ifdef BINTRANS
	m->bintrans_enable = 1;
	m->old_bintrans_enable = 1;
#endif
	m->arch_pagesize = 4096;	/*  Should be overriden in
					    emul.c for other pagesizes.  */
	m->dyntrans_alignment_check = 1;
	m->prom_emulation = 1;
	m->speed_tricks = 1;
	m->byte_order_override = NO_BYTE_ORDER_OVERRIDE;
	m->boot_kernel_filename = "";
	m->boot_string_argument = NULL;
	m->automatic_clock_adjustment = 1;
	m->x11_scaledown = 1;
	m->x11_scaleup = 1;
	m->n_gfx_cards = 1;
	m->dbe_on_nonexistant_memaccess = 1;
	m->show_symbolic_register_names = 1;
	m->bintrans_size = DEFAULT_BINTRANS_SIZE_IN_MB * 1048576;
	symbol_init(&m->symbol_context);

	return m;
}


/*
 *  machine_name_to_type():
 *
 *  Take a type and a subtype as strings, and convert them into numeric
 *  values used internally throughout the code.
 *
 *  Return value is 1 on success, 0 if there was no match.
 *  Also, any errors/warnings are printed using fatal()/debug().
 */
int machine_name_to_type(char *stype, char *ssubtype,
	int *type, int *subtype, int *arch)
{
	struct machine_entry *me;
	int i, j, k, nmatches = 0;

	*type = MACHINE_NONE;
	*subtype = 0;

	/*  Check stype, and optionally ssubtype:  */
	me = first_machine_entry;
	while (me != NULL) {
		for (i=0; i<me->n_aliases; i++)
			if (strcasecmp(me->aliases[i], stype) == 0) {
				/*  Found a type:  */
				*type = me->machine_type;
				*arch = me->arch;

				if (me->n_subtypes == 0)
					return 1;

				/*  Check for subtype:  */
				for (j=0; j<me->n_subtypes; j++)
					for (k=0; k<me->subtype[j]->n_aliases;
					    k++)
						if (strcasecmp(ssubtype,
						    me->subtype[j]->aliases[k]
						    ) == 0) {
							*subtype = me->subtype[
							    j]->machine_subtype;
							return 1;
						}

				fatal("Unknown subtype '%s' for emulation"
				    " '%s'\n", ssubtype, stype);
				if (!ssubtype[0])
					fatal("(Maybe you forgot the -e"
					    " command line option?)\n");
				exit(1);
			}

		me = me->next;
	}

	/*  Not found? Then just check ssubtype:  */
	me = first_machine_entry;
	while (me != NULL) {
		if (me->n_subtypes == 0) {
			me = me->next;
			continue;
		}

		/*  Check for subtype:  */
		for (j=0; j<me->n_subtypes; j++)
			for (k=0; k<me->subtype[j]->n_aliases; k++)
				if (strcasecmp(ssubtype, me->subtype[j]->
				    aliases[k]) == 0) {
					*type = me->machine_type;
					*arch = me->arch;
					*subtype = me->subtype[j]->
					    machine_subtype;
					nmatches ++;
				}

		me = me->next;
	}

	switch (nmatches) {
	case 0:	fatal("\nSorry, emulation \"%s\"", stype);
		if (ssubtype != NULL && ssubtype[0] != '\0')
			fatal(" (subtype \"%s\")", ssubtype);
		fatal(" is unknown.\n");
		break;
	case 1:	return 1;
	default:fatal("\nSorry, multiple matches for \"%s\"", stype);
		if (ssubtype != NULL && ssubtype[0] != '\0')
			fatal(" (subtype \"%s\")", ssubtype);
		fatal(".\n");
	}

	*type = MACHINE_NONE;
	*subtype = 0;

	fatal("Use the -H command line option to get a list of "
	    "available types and subtypes.\n\n");

	return 0;
}


/*
 *  machine_add_tickfunction():
 *
 *  Adds a tick function (a function called every now and then, depending on
 *  clock cycle count) to a machine.
 */
void machine_add_tickfunction(struct machine *machine, void (*func)
	(struct cpu *, void *), void *extra, int clockshift)
{
	int n = machine->n_tick_entries;

	if (n >= MAX_TICK_FUNCTIONS) {
		fprintf(stderr, "machine_add_tickfunction(): too "
		    "many tick functions\n");
		exit(1);
	}

	/*  Don't use too low clockshifts, that would be too inefficient
	    with bintrans.  */
	if (clockshift < N_SAFE_BINTRANS_LIMIT_SHIFT)
		fatal("WARNING! clockshift = %i, less than "
		    "N_SAFE_BINTRANS_LIMIT_SHIFT (%i)\n",
		    clockshift, N_SAFE_BINTRANS_LIMIT_SHIFT);

	machine->ticks_till_next[n]   = 0;
	machine->ticks_reset_value[n] = 1 << clockshift;
	machine->tick_func[n]         = func;
	machine->tick_extra[n]        = extra;

	machine->n_tick_entries ++;
}


/*
 *  machine_bus_register():
 *
 *  Registers a bus in a machine.
 */
void machine_bus_register(struct machine *machine, char *busname,
	void (*debug_dump)(void *), void *extra)
{
	struct machine_bus *tmp, *last = NULL, *new;

	new = zeroed_alloc(sizeof(struct machine_bus));
	new->name = strdup(busname);
	new->debug_dump = debug_dump;
	new->extra = extra;

	/*  Register last in the bus list:  */
	tmp = machine->first_bus;
	while (tmp != NULL) {
		last = tmp;
		tmp = tmp->next;
	}

	if (last == NULL)
		machine->first_bus = new;
	else
		last->next = new;

	machine->n_busses ++;
}


/*
 *  machine_dump_bus_info():
 *
 *  Dumps info about registered busses.
 */
void machine_dump_bus_info(struct machine *m)
{
	struct machine_bus *bus = m->first_bus;
	int iadd = DEBUG_INDENTATION;

	if (m->n_busses > 0)
		debug("busses:\n");
	debug_indentation(iadd);
	while (bus != NULL) {
		bus->debug_dump(bus->extra);
		bus = bus->next;
	}
	debug_indentation(-iadd);
}


/*
 *  machine_dumpinfo():
 *
 *  Dumps info about a machine in some kind of readable format. (Used by
 *  the 'machine' debugger command.)
 */
void machine_dumpinfo(struct machine *m)
{
	int i;

	debug("serial nr: %i", m->serial_nr);
	if (m->nr_of_nics > 0)
		debug("  (nr of nics: %i)", m->nr_of_nics);
	debug("\n");

	debug("memory: %i MB", m->physical_ram_in_mb);
	if (m->memory_offset_in_mb != 0)
		debug(" (offset by %i MB)", m->memory_offset_in_mb);
	if (m->random_mem_contents)
		debug(", randomized contents");
	if (m->dbe_on_nonexistant_memaccess)
		debug(", dbe_on_nonexistant_memaccess");
	debug("\n");

	if (m->single_step_on_bad_addr)
		debug("single-step on bad addresses\n");

	if (m->arch == ARCH_MIPS) {
		if (m->bintrans_enable)
			debug("bintrans enabled (%i MB cache)\n",
			    (int) (m->bintrans_size / 1048576));
		else
			debug("bintrans disabled, other speedtricks %s\n",
			    m->speed_tricks? "enabled" : "disabled");
	}

	debug("clock: ");
	if (m->automatic_clock_adjustment)
		debug("adjusted automatically");
	else
		debug("fixed at %i Hz", m->emulated_hz);
	debug("\n");

	if (!m->prom_emulation)
		debug("PROM emulation disabled\n");

	for (i=0; i<m->ncpus; i++)
		cpu_dumpinfo(m, m->cpus[i]);

	if (m->ncpus > 1)
		debug("Bootstrap cpu is nr %i\n", m->bootstrap_cpu);

	if (m->slow_serial_interrupts_hack_for_linux)
		debug("Using slow_serial_interrupts_hack_for_linux\n");

	if (m->use_x11) {
		debug("Using X11");
		if (m->x11_scaledown > 1)
			debug(", scaledown %i", m->x11_scaledown);
		if (m->x11_scaleup > 1)
			debug(", scaleup %i", m->x11_scaleup);
		if (m->x11_n_display_names > 0) {
			for (i=0; i<m->x11_n_display_names; i++) {
				debug(i? ", " : " (");
				debug("\"%s\"", m->x11_display_names[i]);
			}
			debug(")");
		}
		debug("\n");
	}

	machine_dump_bus_info(m);

	diskimage_dump_info(m);

	if (m->force_netboot)
		debug("Forced netboot\n");
}


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

		cpu->memory_rw(cpu, cpu->mem, addr + i, &ch, sizeof(ch),
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
	if ((addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;
	cpu->memory_rw(cpu, cpu->mem,
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
 *  add_environment_string_dual():
 *
 *  Add "dual" environment strings, one for the variable name and one for the
 *  value, and update pointers afterwards.
 */
void add_environment_string_dual(struct cpu *cpu,
	uint64_t *ptrp, uint64_t *addrp, char *s1, char *s2)
{
	uint64_t ptr = *ptrp, addr = *addrp;

	store_32bit_word(cpu, ptr, addr);
	ptr += sizeof(uint32_t);
	if (addr != 0) {
		store_string(cpu, addr, s1);
		addr += strlen(s1) + 1;
	}
	store_32bit_word(cpu, ptr, addr);
	ptr += sizeof(uint32_t);
	if (addr != 0) {
		store_string(cpu, addr, s2);
		addr += strlen(s2) + 1;
	}

	*ptrp = ptr;
	*addrp = addr;
}


/*
 *  store_64bit_word():
 *
 *  Stores a 64-bit word in emulated RAM.  Byte order is taken into account.
 *  Helper function.
 */
int store_64bit_word(struct cpu *cpu, uint64_t addr, uint64_t data64)
{
	unsigned char data[8];
	if ((addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;
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
	return cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_32bit_word():
 *
 *  Stores a 32-bit word in emulated RAM.  Byte order is taken into account.
 *  (This function takes a 64-bit word as argument, to suppress some
 *  warnings, but only the lowest 32 bits are used.)
 */
int store_32bit_word(struct cpu *cpu, uint64_t addr, uint64_t data32)
{
	unsigned char data[4];
	if (cpu->machine->arch == ARCH_MIPS && (addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;
	data[0] = (data32 >> 24) & 255;
	data[1] = (data32 >> 16) & 255;
	data[2] = (data32 >> 8) & 255;
	data[3] = (data32) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}
	return cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_16bit_word():
 *
 *  Stores a 16-bit word in emulated RAM.  Byte order is taken into account.
 *  (This function takes a 64-bit word as argument, to suppress some
 *  warnings, but only the lowest 16 bits are used.)
 */
int store_16bit_word(struct cpu *cpu, uint64_t addr, uint64_t data16)
{
	unsigned char data[2];
	if (cpu->machine->arch == ARCH_MIPS && (addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;
	data[0] = (data16 >> 8) & 255;
	data[1] = (data16) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[1]; data[1] = tmp;
	}
	return cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_buf():
 *
 *  memcpy()-like helper function, from host RAM to emulated RAM.
 */
void store_buf(struct cpu *cpu, uint64_t addr, char *s, size_t len)
{
	int psize = 1024;	/*  1024 256 64 16 4 1  */

	if (cpu->machine->arch == ARCH_MIPS && (addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;

	while (len != 0) {
		if ((addr & (psize-1)) == 0) {
			while (len >= psize) {
				cpu->memory_rw(cpu, cpu->mem, addr,
				    (unsigned char *)s, psize, MEM_WRITE,
				    CACHE_DATA);
				addr += psize;
				s += psize;
				len -= psize;
			}
		}
		psize >>= 2;
	}

	while (len-- != 0)
		store_byte(cpu, addr++, *s++);
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

	if (cpu->machine->arch == ARCH_MIPS && (addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;
	cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_READ, CACHE_DATA);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}

	return (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + data[3];
}


/*
 *  load_16bit_word():
 *
 *  Helper function.  Prints a warning and returns 0, if the read failed.
 *  Emulated byte order is taken into account.
 */
uint16_t load_16bit_word(struct cpu *cpu, uint64_t addr)
{
	unsigned char data[2];

	if (cpu->machine->arch == ARCH_MIPS && (addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;
	cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_READ, CACHE_DATA);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[1]; data[1] = tmp;
	}

	return (data[0] << 8) + data[1];
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
void kn02_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int current;

	irq_nr -= 8;
	irq_nr &= 0xff;

	if (assrt) {
		/*  OR in the irq_nr into the CSR:  */
		m->md_int.kn02_csr->csr[0] |= irq_nr;
	} else {
		/*  AND out the irq_nr from the CSR:  */
		m->md_int.kn02_csr->csr[0] &= ~irq_nr;
	}

	current = m->md_int.kn02_csr->csr[0] & m->md_int.kn02_csr->csr[2];
	if (current == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);
}


/*
 *  DECstation KMIN interrupts:
 *
 *  TC slot 3 = system slot.
 */
void kmin_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	/*  debug("kmin_interrupt(): irq_nr=%i assrt=%i\n", irq_nr, assrt);  */

	if (assrt)
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10] |= irq_nr;
	else
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10] &= ~irq_nr;

	if (m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10]
	    & m->md_int.dec_ioasic_data->reg[(IOASIC_IMSK - IOASIC_SLOT_1_START) / 0x10])
		cpu_interrupt(cpu, KMIN_INT_TC3);
	else
		cpu_interrupt_ack(cpu, KMIN_INT_TC3);
}


/*
 *  DECstation KN03 interrupts:
 */
void kn03_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	/*  debug("kn03_interrupt(): irq_nr=0x%x assrt=%i\n", irq_nr, assrt);  */

	if (assrt)
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10] |= irq_nr;
	else
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10] &= ~irq_nr;

	if (m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10]
	    & m->md_int.dec_ioasic_data->reg[(IOASIC_IMSK - IOASIC_SLOT_1_START) / 0x10])
		cpu_interrupt(cpu, KN03_INT_ASIC);
	else
		cpu_interrupt_ack(cpu, KN03_INT_ASIC);
}


/*
 *  DECstation MAXINE interrupts:
 */
void maxine_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	irq_nr -= 8;
	debug("maxine_interrupt(): irq_nr=0x%x assrt=%i\n", irq_nr, assrt);

	if (assrt)
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START)
		    / 0x10] |= irq_nr;
	else
		m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START)
		    / 0x10] &= ~irq_nr;

	if (m->md_int.dec_ioasic_data->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10]
	    & m->md_int.dec_ioasic_data->reg[(IOASIC_IMSK - IOASIC_SLOT_1_START)
	    / 0x10])
		cpu_interrupt(cpu, XINE_INT_TC3);
	else
		cpu_interrupt_ack(cpu, XINE_INT_TC3);
}


/*
 *  DECstation KN230 interrupts:
 */
void kn230_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int r2 = 0;

	m->md_int.kn230_csr->csr |= irq_nr;

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
		m->md_int.kn230_csr->csr |= irq_nr;

		/*  Assert MIPS interrupt 2 or 3:  */
		cpu_interrupt(cpu, r2);
	} else {
		/*  AND out the irq_nr mask from the CSR:  */
		m->md_int.kn230_csr->csr &= ~irq_nr;

		/*  If the CSR interrupt bits are all zero,
		    clear the bit in the cause register as well.  */
		if (r2 == 2) {
			/*  irq 2:  */
			if ((m->md_int.kn230_csr->csr & (KN230_CSR_INTR_DZ0
			    | KN230_CSR_INTR_OPT0 | KN230_CSR_INTR_OPT1)) == 0)
				cpu_interrupt_ack(cpu, r2);
		} else {
			/*  irq 3:  */
			if ((m->md_int.kn230_csr->csr & (KN230_CSR_INTR_SII |
			    KN230_CSR_INTR_LANCE)) == 0)
				cpu_interrupt_ack(cpu, r2);
		}
	}
}


/*
 *  Jazz interrupts (for Acer PICA-61 etc):
 *
 *  0..7			MIPS interrupts
 *  8 + x, where x = 0..15	Jazz interrupts
 *  8 + x, where x = 16..31	ISA interrupt (irq nr + 16)
 */
void jazz_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	uint32_t irq;
	int isa = 0;

	irq_nr -= 8;

	/*  debug("jazz_interrupt() irq_nr = %i, assrt = %i\n",
		irq_nr, assrt);  */

	if (irq_nr >= 16) {
		isa = 1;
		irq_nr -= 16;
	}

	irq = 1 << irq_nr;

	if (isa) {
		if (assrt)
			m->md_int.jazz_data->isa_int_asserted |= irq;
		else
			m->md_int.jazz_data->isa_int_asserted &= ~irq;
	} else {
		if (assrt)
			m->md_int.jazz_data->int_asserted |= irq;
		else
			m->md_int.jazz_data->int_asserted &= ~irq;
	}

	/*  debug("   %08x %08x\n", m->md_int.jazz_data->int_asserted,
		m->md_int.jazz_data->int_enable_mask);  */
	/*  debug("   %08x %08x\n", m->md_int.jazz_data->isa_int_asserted,
		m->md_int.jazz_data->isa_int_enable_mask);  */

	if (m->md_int.jazz_data->int_asserted
	    /* & m->md_int.jazz_data->int_enable_mask  */ & ~0x8000 )
		cpu_interrupt(cpu, 3);
	else
		cpu_interrupt_ack(cpu, 3);

	if (m->md_int.jazz_data->isa_int_asserted &
	    m->md_int.jazz_data->isa_int_enable_mask)
		cpu_interrupt(cpu, 4);
	else
		cpu_interrupt_ack(cpu, 4);

	/*  TODO: this "15" (0x8000) is the timer... fix this?  */
	if (m->md_int.jazz_data->int_asserted & 0x8000)
		cpu_interrupt(cpu, 6);
	else
		cpu_interrupt_ack(cpu, 6);
}


/*
 *  VR41xx interrupt routine:
 *
 *  irq_nr = 8 + x
 *	x = 0..15 for level1
 *	x = 16..31 for level2
 *	x = 32+y for GIU interrupt y
 */
void vr41xx_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	int giu_irq = 0;

	irq_nr -= 8;
	if (irq_nr >= 32) {
		giu_irq = irq_nr - 32;

		if (assrt)
			m->md_int.vr41xx_data->giuint |= (1 << giu_irq);
		else
			m->md_int.vr41xx_data->giuint &= ~(1 << giu_irq);
	}

	/*  TODO: This is wrong. What about GIU bit 8?  */

	if (irq_nr != 8) {
		/*  If any GIU bit is asserted, then assert the main
		    GIU interrupt:  */
		if (m->md_int.vr41xx_data->giuint &
		    m->md_int.vr41xx_data->giumask)
			vr41xx_interrupt(m, cpu, 8 + 8, 1);
		else
			vr41xx_interrupt(m, cpu, 8 + 8, 0);
	}

	/*  debug("vr41xx_interrupt(): irq_nr=%i assrt=%i\n",
	    irq_nr, assrt);  */

	if (irq_nr < 16) {
		if (assrt)
			m->md_int.vr41xx_data->sysint1 |= (1 << irq_nr);
		else
			m->md_int.vr41xx_data->sysint1 &= ~(1 << irq_nr);
	} else if (irq_nr < 32) {
		irq_nr -= 16;
		if (assrt)
			m->md_int.vr41xx_data->sysint2 |= (1 << irq_nr);
		else
			m->md_int.vr41xx_data->sysint2 &= ~(1 << irq_nr);
	}

	/*  TODO: Which hardware interrupt pin?  */

	/*  debug("    sysint1=%04x mask=%04x, sysint2=%04x mask=%04x\n",
	    m->md_int.vr41xx_data->sysint1, m->md_int.vr41xx_data->msysint1,
	    m->md_int.vr41xx_data->sysint2, m->md_int.vr41xx_data->msysint2); */

	if ((m->md_int.vr41xx_data->sysint1 & m->md_int.vr41xx_data->msysint1) |
	    (m->md_int.vr41xx_data->sysint2 & m->md_int.vr41xx_data->msysint2))
		cpu_interrupt(cpu, 2);
	else
		cpu_interrupt_ack(cpu, 2);
}


/*
 *  Playstation 2 interrupt routine:
 *
 *  irq_nr =	8 + x		normal irq x
 *		8 + 16 + y	dma irq y
 *		8 + 32 + 0	sbus irq 0 (pcmcia)
 *		8 + 32 + 1	sbus irq 1 (usb)
 */
void ps2_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	irq_nr -= 8;
	debug("ps2_interrupt(): irq_nr=0x%x assrt=%i\n", irq_nr, assrt);

	if (irq_nr >= 32) {
		int msk = 0;
		switch (irq_nr - 32) {
		case 0:	/*  PCMCIA:  */
			msk = 0x100;
			break;
		case 1:	/*  USB:  */
			msk = 0x400;
			break;
		default:
			fatal("ps2_interrupt(): bad irq_nr %i\n", irq_nr);
		}

		if (assrt)
			m->md_int.ps2_data->sbus_smflg |= msk;
		else
			m->md_int.ps2_data->sbus_smflg &= ~msk;

		if (m->md_int.ps2_data->sbus_smflg != 0)
			cpu_interrupt(cpu, 8 + 1);
		else
			cpu_interrupt_ack(cpu, 8 + 1);
		return;
	}

	if (assrt) {
		/*  OR into the INTR:  */
		if (irq_nr < 16)
			m->md_int.ps2_data->intr |= (1 << irq_nr);
		else
			m->md_int.ps2_data->dmac_reg[0x601] |=
			    (1 << (irq_nr-16));
	} else {
		/*  AND out of the INTR:  */
		if (irq_nr < 16)
			m->md_int.ps2_data->intr &= ~(1 << irq_nr);
		else
			m->md_int.ps2_data->dmac_reg[0x601] &=
			    ~(1 << (irq_nr-16));
	}

	/*  TODO: Hm? How about the mask?  */
	if (m->md_int.ps2_data->intr /*  & m->md_int.ps2_data->imask */ )
		cpu_interrupt(cpu, 2);
	else
		cpu_interrupt_ack(cpu, 2);

	/*  TODO: mask?  */
	if (m->md_int.ps2_data->dmac_reg[0x601] & 0xffff)
		cpu_interrupt(cpu, 3);
	else
		cpu_interrupt_ack(cpu, 3);
}


/*
 *  SGI "IP22" interrupt routine:
 */
void sgi_ip22_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
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
		int ms = irq_nr / 64;
		uint32_t new = 1 << ms;
		if (assrt)
			m->md_int.sgi_ip22_data->reg[4] |= new;
		else
			m->md_int.sgi_ip22_data->reg[4] &= ~new;
		/*  TODO: is this enough?  */
		irq_nr &= 63;
	}

	if (irq_nr < 32) {
		if (assrt)
			m->md_int.sgi_ip22_data->reg[0] |= newmask;
		else
			m->md_int.sgi_ip22_data->reg[0] &= ~newmask;
	} else {
		if (assrt)
			m->md_int.sgi_ip22_data->reg[2] |= newmask;
		else
			m->md_int.sgi_ip22_data->reg[2] &= ~newmask;
	}

	/*  Read stat and mask for local0:  */
	stat = m->md_int.sgi_ip22_data->reg[0];
	mask = m->md_int.sgi_ip22_data->reg[1];
	if ((stat & mask) == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);

	/*  Read stat and mask for local1:  */
	stat = m->md_int.sgi_ip22_data->reg[2];
	mask = m->md_int.sgi_ip22_data->reg[3];
	if ((stat & mask) == 0)
		cpu_interrupt_ack(cpu, 3);
	else
		cpu_interrupt(cpu, 3);
}


/*
 *  SGI "IP30" interrupt routine:
 *
 *  irq_nr = 8 + 1 + nr, where nr is:
 *	0..49		HEART irqs	hardware irq 2,3,4
 *	50		HEART timer	hardware irq 5
 *	51..63		HEART errors	hardware irq 6
 *
 *  according to Linux/IP30.
 */
void sgi_ip30_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	uint64_t newmask;
	uint64_t stat, mask;

	irq_nr -= 8;
	if (irq_nr == 0)
		goto just_assert_and_such;
	irq_nr --;

	newmask = (int64_t)1 << irq_nr;

	if (assrt)
		m->md_int.sgi_ip30_data->isr |= newmask;
	else
		m->md_int.sgi_ip30_data->isr &= ~newmask;

just_assert_and_such:

	cpu_interrupt_ack(cpu, 2);
	cpu_interrupt_ack(cpu, 3);
	cpu_interrupt_ack(cpu, 4);
	cpu_interrupt_ack(cpu, 5);
	cpu_interrupt_ack(cpu, 6);

	stat = m->md_int.sgi_ip30_data->isr;
	mask = m->md_int.sgi_ip30_data->imask0;

	if ((stat & mask) & 0x000000000000ffffULL)
		cpu_interrupt(cpu, 2);
	if ((stat & mask) & 0x00000000ffff0000ULL)
		cpu_interrupt(cpu, 3);
	if ((stat & mask) & 0x0003ffff00000000ULL)
		cpu_interrupt(cpu, 4);
	if ((stat & mask) & 0x0004000000000000ULL)
		cpu_interrupt(cpu, 5);
	if ((stat & mask) & 0xfff8000000000000ULL)
		cpu_interrupt(cpu, 6);
}


/*
 *  SGI "IP32" interrupt routine:
 */
void sgi_ip32_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
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
	 */

	uint64_t crime_addr = CRIME_INTSTAT;
	uint64_t mace_addr = 0x10;
	uint64_t crime_interrupts, crime_interrupts_mask;
	uint64_t mace_interrupts, mace_interrupt_mask;
	unsigned int i;
	unsigned char x[8];

	/*  Read current MACE interrupt assertions:  */
	memcpy(x, m->md_int.ip32.mace_data->reg + mace_addr,
	    sizeof(uint64_t));
	mace_interrupts = 0;
	for (i=0; i<sizeof(uint64_t); i++) {
		mace_interrupts <<= 8;
		mace_interrupts |= x[i];
	}

	/*  Read current MACE interrupt mask:  */
	memcpy(x, m->md_int.ip32.mace_data->reg + mace_addr + 8,
	    sizeof(uint64_t));
	mace_interrupt_mask = 0;
	for (i=0; i<sizeof(uint64_t); i++) {
		mace_interrupt_mask <<= 8;
		mace_interrupt_mask |= x[i];
	}

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
		if (assrt)
			mace_interrupts |= (irq_nr & ~MACE_PERIPH_SERIAL);
		else
			mace_interrupts &= ~(irq_nr & ~MACE_PERIPH_SERIAL);

		irq_nr = MACE_PERIPH_SERIAL;
		if ((mace_interrupts & mace_interrupt_mask) == 0)
			assrt = 0;
		else
			assrt = 1;
	}

	/*  Hopefully _MISC and _SERIAL will not be both on at the same time.  */
	if (irq_nr & MACE_PERIPH_MISC) {
		if (assrt)
			mace_interrupts |= (irq_nr & ~MACE_PERIPH_MISC);
		else
			mace_interrupts &= ~(irq_nr & ~MACE_PERIPH_MISC);

		irq_nr = MACE_PERIPH_MISC;
		if ((mace_interrupts & mace_interrupt_mask) == 0)
			assrt = 0;
		else
			assrt = 1;
	}

	/*  Write back MACE interrupt assertions:  */
	for (i=0; i<sizeof(uint64_t); i++)
		x[7-i] = mace_interrupts >> (i*8);
	memcpy(m->md_int.ip32.mace_data->reg + mace_addr, x, sizeof(uint64_t));

	/*  Read CRIME_INTSTAT:  */
	memcpy(x, m->md_int.ip32.crime_data->reg + crime_addr,
	    sizeof(uint64_t));
	crime_interrupts = 0;
	for (i=0; i<sizeof(uint64_t); i++) {
		crime_interrupts <<= 8;
		crime_interrupts |= x[i];
	}

	if (assrt)
		crime_interrupts |= irq_nr;
	else
		crime_interrupts &= ~irq_nr;

	/*  Write back CRIME_INTSTAT:  */
	for (i=0; i<sizeof(uint64_t); i++)
		x[7-i] = crime_interrupts >> (i*8);
	memcpy(m->md_int.ip32.crime_data->reg + crime_addr, x,
	    sizeof(uint64_t));

	/*  Read CRIME_INTMASK:  */
	memcpy(x, m->md_int.ip32.crime_data->reg + CRIME_INTMASK,
	    sizeof(uint64_t));
	crime_interrupts_mask = 0;
	for (i=0; i<sizeof(uint64_t); i++) {
		crime_interrupts_mask <<= 8;
		crime_interrupts_mask |= x[i];
	}

	if ((crime_interrupts & crime_interrupts_mask) == 0)
		cpu_interrupt_ack(cpu, 2);
	else
		cpu_interrupt(cpu, 2);

	/*  printf("sgi_crime_machine_irq(%i,%i): new interrupts = 0x%08x\n",
	    assrt, irq_nr, crime_interrupts);  */
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
void au1x00_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	uint32_t ms;

	irq_nr -= 8;
	debug("au1x00_interrupt(): irq_nr=%i assrt=%i\n", irq_nr, assrt);

	if (irq_nr < 64) {
		ms = 1 << (irq_nr & 31);

		if (assrt)
			m->md_int.au1x00_ic_data->request0_int |= ms;
		else
			m->md_int.au1x00_ic_data->request0_int &= ~ms;

		/*  TODO: Controller 1  */
	}

	if ((m->md_int.au1x00_ic_data->request0_int &
	    m->md_int.au1x00_ic_data->mask) != 0)
		cpu_interrupt(cpu, 2);
	else
		cpu_interrupt_ack(cpu, 2);

	/*  TODO: What _is_ request1?  */

	/*  TODO: Controller 1  */
}


/*
 *  CPC700 interrupt routine:
 *
 *  irq_nr should be 0..31. (32 means reassertion.)
 */
void cpc700_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	if (irq_nr < 32) {
		uint32_t mask = 1 << (irq_nr & 31);
		if (assrt)
			m->md_int.cpc700_data->sr |= mask;
		else
			m->md_int.cpc700_data->sr &= ~mask;
	}

	if ((m->md_int.cpc700_data->sr & m->md_int.cpc700_data->er) != 0)
		cpu_interrupt(cpu, 65);
	else
		cpu_interrupt_ack(cpu, 65);
}


/*
 *  Interrupt function for Cobalt, evbmips (Malta), and Algor.
 *
 *  (irq_nr = 8 + 16 can be used to just reassert/deassert interrupts.)
 */
void isa8_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int mask, x;
	int old_isa_assert, new_isa_assert;

	old_isa_assert = m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier;

	irq_nr -= 8;
	mask = 1 << (irq_nr & 7);

	if (irq_nr < 8) {
		if (assrt)
			m->isa_pic_data.pic1->irr |= mask;
		else
			m->isa_pic_data.pic1->irr &= ~mask;
	} else if (irq_nr < 16) {
		if (assrt)
			m->isa_pic_data.pic2->irr |= mask;
		else
			m->isa_pic_data.pic2->irr &= ~mask;
	}

	/*  Any interrupt assertions on PIC2 go to irq 2 on PIC1  */
	/*  (TODO: don't hardcode this here)  */
	if (m->isa_pic_data.pic2->irr & ~m->isa_pic_data.pic2->ier)
		m->isa_pic_data.pic1->irr |= 0x04;
	else
		m->isa_pic_data.pic1->irr &= ~0x04;

	/*  Now, PIC1:  */
	new_isa_assert = m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier;
	if (old_isa_assert != new_isa_assert) {
		for (x=0; x<16; x++) {
			if (x == 2)
			        continue;
			if (x < 8 && (m->isa_pic_data.pic1->irr &
			    ~m->isa_pic_data.pic1->ier & (1 << x)))
			        break;
			if (x >= 8 && (m->isa_pic_data.pic2->irr &
			    ~m->isa_pic_data.pic2->ier & (1 << (x&7))))
			        break;
		}
		m->isa_pic_data.last_int = x;
	}

	if (m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier)
		cpu_interrupt(cpu, m->isa_pic_data.native_irq);
	else
		cpu_interrupt_ack(cpu, m->isa_pic_data.native_irq);
}


/*
 *  x86 (PC) interrupts:
 *
 *  (irq_nr = 16 can be used to just reassert/deassert interrupts.)
 */
void x86_pc_interrupt(struct machine *m, struct cpu *cpu, int irq_nr, int assrt)
{
	int mask = 1 << (irq_nr & 7);

	if (irq_nr < 8) {
		if (assrt)
			m->isa_pic_data.pic1->irr |= mask;
		else
			m->isa_pic_data.pic1->irr &= ~mask;
	} else if (irq_nr < 16) {
		if (m->isa_pic_data.pic2 == NULL) {
			fatal("x86_pc_interrupt(): pic2 used (irq_nr = %i), "
			    "but we are emulating an XT?\n", irq_nr);
			return;
		}
		if (assrt)
			m->isa_pic_data.pic2->irr |= mask;
		else
			m->isa_pic_data.pic2->irr &= ~mask;
	}

	if (m->isa_pic_data.pic2 != NULL) {
		/*  Any interrupt assertions on PIC2 go to irq 2 on PIC1  */
		/*  (TODO: don't hardcode this here)  */
		if (m->isa_pic_data.pic2->irr & ~m->isa_pic_data.pic2->ier)
			m->isa_pic_data.pic1->irr |= 0x04;
		else
			m->isa_pic_data.pic1->irr &= ~0x04;
	}

	/*  Now, PIC1:  */
	if (m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier)
		cpu->cd.x86.interrupt_asserted = 1;
	else
		cpu->cd.x86.interrupt_asserted = 0;
}


/*
 *  "Generic" ISA interrupt management, 32 native interrupts, 16 ISA
 *  interrupts.  So far: Footbridge (CATS, NetWinder), BeBox, and PReP.
 *
 *  0..31  = footbridge interrupt
 *  32..47 = ISA interrupts
 *  48     = ISA reassert
 *  64     = reassert (non-ISA)
 */
void isa32_interrupt(struct machine *m, struct cpu *cpu, int irq_nr,
	int assrt)
{
	uint32_t mask = 1 << (irq_nr & 31);
	int old_isa_assert, new_isa_assert;

	old_isa_assert = m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier;

	if (irq_nr >= 32 && irq_nr < 32 + 8) {
		int mm = 1 << (irq_nr & 7);
		if (assrt)
			m->isa_pic_data.pic1->irr |= mm;
		else
			m->isa_pic_data.pic1->irr &= ~mm;
	} else if (irq_nr >= 32+8 && irq_nr < 32+16) {
		int mm = 1 << (irq_nr & 7);
		if (assrt)
			m->isa_pic_data.pic2->irr |= mm;
		else
			m->isa_pic_data.pic2->irr &= ~mm;
	}

	/*  Any interrupt assertions on PIC2 go to irq 2 on PIC1  */
	/*  (TODO: don't hardcode this here)  */
	if (m->isa_pic_data.pic2->irr & ~m->isa_pic_data.pic2->ier)
		m->isa_pic_data.pic1->irr |= 0x04;
	else
		m->isa_pic_data.pic1->irr &= ~0x04;

	/*  Now, PIC1:  */
	new_isa_assert = m->isa_pic_data.pic1->irr & ~m->isa_pic_data.pic1->ier;
	if (old_isa_assert != new_isa_assert || irq_nr == 48) {
		if (new_isa_assert) {
			int x;
			for (x=0; x<16; x++) {
				if (x == 2)
				        continue;
				if (x < 8 && (m->isa_pic_data.pic1->irr &
				    ~m->isa_pic_data.pic1->ier & (1 << x)))
				        break;
				if (x >= 8 && (m->isa_pic_data.pic2->irr &
				    ~m->isa_pic_data.pic2->ier & (1 << (x&7))))
				        break;
			}
			m->isa_pic_data.last_int = x;
			cpu_interrupt(cpu, m->isa_pic_data.native_irq);
		} else
			cpu_interrupt_ack(cpu, m->isa_pic_data.native_irq);
		return;
	}

	switch (m->machine_type) {
	case MACHINE_CATS:
	case MACHINE_NETWINDER:
		if (irq_nr < 32) {
			if (assrt)
				m->md_int.footbridge_data->irq_status |= mask;
			else
				m->md_int.footbridge_data->irq_status &= ~mask;
		}
		if (m->md_int.footbridge_data->irq_status &
		    m->md_int.footbridge_data->irq_enable)
			cpu_interrupt(cpu, 65);
		else
			cpu_interrupt_ack(cpu, 65);
		break;
	case MACHINE_BEBOX:
		if (irq_nr < 32) {
			if (assrt)
				m->md_int.bebox_data->int_status |= mask;
			else
				m->md_int.bebox_data->int_status &= ~mask;
		}
		if (m->md_int.bebox_data->int_status &
		    m->md_int.bebox_data->cpu0_int_mask)
			cpu_interrupt(m->cpus[0], 65);
		else
			cpu_interrupt_ack(m->cpus[0], 65);
		if (m->ncpus > 1 &&
		    m->md_int.bebox_data->int_status &
		    m->md_int.bebox_data->cpu1_int_mask)
			cpu_interrupt(m->cpus[1], 65);
		else
			cpu_interrupt_ack(m->cpus[1], 65);
		break;
	case MACHINE_PREP:
		if (irq_nr < 32) {
			if (assrt)
				m->md_int.prep_data->int_status |= mask;
			else
				m->md_int.prep_data->int_status &= ~mask;
		}
		if (m->md_int.prep_data->int_status & 2)
			cpu_interrupt(cpu, 65);
		else
			cpu_interrupt_ack(cpu, 65);
		break;
	}
}


/*
 *  Grand Central interrupt handler.
 *
 *  (Used by MacPPC.)
 */
void gc_interrupt(struct machine *m, struct cpu *cpu, int irq_nr,
	int assrt)
{
	uint32_t mask = 1 << (irq_nr & 31);
	if (irq_nr < 32) {
		if (assrt)
			m->md_int.gc_data->status_lo |= mask;
		else
			m->md_int.gc_data->status_lo &= ~mask;
	}
	if (irq_nr >= 32 && irq_nr < 64) {
		if (assrt)
			m->md_int.gc_data->status_hi |= mask;
		else
			m->md_int.gc_data->status_hi &= ~mask;
	}

#if 0
	printf("status = %08x %08x  enable = %08x %08x\n",
	    m->md_int.gc_data->status_hi, m->md_int.gc_data->status_lo,
	    m->md_int.gc_data->enable_hi, m->md_int.gc_data->enable_lo);
#endif

	if (m->md_int.gc_data->status_lo & m->md_int.gc_data->enable_lo ||
	    m->md_int.gc_data->status_hi & m->md_int.gc_data->enable_hi)
		cpu_interrupt(m->cpus[0], 65);
	else
		cpu_interrupt_ack(m->cpus[0], 65);
}


/****************************************************************************
 *                                                                          *
 *                  Machine dependant Initialization routines               *
 *                                                                          *
 ****************************************************************************/


/*
 *  machine_setup():
 *
 *  This (rather large) function initializes memory, registers, and/or devices
 *  required by specific machine emulations.
 */
void machine_setup(struct machine *machine)
{
	uint64_t addr, addr2;
	int i, j, stable = 0;
	struct memory *mem;
	char tmpstr[1000];
	struct cons_data *cons_data;

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
	int hpc_platid_flags = 0, hpc_platid_cpu_submodel = 0,
	    hpc_platid_cpu_model = 0, hpc_platid_cpu_series = 0,
	    hpc_platid_cpu_arch = 0,
	    hpc_platid_submodel = 0, hpc_platid_model = 0,
	    hpc_platid_series = 0, hpc_platid_vendor = 0;
	uint64_t hpc_fb_addr = 0;
	int hpc_fb_bits = 0, hpc_fb_encoding = 0;
	int hpc_fb_xsize = 0;
	int hpc_fb_ysize = 0;
	int hpc_fb_xsize_mem = 0;
	int hpc_fb_ysize_mem = 0;

	/*  ARCBIOS stuff:  */
	uint64_t sgi_ram_offset = 0;
	int arc_wordlen = sizeof(uint32_t);
	char *eaddr_string = "eaddr=10:20:30:40:50:60";   /*  nonsense  */
	unsigned char macaddr[6];

	/*  Generic bootstring stuff:  */
	int bootdev_type = 0;
	int bootdev_id;
	char *bootstr = NULL;
	char *bootarg = NULL;
	char *init_bootpath;

	/*  PCI stuff:  */
	struct pci_data *pci_data = NULL;

	/*  Framebuffer stuff:  */
	struct vfb_data *fb = NULL;

	/*  Abreviation:  :-)  */
	struct cpu *cpu = machine->cpus[machine->bootstrap_cpu];


	bootdev_id = diskimage_bootdev(machine, &bootdev_type);

	mem = cpu->mem;
	machine->machine_name = NULL;

	/*  TODO: Move this somewhere else?  */
	if (machine->boot_string_argument == NULL) {
		switch (machine->machine_type) {
		case MACHINE_ARC:
			machine->boot_string_argument = "-aN";
			break;
		case MACHINE_CATS:
			machine->boot_string_argument = "-A";
			break;
		case MACHINE_DEC:
			machine->boot_string_argument = "-a";
			break;
		default:
			/*  Important, because boot_string_argument should
			    not be set to NULL:  */
			machine->boot_string_argument = "";
		}
	}

	switch (machine->machine_type) {

	case MACHINE_NONE:
		printf("\nNo emulation type specified.\n");
		exit(1);

#ifdef ENABLE_MIPS
	case MACHINE_BAREMIPS:
		/*
		 *  A "bare" MIPS test machine.
		 *
		 *  NOTE: NO devices at all.
		 */
		cpu->byte_order = EMUL_BIG_ENDIAN;
		machine->machine_name = "\"Bare\" MIPS machine";
		stable = 1;
		break;

	case MACHINE_TESTMIPS:
		/*
		 *  A MIPS test machine (which happens to work with the
		 *  code in my master's thesis).  :-)
		 *
		 *  IRQ map:
		 *	7	CPU counter
		 *	6	SMP IPIs
		 *	5	not used yet
		 *	4	not used yet
		 *	3	ethernet
		 *	2	serial console
		 */
		cpu->byte_order = EMUL_BIG_ENDIAN;
		machine->machine_name = "MIPS test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=2",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testmips generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=3",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;

	case MACHINE_DEC:
		cpu->byte_order = EMUL_LITTLE_ENDIAN;

		/*  An R2020 or R3220 memory thingy:  */
		cpu->cd.mips.coproc[3] = mips_coproc_new(cpu, 3);

		/*  There aren't really any good standard values...  */
		framebuffer_console_name = "osconsole=0,3";
		serial_console_name      = "osconsole=1";

		switch (machine->machine_subtype) {
		case MACHINE_DEC_PMAX_3100:		/*  type  1, KN01  */
			/*  Supposed to have 12MHz or 16.67MHz R2000 CPU, R2010 FPC, R2020 Memory coprocessor  */
			machine->machine_name = "DEC PMAX 3100 (KN01)";

			/*  12 MHz for 2100, 16.67 MHz for 3100  */
			if (machine->emulated_hz == 0)
				machine->emulated_hz = 16670000;

			if (machine->physical_ram_in_mb > 24)
				fprintf(stderr, "WARNING! Real DECstation 3100 machines cannot have more than 24MB RAM. Continuing anyway.\n");

			if ((machine->physical_ram_in_mb % 4) != 0)
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
			fb = dev_fb_init(machine, mem, KN01_PHYS_FBUF_START,
			    color_fb_flag? VFB_DEC_VFB02 : VFB_DEC_VFB01,
			    0,0,0,0,0, color_fb_flag? "VFB02":"VFB01");
			dev_colorplanemask_init(mem, KN01_PHYS_COLMASK_START, &fb->color_plane_mask);
			dev_vdac_init(mem, KN01_SYS_VDAC, fb->rgb_palette, color_fb_flag);
			dev_le_init(machine, mem, KN01_SYS_LANCE, KN01_SYS_LANCE_B_START, KN01_SYS_LANCE_B_END, KN01_INT_LANCE, 4*1048576);
			dev_sii_init(machine, mem, KN01_SYS_SII, KN01_SYS_SII_B_START, KN01_SYS_SII_B_END, KN01_INT_SII);
			dev_dc7085_init(machine, mem, KN01_SYS_DZ, KN01_INT_DZ, machine->use_x11);
			dev_mc146818_init(machine, mem, KN01_SYS_CLOCK, KN01_INT_CLOCK, MC146818_DEC, 1);
			dev_kn01_csr_init(mem, KN01_SYS_CSR, color_fb_flag);

			framebuffer_console_name = "osconsole=0,3";	/*  fb,keyb  */
			serial_console_name      = "osconsole=3";	/*  3  */
			break;

		case MACHINE_DEC_3MAX_5000:		/*  type  2, KN02  */
			/*  Supposed to have 25MHz R3000 CPU, R3010 FPC,  */
			/*  and a R3220 Memory coprocessor  */
			machine->machine_name = "DECstation 5000/200 (3MAX, KN02)";
			stable = 1;

			if (machine->emulated_hz == 0)
				machine->emulated_hz = 25000000;

			if (machine->physical_ram_in_mb < 8)
				fprintf(stderr, "WARNING! Real KN02 machines do not have less than 8MB RAM. Continuing anyway.\n");
			if (machine->physical_ram_in_mb > 480)
				fprintf(stderr, "WARNING! Real KN02 machines cannot have more than 480MB RAM. Continuing anyway.\n");

			/*  An R3220 memory thingy:  */
			cpu->cd.mips.coproc[3] = mips_coproc_new(cpu, 3);

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
			machine->md_interrupt = kn02_interrupt;

			/*
			 *  TURBOchannel slots 0, 1, and 2 are free for
			 *  option cards.  Let's put in zero or more graphics
			 *  boards:
			 *
			 *  TODO: It's also possible to have larger graphics
			 *  cards that occupy several slots. How to solve
			 *  this nicely?
			 */
			dev_turbochannel_init(machine, mem, 0,
			    KN02_PHYS_TC_0_START, KN02_PHYS_TC_0_END,
			    machine->n_gfx_cards >= 1?
				turbochannel_default_gfx_card : "",
			    KN02_IP_SLOT0 +8);

			dev_turbochannel_init(machine, mem, 1,
			    KN02_PHYS_TC_1_START, KN02_PHYS_TC_1_END,
			    machine->n_gfx_cards >= 2?
				turbochannel_default_gfx_card : "",
			    KN02_IP_SLOT1 +8);

			dev_turbochannel_init(machine, mem, 2,
			    KN02_PHYS_TC_2_START, KN02_PHYS_TC_2_END,
			    machine->n_gfx_cards >= 3?
				turbochannel_default_gfx_card : "",
			    KN02_IP_SLOT2 +8);

			/*  TURBOchannel slots 3 and 4 are reserved.  */

			/*  TURBOchannel slot 5 is PMAZ-AA ("asc" SCSI).  */
			dev_turbochannel_init(machine, mem, 5,
			    KN02_PHYS_TC_5_START, KN02_PHYS_TC_5_END,
			    "PMAZ-AA", KN02_IP_SCSI +8);

			/*  TURBOchannel slot 6 is PMAD-AA ("le" ethernet).  */
			dev_turbochannel_init(machine, mem, 6,
			    KN02_PHYS_TC_6_START, KN02_PHYS_TC_6_END,
			    "PMAD-AA", KN02_IP_LANCE +8);

			/*  TURBOchannel slot 7 is system stuff.  */
			machine->main_console_handle =
			    dev_dc7085_init(machine, mem,
			    KN02_SYS_DZ, KN02_IP_DZ +8, machine->use_x11);
			dev_mc146818_init(machine, mem,
			    KN02_SYS_CLOCK, KN02_INT_CLOCK, MC146818_DEC, 1);

			machine->md_int.kn02_csr =
			    dev_kn02_init(cpu, mem, KN02_SYS_CSR);

			framebuffer_console_name = "osconsole=0,7";
								/*  fb,keyb  */
			serial_console_name      = "osconsole=2";
			boot_scsi_boardnumber = 5;
			boot_net_boardnumber = 6;	/*  TODO: 3?  */
			break;

		case MACHINE_DEC_3MIN_5000:		/*  type 3, KN02BA  */
			machine->machine_name = "DECstation 5000/112 or 145 (3MIN, KN02BA)";
			if (machine->emulated_hz == 0)
				machine->emulated_hz = 33000000;
			if (machine->physical_ram_in_mb > 128)
				fprintf(stderr, "WARNING! Real 3MIN machines cannot have more than 128MB RAM. Continuing anyway.\n");

			/*  KMIN interrupts:  */
			machine->md_interrupt = kmin_interrupt;

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
			machine->md_int.dec_ioasic_data = dev_dec_ioasic_init(cpu, mem, 0x1c000000, 0);
			dev_le_init(machine, mem, 0x1c0c0000, 0, 0, KMIN_INTR_LANCE +8, 4*65536);
			dev_scc_init(machine, mem, 0x1c100000, KMIN_INTR_SCC_0 +8, machine->use_x11, 0, 1);
			dev_scc_init(machine, mem, 0x1c180000, KMIN_INTR_SCC_1 +8, machine->use_x11, 1, 1);
			dev_mc146818_init(machine, mem, 0x1c200000, KMIN_INTR_CLOCK +8, MC146818_DEC, 1);
			dev_asc_init(machine, mem, 0x1c300000, KMIN_INTR_SCSI +8,
			    NULL, DEV_ASC_DEC, NULL, NULL);

			/*
			 *  TURBOchannel slots 0, 1, and 2 are free for
			 *  option cards.  These are by default filled with
			 *  zero or more graphics boards.
			 *
			 *  TODO: irqs 
			 */
			dev_turbochannel_init(machine, mem, 0,
			    0x10000000, 0x103fffff,
			    machine->n_gfx_cards >= 1?
				turbochannel_default_gfx_card : "",
			    KMIN_INT_TC0);

			dev_turbochannel_init(machine, mem, 1,
			    0x14000000, 0x143fffff,
			    machine->n_gfx_cards >= 2?
				turbochannel_default_gfx_card : "",
			    KMIN_INT_TC1);

			dev_turbochannel_init(machine, mem, 2,
			    0x18000000, 0x183fffff,
			    machine->n_gfx_cards >= 3?
				turbochannel_default_gfx_card : "",
			    KMIN_INT_TC2);

			/*  (kmin shared irq numbers (IP) are offset by +8 in the emulator)  */
			/*  kmin_csr = dev_kmin_init(cpu, mem, KMIN_REG_INTR);  */

			framebuffer_console_name = "osconsole=0,3";	/*  fb, keyb (?)  */
			serial_console_name      = "osconsole=3";	/*  ?  */
			break;

		case MACHINE_DEC_3MAXPLUS_5000:	/*  type 4, KN03  */
			machine->machine_name = "DECsystem 5900 or 5000 (3MAX+) (KN03)";

			/*  5000/240 (KN03-GA, R3000): 40 MHz  */
			/*  5000/260 (KN05-NB, R4000): 60 MHz  */
			/*  TODO: are both these type 4?  */
			if (machine->emulated_hz == 0)
				machine->emulated_hz = 40000000;
			if (machine->physical_ram_in_mb > 480)
				fprintf(stderr, "WARNING! Real KN03 machines cannot have more than 480MB RAM. Continuing anyway.\n");

			/*  KN03 interrupts:  */
			machine->md_interrupt = kn03_interrupt;

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
			machine->md_int.dec_ioasic_data = dev_dec_ioasic_init(cpu, mem, 0x1f800000, 0);

			dev_le_init(machine, mem, KN03_SYS_LANCE, 0, 0, KN03_INTR_LANCE +8, 4*65536);

			machine->md_int.dec_ioasic_data->dma_func[3] = dev_scc_dma_func;
			machine->md_int.dec_ioasic_data->dma_func_extra[2] = dev_scc_init(machine, mem, KN03_SYS_SCC_0, KN03_INTR_SCC_0 +8, machine->use_x11, 0, 1);
			machine->md_int.dec_ioasic_data->dma_func[2] = dev_scc_dma_func;
			machine->md_int.dec_ioasic_data->dma_func_extra[3] = dev_scc_init(machine, mem, KN03_SYS_SCC_1, KN03_INTR_SCC_1 +8, machine->use_x11, 1, 1);

			dev_mc146818_init(machine, mem, KN03_SYS_CLOCK, KN03_INT_RTC, MC146818_DEC, 1);
			dev_asc_init(machine, mem, KN03_SYS_SCSI,
			    KN03_INTR_SCSI +8, NULL, DEV_ASC_DEC, NULL, NULL);

			/*
			 *  TURBOchannel slots 0, 1, and 2 are free for
			 *  option cards.  These are by default filled with
			 *  zero or more graphics boards.
			 *
			 *  TODO: irqs 
			 */
			dev_turbochannel_init(machine, mem, 0,
			    KN03_PHYS_TC_0_START, KN03_PHYS_TC_0_END,
			    machine->n_gfx_cards >= 1?
				turbochannel_default_gfx_card : "",
			    KN03_INTR_TC_0 +8);

			dev_turbochannel_init(machine, mem, 1,
			    KN03_PHYS_TC_1_START, KN03_PHYS_TC_1_END,
			    machine->n_gfx_cards >= 2?
				turbochannel_default_gfx_card : "",
			    KN03_INTR_TC_1 +8);

			dev_turbochannel_init(machine, mem, 2,
			    KN03_PHYS_TC_2_START, KN03_PHYS_TC_2_END,
			    machine->n_gfx_cards >= 3?
				turbochannel_default_gfx_card : "",
			    KN03_INTR_TC_2 +8);

			/*  TODO: interrupts  */
			/*  shared (turbochannel) interrupts are +8  */

			framebuffer_console_name = "osconsole=0,3";	/*  fb, keyb (?)  */
			serial_console_name      = "osconsole=3";	/*  ?  */
			break;

		case MACHINE_DEC_5800:		/*  type 5, KN5800  */
			machine->machine_name = "DECsystem 5800";

/*  TODO: this is incorrect, banks multiply by 8 etc  */
			if (machine->physical_ram_in_mb < 48)
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

			machine->md_int.dec5800_csr = dev_dec5800_init(machine, mem, 0x10000000);
			dev_decbi_init(mem, 0x10000000);
			dev_ssc_init(machine, mem, 0x10140000, 2, machine->use_x11, &machine->md_int.dec5800_csr->csr);
			dev_decxmi_init(mem, 0x11800000);
			dev_deccca_init(mem, DEC_DECCCA_BASEADDR);

			break;

		case MACHINE_DEC_5400:		/*  type 6, KN210  */
			machine->machine_name = "DECsystem 5400 (KN210)";
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
			device_add(machine, "kn210 addr=0x10080000");
			dev_ssc_init(machine, mem, 0x10140000, 0, machine->use_x11, NULL);	/*  TODO:  not irq 0  */
			break;

		case MACHINE_DEC_MAXINE_5000:	/*  type 7, KN02CA  */
			machine->machine_name = "Personal DECstation 5000/xxx (MAXINE) (KN02CA)";
			if (machine->emulated_hz == 0)
				machine->emulated_hz = 33000000;

			if (machine->physical_ram_in_mb < 8)
				fprintf(stderr, "WARNING! Real KN02CA machines do not have less than 8MB RAM. Continuing anyway.\n");
			if (machine->physical_ram_in_mb > 40)
				fprintf(stderr, "WARNING! Real KN02CA machines cannot have more than 40MB RAM. Continuing anyway.\n");

			/*  Maxine interrupts:  */
			machine->md_interrupt = maxine_interrupt;

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
			machine->md_int.dec_ioasic_data = dev_dec_ioasic_init(cpu, mem, 0x1c000000, 0);

			/*  TURBOchannel slots (0 and 1):  */
			dev_turbochannel_init(machine, mem, 0,
			    0x10000000, 0x103fffff,
			    machine->n_gfx_cards >= 2?
				turbochannel_default_gfx_card : "",
			    XINE_INTR_TC_0 +8);
			dev_turbochannel_init(machine, mem, 1,
			    0x14000000, 0x143fffff,
			    machine->n_gfx_cards >= 3?
				turbochannel_default_gfx_card : "",
			    XINE_INTR_TC_1 +8);

			/*
			 *  TURBOchannel slot 2 is hardwired to be used by
			 *  the framebuffer: (NOTE: 0x8000000, not 0x18000000)
			 */
			dev_turbochannel_init(machine, mem, 2,
			    0x8000000, 0xbffffff, "PMAG-DV", 0);

			/*
			 *  TURBOchannel slot 3: fixed, ioasic
			 *  (the system stuff), 0x1c000000
			 */
			dev_le_init(machine, mem, 0x1c0c0000, 0, 0, XINE_INTR_LANCE +8, 4*65536);
			dev_scc_init(machine, mem, 0x1c100000,
			    XINE_INTR_SCC_0 +8, machine->use_x11, 0, 1);
			dev_mc146818_init(machine, mem, 0x1c200000,
			    XINE_INT_TOY, MC146818_DEC, 1);
			dev_asc_init(machine, mem, 0x1c300000,
			    XINE_INTR_SCSI +8, NULL, DEV_ASC_DEC, NULL, NULL);

			framebuffer_console_name = "osconsole=3,2";	/*  keyb,fb ??  */
			serial_console_name      = "osconsole=3";
			break;

		case MACHINE_DEC_5500:	/*  type 11, KN220  */
			machine->machine_name = "DECsystem 5500 (KN220)";

			/*
			 *  According to NetBSD's pmax ports page:
			 *  KN220-AA is a "30 MHz R3000 CPU with R3010 FPU"
			 *  with "512 kBytes of Prestoserve battery backed RAM."
			 */
			if (machine->emulated_hz == 0)
				machine->emulated_hz = 30000000;

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

			dev_ssc_init(machine, mem, 0x10140000, 0, machine->use_x11, NULL);		/*  TODO:  not irq 0  */

			/*  something at 0x17000000, ultrix says "cpu 0 panic: DS5500 I/O Board is missing" if this is not here  */
			dev_dec5500_ioboard_init(cpu, mem, 0x17000000);

			dev_sgec_init(mem, 0x10008000, 0);		/*  irq?  */

			/*  The asc controller might be TURBOchannel-ish?  */
#if 0
			dev_turbochannel_init(machine, mem, 0, 0x17100000, 0x171fffff, "PMAZ-AA", 0);	/*  irq?  */
#else
			dev_asc_init(machine, mem, 0x17100000, 0, NULL, DEV_ASC_DEC, NULL, NULL);		/*  irq?  */
#endif

			framebuffer_console_name = "osconsole=0,0";	/*  TODO (?)  */
			serial_console_name      = "osconsole=0";
			break;

		case MACHINE_DEC_MIPSMATE_5100:	/*  type 12  */
			machine->machine_name = "DEC MIPSMATE 5100 (KN230)";
			if (machine->emulated_hz == 0)
				machine->emulated_hz = 20000000;
			if (machine->physical_ram_in_mb > 128)
				fprintf(stderr, "WARNING! Real MIPSMATE 5100 machines cannot have more than 128MB RAM. Continuing anyway.\n");

			if (machine->use_x11)
				fprintf(stderr, "WARNING! Real MIPSMATE 5100 machines cannot have a graphical framebuffer. Continuing anyway.\n");

			/*  KN230 interrupts:  */
			machine->md_interrupt = kn230_interrupt;

			/*
			 *  According to NetBSD/pmax:
			 *  dc0 at ibus0 addr 0x1c000000
			 *  le0 at ibus0 addr 0x18000000: address 00:00:00:00:00:00
			 *  sii0 at ibus0 addr 0x1a000000
			 */
			dev_mc146818_init(machine, mem, KN230_SYS_CLOCK, 4, MC146818_DEC, 1);
			dev_dc7085_init(machine, mem, KN230_SYS_DZ0, KN230_CSR_INTR_DZ0, machine->use_x11);		/*  NOTE: CSR_INTR  */
			/* dev_dc7085_init(machine, mem, KN230_SYS_DZ1, KN230_CSR_INTR_OPT0, machine->use_x11); */	/*  NOTE: CSR_INTR  */
			/* dev_dc7085_init(machine, mem, KN230_SYS_DZ2, KN230_CSR_INTR_OPT1, machine->use_x11); */	/*  NOTE: CSR_INTR  */
			dev_le_init(machine, mem, KN230_SYS_LANCE, KN230_SYS_LANCE_B_START, KN230_SYS_LANCE_B_END, KN230_CSR_INTR_LANCE, 4*1048576);
			dev_sii_init(machine, mem, KN230_SYS_SII, KN230_SYS_SII_B_START, KN230_SYS_SII_B_END, KN230_CSR_INTR_SII);

			snprintf(tmpstr, sizeof(tmpstr),
			    "kn230 addr=0x%llx", (long long)KN230_SYS_ICSR);
			machine->md_int.kn230_csr = device_add(machine, tmpstr);

			serial_console_name = "osconsole=0";
			break;

		default:
			;
		}

		/*
		 *  Most OSes on DECstation use physical addresses below
		 *  0x20000000, but both OSF/1 and Sprite use 0xbe...... as if
		 *  it was 0x1e......, so we need this hack:
		 */
		dev_ram_init(machine, 0xa0000000, 0x20000000,
		    DEV_RAM_MIRROR | DEV_RAM_MIGHT_POINT_TO_DEVICES, 0x0);

		if (machine->prom_emulation) {
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

			cpu->cd.mips.gpr[MIPS_GPR_A0] = 3;
			cpu->cd.mips.gpr[MIPS_GPR_A1] = DEC_PROM_INITIAL_ARGV;
			cpu->cd.mips.gpr[MIPS_GPR_A2] = DEC_PROM_MAGIC;
			cpu->cd.mips.gpr[MIPS_GPR_A3] = DEC_PROM_CALLBACK_STRUCT;

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
			if (machine->machine_subtype == MACHINE_DEC_PMAX_3100)
				strlcpy(bootpath, "rz(0,0,0)", sizeof(bootpath));
			else
#endif
				strlcpy(bootpath, "5/rz1/", sizeof(bootpath));

			if (bootdev_id < 0 || machine->force_netboot) {
				/*  tftp boot:  */
				strlcpy(bootpath, "5/tftp/", sizeof(bootpath));
				bootpath[0] = '0' + boot_net_boardnumber;
			} else {
				/*  disk boot:  */
				bootpath[0] = '0' + boot_scsi_boardnumber;
				if (diskimage_is_a_tape(machine, bootdev_id,
				    bootdev_type))
					bootpath[2] = 't';
				bootpath[4] = '0' + bootdev_id;
			}

			init_bootpath = bootpath;
			}

			bootarg = malloc(BOOTARG_BUFLEN);
			if (bootarg == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			strlcpy(bootarg, init_bootpath, BOOTARG_BUFLEN);
			if (strlcat(bootarg, machine->boot_kernel_filename,
			    BOOTARG_BUFLEN) > BOOTARG_BUFLEN) {
				fprintf(stderr, "bootarg truncated?\n");
				exit(1);
			}

			bootstr = "boot";

			store_string(cpu, DEC_PROM_INITIAL_ARGV+0x10, bootstr);
			store_string(cpu, DEC_PROM_INITIAL_ARGV+0x70, bootarg);
			store_string(cpu, DEC_PROM_INITIAL_ARGV+0xe0,
			    machine->boot_string_argument);

			/*  Decrease the nr of args, if there are no args :-)  */
			if (machine->boot_string_argument == NULL ||
			    machine->boot_string_argument[0] == '\0')
				cpu->cd.mips.gpr[MIPS_GPR_A0] --;

			if (machine->boot_string_argument[0] != '\0') {
				strlcat(bootarg, " ", BOOTARG_BUFLEN);
				if (strlcat(bootarg, machine->boot_string_argument,
				    BOOTARG_BUFLEN) >= BOOTARG_BUFLEN) {
					fprintf(stderr, "bootstr truncated?\n");
					exit(1);
				}
			}

			xx.a.common.next = (char *)&xx.b - (char *)&xx;
			xx.a.common.type = BTINFO_MAGIC;
			xx.a.magic = BOOTINFO_MAGIC;

			xx.b.common.next = (char *)&xx.c - (char *)&xx.b;
			xx.b.common.type = BTINFO_BOOTPATH;
			strlcpy(xx.b.bootpath, bootstr, sizeof(xx.b.bootpath));

			xx.c.common.next = 0;
			xx.c.common.type = BTINFO_SYMTAB;
			xx.c.nsym = 0;
			xx.c.ssym = 0;
			xx.c.esym = machine->file_loaded_end_addr;

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
					    1048576*machine->physical_ram_in_mb)?
					    0xff : 0x00;
			}
			store_buf(cpu, DEC_MEMMAP_ADDR, (char *)&memmap, sizeof(memmap));

			/*  Environment variables:  */
			addr = DEC_PROM_STRINGS;

			if (machine->use_x11 && machine->n_gfx_cards > 0)
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
				snprintf(tmps, sizeof(tmps), "cca=%x",
				    (int)(DEC_DECCCA_BASEADDR + 0xa0000000ULL));
				add_environment_string(cpu, tmps, &addr);
			}

			/*  These are needed for Sprite to boot:  */
			{
				char tmps[500];

				snprintf(tmps, sizeof(tmps), "boot=%s", bootarg);
				tmps[sizeof(tmps)-1] = '\0';
				add_environment_string(cpu, tmps, &addr);

				snprintf(tmps, sizeof(tmps), "bitmap=0x%x", (uint32_t)((
				    DEC_MEMMAP_ADDR + sizeof(memmap.pagesize))
				    & 0xffffffffULL));
				tmps[sizeof(tmps)-1] = '\0';
				add_environment_string(cpu, tmps, &addr);

				snprintf(tmps, sizeof(tmps), "bitmaplen=0x%x",
				    machine->physical_ram_in_mb * 1048576 / 4096 / 8);
				tmps[sizeof(tmps)-1] = '\0';
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
		}

		break;

	case MACHINE_COBALT:
		cpu->byte_order = EMUL_LITTLE_ENDIAN;
		machine->machine_name = "Cobalt";
		stable = 1;

		/*
		 *  Interrupts seem to be the following:
		 *  (according to http://www.funet.fi/pub/Linux/PEOPLE/Linus/v2.4/patch-html/patch-2.4.19/linux-2.4.19_arch_mips_cobalt_irq.c.html)
		 *
		 *	2	Galileo chip (timer)
		 *	3	Tulip 0 + NCR SCSI
		 *	4	Tulip 1
		 *	5	16550 UART (serial console)
		 *	6	VIA southbridge PIC
		 *	7	PCI  (Note: Not used. The PCI controller
		 *		interrupts at ISA interrupt 9.)
		 */

		/*  ISA interrupt controllers:  */
		snprintf(tmpstr, sizeof(tmpstr), "8259 irq=24 addr=0x10000020");
		machine->isa_pic_data.pic1 = device_add(machine, tmpstr);
		snprintf(tmpstr, sizeof(tmpstr), "8259 irq=24 addr=0x100000a0");
		machine->isa_pic_data.pic2 = device_add(machine, tmpstr);
		machine->md_interrupt = isa8_interrupt;
		machine->isa_pic_data.native_irq = 6;

		dev_mc146818_init(machine, mem, 0x10000070, 0, MC146818_PC_CMOS, 4);

		machine->main_console_handle = (size_t)
		    device_add(machine, "ns16550 irq=5 addr=0x1c800000 name2=tty0 in_use=1");

		/*  TODO: bus_isa_init() ?  */

#if 0
		device_add(machine, "ns16550 irq=0 addr=0x1f000010 name2=tty1 in_use=0");
#endif

		/*
		 *  According to NetBSD/cobalt:
		 *
		 *  pchb0 at pci0 dev 0 function 0: Galileo GT-64111 System Controller, rev 1   (NOTE: added by dev_gt_init())
		 *  tlp0 at pci0 dev 7 function 0: DECchip 21143 Ethernet, pass 4.1
		 *  Symbios Logic 53c860 (SCSI mass storage, revision 0x02) at pci0 dev 8
		 *  pcib0 at pci0 dev 9 function 0, VIA Technologies VT82C586 (Apollo VP) PCI-ISA Bridge, rev 37
		 *  pciide0 at pci0 dev 9 function 1: VIA Technologies VT82C586 (Apollo VP) ATA33 cr
		 *  tlp1 at pci0 dev 12 function 0: DECchip 21143 Ethernet, pass 4.1
		 *
		 *  The PCI controller interrupts at ISA interrupt 9.
		 */
		pci_data = dev_gt_init(machine, mem, 0x14000000, 2, 8 + 9, 11);
		bus_pci_add(machine, pci_data, mem, 0,  7, 0, "dec21143");
		/*  bus_pci_add(machine, pci_data, mem, 0,  8, 0, "symbios_860");   PCI_VENDOR_SYMBIOS, PCI_PRODUCT_SYMBIOS_860  */
		bus_pci_add(machine, pci_data, mem, 0,  9, 0, "vt82c586_isa");
		bus_pci_add(machine, pci_data, mem, 0,  9, 1, "vt82c586_ide");
		bus_pci_add(machine, pci_data, mem, 0, 12, 0, "dec21143");

		if (machine->prom_emulation) {
			/*
			 *  NetBSD/cobalt expects memsize in a0, but it seems that what
			 *  it really wants is the end of memory + 0x80000000.
			 *
			 *  The bootstring is stored 512 bytes before the end of
			 *  physical ram.
			 */
			cpu->cd.mips.gpr[MIPS_GPR_A0] =
			    machine->physical_ram_in_mb * 1048576 + 0xffffffff80000000ULL;
			bootstr = "root=/dev/hda1 ro";
			/*  bootstr = "nfsroot=/usr/cobalt/";  */
			/*  TODO: bootarg, and/or automagic boot device detection  */
			store_string(cpu, cpu->cd.mips.gpr[MIPS_GPR_A0] - 512, bootstr);
		}
		break;

	case MACHINE_HPCMIPS:
		cpu->byte_order = EMUL_LITTLE_ENDIAN;
		memset(&hpc_bootinfo, 0, sizeof(hpc_bootinfo));
		/*
		NOTE: See http://forums.projectmayo.com/viewtopic.php?topic=2743&forum=23
		for info on framebuffer addresses.
		*/

		switch (machine->machine_subtype) {
		case MACHINE_HPCMIPS_CASIO_BE300:
			/*  166MHz VR4131  */
			machine->machine_name = "Casio Cassiopeia BE-300";
			hpc_fb_addr = 0x0a200000;
			hpc_fb_xsize = 240;
			hpc_fb_ysize = 320;
			hpc_fb_xsize_mem = 256;
			hpc_fb_ysize_mem = 320;
			hpc_fb_bits = 15;
			hpc_fb_encoding = BIFB_D16_0000;

			/*  TODO: irq?  */
			snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=0 addr=0x0a008680 addr_mult=4 in_use=%i", machine->use_x11? 0 : 1);
			machine->main_console_handle = (size_t)device_add(machine, tmpstr);

			machine->md_int.vr41xx_data = dev_vr41xx_init(machine, mem, 4131);
			machine->md_interrupt = vr41xx_interrupt;

			hpc_platid_cpu_arch = 1;	/*  MIPS  */
			hpc_platid_cpu_series = 1;	/*  VR  */
			hpc_platid_cpu_model = 1;	/*  VR41XX  */
			hpc_platid_cpu_submodel = 6;	/*  VR4131  */
			hpc_platid_vendor = 3;		/*  Casio  */
			hpc_platid_series = 1;		/*  CASSIOPEIAE  */
			hpc_platid_model = 2;		/*  EXXX  */
			hpc_platid_submodel = 3;	/*  E500  */
			/*  TODO: Don't use model number for E500, it's a BE300!  */
			break;
		case MACHINE_HPCMIPS_CASIO_E105:
			/*  131MHz VR4121  */
			machine->machine_name = "Casio Cassiopeia E-105";
			hpc_fb_addr = 0x0a200000;	/*  TODO?  */
			hpc_fb_xsize = 240;
			hpc_fb_ysize = 320;
			hpc_fb_xsize_mem = 256;
			hpc_fb_ysize_mem = 320;
			hpc_fb_bits = 16;
			hpc_fb_encoding = BIFB_D16_0000;

			/*  TODO: irq?  */
			snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=0 addr=0x0a008680 addr_mult=4 in_use=%i", machine->use_x11? 0 : 1);
			machine->main_console_handle = (size_t)device_add(machine, tmpstr);

			machine->md_int.vr41xx_data = dev_vr41xx_init(machine, mem, 4121);
			machine->md_interrupt = vr41xx_interrupt;

			hpc_platid_cpu_arch = 1;	/*  MIPS  */
			hpc_platid_cpu_series = 1;	/*  VR  */
			hpc_platid_cpu_model = 1;	/*  VR41XX  */
			hpc_platid_cpu_submodel = 3;	/*  VR4121  */
			hpc_platid_vendor = 3;		/*  Casio  */
			hpc_platid_series = 1;		/*  CASSIOPEIAE  */
			hpc_platid_model = 2;		/*  EXXX  */
			hpc_platid_submodel = 2;	/*  E105  */
			break;
		case MACHINE_HPCMIPS_NEC_MOBILEPRO_770:
			/*  131 MHz VR4121  */
			machine->machine_name = "NEC MobilePro 770";
			stable = 1;
			hpc_fb_addr = 0xa000000;
			hpc_fb_xsize = 640;
			hpc_fb_ysize = 240;
			hpc_fb_xsize_mem = 800;
			hpc_fb_ysize_mem = 240;
			hpc_fb_bits = 16;
			hpc_fb_encoding = BIFB_D16_0000;

			machine->md_int.vr41xx_data = dev_vr41xx_init(machine, mem, 4121);
			machine->md_interrupt = vr41xx_interrupt;

			hpc_platid_cpu_arch = 1;	/*  MIPS  */
			hpc_platid_cpu_series = 1;	/*  VR  */
			hpc_platid_cpu_model = 1;	/*  VR41XX  */
			hpc_platid_cpu_submodel = 3;	/*  VR4121  */
			hpc_platid_vendor = 1;		/*  NEC  */
			hpc_platid_series = 2;		/*  NEC MCR  */
			hpc_platid_model = 2;		/*  MCR 5XX  */
			hpc_platid_submodel = 4;	/*  MCR 520A  */
			break;
		case MACHINE_HPCMIPS_NEC_MOBILEPRO_780:
			/*  166 (or 168) MHz VR4121  */
			machine->machine_name = "NEC MobilePro 780";
			stable = 1;
			hpc_fb_addr = 0xa180100;
			hpc_fb_xsize = 640;
			hpc_fb_ysize = 240;
			hpc_fb_xsize_mem = 640;
			hpc_fb_ysize_mem = 240;
			hpc_fb_bits = 16;
			hpc_fb_encoding = BIFB_D16_0000;

			machine->md_int.vr41xx_data = dev_vr41xx_init(machine, mem, 4121);
			machine->md_interrupt = vr41xx_interrupt;

			hpc_platid_cpu_arch = 1;	/*  MIPS  */
			hpc_platid_cpu_series = 1;	/*  VR  */
			hpc_platid_cpu_model = 1;	/*  VR41XX  */
			hpc_platid_cpu_submodel = 3;	/*  VR4121  */
			hpc_platid_vendor = 1;		/*  NEC  */
			hpc_platid_series = 2;		/*  NEC MCR  */
			hpc_platid_model = 2;		/*  MCR 5XX  */
			hpc_platid_submodel = 8;	/*  MCR 530A  */
			break;
		case MACHINE_HPCMIPS_NEC_MOBILEPRO_800:
			/*  131 MHz VR4121  */
			machine->machine_name = "NEC MobilePro 800";
			stable = 1;
			hpc_fb_addr = 0xa000000;
			hpc_fb_xsize = 800;
			hpc_fb_ysize = 600;
			hpc_fb_xsize_mem = 800;
			hpc_fb_ysize_mem = 600;
			hpc_fb_bits = 16;
			hpc_fb_encoding = BIFB_D16_0000;

			machine->md_int.vr41xx_data = dev_vr41xx_init(machine, mem, 4121);
			machine->md_interrupt = vr41xx_interrupt;

			hpc_platid_cpu_arch = 1;	/*  MIPS  */
			hpc_platid_cpu_series = 1;	/*  VR  */
			hpc_platid_cpu_model = 1;	/*  VR41XX  */
			hpc_platid_cpu_submodel = 3;	/*  VR4121  */
			hpc_platid_vendor = 1;		/*  NEC  */
			hpc_platid_series = 2;		/*  NEC MCR  */
			hpc_platid_model = 3;		/*  MCR 7XX  */
			hpc_platid_submodel = 2;	/*  MCR 700A  */
			break;
		case MACHINE_HPCMIPS_NEC_MOBILEPRO_880:
			/*  168 MHz VR4121  */
			machine->machine_name = "NEC MobilePro 880";
			stable = 1;
			hpc_fb_addr = 0xa0ea600;
			hpc_fb_xsize = 800;
			hpc_fb_ysize = 600;
			hpc_fb_xsize_mem = 800;
			hpc_fb_ysize_mem = 600;
			hpc_fb_bits = 16;
			hpc_fb_encoding = BIFB_D16_0000;

			machine->md_int.vr41xx_data = dev_vr41xx_init(machine, mem, 4121);
			machine->md_interrupt = vr41xx_interrupt;

			hpc_platid_cpu_arch = 1;	/*  MIPS  */
			hpc_platid_cpu_series = 1;	/*  VR  */
			hpc_platid_cpu_model = 1;	/*  VR41XX  */
			hpc_platid_cpu_submodel = 3;	/*  VR4121  */
			hpc_platid_vendor = 1;		/*  NEC  */
			hpc_platid_series = 2;		/*  NEC MCR  */
			hpc_platid_model = 3;		/*  MCR 7XX  */
			hpc_platid_submodel = 4;	/*  MCR 730A  */
			break;
		case MACHINE_HPCMIPS_AGENDA_VR3:
			/*  66 MHz VR4181  */
			machine->machine_name = "Agenda VR3";
			/*  TODO:  */
			hpc_fb_addr = 0x1000;
			hpc_fb_xsize = 160;
			hpc_fb_ysize = 240;
			hpc_fb_xsize_mem = 160;
			hpc_fb_ysize_mem = 240;
			hpc_fb_bits = 4;
			hpc_fb_encoding = BIFB_D4_M2L_F;

			machine->md_int.vr41xx_data = dev_vr41xx_init(machine, mem, 4181);
			machine->md_interrupt = vr41xx_interrupt;

			/*  TODO: Hm... irq 17 according to linux, but
			    VRIP_INTR_SIU (=9) here?  */
			{
				int x;
				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=%i addr=0x0c000010", 8 + VRIP_INTR_SIU);
				x = (size_t)device_add(machine, tmpstr);

				if (!machine->use_x11)
					machine->main_console_handle = x;
			}

			hpc_platid_cpu_arch = 1;	/*  MIPS  */
			hpc_platid_cpu_series = 1;	/*  VR  */
			hpc_platid_cpu_model = 1;	/*  VR41XX  */
			hpc_platid_cpu_submodel = 4;	/*  VR4181  */
			hpc_platid_vendor = 15;		/*  Agenda  */
			hpc_platid_series = 1;		/*  VR  */
			hpc_platid_model = 1;		/*  VR3  */
			hpc_platid_submodel = 0;	/*  -  */

			dev_ram_init(machine, 0x0f000000, 0x01000000,
			    DEV_RAM_MIRROR | DEV_RAM_MIGHT_POINT_TO_DEVICES, 0x0);
			break;
		case MACHINE_HPCMIPS_IBM_WORKPAD_Z50:
			/*  131 MHz VR4121  */
			machine->machine_name = "IBM Workpad Z50";
			/*  TODO:  */
			hpc_fb_addr = 0xa000000;
			hpc_fb_xsize = 640;
			hpc_fb_ysize = 480;
			hpc_fb_xsize_mem = 640;
			hpc_fb_ysize_mem = 480;
			hpc_fb_bits = 16;
			hpc_fb_encoding = BIFB_D16_0000;

			machine->md_int.vr41xx_data = dev_vr41xx_init(machine, mem, 4121);
			machine->md_interrupt = vr41xx_interrupt;

			hpc_platid_cpu_arch = 1;	/*  MIPS  */
			hpc_platid_cpu_series = 1;	/*  VR  */
			hpc_platid_cpu_model = 1;	/*  VR41XX  */
			hpc_platid_cpu_submodel = 3;	/*  VR4121  */
			hpc_platid_vendor = 9;		/*  IBM  */
			hpc_platid_series = 1;		/*  WorkPad  */
			hpc_platid_model = 1;		/*  Z50  */
			hpc_platid_submodel = 0;	/*  0  */
			break;
		default:
			printf("Unimplemented hpcmips machine number.\n");
			exit(1);
		}

		store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.platid_cpu,
		      (hpc_platid_cpu_arch << 26) + (hpc_platid_cpu_series << 20)
		    + (hpc_platid_cpu_model << 14) + (hpc_platid_cpu_submodel <<  8)
		    + hpc_platid_flags);
		store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.platid_machine,
		      (hpc_platid_vendor << 22) + (hpc_platid_series << 16)
		    + (hpc_platid_model <<  8) + hpc_platid_submodel);

		if (machine->use_x11)
			machine->main_console_handle =
			    machine->md_int.vr41xx_data->kiu_console_handle;

		if (machine->prom_emulation) {
			/*  NetBSD/hpcmips and possibly others expects the following:  */

			cpu->cd.mips.gpr[MIPS_GPR_A0] = 1;	/*  argc  */
			cpu->cd.mips.gpr[MIPS_GPR_A1] = machine->physical_ram_in_mb * 1048576
			    + 0xffffffff80000000ULL - 512;	/*  argv  */
			cpu->cd.mips.gpr[MIPS_GPR_A2] = machine->physical_ram_in_mb * 1048576
			    + 0xffffffff80000000ULL - 256;	/*  ptr to hpc_bootinfo  */

			bootstr = machine->boot_kernel_filename;
			store_32bit_word(cpu, 0xffffffff80000000ULL + machine->physical_ram_in_mb * 1048576 - 512, 
			    0xffffffff80000000ULL + machine->physical_ram_in_mb * 1048576 - 512 + 16);
			store_32bit_word(cpu, 0xffffffff80000000ULL + machine->physical_ram_in_mb * 1048576 - 512 + 4, 0);
			store_string(cpu, 0xffffffff80000000ULL + machine->physical_ram_in_mb * 1048576 - 512 + 16, bootstr);

			/*  Special case for the Agenda VR3:  */
			if (machine->machine_subtype == MACHINE_HPCMIPS_AGENDA_VR3) {
				const int tmplen = 1000;
				char *tmp = malloc(tmplen);

				cpu->cd.mips.gpr[MIPS_GPR_A0] = 2;	/*  argc  */

				store_32bit_word(cpu, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 512 + 4, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 512 + 64);
				store_32bit_word(cpu, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 512 + 8, 0);

				snprintf(tmp, tmplen, "root=/dev/rom video=vr4181fb:xres:160,yres:240,bpp:4,"
				    "gray,hpck:3084,inv ether=0,0x03fe0300,eth0");
				tmp[tmplen-1] = '\0';

				if (!machine->use_x11)
					snprintf(tmp+strlen(tmp), tmplen-strlen(tmp), " console=ttyS0,115200");
				tmp[tmplen-1] = '\0';

				if (machine->boot_string_argument[0])
					snprintf(tmp+strlen(tmp), tmplen-strlen(tmp), " %s", machine->boot_string_argument);
				tmp[tmplen-1] = '\0';

				store_string(cpu, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 512 + 64, tmp);

				bootarg = tmp;
			} else if (machine->boot_string_argument[0]) {
				cpu->cd.mips.gpr[MIPS_GPR_A0] ++;	/*  argc  */

				store_32bit_word(cpu, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 512 + 4, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 512 + 64);
				store_32bit_word(cpu, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 512 + 8, 0);

				store_string(cpu, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 512 + 64,
				    machine->boot_string_argument);

				bootarg = machine->boot_string_argument;
			}

			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.length, sizeof(hpc_bootinfo));
			store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.magic, HPC_BOOTINFO_MAGIC);
			store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_addr, 0x80000000 + hpc_fb_addr);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_line_bytes, hpc_fb_xsize_mem * (((hpc_fb_bits-1)|7)+1) / 8);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_width, hpc_fb_xsize);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_height, hpc_fb_ysize);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_type, hpc_fb_encoding);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.bi_cnuse, BI_CNUSE_BUILTIN);  /*  _BUILTIN or _SERIAL  */

			/*  printf("hpc_bootinfo.platid_cpu     = 0x%08x\n", hpc_bootinfo.platid_cpu);
			    printf("hpc_bootinfo.platid_machine = 0x%08x\n", hpc_bootinfo.platid_machine);  */
			store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.timezone, 0);
			store_buf(cpu, 0x80000000 + machine->physical_ram_in_mb * 1048576 - 256, (char *)&hpc_bootinfo, sizeof(hpc_bootinfo));
		}

		if (hpc_fb_addr != 0) {
			dev_fb_init(machine, mem, hpc_fb_addr, VFB_HPC,
			    hpc_fb_xsize, hpc_fb_ysize,
			    hpc_fb_xsize_mem, hpc_fb_ysize_mem,
			    hpc_fb_bits, machine->machine_name);

			/*  NetBSD/hpcmips uses framebuffer at physical
			    address 0x8.......:  */
			dev_ram_init(machine, 0x80000000, 0x20000000,
			    DEV_RAM_MIRROR | DEV_RAM_MIGHT_POINT_TO_DEVICES, 0x0);
		}

		break;

	case MACHINE_PS2:
		cpu->byte_order = EMUL_LITTLE_ENDIAN;
		machine->machine_name = "Playstation 2";

		if (machine->physical_ram_in_mb != 32)
			fprintf(stderr, "WARNING! Playstation 2 machines are supposed to have exactly 32 MB RAM. Continuing anyway.\n");
		if (!machine->use_x11)
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

		machine->md_int.ps2_data = dev_ps2_stuff_init(machine, mem, 0x10000000);
		device_add(machine, "ps2_gs addr=0x12000000");
		device_add(machine, "ps2_ether addr=0x14001000");
		dev_ram_init(machine, 0x1c000000, 4 * 1048576, DEV_RAM_RAM, 0);	/*  TODO: how much?  */
		/*  irq = 8 + 32 + 1 (SBUS/USB)  */
		device_add(machine, "ohci addr=0x1f801600 irq=41");

		machine->md_interrupt = ps2_interrupt;

		/*  Set the Harddisk controller present flag, if either
		    disk 0 or 1 is present:  */
		if (diskimage_exist(machine, 0, DISKIMAGE_IDE) ||
		    diskimage_exist(machine, 1, DISKIMAGE_IDE)) {
			if (machine->prom_emulation)
				store_32bit_word(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x0, 0x100);
			dev_ps2_spd_init(machine, mem, 0x14000000);
		}

		if (machine->prom_emulation) {
			int tmplen = 1000;
			char *tmp = malloc(tmplen);
			time_t timet;
			struct tm *tm_ptr;

			add_symbol_name(&machine->symbol_context,
			    PLAYSTATION2_SIFBIOS, 0x10000, "[SIFBIOS entry]", 0, 0);
			store_32bit_word(cpu, PLAYSTATION2_BDA + 0, PLAYSTATION2_SIFBIOS);
			store_buf(cpu, PLAYSTATION2_BDA + 4, "PS2b", 4);

			store_32bit_word(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x4, PLAYSTATION2_OPTARGS);
			if (tmp == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}

			strlcpy(tmp, "root=/dev/hda1 crtmode=vesa0,60", tmplen);

			if (machine->boot_string_argument[0])
				snprintf(tmp+strlen(tmp), tmplen-strlen(tmp),
				    " %s", machine->boot_string_argument);
			tmp[tmplen-1] = '\0';

			bootstr = tmp;
			store_string(cpu, PLAYSTATION2_OPTARGS, bootstr);

			/*  TODO:  netbsd's bootinfo.h, for symbolic names  */

			/*  RTC data given by the BIOS:  */
			timet = time(NULL) + 9*3600;	/*  PS2 uses Japanese time  */
			tm_ptr = gmtime(&timet);
			/*  TODO:  are these 0- or 1-based?  */
			store_byte(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 1, int_to_bcd(tm_ptr->tm_sec));
			store_byte(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 2, int_to_bcd(tm_ptr->tm_min));
			store_byte(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 3, int_to_bcd(tm_ptr->tm_hour));
			store_byte(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 5, int_to_bcd(tm_ptr->tm_mday));
			store_byte(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 6, int_to_bcd(tm_ptr->tm_mon + 1));
			store_byte(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x10 + 7, int_to_bcd(tm_ptr->tm_year - 100));

			/*  "BOOTINFO_PCMCIA_TYPE" in NetBSD's bootinfo.h. This contains the sbus controller type.  */
			store_32bit_word(cpu, 0xa0000000 + machine->physical_ram_in_mb*1048576 - 0x1000 + 0x1c, 2);
		}

		break;

	case MACHINE_SGI:
	case MACHINE_ARC:
		/*
		 *  SGI and ARC emulation share a lot of code. (SGI is a special case of
		 *  "almost ARC".)
		 *
		 *  http://obsolete.majix.org/computers/sgi/iptable.shtml contains a pretty
		 *  detailed list of IP ("Inhouse Processor") model numbers.
		 *  (Or http://hardware.majix.org/computers/sgi/iptable.shtml)
		 */
		machine->machine_name = malloc(MACHINE_NAME_MAXBUF);
		if (machine->machine_name == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}

		if (machine->machine_type == MACHINE_SGI) {
			cpu->byte_order = EMUL_BIG_ENDIAN;
			snprintf(machine->machine_name, MACHINE_NAME_MAXBUF,
			    "SGI-IP%i", machine->machine_subtype);

			sgi_ram_offset = 1048576 * machine->memory_offset_in_mb;

			/*  Special cases for IP20,22,24,26 memory offset:  */
			if (machine->machine_subtype == 20 || machine->machine_subtype == 22 ||
			    machine->machine_subtype == 24 || machine->machine_subtype == 26) {
				dev_ram_init(machine, 0x00000000, 0x10000, DEV_RAM_MIRROR
				    | DEV_RAM_MIGHT_POINT_TO_DEVICES, sgi_ram_offset);
				dev_ram_init(machine, 0x00050000, sgi_ram_offset-0x50000,
				    DEV_RAM_MIRROR | DEV_RAM_MIGHT_POINT_TO_DEVICES, sgi_ram_offset + 0x50000);
			}

			/*  Special cases for IP28,30 memory offset:  */
			if (machine->machine_subtype == 28 || machine->machine_subtype == 30) {
				/*  TODO: length below should maybe not be 128MB?  */
				dev_ram_init(machine, 0x00000000, 128*1048576, DEV_RAM_MIRROR | DEV_RAM_MIGHT_POINT_TO_DEVICES, sgi_ram_offset);
			}
		} else {
			cpu->byte_order = EMUL_LITTLE_ENDIAN;
			snprintf(machine->machine_name,
			    MACHINE_NAME_MAXBUF, "ARC");
		}

		if (machine->machine_type == MACHINE_SGI) {
			/*  TODO:  Other SGI machine types?  */
			switch (machine->machine_subtype) {
			case 10:
				strlcat(machine->machine_name, " (4D/25)", MACHINE_NAME_MAXBUF);
				/*  TODO  */
				break;
			case 12:
				strlcat(machine->machine_name,
				    " (Iris Indigo IP12)", MACHINE_NAME_MAXBUF);

				/*  TODO  */
				/*  33 MHz R3000, according to http://www.irisindigo.com/  */
				/*  "capable of addressing up to 96MB of memory."  */

				break;
			case 19:
				strlcat(machine->machine_name,
				    " (Everest IP19)", MACHINE_NAME_MAXBUF);
				machine->main_console_handle = (size_t)device_add(machine,
				    "z8530 addr=0x1fbd9830 irq=0 addr_mult=4");
				dev_scc_init(machine, mem, 0x10086000, 0, machine->use_x11, 0, 8);	/*  serial? irix?  */

				device_add(machine, "sgi_ip19 addr=0x18000000");

				/*  Irix' <everest_du_init+0x130> reads this device:  */
				device_add(machine, "random addr=0x10006000 len=16");

				/*  Irix' get_mpconf() looks for this:  (TODO)  */
				store_32bit_word(cpu, 0xa0000000 + 0x3000,
				    0xbaddeed2);

				/*  Memory size, not 4096 byte pages, but 256 bytes?  (16 is size of kernel... approx)  */
				store_32bit_word(cpu, 0xa0000000 + 0x26d0,
				    30000);  /* (machine->physical_ram_in_mb - 16) * (1048576 / 256));  */

				break;
			case 20:
				strlcat(machine->machine_name,
				    " (Indigo)", MACHINE_NAME_MAXBUF);

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
				machine->md_int.sgi_ip20_data = dev_sgi_ip20_init(cpu, mem, DEV_SGI_IP20_BASE);

				/*  imc0 at mainbus0 addr 0x1fa00000: revision 0:  TODO (or in dev_sgi_ip20?)  */

				machine->main_console_handle = (size_t)device_add(machine,
				    "z8530 addr=0x1fbd9830 irq=0 addr_mult=4");

				/*  This is the zsc0 reported by NetBSD:  TODO: irqs  */
				machine->main_console_handle = (size_t)device_add(machine,
				    "z8530 addr=0x1fb80d10 irq=0 addr_mult=4");
				machine->main_console_handle = (size_t)device_add(machine,
				    "z8530 addr=0x1fb80d00 irq=0 addr_mult=4");

				/*  WDSC SCSI controller:  */
				dev_wdsc_init(machine, mem, 0x1fb8011f, 0, 0);

				/*  Return memory read errors so that hpc1
				    and hpc2 are not detected:  */
				device_add(machine, "unreadable addr=0x1fb00000 len=0x10000");
				device_add(machine, "unreadable addr=0x1f980000 len=0x10000");

				/*  Return nothing for gio slots 0, 1, and 2: */
				device_add(machine, "unreadable addr=0x1f400000 len=0x1000");	/*  gio0 slot 0  */
				device_add(machine, "unreadable addr=0x1f600000 len=0x1000");	/*  gio0 slot 1  */
				device_add(machine, "unreadable addr=0x1f000000 len=0x1000");	/*  gio0 slot 2  */

				break;
			case 21:
				strlcat(machine->machine_name,	/*  TODO  */
				    " (uknown SGI-IP21 ?)", MACHINE_NAME_MAXBUF);
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);

				device_add(machine, "random addr=0x418000200, len=0x20000");

				break;
			case 22:
			case 24:
				if (machine->machine_subtype == 22) {
					strlcat(machine->machine_name,
					    " (Indy, Indigo2, Challenge S; Full-house)",
					    MACHINE_NAME_MAXBUF);
					machine->md_int.sgi_ip22_data = dev_sgi_ip22_init(machine, mem, 0x1fbd9000, 0);
				} else {
					strlcat(machine->machine_name,
					    " (Indy, Indigo2, Challenge S; Guiness)",
					    MACHINE_NAME_MAXBUF);
					machine->md_int.sgi_ip22_data = dev_sgi_ip22_init(machine, mem, 0x1fbd9880, 1);
				}

/*
Why is this here? TODO
				dev_ram_init(machine, 0x88000000ULL,
				    128 * 1048576, DEV_RAM_MIRROR, 0x08000000);
*/
				machine->md_interrupt = sgi_ip22_interrupt;

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

				/*  zsc0 serial console. 8 + 32 + 3 + 64*5 = 43+64*5 = 363 */
				i = (size_t)device_add(machine,
				    "z8530 addr=0x1fbd9830 irq=363 addr_mult=4");

				/*  Not supported by NetBSD 1.6.2, but by 2.0_BETA:  */
				j = dev_pckbc_init(machine, mem, 0x1fbd9840, PCKBC_8242,
				    0, 0, machine->use_x11, 0);  /*  TODO: irq numbers  */

				if (machine->use_x11)
					machine->main_console_handle = j;

				/*  sq0: Ethernet.  TODO:  This should have irq_nr = 8 + 3  */
				/*  dev_sq_init...  */

	 			/*  wdsc0: SCSI  */
				dev_wdsc_init(machine, mem, 0x1fbc4000, 0, 8 + 1);

				/*  wdsc1: SCSI  TODO: irq nr  */
				dev_wdsc_init(machine, mem, 0x1fbcc000, 1, 8 + 1);

				/*  dsclock0: TODO:  possibly irq 8 + 33  */

				/*  Return memory read errors so that hpc1 and hpc2 are not detected:  */
				device_add(machine, "unreadable addr=0x1fb00000, len=0x10000");
				device_add(machine, "unreadable addr=0x1f980000, len=0x10000");

				/*  Similarly for gio slots 0, 1, and 2:  */
				device_add(machine, "unreadable addr=0x1f400000, len=0x1000");	/*  gio0 slot 0  */
				device_add(machine, "unreadable addr=0x1f600000, len=0x1000");	/*  gio0 slot 1  */
				device_add(machine, "unreadable addr=0x1f000000, len=0x1000");	/*  gio0 slot 2  */

				break;
			case 25:
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);
				strlcat(machine->machine_name,
				    " (Everest IP25)", MACHINE_NAME_MAXBUF);

				 /*  serial? irix?  */
				dev_scc_init(machine, mem,
				    0x400086000ULL, 0, machine->use_x11, 0, 8);

				/*  NOTE: ip19! (perhaps not really the same  */
				device_add(machine, "sgi_ip19 addr=0x18000000");

				/*
				 *  Memory size, not 4096 byte pages, but 256
				 *  bytes?  (16 is size of kernel... approx)
				 */
				store_32bit_word(cpu, 0xa0000000ULL + 0x26d0,
				    30000);  /* (machine->physical_ram_in_mb - 16)
						 * (1048576 / 256));  */

				break;
			case 26:
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);
				strlcat(machine->machine_name,
				    " (uknown SGI-IP26 ?)",
				    MACHINE_NAME_MAXBUF);	/*  TODO  */
				machine->main_console_handle = (size_t)device_add(machine,
				    "z8530 addr=0x1fbd9830 irq=0 addr_mult=4");
				break;
			case 27:
				strlcat(machine->machine_name,
				    " (Origin 200/2000, Onyx2)",
				    MACHINE_NAME_MAXBUF);
				arc_wordlen = sizeof(uint64_t);
				/*  2 cpus per node  */

				machine->main_console_handle = (size_t)device_add(machine,
				    "z8530 addr=0x1fbd9830 irq=0 addr_mult=4");
				break;
			case 28:
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);
				strlcat(machine->machine_name,
				    " (Impact Indigo2 ?)", MACHINE_NAME_MAXBUF);

				device_add(machine, "random addr=0x1fbe0000, len=1");

				/*  Something at paddr 0x1880fb0000.  */

				break;
			case 30:
				/*  NOTE:  Special case for arc_wordlen:  */
				arc_wordlen = sizeof(uint64_t);
				strlcat(machine->machine_name,
				    " (Octane)", MACHINE_NAME_MAXBUF);

				machine->md_int.sgi_ip30_data = dev_sgi_ip30_init(machine, mem, 0x0ff00000);
				machine->md_interrupt = sgi_ip30_interrupt;

				dev_ram_init(machine,    0xa0000000ULL,
				    128 * 1048576, DEV_RAM_MIRROR
				    | DEV_RAM_MIGHT_POINT_TO_DEVICES,
				    0x00000000);

				dev_ram_init(machine,    0x80000000ULL,
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

				/*  TODO: irq!  */
				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=0 addr=0x1f620170 name2=tty0 in_use=%i", machine->use_x11? 0 : 1);
				machine->main_console_handle = (size_t)device_add(machine, tmpstr);
				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=0 addr=0x1f620178 name2=tty1 in_use=0");
				device_add(machine, tmpstr);

				/*  MardiGras graphics:  */
				device_add(machine, "sgi_mardigras addr=0x1c000000");

				break;
			case 32:
				strlcat(machine->machine_name,
				    " (O2)", MACHINE_NAME_MAXBUF);
				stable = 1;

				/*  TODO:  Find out where the physical ram is actually located.  */
				dev_ram_init(machine, 0x07ffff00ULL,           256, DEV_RAM_MIRROR, 0x03ffff00);
				dev_ram_init(machine, 0x10000000ULL,           256, DEV_RAM_MIRROR, 0x00000000);
				dev_ram_init(machine, 0x11ffff00ULL,           256, DEV_RAM_MIRROR, 0x01ffff00);
				dev_ram_init(machine, 0x12000000ULL,           256, DEV_RAM_MIRROR, 0x02000000);
				dev_ram_init(machine, 0x17ffff00ULL,           256, DEV_RAM_MIRROR, 0x03ffff00);
				dev_ram_init(machine, 0x20000000ULL, 128 * 1048576, DEV_RAM_MIRROR, 0x00000000);
				dev_ram_init(machine, 0x40000000ULL, 128 * 1048576, DEV_RAM_MIRROR, 0x10000000);

				machine->md_int.ip32.crime_data = dev_crime_init(machine, mem, 0x14000000, 2, machine->use_x11);	/*  crime0  */
				dev_sgi_mte_init(mem, 0x15000000);			/*  mte ??? memory thing  */
				dev_sgi_gbe_init(machine, mem, 0x16000000);	/*  gbe?  framebuffer?  */

				/*
				 *  A combination of NetBSD and Linux info:
				 *
				 *      17000000	vice (Video Image Compression Engine)
				 *	1f000000	mace
				 *	1f080000	macepci
				 *	1f100000	vin1
				 *	1f180000	vin2
				 *	1f200000	vout
				 *	1f280000	enet (mec0, MAC-110 Ethernet)
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

				machine->md_int.ip32.mace_data = dev_mace_init(mem, 0x1f310000, 2);
				machine->md_interrupt = sgi_ip32_interrupt;

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

				net_generate_unique_mac(machine, macaddr);
				eaddr_string = malloc(ETHERNET_STRING_MAXLEN);
				if (eaddr_string == NULL) {
					fprintf(stderr, "out of memory\n");
					exit(1);
				}
				snprintf(eaddr_string, ETHERNET_STRING_MAXLEN,
				    "eaddr=%02x:%02x:%02x:%02x:%02x:%02x",
				    macaddr[0], macaddr[1], macaddr[2],
				    macaddr[3], macaddr[4], macaddr[5]);
				dev_sgi_mec_init(machine, mem, 0x1f280000,
				    MACE_ETHERNET, macaddr);

				dev_sgi_ust_init(mem, 0x1f340000);  /*  ust?  */

				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=%i addr=0x1f390000 addr_mult=0x100 in_use=%i name2=tty0",
				    (1<<20) + MACE_PERIPH_SERIAL, machine->use_x11? 0 : 1);
				j = (size_t)device_add(machine, tmpstr);
				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=%i addr=0x1f398000 addr_mult=0x100 in_use=%i name2=tty1",
				    (1<<26) + MACE_PERIPH_SERIAL, 0);
				device_add(machine, tmpstr);

				machine->main_console_handle = j;

				/*  TODO: Once this works, it should be enabled
				    always, not just when using X!  */
				if (machine->use_x11) {
					i = dev_pckbc_init(machine, mem, 0x1f320000,
					    PCKBC_8242, 0x200 + MACE_PERIPH_MISC,
					    0x800 + MACE_PERIPH_MISC, machine->use_x11, 0);
						/*  keyb+mouse (mace irq numbers)  */
					machine->main_console_handle = i;
				}

				dev_mc146818_init(machine, mem, 0x1f3a0000, (1<<8) + MACE_PERIPH_MISC, MC146818_SGI, 0x40);  /*  mcclock0  */
				machine->main_console_handle = (size_t)device_add(machine,
				    "z8530 addr=0x1fbd9830 irq=0 addr_mult=4");

				/*
				 *  PCI devices:   (according to NetBSD's GENERIC config file for sgimips)
				 *
				 *	ne*             at pci? dev ? function ?
				 *	ahc0            at pci0 dev 1 function ?
				 *	ahc1            at pci0 dev 2 function ?
				 */

				pci_data = dev_macepci_init(machine, mem, 0x1f080000, MACE_PCI_BRIDGE);	/*  macepci0  */
				/*  bus_pci_add(machine, pci_data, mem, 0, 0, 0, "ne2000");  TODO  */

				/*  TODO: make this nicer  */
				if (diskimage_exist(machine, 0, DISKIMAGE_SCSI) ||
				    diskimage_exist(machine, 1, DISKIMAGE_SCSI) ||
				    diskimage_exist(machine, 2, DISKIMAGE_SCSI) ||
				    diskimage_exist(machine, 3, DISKIMAGE_SCSI) ||
				    diskimage_exist(machine, 4, DISKIMAGE_SCSI) ||
				    diskimage_exist(machine, 5, DISKIMAGE_SCSI) ||
				    diskimage_exist(machine, 6, DISKIMAGE_SCSI) ||
				    diskimage_exist(machine, 7, DISKIMAGE_SCSI))
					bus_pci_add(machine, pci_data, mem, 0, 1, 0, "ahc");

				/*  TODO: second ahc  */
				/*  bus_pci_add(machine, pci_data, mem, 0, 2, 0, "ahc");  */

				break;
			case 35:
				strlcat(machine->machine_name,
				    " (Origin 3000)", MACHINE_NAME_MAXBUF);
				/*  4 cpus per node  */

				machine->main_console_handle = (size_t)device_add(machine,
				    "z8530 addr=0x1fbd9830 irq=0 addr_mult=4");
				break;
			case 53:
				strlcat(machine->machine_name,
				    " (Origin 350)", MACHINE_NAME_MAXBUF);
				/*
				 *  According to http://kumba.drachentekh.net/xml/myguide.html
				 *  Origin 350, Tezro IP53 R16000
				 */
				break;
			default:
				fatal("unimplemented SGI machine type IP%i\n",
				    machine->machine_subtype);
				exit(1);
			}
		} else {
			switch (machine->machine_subtype) {

			case MACHINE_ARC_NEC_RD94:
			case MACHINE_ARC_NEC_R94:
			case MACHINE_ARC_NEC_R96:
				/*
				 *  "NEC-RD94" (NEC RISCstation 2250)
				 *  "NEC-R94" (NEC RISCstation 2200)
				 *  "NEC-R96" (NEC Express RISCserver)
				 *
				 *  http://mirror.aarnet.edu.au/pub/NetBSD/misc/chs/arcdiag.out (NEC-R96)
				 */

				switch (machine->machine_subtype) {
				case MACHINE_ARC_NEC_RD94:
					strlcat(machine->machine_name,
					    " (NEC-RD94, NEC RISCstation 2250)",
					    MACHINE_NAME_MAXBUF);
					break;
				case MACHINE_ARC_NEC_R94:
					strlcat(machine->machine_name, " (NEC-R94; NEC RISCstation 2200)",
					    MACHINE_NAME_MAXBUF);
					break;
				case MACHINE_ARC_NEC_R96:
					strlcat(machine->machine_name, " (NEC-R96; NEC Express RISCserver)",
					    MACHINE_NAME_MAXBUF);
					break;
				}

				/*  TODO: interrupt controller!  */

				pci_data = device_add(machine,
				    "rd94 addr=0x80000000, irq=0");

				device_add(machine, "sn addr=0x80001000 irq=0");
				dev_mc146818_init(machine, mem, 0x80004000ULL, 0, MC146818_ARC_NEC, 1);
				i = dev_pckbc_init(machine, mem, 0x80005000ULL, PCKBC_8042, 0, 0, machine->use_x11, 0);

				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=3 addr=0x80006000 in_use=%i name2=tty0", machine->use_x11? 0 : 1);
				j = (size_t)device_add(machine, tmpstr);
				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=0 addr=0x80007000 in_use=%i name2=tty1", 0);
				device_add(machine, tmpstr);

				if (machine->use_x11)
					machine->main_console_handle = i;
				else
					machine->main_console_handle = j;

				/*  lpt at 0x80008000  */

				device_add(machine, "fdc addr=0x8000c000, irq=0");

				switch (machine->machine_subtype) {
				case MACHINE_ARC_NEC_RD94:
				case MACHINE_ARC_NEC_R94:
					/*  PCI devices:  (NOTE: bus must be 0, device must be 3, 4, or 5, for NetBSD to accept interrupts)  */
					bus_pci_add(machine, pci_data, mem, 0, 3, 0, "dec21030");	/*  tga graphics  */
					break;
				case MACHINE_ARC_NEC_R96:
					dev_fb_init(machine, mem, 0x100e00000ULL,
					    VFB_GENERIC, 640,480, 1024,480,
					    8, "necvdfrb");
					break;
				}
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

				strlcat(machine->machine_name,
				    " (NEC-R98; NEC RISCserver 4200)",
				    MACHINE_NAME_MAXBUF);

				/*
				 *  Windows NT access stuff at these addresses:
				 *
				 *  19980308, 18000210, 18c0a008,
				 *  19022018, 19026010, andso on.
				 */
				break;

			case MACHINE_ARC_JAZZ_PICA:
			case MACHINE_ARC_JAZZ_MAGNUM:
				/*
				 *  "PICA-61"
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
				 *
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

				switch (machine->machine_subtype) {
				case MACHINE_ARC_JAZZ_PICA:
					strlcat(machine->machine_name, " (Microsoft Jazz, Acer PICA-61)",
					    MACHINE_NAME_MAXBUF);
					stable = 1;
					break;
				case MACHINE_ARC_JAZZ_MAGNUM:
					strlcat(machine->machine_name, " (Microsoft Jazz, MIPS Magnum)",
					    MACHINE_NAME_MAXBUF);
					break;
				default:
					fatal("error in machine.c. jazz\n");
					exit(1);
				}

				machine->md_int.jazz_data = device_add(machine,
				    "jazz addr=0x80000000");
				machine->md_interrupt = jazz_interrupt;

				i = dev_pckbc_init(machine, mem, 0x80005000ULL,
				    PCKBC_JAZZ, 8 + 6, 8 + 7, machine->use_x11, 0);

				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=16 addr=0x80006000 in_use=%i name2=tty0", machine->use_x11? 0 : 1);
				j = (size_t)device_add(machine, tmpstr);
				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=17 addr=0x80007000 in_use=%i name2=tty1", 0);
				device_add(machine, tmpstr);

				if (machine->use_x11)
					machine->main_console_handle = i;
				else
					machine->main_console_handle = j;

				switch (machine->machine_subtype) {
				case MACHINE_ARC_JAZZ_PICA:
					if (machine->use_x11) {
						dev_vga_init(machine, mem,
						    0x400a0000ULL, 0x600003c0ULL,
						    machine->machine_name);
						arcbios_console_init(machine,
						    0x400b8000ULL, 0x600003c0ULL);
					}
					break;
				case MACHINE_ARC_JAZZ_MAGNUM:
					/*  PROM mirror?  */
					dev_ram_init(machine, 0xfff00000, 0x100000,
					    DEV_RAM_MIRROR | DEV_RAM_MIGHT_POINT_TO_DEVICES, 0x1fc00000);

					/*  VXL. TODO  */
					/*  control at 0x60100000?  */
					dev_fb_init(machine, mem, 0x60200000ULL,
					    VFB_GENERIC, 1024,768, 1024,768,
					    8, "VXL");
					break;
				}

				/*  irq 8 + 4  */
				device_add(machine, "sn addr=0x80001000 irq=12");

				dev_asc_init(machine, mem,
				    0x80002000ULL, 8 + 5, NULL, DEV_ASC_PICA,
				    dev_jazz_dma_controller,
				    machine->md_int.jazz_data);

				device_add(machine, "fdc addr=0x80003000, irq=0");

				dev_mc146818_init(machine, mem,
				    0x80004000ULL, 2, MC146818_ARC_JAZZ, 1);

#if 0
Not yet.
				/*  irq = 8+16 + 14  */
				device_add(machine, "wdc addr=0x900001f0, irq=38");
#endif

				break;

			case MACHINE_ARC_JAZZ_M700:
				/*
				 *  "Microsoft-Jazz", "Olivetti M700"
				 *
				 *  Different enough from Pica and Magnum to be
				 *  separate here.
				 *
				 *  See http://mail-index.netbsd.org/port-arc/2000/10/18/0001.html.
				 */

				strlcat(machine->machine_name, " (Microsoft Jazz, Olivetti M700)",
				    MACHINE_NAME_MAXBUF);

				machine->md_int.jazz_data = device_add(machine,
				    "jazz addr=0x80000000");
				machine->md_interrupt = jazz_interrupt;

				dev_mc146818_init(machine, mem,
				    0x80004000ULL, 2, MC146818_ARC_JAZZ, 1);

				i = 0;		/*  TODO: Yuck!  */
#if 0
				i = dev_pckbc_init(machine, mem, 0x80005000ULL,
				    PCKBC_JAZZ, 8 + 6, 8 + 7, machine->use_x11, 0);
#endif

				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=16 addr=0x80006000 in_use=%i name2=tty0", machine->use_x11? 0 : 1);
				j = (size_t)device_add(machine, tmpstr);
				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=17 addr=0x80007000 in_use=%i name2=tty1", 0);
				device_add(machine, tmpstr);

				if (machine->use_x11)
					machine->main_console_handle = i;
				else
					machine->main_console_handle = j;

				dev_m700_fb_init(machine, mem,
				    0x180080000ULL, 0x100000000ULL);

				break;

			case MACHINE_ARC_DESKTECH_TYNE:
				/*
				 *  "Deskstation Tyne" (?)
				 *
				 *  TODO
				 *  http://mail-index.netbsd.org/port-arc/2000/10/14/0000.html
				 */

				strlcat(machine->machine_name, " (Deskstation Tyne)",
				    MACHINE_NAME_MAXBUF);

				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=0 addr=0x9000003f8 in_use=%i name2=tty0", machine->use_x11? 0 : 1);
				i = (size_t)device_add(machine, tmpstr);
				device_add(machine, "ns16550 irq=0 addr=0x9000002f8 in_use=0 name2=tty1");
				device_add(machine, "ns16550 irq=0 addr=0x9000003e8 in_use=0 name2=tty2");
				device_add(machine, "ns16550 irq=0 addr=0x9000002e8 in_use=0 name2=tty3");

				dev_mc146818_init(machine, mem,
				    0x900000070ULL, 2, MC146818_PC_CMOS, 1);

#if 0
				/*  TODO: irq, etc  */
				device_add(machine, "wdc addr=0x9000001f0, irq=0");
				device_add(machine, "wdc addr=0x900000170, irq=0");
#endif
				/*  PC kbd  */
				j = dev_pckbc_init(machine, mem, 0x900000060ULL,
				    PCKBC_8042, 0, 0, machine->use_x11, 0);

				if (machine->use_x11)
					machine->main_console_handle = j;
				else
					machine->main_console_handle = i;

				if (machine->use_x11) {
					dev_vga_init(machine, mem, 0x1000a0000ULL,
					    0x9000003c0ULL, machine->machine_name);

					arcbios_console_init(machine,
					    0x1000b8000ULL, 0x9000003c0ULL);
				}
				break;

			default:
				fatal("Unimplemented ARC machine type %i\n",
				    machine->machine_subtype);
				exit(1);
			}
		}

		/*
		 *  This is important:  :-)
		 *
		 *  TODO:  There should not be any use of ARCBIOS before this
		 *  point.
		 */

		if (machine->prom_emulation) {
			arcbios_init(machine, arc_wordlen == sizeof(uint64_t), 
			    sgi_ram_offset);

			/*
			 *  TODO: How to build the component tree intermixed with
			 *  the rest of device initialization?
			 */

			/*
			 *  Boot string in ARC format:
			 *
			 *  TODO: How about floppies? multi()disk()fdisk()
			 *        Is tftp() good for netbooting?
			 */
			init_bootpath = malloc(500);
			if (init_bootpath == NULL) {
				fprintf(stderr, "out of mem, bootpath\n");
				exit(1);
			}
			init_bootpath[0] = '\0';

			if (bootdev_id < 0 || machine->force_netboot) {
				snprintf(init_bootpath, 400, "tftp()");
			} else {
				/*  TODO: Make this nicer.  */
				if (machine->machine_type == MACHINE_SGI) {
					if (machine->machine_subtype == 30)
						strlcat(init_bootpath, "xio(0)pci(15)",
						    MACHINE_NAME_MAXBUF);
					if (machine->machine_subtype == 32)
						strlcat(init_bootpath, "pci(0)",
						    MACHINE_NAME_MAXBUF);
				}

				if (diskimage_is_a_cdrom(machine, bootdev_id,
				    bootdev_type))
					snprintf(init_bootpath + strlen(init_bootpath),
					    400,"scsi(0)cdrom(%i)fdisk(0)", bootdev_id);
				else
					snprintf(init_bootpath + strlen(init_bootpath),
					    400,"scsi(0)disk(%i)rdisk(0)partition(1)",
					    bootdev_id);
			}

			if (machine->machine_type == MACHINE_ARC)
				strlcat(init_bootpath, "\\", MACHINE_NAME_MAXBUF);

			bootstr = malloc(BOOTSTR_BUFLEN);
			if (bootstr == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			strlcpy(bootstr, init_bootpath, BOOTSTR_BUFLEN);
			if (strlcat(bootstr, machine->boot_kernel_filename,
			    BOOTSTR_BUFLEN) >= BOOTSTR_BUFLEN) {
				fprintf(stderr, "boot string too long?\n");
				exit(1);
			}

			/*  Boot args., eg "-a"  */
			bootarg = machine->boot_string_argument;

			/*  argc, argv, envp in a0, a1, a2:  */
			cpu->cd.mips.gpr[MIPS_GPR_A0] = 0;	/*  note: argc is increased later  */

			/*  TODO:  not needed?  */
			cpu->cd.mips.gpr[MIPS_GPR_SP] = (int64_t)(int32_t)
			    (machine->physical_ram_in_mb * 1048576 + 0x80000000 - 0x2080);

			/*  Set up argc/argv:  */
			addr = ARC_ENV_STRINGS;
			addr2 = ARC_ARGV_START;
			cpu->cd.mips.gpr[MIPS_GPR_A1] = addr2;

			/*  bootstr:  */
			store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
			add_environment_string(cpu, bootstr, &addr);
			cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

			/*  bootarg:  */
			if (bootarg[0] != '\0') {
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, bootarg, &addr);
				cpu->cd.mips.gpr[MIPS_GPR_A0] ++;
			}

			cpu->cd.mips.gpr[MIPS_GPR_A2] = addr2;

			/*
			 *  Add environment variables.  For each variable, add it
			 *  as a string using add_environment_string(), and add a
			 *  pointer to it to the ARC_ENV_POINTERS array.
			 */
			if (machine->use_x11) {
				if (machine->machine_type == MACHINE_ARC) {
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
				if (machine->machine_type == MACHINE_ARC) {
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

			if (machine->machine_type == MACHINE_SGI) {
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "AutoLoad=No", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "diskless=0", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "volume=80", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "sgilogo=y", &addr);

				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "monitor=h", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "TimeZone=GMT", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "nogfxkbd=1", &addr);

				/*  TODO: 'xio(0)pci(15)scsi(0)disk(1)rdisk(0)partition(0)' on IP30 at least  */

				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "SystemPartition=pci(0)scsi(0)disk(2)rdisk(0)partition(8)", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "OSLoadPartition=pci(0)scsi(0)disk(2)rdisk(0)partition(0)", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "OSLoadFilename=/unix", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "OSLoader=sash", &addr);

				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "rbaud=9600", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "rebound=y", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "crt_option=1", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "netaddr=10.0.0.1", &addr);

				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "keybd=US", &addr);

				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "cpufreq=3", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "dbaud=9600", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, eaddr_string, &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "verbose=istrue", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "showconfig=istrue", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "diagmode=v", &addr);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "kernname=unix", &addr);
			} else {
				char *tmp;
				size_t mlen = strlen(bootarg) + strlen("OSLOADOPTIONS=") + 2;
				tmp = malloc(mlen);
				snprintf(tmp, mlen, "OSLOADOPTIONS=%s", bootarg);
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, tmp, &addr);

				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "OSLOADPARTITION=scsi(0)cdrom(6)fdisk(0);scsi(0)disk(0)rdisk(0)partition(1)", &addr);

				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, "SYSTEMPARTITION=scsi(0)cdrom(6)fdisk(0);scsi(0)disk(0)rdisk(0)partition(1)", &addr);
			}

			/*  End the environment strings with an empty zero-terminated
			    string, and the envp array with a NULL pointer.  */
			add_environment_string(cpu, "", &addr);	/*  the end  */
			store_pointer_and_advance(cpu, &addr2,
			    0, arc_wordlen==sizeof(uint64_t));

			/*  Return address:  (0x20 = ReturnFromMain())  */
			cpu->cd.mips.gpr[MIPS_GPR_RA] = ARC_FIRMWARE_ENTRIES + 0x20;
		}

		break;

	case MACHINE_MESHCUBE:
		machine->machine_name = "MeshCube";

		if (machine->physical_ram_in_mb != 64)
			fprintf(stderr, "WARNING! MeshCubes are supposed to have exactly 64 MB RAM. Continuing anyway.\n");
		if (machine->use_x11)
			fprintf(stderr, "WARNING! MeshCube with -X is meaningless. Continuing anyway.\n");

		/*  First of all, the MeshCube has an Au1500 in it:  */
		machine->md_interrupt = au1x00_interrupt;
		machine->md_int.au1x00_ic_data = dev_au1x00_init(machine, mem);

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

		device_add(machine, "random addr=0x1017fffc len=4");

		if (machine->prom_emulation) {
			/*
			 *  TODO:  A Linux kernel wants "memsize" from somewhere... I
			 *  haven't found any docs on how it is used though.
			 */
			cpu->cd.mips.gpr[MIPS_GPR_A0] = 1;
			cpu->cd.mips.gpr[MIPS_GPR_A1] = 0xa0001000ULL;
			store_32bit_word(cpu, cpu->cd.mips.gpr[MIPS_GPR_A1],
			    0xa0002000ULL);
			store_string(cpu, 0xa0002000ULL, "something=somethingelse");

			cpu->cd.mips.gpr[MIPS_GPR_A2] = 0xa0003000ULL;
			store_string(cpu, 0xa0002000ULL, "hello=world\n");
		}
		break;

	case MACHINE_NETGEAR:
		machine->machine_name = "NetGear WG602v1";

		if (machine->use_x11)
			fprintf(stderr, "WARNING! NetGear with -X is meaningless. Continuing anyway.\n");
		if (machine->physical_ram_in_mb != 16)
			fprintf(stderr, "WARNING! Real NetGear WG602v1 boxes have exactly 16 MB RAM. Continuing anyway.\n");

		/*
		 *  Lots of info about the IDT 79RC 32334
		 *  http://www.idt.com/products/pages/Integrated_Processors-79RC32334.html
		 */
		device_add(machine, "8250 addr=0x18000800 addr_mult=4 irq=0");
		break;

	case MACHINE_SONYNEWS:
		/*
		 *  There are several models, according to
		 *  http://www.netbsd.org/Ports/newsmips/:
		 *
		 *  "R3000 and hyper-bus based models"
		 *	NWS-3470D, -3410, -3460, -3710, -3720
		 *
		 *  "R4000/4400 and apbus based models"
		 *	NWS-5000
		 *
		 *  For example: (found using google)
		 *
		 *    cpu_model = news3700
		 *    SONY NET WORK STATION, Model NWS-3710, Machine ID #30145
		 *    cpu0: MIPS R3000 (0x220) Rev. 2.0 with MIPS R3010 Rev.2.0
		 *    64KB/4B direct-mapped I, 64KB/4B direct-mapped w-thr. D
		 *
		 *  See http://katsu.watanabe.name/doc/sonynews/model.html
		 *  for more details.
		 */
		cpu->byte_order = EMUL_BIG_ENDIAN;
		machine->machine_name = "Sony NeWS (NET WORK STATION)";

		if (machine->prom_emulation) {
			/*  This is just a test.  TODO  */
			int i;
			for (i=0; i<32; i++)
				cpu->cd.mips.gpr[i] =
				    0x01230000 + (i << 8) + 0x55;
		}

		machine->main_console_handle = (size_t)device_add(machine,
		    "z8530 addr=0x1e950000 irq=0 addr_mult=4");
		break;

	case MACHINE_EVBMIPS:
		/*  http://www.netbsd.org/Ports/evbmips/  */
		cpu->byte_order = EMUL_LITTLE_ENDIAN;

		switch (machine->machine_subtype) {
		case MACHINE_EVBMIPS_MALTA:
		case MACHINE_EVBMIPS_MALTA_BE:
			machine->machine_name = "MALTA (evbmips, little endian)";
			cpu->byte_order = EMUL_LITTLE_ENDIAN;
			stable = 1;

			if (machine->machine_subtype == MACHINE_EVBMIPS_MALTA_BE) {
				machine->machine_name = "MALTA (evbmips, big endian)";
				cpu->byte_order = EMUL_BIG_ENDIAN;
			}

			machine->md_interrupt = isa8_interrupt;
			machine->isa_pic_data.native_irq = 2;

			bus_isa_init(machine, 0, 0x18000000, 0x10000000, 8, 24);

			snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=4 addr=0x%x name2=tty2", MALTA_CBUSUART);
			device_add(machine, tmpstr);

			pci_data = dev_gt_init(machine, mem, 0x1be00000, 8+9, 8+9, 120);

			if (machine->use_x11) {
				if (strlen(machine->boot_string_argument) < 3)
					fatal("WARNING: remember to use  -o 'console=tty0'  "
					    "if you are emulating Linux. (Not needed for NetBSD.)\n");
				bus_pci_add(machine, pci_data, mem, 0xc0, 8, 0, "s3_virge");
			}

			bus_pci_add(machine, pci_data, mem, 0,  9, 0, "i82371ab_isa");
			bus_pci_add(machine, pci_data, mem, 0,  9, 1, "i82371ab_ide");

			device_add(machine, "malta_lcd addr=0x1f000400");
			break;
		case MACHINE_EVBMIPS_PB1000:
			machine->machine_name = "PB1000 (evbmips)";
			cpu->byte_order = EMUL_BIG_ENDIAN;

			machine->md_interrupt = au1x00_interrupt;
			machine->md_int.au1x00_ic_data = dev_au1x00_init(machine, mem);
			/*  TODO  */
			break;
		default:
			fatal("Unimplemented EVBMIPS model.\n");
			exit(1);
		}

		if (machine->prom_emulation) {
			/*  NetBSD/evbmips wants these: (at least for Malta)  */

			/*  a0 = argc  */
			cpu->cd.mips.gpr[MIPS_GPR_A0] = 2;

			/*  a1 = argv  */
			cpu->cd.mips.gpr[MIPS_GPR_A1] = (int32_t)0x9fc01000;
			store_32bit_word(cpu, (int32_t)0x9fc01000, 0x9fc01040);
			store_32bit_word(cpu, (int32_t)0x9fc01004, 0x9fc01200);
			store_32bit_word(cpu, (int32_t)0x9fc01008, 0);

			bootstr = strdup(machine->boot_kernel_filename);
			bootarg = strdup(machine->boot_string_argument);
			store_string(cpu, (int32_t)0x9fc01040, bootstr);
			store_string(cpu, (int32_t)0x9fc01200, bootarg);

			/*  a2 = (yamon_env_var *)envp  */
			cpu->cd.mips.gpr[MIPS_GPR_A2] = (int32_t)0x9fc01800;
			{
				uint64_t env = cpu->cd.mips.gpr[MIPS_GPR_A2];
				uint64_t tmpptr = 0xffffffff9fc01c00ULL;
				char tmps[50];

				snprintf(tmps, sizeof(tmps), "0x%08x",
				    machine->physical_ram_in_mb * 1048576);
				add_environment_string_dual(cpu,
				    &env, &tmpptr, "memsize", tmps);

				add_environment_string_dual(cpu,
				    &env, &tmpptr, "yamonrev", "02.06");

				/*  End of env:  */
				tmpptr = 0;
				add_environment_string_dual(cpu,
				    &env, &tmpptr, NULL, NULL);
			}

			/*  a3 = memsize  */
			cpu->cd.mips.gpr[MIPS_GPR_A3] =
			    machine->physical_ram_in_mb * 1048576;
			/*  Hm. Linux ignores a3.  */

			/*
			 *  TODO:
			 *	Core ID numbers.
			 *	How much of this is not valid for PBxxxx?
			 *
			 *  See maltareg.h for more info.
			 */
			store_32bit_word(cpu, (int32_t)(0x80000000 + MALTA_REVISION), (1 << 10) + 0x26);

			/*  Call vectors at 0x9fc005xx:  */
			for (i=0; i<0x100; i+=4)
				store_32bit_word(cpu, (int64_t)(int32_t)0x9fc00500 + i,
				    (int64_t)(int32_t)0x9fc00800 + i);
		}
		break;

	case MACHINE_PSP:
		/*
		 *  The Playstation Portable seems to be a strange beast.
		 *
		 *  http://yun.cup.com/psppg004.html (in Japanese) seems to
		 *  suggest that virtual addresses are not displaced by
		 *  0x80000000 as on normal CPUs, but by 0x40000000?
		 */
		machine->machine_name = "Playstation Portable";
		cpu->byte_order = EMUL_LITTLE_ENDIAN;

		if (!machine->use_x11 && !quiet_mode)
			fprintf(stderr, "-------------------------------------"
			    "------------------------------------------\n"
			    "\n  WARNING! You are emulating a PSP without -X. "
			    "You will miss graphical output!\n\n"
			    "-------------------------------------"
			    "------------------------------------------\n");

		/*  480 x 272 pixels framebuffer (512 bytes per line)  */
		fb = dev_fb_init(machine, mem, 0x04000000, VFB_HPC,
		    480,272, 512,1088, -15, "Playstation Portable");

		/*
		 *  TODO/NOTE: This is ugly, but necessary since GXemul doesn't
		 *  emulate any MIPS CPU without MMU right now.
		 */
		mips_coproc_tlb_set_entry(cpu, 0, 1048576*16,
		    0x44000000 /*vaddr*/, 0x4000000, 0x4000000 + 1048576*16,
		    1,1,1,1,1, 0, 2, 2);
		mips_coproc_tlb_set_entry(cpu, 1, 1048576*16,
		    0x8000000 /*vaddr*/, 0x0, 0x0 + 1048576*16,
		    1,1,1,1,1, 0, 2, 2);
		mips_coproc_tlb_set_entry(cpu, 2, 1048576*16,
		    0x9000000 /*vaddr*/, 0x01000000, 0x01000000 + 1048576*16,
		    1,1,1,1,1, 0, 2, 2);
		mips_coproc_tlb_set_entry(cpu, 3, 1048576*16,
		    0x0 /*vaddr*/, 0, 0 + 1048576*16, 1,1,1,1,1, 0, 2, 2);

		cpu->cd.mips.gpr[MIPS_GPR_SP] = 0xfff0;

		break;

	case MACHINE_ALGOR:
		switch (machine->machine_subtype) {
		case MACHINE_ALGOR_P4032:
			machine->machine_name = "\"Algor\" P4032";
			break;
		case MACHINE_ALGOR_P5064:
			machine->machine_name = "\"Algor\" P5064";
			break;
		default:fatal("Unimplemented Algor machine.\n");
			exit(1);
		}

		machine->md_interrupt = isa8_interrupt;
		machine->isa_pic_data.native_irq = 6;

		/*  TODO: correct isa irq? 6 is just a bogus guess  */

		bus_isa_init(machine, 0, 0x1d000000, 0xc0000000, 8, 24);

		if (machine->prom_emulation) {
			/*  NetBSD/algor wants these:  */

			/*  a0 = argc  */
			cpu->cd.mips.gpr[MIPS_GPR_A0] = 2;

			/*  a1 = argv  */
			cpu->cd.mips.gpr[MIPS_GPR_A1] = (int32_t)0x9fc01000;
			store_32bit_word(cpu, (int32_t)0x9fc01000, 0x9fc01040);
			store_32bit_word(cpu, (int32_t)0x9fc01004, 0x9fc01200);
			store_32bit_word(cpu, (int32_t)0x9fc01008, 0);

			bootstr = strdup(machine->boot_kernel_filename);
			bootarg = strdup(machine->boot_string_argument);
			store_string(cpu, (int32_t)0x9fc01040, bootstr);
			store_string(cpu, (int32_t)0x9fc01200, bootarg);

			/*  a2 = (yamon_env_var *)envp  */
			cpu->cd.mips.gpr[MIPS_GPR_A2] = (int32_t)0x9fc01800;
			{
				char tmps[50];

				store_32bit_word(cpu, (int32_t)0x9fc01800, 0x9fc01900);
				store_32bit_word(cpu, (int32_t)0x9fc01804, 0x9fc01a00);
				store_32bit_word(cpu, (int32_t)0x9fc01808, 0);

				snprintf(tmps, sizeof(tmps), "memsize=0x%08x",
				    machine->physical_ram_in_mb * 1048576);
				store_string(cpu, (int)0x9fc01900, tmps);
				store_string(cpu, (int)0x9fc01a00, "ethaddr=10:20:30:30:20:10");
			}
		}
		break;
#endif	/*  ENABLE_MIPS  */

#ifdef ENABLE_PPC
	case MACHINE_BAREPPC:
		/*
		 *  A "bare" PPC machine.
		 *
		 *  NOTE: NO devices at all.
		 */
		machine->machine_name = "\"Bare\" PPC machine";
		stable = 1;
		break;

	case MACHINE_TESTPPC:
		/*
		 *  A PPC test machine, similar to the test machine for MIPS.
		 */
		machine->machine_name = "PPC test machine";
		stable = 1;

		/*  TODO: interrupt for PPC?  */
		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testppc generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;

	case MACHINE_WALNUT:
		/*
		 *  NetBSD/evbppc (http://www.netbsd.org/Ports/evbppc/)
		 */
		machine->machine_name = "Walnut evaluation board";

		/*  "OpenBIOS" entrypoint (?):  */
		dev_ram_init(machine, 0xfffe0b50, 8, DEV_RAM_RAM, 0);
		store_32bit_word(cpu, 0xfffe0b50, 0xfffe0b54);
		store_32bit_word(cpu, 0xfffe0b54, 0x4e800020);  /*  blr  */

		break;

	case MACHINE_PMPPC:
		/*
		 *  NetBSD/pmppc (http://www.netbsd.org/Ports/pmppc/)
		 */
		machine->machine_name = "Artesyn's PM/PPC board";
		if (machine->emulated_hz == 0)
			machine->emulated_hz = 10000000;

		dev_pmppc_init(mem);

		machine->md_int.cpc700_data = dev_cpc700_init(machine, mem);
		machine->md_interrupt = cpc700_interrupt;

		/*  RTC at "ext int 5" = "int 25" in IBM jargon, int
		    31-25 = 6 for the rest of us.  */
		dev_mc146818_init(machine, mem, 0x7ff00000, 31-25, MC146818_PMPPC, 1);

		bus_pci_add(machine, machine->md_int.cpc700_data->pci_data,
		    mem, 0, 8, 0, "dec21143");

		break;

	case MACHINE_SANDPOINT:
		/*
		 *  NetBSD/sandpoint (http://www.netbsd.org/Ports/sandpoint/)
		 */
		machine->machine_name = "Motorola Sandpoint";

		/*  r4 should point to first free byte after the loaded kernel:  */
		cpu->cd.ppc.gpr[4] = 6 * 1048576;

		break;

	case MACHINE_BEBOX:
		/*
		 *  NetBSD/bebox (http://www.netbsd.org/Ports/bebox/)
		 */
		machine->machine_name = "BeBox";

		machine->md_int.bebox_data = device_add(machine, "bebox");
		machine->isa_pic_data.native_irq = 5;
		machine->md_interrupt = isa32_interrupt;

		pci_data = dev_eagle_init(machine, mem,
		    32 /*  isa irq base */, 0 /*  pci irq: TODO */);

		bus_isa_init(machine, BUS_ISA_IDE0 | BUS_ISA_VGA,
		    0x80000000, 0xc0000000, 32, 48);

		if (machine->prom_emulation) {
			/*  According to the docs, and also used by NetBSD:  */
			store_32bit_word(cpu, 0x3010, machine->physical_ram_in_mb * 1048576);

			/*  Used by Linux:  */
			store_32bit_word(cpu, 0x32f8, machine->physical_ram_in_mb * 1048576);

			/*  TODO: List of stuff, see http://www.beatjapan.org/
			    mirror/www.be.com/aboutbe/benewsletter/
			    Issue27.html#Cookbook  for the details.  */
			store_32bit_word(cpu, 0x301c, 0);

			/*  NetBSD/bebox: r3 = startkernel, r4 = endkernel,
			    r5 = args, r6 = ptr to bootinfo?  */
			cpu->cd.ppc.gpr[3] = 0x3100;
			cpu->cd.ppc.gpr[4] = 0x400000;
			cpu->cd.ppc.gpr[5] = 0x2000;
			store_string(cpu, cpu->cd.ppc.gpr[5], "-a");
			cpu->cd.ppc.gpr[6] = machine->physical_ram_in_mb * 1048576 - 0x100;

			/*  See NetBSD's bebox/include/bootinfo.h for details  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 0, 12);  /*  next  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 4, 0);  /*  mem  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 8,
			    machine->physical_ram_in_mb * 1048576);

			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 12, 20);  /* next */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 16, 1); /* console */
			store_buf(cpu, cpu->cd.ppc.gpr[6] + 20,
			    machine->use_x11? "vga" : "com", 4);
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 24, 0x3f8);/* addr */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 28, 9600);/* speed */

			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 32, 0);  /*  next  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 36, 2);  /*  clock */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6] + 40, 100);
		}
		break;

	case MACHINE_PREP:
		/*
		 *  NetBSD/prep (http://www.netbsd.org/Ports/prep/)
		 */
		machine->machine_name = "PowerPC Reference Platform";
		stable = 1;
		if (machine->emulated_hz == 0)
			machine->emulated_hz = 20000000;

		machine->md_int.bebox_data = device_add(machine, "prep");
		machine->isa_pic_data.native_irq = 1;	/*  Semi-bogus  */
		machine->md_interrupt = isa32_interrupt;

		pci_data = dev_eagle_init(machine, mem,
		    32 /*  isa irq base */, 0 /*  pci irq: TODO */);

		bus_isa_init(machine, BUS_ISA_IDE0 | BUS_ISA_IDE1,
		    0x80000000, 0xc0000000, 32, 48);

		bus_pci_add(machine, pci_data, mem, 0, 13, 0, "dec21143");

		if (machine->use_x11)
			bus_pci_add(machine, pci_data, mem, 0, 14, 0, "s3_virge");

		if (machine->prom_emulation) {
			/*  Linux on PReP has 0xdeadc0de at address 0? (See
			    http://joshua.raleigh.nc.us/docs/linux-2.4.10_html/113568.html)  */
			store_32bit_word(cpu, 0, 0xdeadc0de);

			/*  r4 should point to first free byte after the loaded kernel:  */
			cpu->cd.ppc.gpr[4] = 6 * 1048576;

			/*
			 *  r6 should point to bootinfo.
			 *  (See NetBSD's prep/include/bootinfo.h for details.)
			 */
			cpu->cd.ppc.gpr[6] = machine->physical_ram_in_mb * 1048576 - 0x8000;

			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+ 0, 12);  /*  next  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+ 4, 2);  /*  type: clock  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+ 8, machine->emulated_hz);

			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+12, 20);  /*  next  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+16, 1);  /*  type: console  */
			store_buf(cpu, cpu->cd.ppc.gpr[6] + 20,
			    machine->use_x11? "vga" : "com", 4);
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+24, 0x3f8);  /*  addr  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+28, 9600);  /*  speed  */

			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+32, 0);  /*  next  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+36, 0);  /*  type: residual  */
			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+40,	/*  addr of data  */
			    cpu->cd.ppc.gpr[6] + 0x100);

			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+0x100, 0x200);  /*  TODO: residual  */
			/*  store_string(cpu, cpu->cd.ppc.gpr[6]+0x100+0x8, "IBM PPS Model 7248 (E)");  */
			store_string(cpu, cpu->cd.ppc.gpr[6]+0x100+0x8, "IBM PPS Model 6050/6070 (E)");

			store_32bit_word(cpu, cpu->cd.ppc.gpr[6]+0x100+0x1f8, machine->physical_ram_in_mb * 1048576);  /*  memsize  */
		}
		break;

	case MACHINE_MACPPC:
		/*
		 *  NetBSD/macppc (http://www.netbsd.org/Ports/macppc/)
		 *  OpenBSD/macppc (http://www.openbsd.org/macppc.html)
		 */
		machine->machine_name = "Macintosh (PPC)";
		if (machine->emulated_hz == 0)
			machine->emulated_hz = 40000000;

		machine->md_int.gc_data = dev_gc_init(machine, mem, 0xf3000000, 64);
		machine->md_interrupt = gc_interrupt;

		pci_data = dev_uninorth_init(machine, mem, 0xe2000000,
		    64 /*  isa irq base */, 0 /*  pci irq: TODO */);

		bus_pci_add(machine, pci_data, mem, 0, 12, 0, "dec21143");
		bus_pci_add(machine, pci_data, mem, 0, 15, 0, "gc_obio");

		if (machine->use_x11)
			bus_pci_add(machine, pci_data, mem, 0, 16, 0, "ati_radeon_9200_2");

		machine->main_console_handle = (size_t)device_add(machine,
		    "z8530 addr=0xf3013000 irq=23 dma_irq=8 addr_mult=0x10");

		fb = dev_fb_init(machine, mem, 0xf1000000,
		    VFB_GENERIC | VFB_REVERSE_START, 1024,768, 1024,768, 8, "ofb");

		device_add(machine, "hammerhead addr=0xf2800000");

		device_add(machine, "adb addr=0xf3016000 irq=1");

		if (machine->prom_emulation) {
			uint64_t b = 8 * 1048576, a = b - 0x800;
			int i;

			of_emul_init(machine, fb, 0xf1000000, 1024, 768);
			of_emul_init_uninorth(machine);
			of_emul_init_zs(machine);
			/*  of_emul_init_adb(machine);  */

			/*
			 *  r3 = pointer to boot_args (for the Mach kernel).
			 *  See http://darwinsource.opendarwin.org/10.3/
			 *  BootX-59/bootx.tproj/include.subproj/boot_args.h
			 *  for more info.
			 */
			cpu->cd.ppc.gpr[3] = a;
			store_16bit_word(cpu, a + 0x0000, 1);	/*  revision  */
			store_16bit_word(cpu, a + 0x0002, 2);	/*  version  */
			store_buf(cpu, a + 0x0004, machine->boot_string_argument, 256);
			/*  26 dram banks; "long base; long size"  */
			store_32bit_word(cpu, a + 0x0104, 0);	/*  base  */
			store_32bit_word(cpu, a + 0x0108, machine->physical_ram_in_mb
			    * 256);		/*  size (in pages)  */
			for (i=8; i<26*8; i+= 4)
				store_32bit_word(cpu, a + 0x0104 + i, 0);
			a += (0x104 + 26 * 8);
			/*  Video info:  */
			store_32bit_word(cpu, a+0, 0xf1000000);	/*  video base  */
			store_32bit_word(cpu, a+4, 0);		/*  display code (?)  */
			store_32bit_word(cpu, a+8, 1024);	/*  bytes per pixel row  */
			store_32bit_word(cpu, a+12, 1024);	/*  width  */
			store_32bit_word(cpu, a+16, 768);	/*  height  */
			store_32bit_word(cpu, a+20, 8);		/*  pixel depth  */
			a += 24;
			store_32bit_word(cpu, a+0, 127);	/*  gestalt number (TODO)  */
			store_32bit_word(cpu, a+4, 0);		/*  device tree pointer (TODO)  */
			store_32bit_word(cpu, a+8, 0);		/*  device tree length  */
			store_32bit_word(cpu, a+12, b);		/*  last address of kernel data area  */

			/*  r4 = "MOSX" (0x4D4F5358)  */
			cpu->cd.ppc.gpr[4] = 0x4D4F5358;

			/*
			 *  r5 = OpenFirmware entry point.  NOTE: See
			 *  cpu_ppc.c for the rest of this semi-ugly hack.
			 */
			dev_ram_init(machine, cpu->cd.ppc.of_emul_addr,
			    0x1000, DEV_RAM_RAM, 0x0);
			store_32bit_word(cpu, cpu->cd.ppc.of_emul_addr,
			    0x44ee0002);
			cpu->cd.ppc.gpr[5] = cpu->cd.ppc.of_emul_addr;

#if 0
			/*  r6 = args  */
			cpu->cd.ppc.gpr[1] -= 516;
			cpu->cd.ppc.gpr[6] = cpu->cd.ppc.gpr[1] + 4;
			store_string(cpu, cpu->cd.ppc.gpr[6],
			    machine->boot_string_argument);
			/*  should be something like '/controller/disk/bsd'  */

			/*  r7 = length? TODO  */
			cpu->cd.ppc.gpr[7] = 5;
#endif
		}
		break;

	case MACHINE_DB64360:
		/*  For playing with PMON2000 for PPC:  */
		machine->machine_name = "DB64360";

		machine->main_console_handle = (size_t)device_add(machine,
		    "ns16550 irq=0 addr=0x1d000020 addr_mult=4");

		if (machine->prom_emulation) {
			int i;
			for (i=0; i<32; i++)
				cpu->cd.ppc.gpr[i] =
				    0x12340000 + (i << 8) + 0x55;
		}

		break;
#endif	/*  ENABLE_PPC  */

#ifdef ENABLE_SH
	case MACHINE_BARESH:
		/*  A bare SH machine, with no devices.  */
		machine->machine_name = "\"Bare\" SH machine";
		stable = 1;
		break;

	case MACHINE_TESTSH:
		machine->machine_name = "SH test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testsh generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;

	case MACHINE_HPCSH:
		/*  Handheld SH-based machines:  */
		machine->machine_name = "HPCsh";

		/*  TODO  */

		break;
#endif	/*  ENABLE_SH  */

#ifdef ENABLE_HPPA
	case MACHINE_BAREHPPA:
		/*  A bare HPPA machine, with no devices.  */
		machine->machine_name = "\"Bare\" HPPA machine";
		stable = 1;
		break;

	case MACHINE_TESTHPPA:
		machine->machine_name = "HPPA test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testhppa generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;
#endif	/*  ENABLE_HPPA  */

#ifdef ENABLE_I960
	case MACHINE_BAREI960:
		/*  A bare I960 machine, with no devices.  */
		machine->machine_name = "\"Bare\" i960 machine";
		stable = 1;
		break;

	case MACHINE_TESTI960:
		machine->machine_name = "i960 test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testi960 generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;
#endif	/*  ENABLE_I960  */

#ifdef ENABLE_SPARC
	case MACHINE_BARESPARC:
		/*  A bare SPARC machine, with no devices.  */
		machine->machine_name = "\"Bare\" SPARC machine";
		stable = 1;
		break;

	case MACHINE_TESTSPARC:
		machine->machine_name = "SPARC test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testsparc generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;

	case MACHINE_ULTRA1:
		/*
		 *  NetBSD/sparc64 (http://www.netbsd.org/Ports/sparc64/)
		 *  OpenBSD/sparc64 (http://www.openbsd.org/sparc64.html)
		 */
		machine->machine_name = "Sun Ultra1";
		break;
#endif	/*  ENABLE_SPARC  */

#ifdef ENABLE_ALPHA
	case MACHINE_BAREALPHA:
		machine->machine_name = "\"Bare\" Alpha machine";
		stable = 1;
		break;

	case MACHINE_TESTALPHA:
		machine->machine_name = "Alpha test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testalpha generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;

	case MACHINE_ALPHA:
		if (machine->prom_emulation) {
			struct rpb rpb;
			struct crb crb;
			struct ctb ctb;

			/*  TODO:  Most of these... They are used by NetBSD/alpha:  */
			/*  a0 = First free Page Frame Number  */
			/*  a1 = PFN of current Level 1 page table  */
			/*  a2 = Bootinfo magic  */
			/*  a3 = Bootinfo pointer  */
			/*  a4 = Bootinfo version  */
			cpu->cd.alpha.r[ALPHA_A0] = 16*1024*1024 / 8192;
			cpu->cd.alpha.r[ALPHA_A1] = 0;
			cpu->cd.alpha.r[ALPHA_A2] = 0;
			cpu->cd.alpha.r[ALPHA_A3] = 0;
			cpu->cd.alpha.r[ALPHA_A4] = 0;

			/*  HWRPB: Hardware Restart Parameter Block  */
			memset(&rpb, 0, sizeof(struct rpb));
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(rpb.rpb_phys), HWRPB_ADDR);
			strlcpy((char *)&(rpb.rpb_magic), "HWRPB", 8);
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(rpb.rpb_size), sizeof(struct rpb));
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(rpb.rpb_page_size), 8192);
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(rpb.rpb_type), machine->machine_subtype);
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(rpb.rpb_cc_freq), 100000000);
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(rpb.rpb_ctb_off), CTB_ADDR - HWRPB_ADDR);
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(rpb.rpb_crb_off), CRB_ADDR - HWRPB_ADDR);

			/*  CTB: Console Terminal Block  */
			memset(&ctb, 0, sizeof(struct ctb));
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(ctb.ctb_term_type), machine->use_x11?
			    CTB_GRAPHICS : CTB_PRINTERPORT);

			/*  CRB: Console Routine Block  */
			memset(&crb, 0, sizeof(struct crb));
			store_64bit_word_in_host(cpu, (unsigned char *)
			    &(crb.crb_v_dispatch), CRB_ADDR - 0x100);
			store_64bit_word(cpu, CRB_ADDR - 0x100 + 8, 0x10000);

			/*
			 *  Place a special "hack" palcode call at 0x10000:
			 *  (Hopefully nothing else will be there.)
			 */
			store_32bit_word(cpu, 0x10000, 0x3fffffe);

			store_buf(cpu, HWRPB_ADDR, (char *)&rpb, sizeof(struct rpb));
			store_buf(cpu, CTB_ADDR, (char *)&ctb, sizeof(struct ctb));
			store_buf(cpu, CRB_ADDR, (char *)&crb, sizeof(struct crb));
		}

		switch (machine->machine_subtype) {
		case ST_DEC_3000_300:
			machine->machine_name = "DEC 3000/300";
			machine->main_console_handle = (size_t)device_add(machine,
			    "z8530 addr=0x1b0200000 irq=0 addr_mult=4");
			break;
		case ST_EB164:
			machine->machine_name = "EB164";
			break;
		default:fatal("Unimplemented Alpha machine type %i\n",
			    machine->machine_subtype);
			exit(1);
		}

		break;
#endif	/*  ENABLE_ALPHA  */

#ifdef ENABLE_ARM
	case MACHINE_BAREARM:
		machine->machine_name = "\"Bare\" ARM machine";
		stable = 1;
		break;

	case MACHINE_TESTARM:
		machine->machine_name = "ARM test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testarm generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		/*  Place a tiny stub at end of memory, and set the link
		    register to point to it. This stub halts the machine.  */
		cpu->cd.arm.r[ARM_SP] =
		    machine->physical_ram_in_mb * 1048576 - 4096;
		cpu->cd.arm.r[ARM_LR] = cpu->cd.arm.r[ARM_SP] + 32;
		store_32bit_word(cpu, cpu->cd.arm.r[ARM_LR] + 0, 0xe3a00201);
		store_32bit_word(cpu, cpu->cd.arm.r[ARM_LR] + 4, 0xe5c00010);
		store_32bit_word(cpu, cpu->cd.arm.r[ARM_LR] + 8,
		    0xeafffffe);
		break;

	case MACHINE_CATS:
		machine->machine_name = "CATS evaluation board";
		stable = 1;

		if (machine->emulated_hz == 0)
			machine->emulated_hz = 50000000;

		if (machine->physical_ram_in_mb > 256)
			fprintf(stderr, "WARNING! Real CATS machines cannot"
			    " have more than 256 MB RAM. Continuing anyway.\n");

		machine->md_int.footbridge_data =
		    device_add(machine, "footbridge addr=0x42000000");
		machine->md_interrupt = isa32_interrupt;
		machine->isa_pic_data.native_irq = 10;

		/*
		 *  DC21285_ROM_BASE (0x41000000): "reboot" code. Works
		 *  with NetBSD.
		 */
		dev_ram_init(machine, 0x41000000, 12, DEV_RAM_RAM, 0);
		store_32bit_word(cpu, 0x41000008ULL, 0xef8c64ebUL);

		/*  OpenBSD reboot needs 0xf??????? to be mapped to phys.:  */
		dev_ram_init(machine, 0xf0000000, 0x1000000,
		    DEV_RAM_MIRROR, 0x0);

		/*  NetBSD and OpenBSD clean their caches here:  */
		dev_ram_init(machine, 0x50000000, 0x4000, DEV_RAM_RAM, 0);

		/*  Interrupt ack space?  */
		dev_ram_init(machine, 0x80000000, 0x1000, DEV_RAM_RAM, 0);

		bus_isa_init(machine, BUS_ISA_PCKBC_FORCE_USE | BUS_ISA_PCKBC_NONPCSTYLE,
		    0x7c000000, 0x80000000, 32, 48);

		bus_pci_add(machine, machine->md_int.footbridge_data->pcibus,
		    mem, 0xc0, 8, 0, "s3_virge");

		if (machine->prom_emulation) {
			struct ebsaboot ebsaboot;
			char bs[300];
			int boot_id = bootdev_id >= 0? bootdev_id : 0;

			cpu->cd.arm.r[0] = /* machine->physical_ram_in_mb */
			    7 * 1048576 - 0x1000;

			memset(&ebsaboot, 0, sizeof(struct ebsaboot));
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_magic), BT_MAGIC_NUMBER_CATS);
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_vargp), 0);
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_pargp), 0);
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_args), cpu->cd.arm.r[0]
			    + sizeof(struct ebsaboot));
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_l1), 7 * 1048576 - 32768);
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_memstart), 0);
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_memend),
			    machine->physical_ram_in_mb * 1048576);
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_memavail), 7 * 1048576);
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_fclk), 50 * 1000000);
			store_32bit_word_in_host(cpu, (unsigned char *)
			    &(ebsaboot.bt_pciclk), 66 * 1000000);
			/*  TODO: bt_vers  */
			/*  TODO: bt_features  */

			store_buf(cpu, cpu->cd.arm.r[0],
			    (char *)&ebsaboot, sizeof(struct ebsaboot));

			snprintf(bs, sizeof(bs), "(hd%i)%s root=/dev/wd%i%s%s",
			    boot_id, machine->boot_kernel_filename, boot_id,
			    (machine->boot_string_argument[0])? " " : "",
			    machine->boot_string_argument);

			store_string(cpu, cpu->cd.arm.r[0] +
			    sizeof(struct ebsaboot), bs);

			arm_setup_initial_translation_table(cpu,
			    7 * 1048576 - 32768);
		}
		break;

	case MACHINE_HPCARM:
		cpu->byte_order = EMUL_LITTLE_ENDIAN;
		memset(&hpc_bootinfo, 0, sizeof(hpc_bootinfo));
		switch (machine->machine_subtype) {
		case MACHINE_HPCARM_IPAQ:
			/*  SA-1110 206MHz  */
			machine->machine_name = "Compaq iPAQ H3600";
			hpc_fb_addr = 0x48200000;	/*  TODO  */
			hpc_fb_xsize = 240;
			hpc_fb_ysize = 320;
			hpc_fb_xsize_mem = 256;
			hpc_fb_ysize_mem = 320;
			hpc_fb_bits = 15;
			hpc_fb_encoding = BIFB_D16_0000;
			hpc_platid_cpu_arch = 3;	/*  ARM  */
			hpc_platid_cpu_series = 1;	/*  StrongARM  */
			hpc_platid_cpu_model = 2;	/*  SA-1110  */
			hpc_platid_cpu_submodel = 0;
			hpc_platid_vendor = 7;		/*  Compaq  */
			hpc_platid_series = 4;		/*  IPAQ  */
			hpc_platid_model = 2;		/*  H36xx  */
			hpc_platid_submodel = 1;	/*  H3600  */
			break;
		case MACHINE_HPCARM_JORNADA720:
			/*  SA-1110 206MHz  */
			machine->machine_name = "Jornada 720";
			hpc_fb_addr = 0x48200000;
			hpc_fb_xsize = 640;
			hpc_fb_ysize = 240;
			hpc_fb_xsize_mem = 640;
			hpc_fb_ysize_mem = 240;
			hpc_fb_bits = 16;
			hpc_fb_encoding = BIFB_D16_0000;
			hpc_platid_cpu_arch = 3;	/*  ARM  */
			hpc_platid_cpu_series = 1;	/*  StrongARM  */
			hpc_platid_cpu_model = 2;	/*  SA-1110  */
			hpc_platid_cpu_submodel = 0;
			hpc_platid_vendor = 11;		/*  HP  */
			hpc_platid_series = 2;		/*  Jornada  */
			hpc_platid_model = 2;		/*  7xx  */
			hpc_platid_submodel = 1;	/*  720  */
			break;
		default:
			printf("Unimplemented hpcarm machine number.\n");
			exit(1);
		}

		store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.platid_cpu,
		      (hpc_platid_cpu_arch << 26) + (hpc_platid_cpu_series << 20)
		    + (hpc_platid_cpu_model << 14) + (hpc_platid_cpu_submodel <<  8)
		    + hpc_platid_flags);
		store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.platid_machine,
		      (hpc_platid_vendor << 22) + (hpc_platid_series << 16)
		    + (hpc_platid_model <<  8) + hpc_platid_submodel);

		if (machine->prom_emulation) {
			/*  NetBSD/hpcarm and possibly others expects the following:  */

			cpu->cd.arm.r[0] = 1;	/*  argc  */
			cpu->cd.arm.r[1] = machine->physical_ram_in_mb * 1048576 - 512;	/*  argv  */
			cpu->cd.arm.r[2] = machine->physical_ram_in_mb * 1048576 - 256;	/*  ptr to hpc_bootinfo  */

			bootstr = machine->boot_kernel_filename;
			store_32bit_word(cpu, machine->physical_ram_in_mb * 1048576 - 512,
			    machine->physical_ram_in_mb * 1048576 - 512 + 16);
			store_32bit_word(cpu, machine->physical_ram_in_mb * 1048576 - 512 + 4, 0);
			store_string(cpu, machine->physical_ram_in_mb * 1048576 - 512 + 16, bootstr);

			if (machine->boot_string_argument[0]) {
				cpu->cd.arm.r[0] ++;	/*  argc  */

				store_32bit_word(cpu, machine->physical_ram_in_mb * 1048576 - 512 + 4, machine->physical_ram_in_mb * 1048576 - 512 + 64);
				store_32bit_word(cpu, machine->physical_ram_in_mb * 1048576 - 512 + 8, 0);

				store_string(cpu, machine->physical_ram_in_mb * 1048576 - 512 + 64,
				    machine->boot_string_argument);

				bootarg = machine->boot_string_argument;
			}

			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.length, sizeof(hpc_bootinfo));
			store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.magic, HPC_BOOTINFO_MAGIC);
			store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_addr, hpc_fb_addr);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_line_bytes, hpc_fb_xsize_mem * (((hpc_fb_bits-1)|7)+1) / 8);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_width, hpc_fb_xsize);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_height, hpc_fb_ysize);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.fb_type, hpc_fb_encoding);
			store_16bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.bi_cnuse,
			    machine->use_x11? BI_CNUSE_BUILTIN : BI_CNUSE_SERIAL);

			store_32bit_word_in_host(cpu, (unsigned char *)&hpc_bootinfo.timezone, 0);
			store_buf(cpu, machine->physical_ram_in_mb * 1048576 - 256, (char *)&hpc_bootinfo, sizeof(hpc_bootinfo));

			/*
			 *  TODO: ugly hack, only works with small NetBSD
			 *        kernels!
			 */
			cpu->cd.arm.r[ARM_SP] = 0xc02c0000;
		}

		/*  Physical RAM at 0xc0000000:  */
		dev_ram_init(machine, 0xc0000000, 0x20000000,
		    DEV_RAM_MIRROR, 0x0);

		/*  Cache flush region:  */
		dev_ram_init(machine, 0xe0000000, 0x10000, DEV_RAM_RAM, 0x0);

		if (hpc_fb_addr != 0) {
			dev_fb_init(machine, mem, hpc_fb_addr, VFB_HPC,
			    hpc_fb_xsize, hpc_fb_ysize,
			    hpc_fb_xsize_mem, hpc_fb_ysize_mem,
			    hpc_fb_bits, machine->machine_name);
		}
		break;

	case MACHINE_ZAURUS:
		machine->machine_name = "Zaurus";
		dev_ram_init(machine, 0xa0000000, 0x20000000,
		    DEV_RAM_MIRROR, 0x0);
		dev_ram_init(machine, 0xc0000000, 0x20000000,
		    DEV_RAM_MIRROR, 0x0);

		/*  TODO: replace this with the correct device  */
		dev_ram_init(machine, 0x40d00000, 0x1000, DEV_RAM_RAM, 0);

		device_add(machine, "ns16550 irq=0 addr=0x40100000 addr_mult=4");
		device_add(machine, "ns16550 irq=0 addr=0xfd400000 addr_mult=4");

		/*  TODO  */
		if (machine->prom_emulation) {
			arm_setup_initial_translation_table(cpu, 0x4000);
		}
		break;

	case MACHINE_NETWINDER:
		machine->machine_name = "NetWinder";

		if (machine->physical_ram_in_mb > 256)
			fprintf(stderr, "WARNING! Real NetWinders cannot"
			    " have more than 256 MB RAM. Continuing anyway.\n");

		machine->md_int.footbridge_data =
		    device_add(machine, "footbridge addr=0x42000000");
		machine->md_interrupt = isa32_interrupt;
		machine->isa_pic_data.native_irq = 11;

		bus_isa_init(machine, 0, 0x7c000000, 0x80000000, 32, 48);
#if 0
		snprintf(tmpstr, sizeof(tmpstr), "8259 irq=64 addr=0x7c000020");
		machine->isa_pic_data.pic1 = device_add(machine, tmpstr);
		snprintf(tmpstr, sizeof(tmpstr), "8259 irq=64 addr=0x7c0000a0");
		machine->isa_pic_data.pic2 = device_add(machine, tmpstr);

		device_add(machine, "ns16550 irq=36 addr=0x7c0003f8 name2=com0");
		device_add(machine, "ns16550 irq=35 addr=0x7c0002f8 name2=com1");

			dev_vga_init(machine, mem, 0x800a0000ULL, 0x7c0003c0, machine->machine_name);
			j = dev_pckbc_init(machine, mem, 0x7c000060, PCKBC_8042,
			    32 + 1, 32 + 12, machine->use_x11, 0);
			machine->main_console_handle = j;
#endif
		if (machine->use_x11) {
			bus_pci_add(machine, machine->md_int.footbridge_data->pcibus,
			    mem, 0xc0, 8, 0, "igsfb");
		}

		if (machine->prom_emulation) {
			arm_setup_initial_translation_table(cpu, 0x4000);
		}
		break;

	case MACHINE_SHARK:
		machine->machine_name = "Digital DNARD (\"Shark\")";

		bus_isa_init(machine, BUS_ISA_IDE0, 0x08100000, 0xc0000000, 32, 48);

		if (machine->prom_emulation) {
			arm_setup_initial_translation_table(cpu,
			    machine->physical_ram_in_mb * 1048576 - 65536);

			/*  TODO: Framebuffer  */
			of_emul_init(machine, NULL, 0xf1000000, 1024, 768);
			of_emul_init_isa(machine);

			/*
			 *  r0 = OpenFirmware entry point.  NOTE: See
			 *  cpu_arm.c for the rest of this semi-ugly hack.
			 */
			cpu->cd.arm.r[0] = cpu->cd.arm.of_emul_addr;
		}
		break;

	case MACHINE_IQ80321:
		/*
		 *  Intel IQ80321. See http://sources.redhat.com/ecos/docs-latest/redboot/iq80321.html
		 *  for more details about the memory map.
		 */
		machine->machine_name = "Intel IQ80321 (ARM)";
		cpu->cd.arm.coproc[6] = arm_coproc_i80321;
		cpu->cd.arm.coproc[14] = arm_coproc_i80321_14;
		device_add(machine, "ns16550 irq=0 addr=0xfe800000");

		/*  Used by "Redboot":  */
		dev_ram_init(machine, 0xa800024, 4, DEV_RAM_RAM, 0);
		store_32bit_word(cpu, 0xa800024, 0x7fff);
		device_add(machine, "ns16550 irq=0 addr=0x0d800000 addr_mult=4");
		device_add(machine, "ns16550 irq=0 addr=0x0d800020 addr_mult=4");

		/*  0xa0000000 = physical ram, 0xc0000000 = uncached  */
		dev_ram_init(machine, 0xa0000000, 0x20000000,
		    DEV_RAM_MIRROR, 0x0);
		dev_ram_init(machine, 0xc0000000, 0x20000000,
		    DEV_RAM_MIRROR, 0x0);

		/*  0xe0000000 and 0xff000000 = cache flush regions  */
		dev_ram_init(machine, 0xe0000000, 0x100000, DEV_RAM_RAM, 0x0);
		dev_ram_init(machine, 0xff000000, 0x100000, DEV_RAM_RAM, 0x0);

		device_add(machine, "i80321 addr=0xffffe000");

		if (machine->prom_emulation) {
			arm_setup_initial_translation_table(cpu, 0x4000);
			arm_translation_table_set_l1(cpu, 0xa0000000, 0xa0000000);
			arm_translation_table_set_l1(cpu, 0xc0000000, 0xa0000000);
			arm_translation_table_set_l1(cpu, 0xe0000000, 0xe0000000);
			arm_translation_table_set_l1(cpu, 0xf0000000, 0xf0000000);
		}
		break;

	case MACHINE_IYONIX:
		machine->machine_name = "Iyonix";
		cpu->cd.arm.coproc[6] = arm_coproc_i80321;
		cpu->cd.arm.coproc[14] = arm_coproc_i80321_14;

		device_add(machine, "ns16550 irq=0 addr=0xfe800000");
		device_add(machine, "ns16550 irq=0 addr=0x900003f8");
		device_add(machine, "ns16550 irq=0 addr=0x900002f8");

		/*  0xa0000000 = physical ram, 0xc0000000 = uncached  */
		dev_ram_init(machine, 0xa0000000, 0x20000000,
		    DEV_RAM_MIRROR, 0x0);
		dev_ram_init(machine, 0xc0000000, 0x20000000,
		    DEV_RAM_MIRROR, 0x0);
		dev_ram_init(machine, 0xf0000000, 0x08000000,
		    DEV_RAM_MIRROR, 0x0);

		device_add(machine, "i80321 addr=0xffffe000");

		if (machine->prom_emulation) {
			arm_setup_initial_translation_table(cpu,
			    machine->physical_ram_in_mb * 1048576 - 65536);
			arm_translation_table_set_l1(cpu, 0xa0000000, 0xa0000000);
			arm_translation_table_set_l1(cpu, 0xc0000000, 0xa0000000);
			arm_translation_table_set_l1_b(cpu, 0xff000000, 0xff000000);
		}
		break;
#endif	/*  ENABLE_ARM  */

#ifdef ENABLE_AVR
	case MACHINE_BAREAVR:
		/*  A bare Atmel AVR machine, with no devices.  */
		machine->machine_name = "\"Bare\" Atmel AVR machine";
		stable = 1;
		break;
#endif	/*  ENABLE_AVR  */

#ifdef ENABLE_IA64
	case MACHINE_BAREIA64:
		machine->machine_name = "\"Bare\" IA64 machine";
		stable = 1;
		break;

	case MACHINE_TESTIA64:
		machine->machine_name = "IA64 test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testia64 generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;
#endif	/*  ENABLE_IA64  */

#ifdef ENABLE_M68K
	case MACHINE_BAREM68K:
		machine->machine_name = "\"Bare\" M68K machine";
		stable = 1;
		break;

	case MACHINE_TESTM68K:
		machine->machine_name = "M68K test machine";
		stable = 1;

		snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
		    (long long)DEV_CONS_ADDRESS);
		cons_data = device_add(machine, tmpstr);
		machine->main_console_handle = cons_data->console_handle;

		snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
		    (long long)DEV_MP_ADDRESS);
		device_add(machine, tmpstr);

		fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, VFB_GENERIC,
		    640,480, 640,480, 24, "testm68k generic");

		snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
		    (long long)DEV_DISK_ADDRESS);
		device_add(machine, tmpstr);

		snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx irq=0",
		    (long long)DEV_ETHER_ADDRESS);
		device_add(machine, tmpstr);

		break;
#endif	/*  ENABLE_M68K  */

#ifdef ENABLE_X86
	case MACHINE_BAREX86:
		machine->machine_name = "\"Bare\" x86 machine";
		stable = 1;
		break;

	case MACHINE_X86:
		if (machine->machine_subtype == MACHINE_X86_XT)
			machine->machine_name = "PC XT";
		else
			machine->machine_name = "Generic x86 PC";

		machine->md_interrupt = x86_pc_interrupt;

		bus_isa_init(machine, BUS_ISA_IDE0 | BUS_ISA_IDE1 | BUS_ISA_VGA |
		    BUS_ISA_PCKBC_FORCE_USE |
		    (machine->machine_subtype == MACHINE_X86_XT?
		    BUS_ISA_NO_SECOND_PIC : 0) | BUS_ISA_FDC,
		    X86_IO_BASE, 0x00000000, 0, 16);

		if (machine->prom_emulation)
			pc_bios_init(cpu);

		if (!machine->use_x11 && !quiet_mode)
			fprintf(stderr, "-------------------------------------"
			    "------------------------------------------\n"
			    "\n  WARNING! You are emulating a PC without -X. "
			    "You will miss graphical output!\n\n"
			    "-------------------------------------"
			    "------------------------------------------\n");
		break;
#endif	/*  ENABLE_X86  */

	default:
		fatal("Unknown emulation type %i\n", machine->machine_type);
		exit(1);
	}

	if (machine->machine_name != NULL)
		debug("machine: %s", machine->machine_name);

	if (machine->emulated_hz > 0)
		debug(" (%.2f MHz)", (float)machine->emulated_hz / 1000000);
	debug("\n");

	/*  Default fake speed: 5 MHz  */
	if (machine->emulated_hz < 1)
		machine->emulated_hz = 5000000;

	if (bootstr != NULL) {
		debug("bootstring%s: %s", (bootarg!=NULL &&
		    strlen(bootarg) >= 1)? "(+bootarg)" : "", bootstr);
		if (bootarg != NULL && strlen(bootarg) >= 1)
			debug(" %s", bootarg);
		debug("\n");
	}

	if (verbose >= 2)
		machine_dump_bus_info(machine);

	if (!stable)
		fatal("!\n!  NOTE: This machine type is not implemented well"
		    " enough yet to\n!  run any real-world code!\n"
		    "!\n!  (At least, it hasn't been verified to do so.)\n!\n");
}


/*
 *  machine_memsize_fix():
 *
 *  Sets physical_ram_in_mb (if not already set), and memory_offset_in_mb,
 *  depending on machine type.
 */
void machine_memsize_fix(struct machine *m)
{
	if (m == NULL) {
		fatal("machine_defaultmemsize(): m == NULL?\n");
		exit(1);
	}

	if (m->physical_ram_in_mb == 0) {
		switch (m->machine_type) {
		case MACHINE_PS2:
			m->physical_ram_in_mb = 32;
			break;
		case MACHINE_SGI:
			m->physical_ram_in_mb = 64;
			break;
		case MACHINE_HPCMIPS:
			/*  Most have 32 MB by default.  */
			m->physical_ram_in_mb = 32;
			switch (m->machine_subtype) {
			case MACHINE_HPCMIPS_CASIO_BE300:
				m->physical_ram_in_mb = 16;
				break;
			case MACHINE_HPCMIPS_CASIO_E105:
				m->physical_ram_in_mb = 32;
				break;
			case MACHINE_HPCMIPS_AGENDA_VR3:
				m->physical_ram_in_mb = 16;
				break;
			}
			break;
		case MACHINE_MESHCUBE:
			m->physical_ram_in_mb = 64;
			break;
		case MACHINE_NETGEAR:
			m->physical_ram_in_mb = 16;
			break;
		case MACHINE_EVBMIPS:
			m->physical_ram_in_mb = 64;
			break;
		case MACHINE_PSP:
			/*
			 *  According to
			 *  http://wiki.ps2dev.org/psp:memory_map:
			 *	0×08000000 = 8 MB kernel memory
			 *	0×08800000 = 24 MB user memory
			 */
			m->physical_ram_in_mb = 8 + 24;
			break;
		case MACHINE_ARC:
			switch (m->machine_subtype) {
			case MACHINE_ARC_JAZZ_PICA:
				m->physical_ram_in_mb = 64;
				break;
			case MACHINE_ARC_JAZZ_M700:
				m->physical_ram_in_mb = 64;
				break;
			default:
				m->physical_ram_in_mb = 32;
			}
			break;
		case MACHINE_DEC:
			switch (m->machine_subtype) {
			case MACHINE_DEC_PMAX_3100:
				m->physical_ram_in_mb = 24;
				break;
			case MACHINE_DEC_3MAX_5000:
				m->physical_ram_in_mb = 64;
				break;
			default:
				m->physical_ram_in_mb = 32;
			}
			break;
		case MACHINE_ALPHA:
		case MACHINE_BEBOX:
		case MACHINE_PREP:
		case MACHINE_CATS:
		case MACHINE_ZAURUS:
			m->physical_ram_in_mb = 64;
			break;
		case MACHINE_HPCARM:
			m->physical_ram_in_mb = 32;
			break;
		case MACHINE_NETWINDER:
			m->physical_ram_in_mb = 16;
			break;
		case MACHINE_X86:
			if (m->machine_subtype == MACHINE_X86_XT)
				m->physical_ram_in_mb = 1;
			break;
		}
	}

	/*  Special hack for hpcmips machines:  */
	if (m->machine_type == MACHINE_HPCMIPS) {
		m->dbe_on_nonexistant_memaccess = 0;
	}

	/*  Special SGI memory offsets:  */
	if (m->machine_type == MACHINE_SGI) {
		switch (m->machine_subtype) {
		case 20:
		case 22:
		case 24:
		case 26:
			m->memory_offset_in_mb = 128;
			break;
		case 28:
		case 30:
			m->memory_offset_in_mb = 512;
			break;
		}
	}

	if (m->physical_ram_in_mb == 0)
		m->physical_ram_in_mb = DEFAULT_RAM_IN_MB;
}


/*
 *  machine_default_cputype():
 *
 *  Sets m->cpu_name, if it isn't already set, depending on the machine
 *  type.
 */
void machine_default_cputype(struct machine *m)
{
	if (m == NULL) {
		fatal("machine_default_cputype(): m == NULL?\n");
		exit(1);
	}

	if (m->cpu_name != NULL)
		return;

	switch (m->machine_type) {
	case MACHINE_BAREMIPS:
	case MACHINE_TESTMIPS:
		m->cpu_name = strdup("R4000");
		break;
	case MACHINE_PS2:
		m->cpu_name = strdup("R5900");
		break;
	case MACHINE_DEC:
		if (m->machine_subtype > 2)
			m->cpu_name = strdup("R3000A");
		if (m->machine_subtype > 1 && m->cpu_name == NULL)
			m->cpu_name = strdup("R3000");
		if (m->cpu_name == NULL)
			m->cpu_name = strdup("R2000");
		break;
	case MACHINE_SONYNEWS:
		m->cpu_name = strdup("R3000");
		break;
	case MACHINE_HPCMIPS:
		switch (m->machine_subtype) {
		case MACHINE_HPCMIPS_CASIO_BE300:
			m->cpu_name = strdup("VR4131");
			break;
		case MACHINE_HPCMIPS_CASIO_E105:
			m->cpu_name = strdup("VR4121");
			break;
		case MACHINE_HPCMIPS_NEC_MOBILEPRO_770:
		case MACHINE_HPCMIPS_NEC_MOBILEPRO_780:
		case MACHINE_HPCMIPS_NEC_MOBILEPRO_800:
		case MACHINE_HPCMIPS_NEC_MOBILEPRO_880:
			m->cpu_name = strdup("VR4121");
			break;
		case MACHINE_HPCMIPS_AGENDA_VR3:
			m->cpu_name = strdup("VR4181");
			break;
		case MACHINE_HPCMIPS_IBM_WORKPAD_Z50:
			m->cpu_name = strdup("VR4121");
			break;
		default:
			printf("Unimplemented HPCMIPS model?\n");
			exit(1);
		}
		break;
	case MACHINE_COBALT:
		m->cpu_name = strdup("RM5200");
		break;
	case MACHINE_MESHCUBE:
		m->cpu_name = strdup("R4400");
		/*  TODO:  Should be AU1500, but Linux doesn't like
		    the absence of caches in the emulator  */
		break;
	case MACHINE_NETGEAR:
		m->cpu_name = strdup("RC32334");
		break;
	case MACHINE_ARC:
		switch (m->machine_subtype) {
		case MACHINE_ARC_JAZZ_PICA:
			m->cpu_name = strdup("R4000");
			break;
		default:
			m->cpu_name = strdup("R4400");
		}
		break;
	case MACHINE_SGI:
		if (m->machine_subtype <= 12)
			m->cpu_name = strdup("R3000");
		if (m->cpu_name == NULL && m->machine_subtype == 35)
			m->cpu_name = strdup("R12000");
		if (m->cpu_name == NULL && (m->machine_subtype == 25 ||
		    m->machine_subtype == 27 ||
		    m->machine_subtype == 28 ||
		    m->machine_subtype == 30 ||
		    m->machine_subtype == 32))
			m->cpu_name = strdup("R10000");
		if (m->cpu_name == NULL && (m->machine_subtype == 21 ||
		    m->machine_subtype == 26))
			m->cpu_name = strdup("R8000");
		if (m->cpu_name == NULL && m->machine_subtype == 24)
			m->cpu_name = strdup("R5000");

		/*  Other SGIs should probably work with
		    R4000, R4400 or R5000 or similar:  */
		if (m->cpu_name == NULL)
			m->cpu_name = strdup("R4400");
		break;
	case MACHINE_EVBMIPS:
		switch (m->machine_subtype) {
		case MACHINE_EVBMIPS_MALTA:
		case MACHINE_EVBMIPS_MALTA_BE:
			m->cpu_name = strdup("5Kc");
			break;
		case MACHINE_EVBMIPS_PB1000:
			m->cpu_name = strdup("AU1000");
			break;
		default:fatal("Unimpl. evbmips.\n");
			exit(1);
		}
		break;
	case MACHINE_PSP:
		m->cpu_name = strdup("Allegrex");
		break;
	case MACHINE_ALGOR:
		m->cpu_name = strdup("RM5200");
		break;

	/*  PowerPC:  */
	case MACHINE_BAREPPC:
	case MACHINE_TESTPPC:
		m->cpu_name = strdup("PPC970");
		break;
	case MACHINE_WALNUT:
		/*  For NetBSD/evbppc.  */
		m->cpu_name = strdup("PPC405GP");
		break;
	case MACHINE_PMPPC:
		/*  For NetBSD/pmppc.  */
		m->cpu_name = strdup("PPC750");
		break;
	case MACHINE_SANDPOINT:
		/*
		 *  For NetBSD/sandpoint. According to NetBSD's page:
		 *
		 *  "Unity" module has an MPC8240.
		 *  "Altimus" module has an MPC7400 (G4) or an MPC107.
		 */
		m->cpu_name = strdup("MPC7400");
		break;
	case MACHINE_BEBOX:
		/*  For NetBSD/bebox. Dual 133 MHz 603e CPUs, for example.  */
		m->cpu_name = strdup("PPC603e");
		break;
	case MACHINE_PREP:
		/*  For NetBSD/prep. TODO: Differs between models!  */
		m->cpu_name = strdup("PPC604");
		break;
	case MACHINE_MACPPC:
		switch (m->machine_subtype) {
		case MACHINE_MACPPC_G4:
			m->cpu_name = strdup("PPC750");
			break;
		case MACHINE_MACPPC_G5:
			m->cpu_name = strdup("PPC970");
			break;
		}
		break;
	case MACHINE_DB64360:
		m->cpu_name = strdup("PPC750");
		break;

	/*  SH:  */
	case MACHINE_BARESH:
	case MACHINE_TESTSH:
	case MACHINE_HPCSH:
		m->cpu_name = strdup("SH");
		break;

	/*  HPPA:  */
	case MACHINE_BAREHPPA:
	case MACHINE_TESTHPPA:
		m->cpu_name = strdup("HPPA");
		break;

	/*  i960:  */
	case MACHINE_BAREI960:
	case MACHINE_TESTI960:
		m->cpu_name = strdup("i960");
		break;

	/*  SPARC:  */
	case MACHINE_BARESPARC:
	case MACHINE_TESTSPARC:
	case MACHINE_ULTRA1:
		m->cpu_name = strdup("SPARCv9");
		break;

	/*  Alpha:  */
	case MACHINE_BAREALPHA:
	case MACHINE_TESTALPHA:
	case MACHINE_ALPHA:
		m->cpu_name = strdup("Alpha");
		break;

	/*  ARM:  */
	case MACHINE_BAREARM:
	case MACHINE_TESTARM:
	case MACHINE_HPCARM:
		m->cpu_name = strdup("SA1110");
		break;
	case MACHINE_IQ80321:
	case MACHINE_IYONIX:
		m->cpu_name = strdup("80321_600_B0");
		break;
	case MACHINE_CATS:
	case MACHINE_NETWINDER:
	case MACHINE_SHARK:
		m->cpu_name = strdup("SA110");
		break;
	case MACHINE_ZAURUS:
		m->cpu_name = strdup("PXA210");
		break;

	/*  AVR:  */
	case MACHINE_BAREAVR:
		m->cpu_name = strdup("AVR");
		break;

	/*  IA64:  */
	case MACHINE_BAREIA64:
	case MACHINE_TESTIA64:
		m->cpu_name = strdup("IA64");
		break;

	/*  M68K:  */
	case MACHINE_BAREM68K:
	case MACHINE_TESTM68K:
		m->cpu_name = strdup("68020");
		break;

	/*  x86:  */
	case MACHINE_BAREX86:
	case MACHINE_X86:
		if (m->machine_subtype == MACHINE_X86_XT)
			m->cpu_name = strdup("8086");
		else
			m->cpu_name = strdup("AMD64");
		break;
	}

	if (m->cpu_name == NULL) {
		fprintf(stderr, "machine_default_cputype(): no default"
		    " cpu for machine type %i subtype %i\n",
		    m->machine_type, m->machine_subtype);
		exit(1);
	}
}


/*
 *  machine_entry_new():
 *
 *  This function creates a new machine_entry struct, and fills it with some
 *  valid data; it is up to the caller to add additional data that weren't
 *  passed as arguments to this function.
 *
 *  For internal use.
 */
static struct machine_entry *machine_entry_new(const char *name,
	int arch, int oldstyle_type, int n_aliases, int n_subtypes)
{
	struct machine_entry *me;

	me = malloc(sizeof(struct machine_entry));
	if (me == NULL) {
		fprintf(stderr, "machine_entry_new(): out of memory (1)\n");
		exit(1);
	}

	memset(me, 0, sizeof(struct machine_entry));

	me->name = name;
	me->arch = arch;
	me->machine_type = oldstyle_type;
	me->n_aliases = n_aliases;
	me->aliases = malloc(sizeof(char *) * n_aliases);
	if (me->aliases == NULL) {
		fprintf(stderr, "machine_entry_new(): out of memory (2)\n");
		exit(1);
	}
	me->n_subtypes = n_subtypes;

	if (n_subtypes > 0) {
		me->subtype = malloc(sizeof(struct machine_entry_subtype *) *
		    n_subtypes);
		if (me->subtype == NULL) {
			fprintf(stderr, "machine_entry_new(): out of "
			    "memory (3)\n");
			exit(1);
		}
	}

	return me;
}


/*
 *  machine_entry_subtype_new():
 *
 *  This function creates a new machine_entry_subtype struct, and fills it with
 *  some valid data; it is up to the caller to add additional data that weren't
 *  passed as arguments to this function.
 *
 *  For internal use.
 */
static struct machine_entry_subtype *machine_entry_subtype_new(
	const char *name, int oldstyle_type, int n_aliases)
{
	struct machine_entry_subtype *mes;

	mes = malloc(sizeof(struct machine_entry_subtype));
	if (mes == NULL) {
		fprintf(stderr, "machine_entry_subtype_new(): out "
		    "of memory (1)\n");
		exit(1);
	}

	memset(mes, 0, sizeof(struct machine_entry_subtype));
	mes->name = name;
	mes->machine_subtype = oldstyle_type;
	mes->n_aliases = n_aliases;
	mes->aliases = malloc(sizeof(char *) * n_aliases);
	if (mes->aliases == NULL) {
		fprintf(stderr, "machine_entry_subtype_new(): "
		    "out of memory (2)\n");
		exit(1);
	}

	return mes;
}


/*
 *  machine_list_available_types_and_cpus():
 *
 *  List all available machine types (for example when running the emulator
 *  with just -H as command line argument).
 */
void machine_list_available_types_and_cpus(void)
{
	struct machine_entry *me;
	int iadd = DEBUG_INDENTATION * 2;

	debug("Available CPU types:\n\n");

	debug_indentation(iadd);
	cpu_list_available_types();
	debug_indentation(-iadd);  

	debug("\nMost of the CPU types are bogus, and not really implemented."
	    " The main effect of\nselecting a specific CPU type is to choose "
	    "what kind of 'id' it will have.\n\nAvailable machine types (with "
	    "aliases) and their subtypes:\n\n");

	debug_indentation(iadd);
	me = first_machine_entry;

	if (me == NULL)
		fatal("No machines defined!\n");

	while (me != NULL) {
		int i, j, iadd = DEBUG_INDENTATION;

		debug("%s", me->name);
		debug(" (");
		for (i=0; i<me->n_aliases; i++)
			debug("%s\"%s\"", i? ", " : "", me->aliases[i]);
		debug(")\n");

		debug_indentation(iadd);
		for (i=0; i<me->n_subtypes; i++) {
			struct machine_entry_subtype *mes;
			mes = me->subtype[i];
			debug("- %s", mes->name);
			debug(" (");
			for (j=0; j<mes->n_aliases; j++)
				debug("%s\"%s\"", j? ", " : "",
				    mes->aliases[j]);
			debug(")\n");
		}
		debug_indentation(-iadd);

		me = me->next;
	}
	debug_indentation(-iadd);

	debug("\nMost of the machine types are bogus too. Please read the "
	    "GXemul documentation\nfor information about which machine types "
	    "that actually work. Use the alias\nwhen selecting a machine type "
	    "or subtype, not the real name.\n");

	debug("\n");

	useremul_list_emuls();
	debug("Userland emulation works for programs with the complexity"
	    " of Hello World,\nbut not much more.\n");
}


/*
 *  machine_init():
 *
 *  This function should be called before any other machine_*() function
 *  is used.
 */
void machine_init(void)
{
	struct machine_entry *me;

	/*
	 *  NOTE: This list is in reverse order, so that the
	 *  entries will appear in normal order when listed.  :-)
	 */

	/*  Zaurus:  */
	me = machine_entry_new("Zaurus (ARM)",
	    ARCH_ARM, MACHINE_ZAURUS, 1, 0);
	me->aliases[0] = "zaurus";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  X86 machine:  */
	me = machine_entry_new("x86-based PC", ARCH_X86,
	    MACHINE_X86, 2, 2);
	me->aliases[0] = "pc";
	me->aliases[1] = "x86";
	me->subtype[0] = machine_entry_subtype_new("Generic PC",
	    MACHINE_X86_GENERIC, 2);
	me->subtype[0]->aliases[0] = "pc";
	me->subtype[0]->aliases[1] = "generic";
	me->subtype[1] = machine_entry_subtype_new("PC XT", MACHINE_X86_XT, 1);
	me->subtype[1]->aliases[0] = "xt";
	if (cpu_family_ptr_by_number(ARCH_X86) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Walnut: (NetBSD/evbppc)  */
	me = machine_entry_new("Walnut evaluation board", ARCH_PPC,
	    MACHINE_WALNUT, 2, 0);
	me->aliases[0] = "walnut";
	me->aliases[1] = "evbppc";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for SPARC:  */
	me = machine_entry_new("Test-machine for SPARC", ARCH_SPARC,
	    MACHINE_TESTSPARC, 1, 0);
	me->aliases[0] = "testsparc";
	if (cpu_family_ptr_by_number(ARCH_SPARC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for SH:  */
	me = machine_entry_new("Test-machine for SH", ARCH_SH,
	    MACHINE_TESTSH, 1, 0);
	me->aliases[0] = "testsh";
	if (cpu_family_ptr_by_number(ARCH_SH) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for PPC:  */
	me = machine_entry_new("Test-machine for PPC", ARCH_PPC,
	    MACHINE_TESTPPC, 1, 0);
	me->aliases[0] = "testppc";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for MIPS:  */
	me = machine_entry_new("Test-machine for MIPS", ARCH_MIPS,
	    MACHINE_TESTMIPS, 1, 0);
	me->aliases[0] = "testmips";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for M68K:  */
	me = machine_entry_new("Test-machine for M68K", ARCH_M68K,
	    MACHINE_TESTM68K, 1, 0);
	me->aliases[0] = "testm68k";
	if (cpu_family_ptr_by_number(ARCH_M68K) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for IA64:  */
	me = machine_entry_new("Test-machine for IA64", ARCH_IA64,
	    MACHINE_TESTIA64, 1, 0);
	me->aliases[0] = "testia64";
	if (cpu_family_ptr_by_number(ARCH_IA64) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for i960:  */
	me = machine_entry_new("Test-machine for i960", ARCH_I960,
	    MACHINE_TESTI960, 1, 0);
	me->aliases[0] = "testi960";
	if (cpu_family_ptr_by_number(ARCH_I960) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for HPPA:  */
	me = machine_entry_new("Test-machine for HPPA", ARCH_HPPA,
	    MACHINE_TESTHPPA, 1, 0);
	me->aliases[0] = "testhppa";
	if (cpu_family_ptr_by_number(ARCH_HPPA) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for ARM:  */
	me = machine_entry_new("Test-machine for ARM", ARCH_ARM,
	    MACHINE_TESTARM, 1, 0);
	me->aliases[0] = "testarm";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Test-machine for Alpha:  */
	me = machine_entry_new("Test-machine for Alpha", ARCH_ALPHA,
	    MACHINE_TESTALPHA, 1, 0);
	me->aliases[0] = "testalpha";
	if (cpu_family_ptr_by_number(ARCH_ALPHA) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Sun Ultra1:  */
	me = machine_entry_new("Sun Ultra1", ARCH_SPARC, MACHINE_ULTRA1, 1, 0);
	me->aliases[0] = "ultra1";
	if (cpu_family_ptr_by_number(ARCH_SPARC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Sony Playstation 2:  */
	me = machine_entry_new("Sony Playstation 2", ARCH_MIPS,
	    MACHINE_PS2, 2, 0);
	me->aliases[0] = "playstation2";
	me->aliases[1] = "ps2";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Sony NeWS:  */
	me = machine_entry_new("Sony NeWS", ARCH_MIPS,
	    MACHINE_SONYNEWS, 2, 0);
	me->aliases[0] = "sonynews";
	me->aliases[1] = "news";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  SGI:  */
	me = machine_entry_new("SGI", ARCH_MIPS, MACHINE_SGI, 2, 10);
	me->aliases[0] = "silicon graphics";
	me->aliases[1] = "sgi";
	me->subtype[0] = machine_entry_subtype_new("IP12", 12, 1);
	me->subtype[0]->aliases[0] = "ip12";
	me->subtype[1] = machine_entry_subtype_new("IP19", 19, 1);
	me->subtype[1]->aliases[0] = "ip19";
	me->subtype[2] = machine_entry_subtype_new("IP20", 20, 1);
	me->subtype[2]->aliases[0] = "ip20";
	me->subtype[3] = machine_entry_subtype_new("IP22", 22, 2);
	me->subtype[3]->aliases[0] = "ip22";
	me->subtype[3]->aliases[1] = "indy";
	me->subtype[4] = machine_entry_subtype_new("IP24", 24, 1);
	me->subtype[4]->aliases[0] = "ip24";
	me->subtype[5] = machine_entry_subtype_new("IP27", 27, 3);
	me->subtype[5]->aliases[0] = "ip27";
	me->subtype[5]->aliases[1] = "origin 200";
	me->subtype[5]->aliases[2] = "origin 2000";
	me->subtype[6] = machine_entry_subtype_new("IP28", 28, 1);
	me->subtype[6]->aliases[0] = "ip28";
	me->subtype[7] = machine_entry_subtype_new("IP30", 30, 2);
	me->subtype[7]->aliases[0] = "ip30";
	me->subtype[7]->aliases[1] = "octane";
	me->subtype[8] = machine_entry_subtype_new("IP32", 32, 2);
	me->subtype[8]->aliases[0] = "ip32";
	me->subtype[8]->aliases[1] = "o2";
	me->subtype[9] = machine_entry_subtype_new("IP35", 35, 1);
	me->subtype[9]->aliases[0] = "ip35";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  PReP: (NetBSD/prep etc.)  */
	me = machine_entry_new("PowerPC Reference Platform", ARCH_PPC,
	    MACHINE_PREP, 1, 0);
	me->aliases[0] = "prep";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Playstation Portable:  */
	me = machine_entry_new("Playstation Portable", ARCH_MIPS,
	    MACHINE_PSP, 1, 0);
	me->aliases[0] = "psp";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  NetWinder:  */
	me = machine_entry_new("NetWinder", ARCH_ARM, MACHINE_NETWINDER, 1, 0);
	me->aliases[0] = "netwinder";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  NetGear:  */
	me = machine_entry_new("NetGear WG602v1", ARCH_MIPS,
	    MACHINE_NETGEAR, 2, 0);
	me->aliases[0] = "netgear";
	me->aliases[1] = "wg602v1";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Motorola Sandpoint: (NetBSD/sandpoint)  */
	me = machine_entry_new("Motorola Sandpoint",
	    ARCH_PPC, MACHINE_SANDPOINT, 1, 0);
	me->aliases[0] = "sandpoint";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Meshcube:  */
	me = machine_entry_new("Meshcube", ARCH_MIPS, MACHINE_MESHCUBE, 1, 0);
	me->aliases[0] = "meshcube";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Macintosh (PPC):  */
	me = machine_entry_new("Macintosh (PPC)", ARCH_PPC,
	    MACHINE_MACPPC, 1, 2);
	me->aliases[0] = "macppc";
	me->subtype[0] = machine_entry_subtype_new("MacPPC G4",
	    MACHINE_MACPPC_G4, 1);
	me->subtype[0]->aliases[0] = "g4";
	me->subtype[1] = machine_entry_subtype_new("MacPPC G5",
	    MACHINE_MACPPC_G5, 1);
	me->subtype[1]->aliases[0] = "g5";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Iyonix:  */
	me = machine_entry_new("Iyonix", ARCH_ARM,
	    MACHINE_IYONIX, 1, 0);
	me->aliases[0] = "iyonix";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Intel IQ80321 (ARM):  */
	me = machine_entry_new("Intel IQ80321 (ARM)", ARCH_ARM,
	    MACHINE_IQ80321, 1, 0);
	me->aliases[0] = "iq80321";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  HPCarm:  */
	me = machine_entry_new("Handheld SH (HPCsh)",
	    ARCH_SH, MACHINE_HPCSH, 1, 2);
	me->aliases[0] = "hpcsh";
	me->subtype[0] = machine_entry_subtype_new("Jornada 680",
	    MACHINE_HPCSH_JORNADA680, 1);
	me->subtype[0]->aliases[0] = "jornada680";
	me->subtype[1] = machine_entry_subtype_new(
	    "Jornada 690", MACHINE_HPCSH_JORNADA690, 1);
	me->subtype[1]->aliases[0] = "jornada690";
	if (cpu_family_ptr_by_number(ARCH_SH) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  HPCmips:  */
	me = machine_entry_new("Handheld MIPS (HPCmips)",
	    ARCH_MIPS, MACHINE_HPCMIPS, 1, 8);
	me->aliases[0] = "hpcmips";
	me->subtype[0] = machine_entry_subtype_new(
	    "Casio Cassiopeia BE-300", MACHINE_HPCMIPS_CASIO_BE300, 2);
	me->subtype[0]->aliases[0] = "be-300";
	me->subtype[0]->aliases[1] = "be300";
	me->subtype[1] = machine_entry_subtype_new(
	    "Casio Cassiopeia E-105", MACHINE_HPCMIPS_CASIO_E105, 2);
	me->subtype[1]->aliases[0] = "e-105";
	me->subtype[1]->aliases[1] = "e105";
	me->subtype[2] = machine_entry_subtype_new(
	    "Agenda VR3", MACHINE_HPCMIPS_AGENDA_VR3, 2);
	me->subtype[2]->aliases[0] = "agenda";
	me->subtype[2]->aliases[1] = "vr3";
	me->subtype[3] = machine_entry_subtype_new(
	    "IBM WorkPad Z50", MACHINE_HPCMIPS_IBM_WORKPAD_Z50, 2);
	me->subtype[3]->aliases[0] = "workpad";
	me->subtype[3]->aliases[1] = "z50";
	me->subtype[4] = machine_entry_subtype_new(
	    "NEC MobilePro 770", MACHINE_HPCMIPS_NEC_MOBILEPRO_770, 1);
	me->subtype[4]->aliases[0] = "mobilepro770";
	me->subtype[5] = machine_entry_subtype_new(
	    "NEC MobilePro 780", MACHINE_HPCMIPS_NEC_MOBILEPRO_780, 1);
	me->subtype[5]->aliases[0] = "mobilepro780";
	me->subtype[6] = machine_entry_subtype_new(
	    "NEC MobilePro 800", MACHINE_HPCMIPS_NEC_MOBILEPRO_800, 1);
	me->subtype[6]->aliases[0] = "mobilepro800";
	me->subtype[7] = machine_entry_subtype_new(
	    "NEC MobilePro 880", MACHINE_HPCMIPS_NEC_MOBILEPRO_880, 1);
	me->subtype[7]->aliases[0] = "mobilepro880";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  HPCarm:  */
	me = machine_entry_new("Handheld ARM (HPCarm)",
	    ARCH_ARM, MACHINE_HPCARM, 1, 2);
	me->aliases[0] = "hpcarm";
	me->subtype[0] = machine_entry_subtype_new("Ipaq",
	    MACHINE_HPCARM_IPAQ, 1);
	me->subtype[0]->aliases[0] = "ipaq";
	me->subtype[1] = machine_entry_subtype_new(
	    "Jornada 720", MACHINE_HPCARM_JORNADA720, 1);
	me->subtype[1]->aliases[0] = "jornada720";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" X86 machine:  */
	me = machine_entry_new("Generic \"bare\" X86 machine", ARCH_X86,
	    MACHINE_BAREX86, 1, 0);
	me->aliases[0] = "barex86";
	if (cpu_family_ptr_by_number(ARCH_X86) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" SPARC machine:  */
	me = machine_entry_new("Generic \"bare\" SPARC machine", ARCH_SPARC,
	    MACHINE_BARESPARC, 1, 0);
	me->aliases[0] = "baresparc";
	if (cpu_family_ptr_by_number(ARCH_SPARC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" SH machine:  */
	me = machine_entry_new("Generic \"bare\" SH machine", ARCH_SH,
	    MACHINE_BARESH, 1, 0);
	me->aliases[0] = "baresh";
	if (cpu_family_ptr_by_number(ARCH_SH) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" PPC machine:  */
	me = machine_entry_new("Generic \"bare\" PPC machine", ARCH_PPC,
	    MACHINE_BAREPPC, 1, 0);
	me->aliases[0] = "bareppc";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" MIPS machine:  */
	me = machine_entry_new("Generic \"bare\" MIPS machine", ARCH_MIPS,
	    MACHINE_BAREMIPS, 1, 0);
	me->aliases[0] = "baremips";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" M68K machine:  */
	me = machine_entry_new("Generic \"bare\" M68K machine", ARCH_M68K,
	    MACHINE_BAREM68K, 1, 0);
	me->aliases[0] = "barem68k";
	if (cpu_family_ptr_by_number(ARCH_M68K) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" IA64 machine:  */
	me = machine_entry_new("Generic \"bare\" IA64 machine", ARCH_IA64,
	    MACHINE_BAREIA64, 1, 0);
	me->aliases[0] = "bareia64";
	if (cpu_family_ptr_by_number(ARCH_IA64) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" i960 machine:  */
	me = machine_entry_new("Generic \"bare\" i960 machine", ARCH_I960,
	    MACHINE_BAREI960, 1, 0);
	me->aliases[0] = "barei960";
	if (cpu_family_ptr_by_number(ARCH_I960) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" HPPA machine:  */
	me = machine_entry_new("Generic \"bare\" HPPA machine", ARCH_HPPA,
	    MACHINE_BAREHPPA, 1, 0);
	me->aliases[0] = "barehppa";
	if (cpu_family_ptr_by_number(ARCH_HPPA) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" Atmel AVR machine:  */
	me = machine_entry_new("Generic \"bare\" Atmel AVR machine", ARCH_AVR,
	    MACHINE_BAREAVR, 1, 0);
	me->aliases[0] = "bareavr";
	if (cpu_family_ptr_by_number(ARCH_AVR) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" ARM machine:  */
	me = machine_entry_new("Generic \"bare\" ARM machine", ARCH_ARM,
	    MACHINE_BAREARM, 1, 0);
	me->aliases[0] = "barearm";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Generic "bare" Alpha machine:  */
	me = machine_entry_new("Generic \"bare\" Alpha machine", ARCH_ALPHA,
	    MACHINE_BAREALPHA, 1, 0);
	me->aliases[0] = "barealpha";
	if (cpu_family_ptr_by_number(ARCH_ALPHA) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Evaluation Boards (MALTA etc):  */
	me = machine_entry_new("Evaluation boards (evbmips)", ARCH_MIPS,
	    MACHINE_EVBMIPS, 1, 3);
	me->aliases[0] = "evbmips";
	me->subtype[0] = machine_entry_subtype_new("Malta",
	    MACHINE_EVBMIPS_MALTA, 1);
	me->subtype[0]->aliases[0] = "malta";
	me->subtype[1] = machine_entry_subtype_new("Malta (Big-Endian)",
	    MACHINE_EVBMIPS_MALTA_BE, 1);
	me->subtype[1]->aliases[0] = "maltabe";
	me->subtype[2] = machine_entry_subtype_new("PB1000",
	    MACHINE_EVBMIPS_PB1000, 1);
	me->subtype[2]->aliases[0] = "pb1000";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Digital DNARD ("Shark"):  */
	me = machine_entry_new("Digital DNARD (\"Shark\")", ARCH_ARM,
	    MACHINE_SHARK, 2, 0);
	me->aliases[0] = "shark";
	me->aliases[1] = "dnard";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  DECstation:  */
	me = machine_entry_new("DECstation/DECsystem",
	    ARCH_MIPS, MACHINE_DEC, 3, 9);
	me->aliases[0] = "decstation";
	me->aliases[1] = "decsystem";
	me->aliases[2] = "dec";
	me->subtype[0] = machine_entry_subtype_new(
	    "DECstation 3100 (PMAX)", MACHINE_DEC_PMAX_3100, 3);
	me->subtype[0]->aliases[0] = "pmax";
	me->subtype[0]->aliases[1] = "3100";
	me->subtype[0]->aliases[2] = "2100";

	me->subtype[1] = machine_entry_subtype_new(
	    "DECstation 5000/200 (3MAX)", MACHINE_DEC_3MAX_5000, 2);
	me->subtype[1]->aliases[0] = "3max";
	me->subtype[1]->aliases[1] = "5000/200";

	me->subtype[2] = machine_entry_subtype_new(
	    "DECstation 5000/1xx (3MIN)", MACHINE_DEC_3MIN_5000, 2);
	me->subtype[2]->aliases[0] = "3min";
	me->subtype[2]->aliases[1] = "5000/1xx";

	me->subtype[3] = machine_entry_subtype_new(
	    "DECstation 5000 (3MAXPLUS)", MACHINE_DEC_3MAXPLUS_5000, 2);
	me->subtype[3]->aliases[0] = "3maxplus";
	me->subtype[3]->aliases[1] = "3max+";

	me->subtype[4] = machine_entry_subtype_new(
	    "DECsystem 58x0", MACHINE_DEC_5800, 2);
	me->subtype[4]->aliases[0] = "5800";
	me->subtype[4]->aliases[1] = "58x0";

	me->subtype[5] = machine_entry_subtype_new(
	    "DECsystem 5400", MACHINE_DEC_5400, 1);
	me->subtype[5]->aliases[0] = "5400";

	me->subtype[6] = machine_entry_subtype_new(
	    "DECstation Maxine (5000)", MACHINE_DEC_MAXINE_5000, 1);
	me->subtype[6]->aliases[0] = "maxine";

	me->subtype[7] = machine_entry_subtype_new(
	    "DECsystem 5500", MACHINE_DEC_5500, 1);
	me->subtype[7]->aliases[0] = "5500";

	me->subtype[8] = machine_entry_subtype_new(
	    "DECstation MipsMate (5100)", MACHINE_DEC_MIPSMATE_5100, 2);
	me->subtype[8]->aliases[0] = "5100";
	me->subtype[8]->aliases[1] = "mipsmate";

	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  DB64360: (for playing with PMON for PPC)  */
	me = machine_entry_new("DB64360", ARCH_PPC, MACHINE_DB64360, 1, 0);
	me->aliases[0] = "db64360";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Cobalt:  */
	me = machine_entry_new("Cobalt", ARCH_MIPS, MACHINE_COBALT, 1, 0);
	me->aliases[0] = "cobalt";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  CATS (ARM) evaluation board:  */
	me = machine_entry_new("CATS evaluation board (ARM)", ARCH_ARM,
	    MACHINE_CATS, 1, 0);
	me->aliases[0] = "cats";
	if (cpu_family_ptr_by_number(ARCH_ARM) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  BeBox: (NetBSD/bebox)  */
	me = machine_entry_new("BeBox", ARCH_PPC, MACHINE_BEBOX, 1, 0);
	me->aliases[0] = "bebox";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Artesyn's PM/PPC board: (NetBSD/pmppc)  */
	me = machine_entry_new("Artesyn's PM/PPC board", ARCH_PPC,
	    MACHINE_PMPPC, 1, 0);
	me->aliases[0] = "pmppc";
	if (cpu_family_ptr_by_number(ARCH_PPC) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  ARC:  */
	me = machine_entry_new("ARC", ARCH_MIPS, MACHINE_ARC, 1, 8);
	me->aliases[0] = "arc";

	me->subtype[0] = machine_entry_subtype_new(
	    "Acer PICA-61", MACHINE_ARC_JAZZ_PICA, 3);
	me->subtype[0]->aliases[0] = "pica-61";
	me->subtype[0]->aliases[1] = "acer pica";
	me->subtype[0]->aliases[2] = "pica";

	me->subtype[1] = machine_entry_subtype_new(
	    "Deskstation Tyne", MACHINE_ARC_DESKTECH_TYNE, 3);
	me->subtype[1]->aliases[0] = "deskstation tyne";
	me->subtype[1]->aliases[1] = "desktech";
	me->subtype[1]->aliases[2] = "tyne";

	me->subtype[2] = machine_entry_subtype_new(
	    "Jazz Magnum", MACHINE_ARC_JAZZ_MAGNUM, 2);
	me->subtype[2]->aliases[0] = "magnum";
	me->subtype[2]->aliases[1] = "jazz magnum";

	me->subtype[3] = machine_entry_subtype_new(
	    "NEC-R94", MACHINE_ARC_NEC_R94, 2);
	me->subtype[3]->aliases[0] = "nec-r94";
	me->subtype[3]->aliases[1] = "r94";

	me->subtype[4] = machine_entry_subtype_new(
	    "NEC-RD94", MACHINE_ARC_NEC_RD94, 2);
	me->subtype[4]->aliases[0] = "nec-rd94";
	me->subtype[4]->aliases[1] = "rd94";

	me->subtype[5] = machine_entry_subtype_new(
	    "NEC-R96", MACHINE_ARC_NEC_R96, 2);
	me->subtype[5]->aliases[0] = "nec-r96";
	me->subtype[5]->aliases[1] = "r96";

	me->subtype[6] = machine_entry_subtype_new(
	    "NEC-R98", MACHINE_ARC_NEC_R98, 2);
	me->subtype[6]->aliases[0] = "nec-r98";
	me->subtype[6]->aliases[1] = "r98";

	me->subtype[7] = machine_entry_subtype_new(
	    "Olivetti M700", MACHINE_ARC_JAZZ_M700, 2);
	me->subtype[7]->aliases[0] = "olivetti";
	me->subtype[7]->aliases[1] = "m700";

	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Alpha:  */
	me = machine_entry_new("Alpha", ARCH_ALPHA, MACHINE_ALPHA, 1, 2);
	me->aliases[0] = "alpha";
	me->subtype[0] = machine_entry_subtype_new(
	    "DEC 3000/300", ST_DEC_3000_300, 1);
	me->subtype[0]->aliases[0] = "3000/300";
	me->subtype[1] = machine_entry_subtype_new(
	    "EB164", ST_EB164, 1);
	me->subtype[1]->aliases[0] = "eb164";
	if (cpu_family_ptr_by_number(ARCH_ALPHA) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}

	/*  Algor evaluation board:  */
	me = machine_entry_new("Algor", ARCH_MIPS, MACHINE_ALGOR, 1, 2);
	me->aliases[0] = "algor";
	me->subtype[0] = machine_entry_subtype_new("P4032",
	    MACHINE_ALGOR_P4032, 1);
	me->subtype[0]->aliases[0] = "p4032";
	me->subtype[1] = machine_entry_subtype_new("P5064",
	    MACHINE_ALGOR_P5064, 1);
	me->subtype[1]->aliases[0] = "p5064";
	if (cpu_family_ptr_by_number(ARCH_MIPS) != NULL) {
		me->next = first_machine_entry; first_machine_entry = me;
	}
}

