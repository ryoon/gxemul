/*
 *  Copyright (C) 2003-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: machine.c,v 1.662 2006-01-14 11:29:34 debug Exp $
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
#include "machine_interrupts.h"
#include "memory.h"
#include "misc.h"
#include "net.h"
#include "symbol.h"

/*  For SGI and ARC emulation:  */
#include "sgi_arcbios.h"
#include "crimereg.h"

#define	BOOTSTR_BUFLEN		1000
#define	ETHERNET_STRING_MAXLEN	40


/*  See main.c:  */
extern int quiet_mode;
extern int verbose;


/*  This is initialized by machine_init():  */
struct machine_entry *first_machine_entry = NULL;


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
		debug("  (nr of NICs: %i)", m->nr_of_nics);
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
	size_t psize = 1024;	/*  1024 256 64 16 4 1  */

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
	int i, j;
	struct memory *mem;
	char tmpstr[1000];
	struct machine_entry *me;

	/*  ARCBIOS stuff:  */
	uint64_t sgi_ram_offset = 0;
	int arc_wordlen = sizeof(uint32_t);
	char *eaddr_string = "eaddr=10:20:30:40:50:60";   /*  nonsense  */
	unsigned char macaddr[6];

	/*  Generic bootstring stuff:  */
	char *init_bootpath;

	/*  PCI stuff:  */
	struct pci_data *pci_data = NULL;

	/*  Abreviation:  :-)  */
	struct cpu *cpu = machine->cpus[machine->bootstrap_cpu];


	machine->bootdev_id = diskimage_bootdev(machine,
	    &machine->bootdev_type);

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
		case MACHINE_PMAX:
			machine->boot_string_argument = "-a";
			break;
		default:
			/*  Important, because boot_string_argument should
			    not be set to NULL:  */
			machine->boot_string_argument = "";
		}
	}


	/*
	 *  If the machine has a setup function in src/machines/machine_*.c
	 *  then use that one, otherwise use the old hardcoded stuff here:
	 */

	me = first_machine_entry;
	while (me != NULL) {
		if (machine->machine_type == me->machine_type &&
		    me->setup != NULL) {
			me->setup(machine, cpu);
			goto machine_setup_done;
		}
		me = me->next;
	}


	/*
	 *  Old-style setup:
	 */

	switch (machine->machine_type) {

	case MACHINE_NONE:
		printf("\nNo emulation type specified.\n");
		exit(1);

#ifdef ENABLE_MIPS
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
				machine->stable = 1;

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
					machine->stable = 1;
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

				/*  TODO: IRQs!  */
				bus_isa_init(machine, 0, 0x900000000ULL,
				    0x100000000ULL, 8, 24);
#if 0
				snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=0 addr=0x9000003f8 in_use=%i name2=tty0", machine->use_x11? 0 : 1);
				i = (size_t)device_add(machine, tmpstr);
				device_add(machine, "ns16550 irq=0 addr=0x9000002f8 in_use=0 name2=tty1");
#endif
				device_add(machine, "ns16550 irq=0 addr=0x9000003e8 in_use=0 name2=tty2");
				device_add(machine, "ns16550 irq=0 addr=0x9000002e8 in_use=0 name2=tty3");
#if 0
				dev_mc146818_init(machine, mem,
				    0x900000070ULL, 2, MC146818_PC_CMOS, 1);
				/*  TODO: irq, etc  */
				device_add(machine, "wdc addr=0x9000001f0, irq=0");
				device_add(machine, "wdc addr=0x900000170, irq=0");

				/*  PC kbd  */
				j = dev_pckbc_init(machine, mem, 0x900000060ULL,
				    PCKBC_8042, 0, 0, machine->use_x11, 0);

				if (machine->use_x11)
					machine->main_console_handle = j;
				else
					machine->main_console_handle = i;
#endif

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

			if (machine->bootdev_id < 0 || machine->force_netboot) {
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

				if (diskimage_is_a_cdrom(machine, machine->bootdev_id,
				    machine->bootdev_type))
					snprintf(init_bootpath + strlen(init_bootpath),
					    400,"scsi(0)cdrom(%i)fdisk(0)", machine->bootdev_id);
				else
					snprintf(init_bootpath + strlen(init_bootpath),
					    400,"scsi(0)disk(%i)rdisk(0)partition(1)",
					    machine->bootdev_id);
			}

			if (machine->machine_type == MACHINE_ARC)
				strlcat(init_bootpath, "\\", MACHINE_NAME_MAXBUF);

			machine->bootstr = malloc(BOOTSTR_BUFLEN);
			if (machine->bootstr == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			strlcpy(machine->bootstr, init_bootpath, BOOTSTR_BUFLEN);
			if (strlcat(machine->bootstr, machine->boot_kernel_filename,
			    BOOTSTR_BUFLEN) >= BOOTSTR_BUFLEN) {
				fprintf(stderr, "boot string too long?\n");
				exit(1);
			}

			/*  Boot args., eg "-a"  */
			machine->bootarg = machine->boot_string_argument;

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
			add_environment_string(cpu, machine->bootstr, &addr);
			cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

			/*  bootarg:  */
			if (machine->bootarg[0] != '\0') {
				store_pointer_and_advance(cpu, &addr2, addr, arc_wordlen==sizeof(uint64_t));
				add_environment_string(cpu, machine->bootarg, &addr);
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
				size_t mlen = strlen(machine->bootarg) + strlen("OSLOADOPTIONS=") + 2;
				tmp = malloc(mlen);
				snprintf(tmp, mlen, "OSLOADOPTIONS=%s", machine->bootarg);
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
#endif	/*  ENABLE_MIPS  */

	default:
		fatal("Unknown emulation type %i\n", machine->machine_type);
		exit(1);
	}


	/*
	 *  NOTE: Ugly goto usage. Might be possible to remove if/when all
	 *        machines are moved to src/machines/machine_*.c.
	 */
machine_setup_done:

	if (machine->machine_name != NULL)
		debug("machine: %s", machine->machine_name);

	if (machine->emulated_hz > 0)
		debug(" (%.2f MHz)", (float)machine->emulated_hz / 1000000);
	debug("\n");

	/*  Default fake speed: 5 MHz  */
	if (machine->emulated_hz < 1)
		machine->emulated_hz = 5000000;

	if (machine->bootstr != NULL) {
		debug("bootstring%s: %s", (machine->bootarg!=NULL &&
		    strlen(machine->bootarg) >= 1)? "(+bootarg)" : "", machine->bootstr);
		if (machine->bootarg != NULL && strlen(machine->bootarg) >= 1)
			debug(" %s", machine->bootarg);
		debug("\n");
	}

	if (verbose >= 2)
		machine_dump_bus_info(machine);

	if (!machine->stable)
		fatal("!\n!  NOTE: This machine type is not implemented well"
		    " enough yet to run\n!  any real-world code!"
		    " (At least, it hasn't been verified to do so.)\n!\n"
		    "!  Please read the GXemul documentation for information"
		    " about which\n!  machine types that actually work.\n!\n");
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
		struct machine_entry *me = first_machine_entry;
		while (me != NULL) {
			if (m->machine_type == me->machine_type &&
			    me->set_default_ram != NULL) {
				me->set_default_ram(m);
				goto default_ram_done;
			}
			me = me->next;
		}

		switch (m->machine_type) {
		case MACHINE_SGI:
			m->physical_ram_in_mb = 64;
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
		}
	}

default_ram_done:

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
	struct machine_entry *me;

	if (m == NULL) {
		fatal("machine_default_cputype(): m == NULL?\n");
		exit(1);
	}

	if (m->cpu_name != NULL)
		return;

	me = first_machine_entry;
	while (me != NULL) {
		if (m->machine_type == me->machine_type &&
		    me->set_default_cpu != NULL) {
			me->set_default_cpu(m);
			goto default_cpu_done;
		}
		me = me->next;
	}

	switch (m->machine_type) {
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
	}

default_cpu_done:

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
 */
struct machine_entry *machine_entry_new(const char *name,
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
	me->setup = NULL;

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
struct machine_entry_subtype *machine_entry_subtype_new(
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
 *  machine_entry_add():
 *
 *  Inserts a new machine_entry into the machine entries list.
 */
void machine_entry_add(struct machine_entry *me, int arch)
{
	struct machine_entry *prev, *next;

	/*  Only insert it if the architecture is implemented in this
	    emulator configuration:  */
	if (cpu_family_ptr_by_number(arch) == NULL)
		return;

	prev = NULL;
	next = first_machine_entry;

	for (;;) {
		if (next == NULL)
			break;
		if (strcasecmp(me->name, next->name) < 0)
			break;

		prev = next;
		next = next->next;
	}

	if (prev != NULL)
		prev->next = me;
	else
		first_machine_entry = me;
	me->next = next;
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
	 *  First, add all machines in src/machines/machine_*.c:
	 */

	automachine_init();


	/*
	 *  The following are old-style hardcoded machine definitions:
	 *
	 *  TODO: Move these to individual files in src/machines/.
	 */

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

	machine_entry_add(me, ARCH_MIPS);


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
	machine_entry_add(me, ARCH_MIPS);
}

