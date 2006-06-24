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
 *  $Id: machine.c,v 1.673 2006-06-24 19:52:27 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#ifdef SOLARIS
/*  TODO: is this strings vs string separation really necessary?  */
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
#include "debugger.h"
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
 *
 *  If tickshift is non-zero, a tick will occur every (1 << tickshift) cycles.
 *  This is used for the normal (fast dyntrans) emulation modes.
 *
 *  If tickshift is zero, then this is a cycle-accurate tick function.
 *  The hz value is used in this case.
 */
void machine_add_tickfunction(struct machine *machine, void (*func)
	(struct cpu *, void *), void *extra, int tickshift, double hz)
{
	int n = machine->n_tick_entries;

	if (n >= MAX_TICK_FUNCTIONS) {
		fprintf(stderr, "machine_add_tickfunction(): too "
		    "many tick functions\n");
		exit(1);
	}

	if (!machine->cycle_accurate) {
		/*
		 *  The dyntrans subsystem wants to run code in relatively
		 *  large chunks without checking for external interrupts,
		 *  so we cannot allow too low tickshifts:
		 */
		if (tickshift < N_SAFE_DYNTRANS_LIMIT_SHIFT) {
			fatal("ERROR! tickshift = %i, less than "
			    "N_SAFE_DYNTRANS_LIMIT_SHIFT (%i)\n",
			    tickshift, N_SAFE_DYNTRANS_LIMIT_SHIFT);
			exit(1);
		}
	}

	machine->ticks_till_next[n]   = 0;
	machine->ticks_reset_value[n] = 1 << tickshift;
	machine->tick_func[n]         = func;
	machine->tick_extra[n]        = extra;
	machine->tick_hz[n]           = hz;

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
 *  target address. (Useful for e.g. ARCBIOS environment initialization.)
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


/*
 *  machine_setup():
 *
 *  This (rather large) function initializes memory, registers, and/or devices
 *  required by specific machine emulations.
 */
void machine_setup(struct machine *machine)
{
	struct memory *mem;
	struct machine_entry *me;

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
			break;
		}
		me = me->next;
	}

	if (me == NULL) {
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

	if (machine->bootstr != NULL) {
		debug("bootstring%s: %s", (machine->bootarg!=NULL &&
		    strlen(machine->bootarg) >= 1)? "(+bootarg)" : "",
		    machine->bootstr);
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
				break;
			}
			me = me->next;
		}
	}

	/*  Special hack for hpcmips machines:  */
	if (m->machine_type == MACHINE_HPCMIPS) {
		m->dbe_on_nonexistant_memaccess = 0;
	}

	/*  Special SGI memory offsets:  (TODO: move this somewhere else)  */
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
 *  Sets m->cpu_name, if it isn't already set, depending on the machine type.
 */
void machine_default_cputype(struct machine *m)
{
	struct machine_entry *me;

	if (m == NULL) {
		fatal("machine_default_cputype(): m == NULL?\n");
		exit(1);
	}

	/*  Already set? Then return.  */
	if (m->cpu_name != NULL)
		return;

	me = first_machine_entry;
	while (me != NULL) {
		if (m->machine_type == me->machine_type &&
		    me->set_default_cpu != NULL) {
			me->set_default_cpu(m);
			break;
		}
		me = me->next;
	}

	if (m->cpu_name == NULL) {
		fprintf(stderr, "machine_default_cputype(): no default"
		    " cpu for machine type %i subtype %i\n",
		    m->machine_type, m->machine_subtype);
		exit(1);
	}
}


/*****************************************************************************/


/*
 *  machine_run():
 *
 *  Run one or more instructions on all CPUs in this machine. (Usually,
 *  around N_SAFE_DYNTRANS_LIMIT instructions will be run by the dyntrans
 *  system.)
 *
 *  Return value is 1 if any CPU in this machine is still running,
 *  or 0 if all CPUs are stopped.
 */
int machine_run(struct machine *machine)
{
	struct cpu **cpus = machine->cpus;
	int ncpus = machine->ncpus, cpu0instrs = 0, i, te;

	for (i=0; i<ncpus; i++) {
		if (cpus[i]->running) {
			int instrs_run = machine->cpu_family->run_instr(
			    machine->emul, cpus[i]);
			if (i == 0)
				cpu0instrs += instrs_run;
		}
	}

	/*
	 *  Hardware 'ticks':  (clocks, interrupt sources...)
	 *
	 *  Here, cpu0instrs is the number of instructions
	 *  executed on cpu0.  (TODO: don't use cpu 0 for this,
	 *  use some kind of "mainbus" instead.)
	 */

	machine->ncycles += cpu0instrs;

	for (te=0; te<machine->n_tick_entries; te++) {
		machine->ticks_till_next[te] -= cpu0instrs;
		if (machine->ticks_till_next[te] <= 0) {
			while (machine->ticks_till_next[te] <= 0) {
				machine->ticks_till_next[te] +=
				    machine->ticks_reset_value[te];
			}

			machine->tick_func[te](cpus[0],
			    machine->tick_extra[te]);
		}
	}

	/*  Is any CPU still alive?  */
	for (i=0; i<ncpus; i++)
		if (cpus[i]->running)
			return 1;

	return 0;
}


/*****************************************************************************/


/*
 *  machine_entry_new():
 *
 *  This function creates a new machine_entry struct, and fills it with some
 *  valid data; it is up to the caller to add additional data that weren't
 *  passed as arguments to this function, such as alias names and machine
 *  subtypes.
 */
struct machine_entry *machine_entry_new(const char *name, int arch,
	int oldstyle_type)
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
	me->n_aliases = 0;
	me->aliases = NULL;
	me->n_subtypes = 0;
	me->setup = NULL;

	return me;
}


/*
 *  machine_entry_add_alias():
 *
 *  This function adds an "alias" to a machine entry.
 */
void machine_entry_add_alias(struct machine_entry *me, const char *name)
{
	me->n_aliases ++;
	me->aliases = realloc(me->aliases, sizeof(char *) * me->n_aliases);
	if (me->aliases == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	me->aliases[me->n_aliases - 1] = (char *) name;
}


/*
 *  machine_entry_add_subtype():
 *
 *  This function adds a subtype to a machine entry. The argument list after
 *  oldstyle_subtype is a list of one or more char *, followed by NULL. E.g.:
 *
 *	machine_entry_add_subtype(me, "Machine X", MACHINE_X,
 *	    "machine-x", "x", NULL);
 */
void machine_entry_add_subtype(struct machine_entry *me, const char *name,
	int oldstyle_subtype, ...)
{
	va_list argp;
	struct machine_entry_subtype *mes;

	/*  Allocate a new subtype struct:  */
	mes = malloc(sizeof(struct machine_entry_subtype));
	if (mes == NULL) {
		fprintf(stderr, "machine_entry_subtype_new(): out "
		    "of memory (1)\n");
		exit(1);
	}

	/*  Add the subtype to the machine entry:  */
	me->n_subtypes ++;
	me->subtype = realloc(me->subtype, sizeof(struct
	    machine_entry_subtype *) * me->n_subtypes);
	if (me->subtype == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	me->subtype[me->n_subtypes - 1] = mes;

	/*  Fill the struct with subtype data:  */
	memset(mes, 0, sizeof(struct machine_entry_subtype));
	mes->name = name;
	mes->machine_subtype = oldstyle_subtype;

	/*  ... and all aliases:  */
	mes->n_aliases = 0;
	mes->aliases = NULL;

	va_start(argp, oldstyle_subtype);

	for (;;) {
		char *s = va_arg(argp, char *);
		if (s == NULL)
			break;

		mes->n_aliases ++;
		mes->aliases = realloc(mes->aliases, sizeof(char *) *
		    mes->n_aliases);
		if (mes->aliases == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}

		mes->aliases[mes->n_aliases - 1] = s;
	}

	va_end(argp);
}


/*
 *  machine_entry_register():
 *
 *  Inserts a new machine_entry into the machine entries list.
 */
void machine_entry_register(struct machine_entry *me, int arch)
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

		debug("%s [%s] (", me->name,
		    cpu_family_ptr_by_number(me->arch)->name);
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

#ifdef UNSTABLE_DEVEL
	debug("\n");

	useremul_list_emuls();
	debug("Userland emulation works for programs with the complexity"
	    " of Hello World,\nbut not much more.\n");
#endif
}


/*
 *  machine_init():
 *
 *  This function should be called before any other machine_*() function
 *  is used.  automachine_init() registers all machines in src/machines/.
 */
void machine_init(void)
{
	if (first_machine_entry != NULL) {
		fatal("machine_init(): already called?\n");
		exit(1);
	}

	automachine_init();
}

