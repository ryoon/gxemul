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
 *  $Id: cpu.c,v 1.100 2004-07-08 00:40:17 debug Exp $
 *
 *  MIPS core CPU emulation.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "bintrans.h"
#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"

#include "dec_5100.h"
#include "dec_kn02.h"

extern int emulation_type;
extern int machine;

extern int show_trace_tree;
extern int emulated_hz;
extern int bintrans_enable;
extern int register_dump;
extern int instruction_trace;
extern int show_nr_of_instructions;
extern int quiet_mode;
extern int use_x11;
extern int speed_tricks;
extern int prom_emulation;
extern int tlb_dump;
extern int userland_emul;
extern int bootstrap_cpu;
extern int64_t max_instructions;
extern struct cpu **cpus;
extern int ncpus;
extern int show_opcode_statistics;
extern int automatic_clock_adjustment;
extern int n_dumppoints;
extern uint64_t dumppoint_pc[MAX_PC_DUMPPOINTS];
extern int dumppoint_flag_r[MAX_PC_DUMPPOINTS];

char *exception_names[] = EXCEPTION_NAMES;

static char *hi6_names[] = HI6_NAMES;
static char *regimm_names[] = REGIMM_NAMES;
static char *special_names[] = SPECIAL_NAMES;
static char *special2_names[] = SPECIAL2_NAMES;


#include "memory.c"


/*
 *  cpu_new():
 *
 *  Create a new cpu object.
 */
struct cpu *cpu_new(struct memory *mem, int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;
	int i, j, tags_size, n_cache_lines, size_per_cache_line;
	struct cpu_type_def cpu_type_defs[] = CPU_TYPE_DEFS;

	assert(mem != NULL);

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->mem                = mem;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_LITTLE_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;
	cpu->gpr[GPR_SP]	= INITIAL_STACK_POINTER;

	/*  Scan the cpu_type_defs list for this cpu type:  */
	i = 0;
	while (i >= 0 && cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			cpu->cpu_type = cpu_type_defs[i];
			i = -1;
			break;
		}
		i++;
	}

	if (i != -1) {
		fprintf(stderr, "cpu_new(): unknown cpu type '%s'\n",
		    cpu_type_name);
		exit(1);
	}

	/*
	 *  Data and Instruction caches:
	 *
	 *  TODO: These should be configurable at runtime, perhaps.
	 */
	for (i=CACHE_DATA; i<=CACHE_INSTRUCTION; i++) {
		switch (cpu->cpu_type.rev) {
		case MIPS_R2000:
		case MIPS_R3000:
			cpu->cache_size[i] = 65536;
			cpu->cache_linesize[i] = 4;
			size_per_cache_line = sizeof(struct r3000_cache_line);
			break;
		default:
			cpu->cache_size[i] = 32768;
			cpu->cache_linesize[i] = 32;
			size_per_cache_line = sizeof(struct r4000_cache_line);
		}

		cpu->cache_mask[i] = cpu->cache_size[i] - 1;
		cpu->cache_miss_penalty[i] = 10;	/*  TODO ?  */

		cpu->cache[i] = malloc(cpu->cache_size[i]);
		if (cpu->cache[i] == NULL) {
			fprintf(stderr, "out of memory\n");
		}

		n_cache_lines = cpu->cache_size[i] / cpu->cache_linesize[i];
		tags_size = n_cache_lines * size_per_cache_line;

		cpu->cache_tags[i] = malloc(tags_size);
		if (cpu->cache_tags[i] == NULL) {
			fprintf(stderr, "out of memory\n");
		}

		/*  Initialize the cache tags:  */
		switch (cpu->cpu_type.rev) {
		case MIPS_R2000:
		case MIPS_R3000:
			for (j=0; j<n_cache_lines; j++) {
				struct r3000_cache_line *rp;
				rp = (struct r3000_cache_line *)
				    cpu->cache_tags[i];
				rp[j].tag_paddr = 0;
				rp[j].tag_valid = 0;
			}
			break;
		default:
			;
		}

		/*  Set cache_last_paddr to something "impossible":  */
		cpu->cache_last_paddr[i] = IMPOSSIBLE_PADDR;
	}

	cpu->coproc[0] = coproc_new(cpu, 0);	/*  System control, MMU  */
	cpu->coproc[1] = coproc_new(cpu, 1);	/*  FPU  */

	/*
	 *  Initialize the cpu->pc_last_* cache (a 1-entry cache of the
	 *  last program counter value).  For pc_last_virtual_page, any
	 *  "impossible" value will do.  The pc should never ever get this
	 *  value.  (The other pc_last* variables do not need initialization,
	 *  as they are not used before pc_last_virtual_page.)
	 */
	cpu->pc_last_virtual_page = PC_LAST_PAGE_IMPOSSIBLE_VALUE;

	return cpu;
}


/*
 *  cpu_add_tickfunction():
 *
 *  Adds a tick function (a function called every now and then, depending on
 *  clock cycle count).
 */
void cpu_add_tickfunction(struct cpu *cpu, void (*func)(struct cpu *, void *),
	void *extra, int clockshift)
{
	int n = cpu->n_tick_entries;

	if (n >= MAX_TICK_FUNCTIONS) {
		fprintf(stderr, "cpu_add_tickfunction(): too many tick functions\n");
		exit(1);
	}

	cpu->ticks_till_next[n]   = 0;
	cpu->ticks_reset_value[n] = 1 << clockshift;
	cpu->tick_func[n]         = func;
	cpu->tick_extra[n]        = extra;

	cpu->n_tick_entries ++;
}


/*
 *  cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 */
void cpu_register_dump(struct cpu *cpu)
{
	int i, offset;
	char *symbol;

	symbol = get_symbol_name(cpu->pc, &offset);

	debug("cpu%i:  hi  = %016llx  lo  = %016llx  pc  = %016llx",
	    cpu->cpu_id, cpu->hi, cpu->lo, cpu->pc);
	if (symbol != NULL)
		debug(" <%s>", symbol);
	debug("\n");
	for (i=0; i<32; i++) {
		if ((i & 3) == 0)
			debug("cpu%i:", cpu->cpu_id);
		debug("  r%02i = %016llx", i, cpu->gpr[i]);
		if ((i & 3) == 3)
			debug("\n");
	}
}


/*
 *  show_trace():
 *
 *  Show trace tree.   This function should be called every time
 *  a function is called.  cpu->trace_tree_depth is increased here
 *  and should not be increased by the caller.
 *
 *  Note:  This function should not be called if show_trace_tree == 0.
 */
void show_trace(struct cpu *cpu, uint64_t addr)
{
	int offset, x;
	int n_args_to_print;
	char strbuf[50];
	char *symbol;

	cpu->trace_tree_depth ++;

	symbol = get_symbol_name(addr, &offset);

	for (x=0; x<cpu->trace_tree_depth; x++)
		debug("  ");

	/*  debug("<%s>\n", symbol!=NULL? symbol : "no symbol");  */

	if (symbol != NULL)
		debug("<%s(", symbol);
	else
		debug("<0x%016llx(", (long long)addr);

	/*
	 *  TODO:  The number of arguments and the symbol type of each
	 *  argument should be taken from the symbol table, in some way.
	 *
	 *  The MIPS binary calling convention is that the first 4
	 *  arguments are in registers a0..a3.
	 *
	 *  Choose a value greater than 4 (eg 5) to print all values in
	 *  the A0..A3 registers and then add a ".." to indicate that
	 *  there might be more arguments.
	 */
	n_args_to_print = 5;

	for (x=0; x<n_args_to_print; x++) {
		int64_t d = cpu->gpr[x + GPR_A0];

		if (d > -256 && d < 256)
			debug("%i", (int)d);
		else if (memory_points_to_string(cpu, cpu->mem, d, 1))
			debug("\"%s\"", memory_conv_to_string(cpu, cpu->mem, d, strbuf, sizeof(strbuf)));
		else
			debug("0x%llx", (long long)d);

		if (x < n_args_to_print - 1)
			debug(",");

		/*  Cannot go beyound GPR_A3:  */
		if (x == 3)
			break;
	}

	if (n_args_to_print > 4)
		debug("..");

	debug(")>\n");
}


/*
 *  cpu_interrupt():
 *
 *  Cause an interrupt. If irq_nr is 2..7, then it is a MIPS interrupt.
 *  0 and 1 are ignored (software interrupts).
 *
 *  If it is >= 8, then it is an external (machine dependant) interrupt.
 *  cpu->md_interrupt() is called. That function may call cpu_interrupt()
 *  using low values (2..7). There's no actual check to make sure that
 *  there's no recursion, so the md_interrupt routine has to make sure of
 *  this.
 */
int cpu_interrupt(struct cpu *cpu, int irq_nr)
{
	if (irq_nr >= 8) {
		if (cpu->md_interrupt != NULL)
			cpu->md_interrupt(cpu, irq_nr, 1);
		else
			fatal("cpu_interrupt(): irq_nr = %i, but md_interrupt = NULL ?\n", irq_nr);
		return 1;
	}

	if (irq_nr < 2)
		return 0;

	cpu->coproc[0]->reg[COP0_CAUSE] |= ((1 << irq_nr) << STATUS_IM_SHIFT);
	return 1;
}


/*
 *  cpu_interrupt_ack():
 *
 *  Acknowledge an interrupt. If irq_nr is 2..7, then it is a MIPS interrupt.
 *  If it is >= 8, then it is machine dependant.
 */
int cpu_interrupt_ack(struct cpu *cpu, int irq_nr)
{
	if (irq_nr >= 8) {
		if (cpu->md_interrupt != NULL)
			cpu->md_interrupt(cpu, irq_nr, 0);
		else
			fatal("cpu_interrupt_ack(): irq_nr = %i, but md_interrupt = NULL ?\n", irq_nr);
		return 1;
	}

	if (irq_nr < 2)
		return 0;

	cpu->coproc[0]->reg[COP0_CAUSE] &= ~((1 << irq_nr) << STATUS_IM_SHIFT);
	return 1;
}


/*
 *  cpu_exception():
 *
 *  Cause an exception in a CPU.  This sets a couple of coprocessor 0
 *  registers, and the program counter.
 *
 *	exccode		the exception code
 *	tlb		set to non-zero if the exception handler at
 *			0x80000000 should be used. (normal = 0x80000180)
 *	vaddr		virtual address (for some exceptions)
 *	coproc_nr	coprocessor number (for some exceptions)
 *	vaddr_vpn2	vpn2 (for some exceptions)
 *	vaddr_asid	asid (for some exceptions)
 *	x_64		non-zero for 64-bit mode for R4000-style tlb misses
 */
void cpu_exception(struct cpu *cpu, int exccode, int tlb, uint64_t vaddr,
	int coproc_nr, uint64_t vaddr_vpn2, int vaddr_asid, int x_64)
{
	int offset, x;
	char *symbol = "";

	if (!quiet_mode) {
		symbol = get_symbol_name(cpu->pc_last, &offset);

		debug("[ exception %s%s",
		    exception_names[exccode], tlb? " <tlb>" : "");

		switch (exccode) {
		case EXCEPTION_INT:
			debug(" cause_im=0x%02x", (int)((cpu->coproc[0]->reg[COP0_CAUSE] & CAUSE_IP_MASK) >> CAUSE_IP_SHIFT));
			break;
		case EXCEPTION_SYS:
			debug(" v0=%i", (int)cpu->gpr[GPR_V0]);
			for (x=0; x<4; x++) {
				int64_t d = cpu->gpr[GPR_A0 + x];
				char strbuf[30];

				if (d > -256 && d < 256)
					debug(" a%i=%i", x, (int)d);
				else if (memory_points_to_string(cpu, cpu->mem, d, 1))
					debug(" a%i=\"%s\"", x, memory_conv_to_string(cpu, cpu->mem, d, strbuf, sizeof(strbuf)));
				else
					debug(" a%i=0x%llx", x, (long long)d);
			}
			break;
		default:
			debug(" vaddr=%08llx", (long long)vaddr);
		}

		debug(" pc->last=%08llx <%s> ]\n",
		    (long long)cpu->pc_last, symbol? symbol : "(no symbol)");
	}

	if (tlb && vaddr == 0) {
		symbol = get_symbol_name(cpu->pc_last, &offset);
		fatal("warning: NULL reference, exception %s, pc->last=%08llx <%s>\n",
		    exception_names[exccode], (long long)cpu->pc_last, symbol? symbol : "(no symbol)");
/*		tlb_dump = 1;  */
	}

	if (vaddr != 0 && vaddr < 0x1000) {
		symbol = get_symbol_name(cpu->pc_last, &offset);
		fatal("warning: LOW reference vaddr=0x%08x, exception %s, pc->last=%08llx <%s>\n",
		    (int)vaddr, exception_names[exccode], (long long)cpu->pc_last, symbol? symbol : "(no symbol)");
/*		tlb_dump = 1;  */
	}

	/*  Clear the exception code bits of the cause register...  */
	if (cpu->cpu_type.exc_model == EXC3K) {
		cpu->coproc[0]->reg[COP0_CAUSE] &= ~R2K3K_CAUSE_EXCCODE_MASK;
		if (exccode >= 16) {
			fatal("exccode = %i  (there are only 16 exceptions on R3000 and lower)\n", exccode);
			cpu->running = 0;
			return;
		}
	} else
		cpu->coproc[0]->reg[COP0_CAUSE] &= ~CAUSE_EXCCODE_MASK;

	/*  ... and OR in the exception code:  */
	cpu->coproc[0]->reg[COP0_CAUSE] |= (exccode << CAUSE_EXCCODE_SHIFT);

	/*  Always set CE (according to the R5000 manual):  */
	cpu->coproc[0]->reg[COP0_CAUSE] &= ~CAUSE_CE_MASK;
	cpu->coproc[0]->reg[COP0_CAUSE] |= (coproc_nr << CAUSE_CE_SHIFT);

	/*  TODO:  On R4000, vaddr should NOT be set on bus errors!!!  */
#if 0
	if (exccode == EXCEPTION_DBE) {
		cpu->coproc[0]->reg[COP0_BADVADDR] = vaddr;
		/*  sign-extend vaddr, if it is 32-bit  */
		if ((vaddr >> 32) == 0 && (vaddr & 0x80000000ULL))
			cpu->coproc[0]->reg[COP0_BADVADDR] |=
			    0xffffffff00000000ULL;
	}
#endif

	if ((exccode >= EXCEPTION_MOD && exccode <= EXCEPTION_ADES) ||
	    exccode == EXCEPTION_VCEI || exccode == EXCEPTION_VCED || tlb) {
		cpu->coproc[0]->reg[COP0_BADVADDR] = vaddr;
		/*  sign-extend vaddr, if it is 32-bit  */
		if ((vaddr >> 32) == 0 && (vaddr & 0x80000000ULL))
			cpu->coproc[0]->reg[COP0_BADVADDR] |=
			    0xffffffff00000000ULL;

		if (cpu->cpu_type.exc_model == EXC3K) {
			cpu->coproc[0]->reg[COP0_CONTEXT] &= ~R2K3K_CONTEXT_BADVPN_MASK;
			cpu->coproc[0]->reg[COP0_CONTEXT] |= ((vaddr_vpn2 << R2K3K_CONTEXT_BADVPN_SHIFT) & R2K3K_CONTEXT_BADVPN_MASK);

			cpu->coproc[0]->reg[COP0_ENTRYHI] = (vaddr & R2K3K_ENTRYHI_VPN_MASK)
			    + (vaddr_asid << R2K3K_ENTRYHI_ASID_SHIFT);
		} else {
			cpu->coproc[0]->reg[COP0_CONTEXT] &= ~CONTEXT_BADVPN2_MASK;
			cpu->coproc[0]->reg[COP0_CONTEXT] |= ((vaddr_vpn2 << CONTEXT_BADVPN2_SHIFT) & CONTEXT_BADVPN2_MASK);

			cpu->coproc[0]->reg[COP0_XCONTEXT] &= ~XCONTEXT_R_MASK;
			cpu->coproc[0]->reg[COP0_XCONTEXT] &= ~XCONTEXT_BADVPN2_MASK;
			cpu->coproc[0]->reg[COP0_XCONTEXT] |= (vaddr_vpn2 << XCONTEXT_BADVPN2_SHIFT) & XCONTEXT_BADVPN2_MASK;
			cpu->coproc[0]->reg[COP0_XCONTEXT] |= ((vaddr >> 62) & 0x3) << XCONTEXT_R_SHIFT;

			/*  cpu->coproc[0]->reg[COP0_PAGEMASK] = cpu->coproc[0]->tlbs[0].mask & PAGEMASK_MASK;  */

			if (cpu->cpu_type.mmu_model == MMU10K)
				cpu->coproc[0]->reg[COP0_ENTRYHI] = (vaddr & (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK_R10K)) | vaddr_asid;
			else
				cpu->coproc[0]->reg[COP0_ENTRYHI] = (vaddr & (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK)) | vaddr_asid;
		}
	}

	if (cpu->cpu_type.exc_model == EXC4K && cpu->coproc[0]->reg[COP0_STATUS] & STATUS_EXL) {
		/*  Don't set EPC if STATUS_EXL is set, for R4000  */
		fatal("warning: cpu%i exception while EXL is set, not setting EPC!\n", cpu->cpu_id);
	} else {
		if (cpu->delay_slot) {
			cpu->coproc[0]->reg[COP0_EPC] = cpu->pc_last - 4;
			cpu->coproc[0]->reg[COP0_CAUSE] |= CAUSE_BD;
		} else {
			cpu->coproc[0]->reg[COP0_EPC] = cpu->pc_last;
			cpu->coproc[0]->reg[COP0_CAUSE] &= ~CAUSE_BD;
		}
	}

	cpu->delay_slot = NOT_DELAYED;

	if (cpu->cpu_type.exc_model == EXC3K) {
		/*  Userspace tlb, vs others:  */
		if (tlb && !(vaddr & 0x80000000ULL) &&
		    (exccode == EXCEPTION_TLBL || exccode == EXCEPTION_TLBS) )
			cpu->pc = 0xffffffff80000000ULL;
		else
			cpu->pc = 0xffffffff80000080ULL;
	} else {
		/*  R4000:  */
		if (tlb && (exccode == EXCEPTION_TLBL || exccode == EXCEPTION_TLBS)
		    && !(cpu->coproc[0]->reg[COP0_STATUS] & STATUS_EXL)) {
			if (x_64)
				cpu->pc = 0xffffffff80000080ULL;
			else
				cpu->pc = 0xffffffff80000000ULL;
		} else
			cpu->pc = 0xffffffff80000180ULL;
	}

	if (cpu->cpu_type.exc_model == EXC3K) {
		/*  R2000/R3000:  Shift the lowest 6 bits to the left two steps:  */
		cpu->coproc[0]->reg[COP0_STATUS] =
		    (cpu->coproc[0]->reg[COP0_STATUS] & ~0x3f) +
		    ((cpu->coproc[0]->reg[COP0_STATUS] & 0xf) << 2);
	} else {
		/*  R4000:  */
		cpu->coproc[0]->reg[COP0_STATUS] |= STATUS_EXL;
	}
}


/*
 *  cpu_flags():
 *
 *  Returns a pointer to a string containing "(d)" "(j)" "(dj)" or "",
 *  depending on the cpu's current delay_slot and last_was_jumptoself
 *  flags.
 */
const char *cpu_flags(struct cpu *cpu)
{
	if (cpu->delay_slot) {
		if (cpu->last_was_jumptoself)
			return " (dj)";
		else
			return " (d)";
	} else {
		if (cpu->last_was_jumptoself)
			return " (j)";
		else
			return "";
	}
}


/*
 *  cpu_run_instr():
 *
 *  Execute one instruction on a cpu.
 *
 *  If we are in a delay slot, set cpu->pc to cpu->delay_jmpaddr after the
 *  instruction is executed.
 *
 *  Return value is the number of instructions executed during this call
 *  to cpu_run_instr() (0 if no instruction was executed).
 */
int cpu_run_instr(struct cpu *cpu)
{
	int quiet_mode_cached = quiet_mode;
	int instruction_trace_cached = instruction_trace;
	struct coproc *cp0 = cpu->coproc[0];
	int i;
	unsigned char instr[4];
	uint32_t instrword;
	uint64_t cached_pc;
	int hi6, special6, regimm5, rd, rs, rt, sa, imm;
	int copz, stype, which_cache, cache_op;
	char *instr_mnem = NULL;			/*  for instruction trace  */

	int cond=0, likely, and_link;

	uint64_t dir, is_left, reg_ofs, reg_dir;	/*  for unaligned load/store  */
	uint64_t tmpvalue, tmpaddr;

	int cpnr;					/*  coprocessor nr  */

	uint64_t addr, value, value_hi=0, result_value;	/*  for load/store  */
	int wlen, st, signd, linked, dataflag = 0;
	unsigned char d[16];				/*  room for at most 128 bits  */


	/*
	 *  Update Coprocessor 0 registers:
	 *
	 *  The COUNT register needs to be updated on every [other] instruction.
	 *  The RANDOM register should decrease for every instruction.
	 */

	if (cpu->cpu_type.exc_model == EXC3K) {
		int r = (cp0->reg[COP0_RANDOM] & R2K3K_RANDOM_MASK) >> R2K3K_RANDOM_SHIFT;
		r --;
		if (r >= cp0->nr_of_tlbs || r < 8)
			r = cp0->nr_of_tlbs-1;
		cp0->reg[COP0_RANDOM] = r << R2K3K_RANDOM_SHIFT;
		cp0->reg[COP0_COUNT] ++;
	} else {
		/*  TODO: double count blah blah  */
		cp0->reg[COP0_COUNT] ++;

		cp0->reg[COP0_RANDOM] --;
		if ((int64_t)cp0->reg[COP0_RANDOM] >= cp0->nr_of_tlbs ||
		    (int64_t)cp0->reg[COP0_RANDOM] < (int64_t) cp0->reg[COP0_WIRED])
			cp0->reg[COP0_RANDOM] = cp0->nr_of_tlbs-1;

		if (cp0->reg[COP0_COUNT] == cp0->reg[COP0_COMPARE])
			cpu_interrupt(cpu, 7);
	}


#ifdef ENABLE_INSTRUCTION_DELAYS
	if (cpu->instruction_delay > 0) {
		cpu->instruction_delay --;
		return 1;
	}
#endif

	if (cpu->delay_slot) {
		if (cpu->delay_slot == DELAYED) {
			cpu->pc = cpu->delay_jmpaddr;
			cpu->delay_slot = NOT_DELAYED;
		}
		if (cpu->delay_slot == TO_BE_DELAYED) {
			/*  next instruction will be delayed  */
			cpu->delay_slot = DELAYED;
		}
	}

	/*  Cache the program counter in a local variable:  */
	cached_pc = cpu->pc;

	if (cpu->last_was_jumptoself > 0)
		cpu->last_was_jumptoself --;

	/*  Hardwire the zero register to 0:  */
	cpu->gpr[GPR_ZERO] = 0;

	/*  Check PC dumppoints:  */
	for (i=0; i<n_dumppoints; i++)
		if (cached_pc == dumppoint_pc[i]) {
			instruction_trace = instruction_trace_cached = 1;
			quiet_mode = quiet_mode_cached = 0;
			if (dumppoint_flag_r[i])
				register_dump = 1;
		}

	if (!quiet_mode_cached) {
		/*  Dump CPU registers for debugging:  */
		if (register_dump) {
			debug("\n");
			cpu_register_dump(cpu);
		}
	}

#ifdef ALWAYS_SIGNEXTEND_32
	/*
	 *  An extra check for 32-bit mode to make sure that all
	 *  registers are sign-extended:   (Slow, but might be useful
	 *  to detect bugs that have to do with sign-extension.)
	 */
	if (cpu->cpu_type.mmu_model == MMU3K) {
		/*  Sign-extend ALL registers, including coprocessor registers and tlbs:  */
		for (i=0; i<32; i++) {
			cpu->gpr[i] &= 0xffffffff;
			if (cpu->gpr[i] & 0x80000000ULL)
				cpu->gpr[i] |= 0xffffffff00000000ULL;
		}
		for (i=0; i<32; i++) {
			cpu->coproc[0]->reg[i] &= 0xffffffffULL;
			if (cpu->coproc[0]->reg[i] & 0x80000000ULL)
				cpu->coproc[0]->reg[i] |=
				    0xffffffff00000000ULL;
		}
		for (i=0; i<cpu->coproc[0]->nr_of_tlbs; i++) {
			cpu->coproc[0]->tlbs[i].hi &= 0xffffffffULL;
			if (cpu->coproc[0]->tlbs[i].hi & 0x80000000ULL)
				cpu->coproc[0]->tlbs[i].hi |=
				    0xffffffff00000000ULL;
			cpu->coproc[0]->tlbs[i].lo0 &= 0xffffffffULL;
			if (cpu->coproc[0]->tlbs[i].lo0 & 0x80000000ULL)
				cpu->coproc[0]->tlbs[i].lo0 |=
				    0xffffffff00000000ULL;
		}
	}
#endif

#ifdef HALT_IF_PC_ZERO
	/*  Halt if PC = 0:  */
	if (cached_pc == 0) {
		debug("cpu%i: pc=0, halting\n", cpu->cpu_id);
		cpu->running = 0;
		return 0;
	}
#endif


	/*
	 *  ROM emulation:
	 *
	 *  This assumes that a jal was made to a ROM address,
	 *  and we should return via gpr ra.
	 */
	if ((cached_pc & 0xfff00000) == 0xbfc00000 && prom_emulation) {
		int rom_jal = 1;
		switch (emulation_type) {
		case EMULTYPE_DEC:
			decstation_prom_emul(cpu);
			break;
		case EMULTYPE_PS2:
			playstation2_sifbios_emul(cpu);
			break;
		case EMULTYPE_ARC:
		case EMULTYPE_SGI:
			arcbios_emul(cpu);
			break;
		default:
			rom_jal = 0;
		}

		if (rom_jal) {
			cpu->pc = cpu->gpr[GPR_RA];
			/*  no need to update cached_pc, as we're returning  */
			cpu->delay_slot = NOT_DELAYED;
			cpu->trace_tree_depth --;

			/*  TODO: how many instrs should this count as?  */
			return 1;
		}
	}

	/*  Remember where we are, in case of interrupt or exception:  */
	cpu->pc_last = cached_pc;

	if (!quiet_mode_cached) {
		if (cpu->show_trace_delay > 0) {
			cpu->show_trace_delay --;
			if (cpu->show_trace_delay == 0 && show_trace_tree)
				show_trace(cpu, cpu->show_trace_addr);
		}
	}

#ifdef MFHILO_DELAY
	/*  Decrease the MFHI/MFLO delays:  */
	if (cpu->mfhi_delay > 0)
		cpu->mfhi_delay--;
	if (cpu->mflo_delay > 0)
		cpu->mflo_delay--;
#endif

	/*  Read an instruction from memory:  */
#ifdef ENABLE_MIPS16
	if (cpu->mips16 && (cached_pc & 1)) {
		/*  16-bit instruction word:  */
		unsigned char instr16[2];
		int mips16_offset = 0;

		if (!memory_rw(cpu, cpu->mem, cached_pc ^ 1, &instr16[0], sizeof(instr16), MEM_READ, CACHE_INSTRUCTION))
			return 0;

		/*  TODO:  If Reverse-endian is set in the status cop0 register, and
			we are in usermode, then reverse endianness!  */

		/*  The rest of the code is written for little endian, so swap if neccessary:  */
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			int tmp;
			tmp  = instr16[0]; instr16[0] = instr16[1]; instr16[1] = tmp;
		}

		cpu->mips16_extend = 0;

		/*
		 *  Translate into 32-bit instruction, little endian (instr[3..0]):
		 *
		 *  This ugly loop is neccessary because if we would get an exception between
		 *  reading an extend instruction and the next instruction, and execution
		 *  continues on the second instruction, the extend data would be lost. So the
		 *  entire instruction (the two parts) need to be read in. If an exception is
		 *  caused, it will appear as if it was caused when reading the extend instruction.
		 */
		while (mips16_to_32(cpu, instr16, instr) == 0) {
			if (instruction_trace_cached)
				debug("cpu%i @ %016llx: %02x%02x\t\t\textend\n",
				    cpu->cpu_id, (cpu->pc_last ^ 1) + mips16_offset,
				    instr16[1], instr16[0]);

			/*  instruction with extend:  */
			mips16_offset += 2;
			if (!memory_rw(cpu, cpu->mem, (cached_pc ^ 1) + mips16_offset, &instr16[0], sizeof(instr16), MEM_READ, CACHE_INSTRUCTION))
				return 0;

			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				int tmp;
				tmp  = instr16[0]; instr16[0] = instr16[1]; instr16[1] = tmp;
			}
		}

		/*  Advance the program counter:  */
		cpu->pc += sizeof(instr16) + mips16_offset;
		cached_pc = cpu->pc;

		if (instruction_trace_cached) {
			int offset;
			char *symbol = get_symbol_name(cpu->pc_last ^ 1, &offset);
			if (symbol != NULL && offset==0)
				debug("<%s>\n", symbol);

			debug("cpu%i @ %016llx: %02x%02x => %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, (cpu->pc_last ^ 1) + mips16_offset,
			    instr16[1], instr16[0],
			    instr[3], instr[2], instr[1], instr[0],
			    cpu_flags(cpu));
		}
	} else
#endif
	    {
		/*
		 *  Fetch a 32-bit instruction word from memory:
		 *
		 *  1)  The special case of reading an instruction from the
		 *      same host RAM page as the last one is handled here,
		 *      to gain a little bit performance.
		 *
		 *  2)  Fallback to reading from memory the usual way.
		 */
		if (cpu->pc_last_was_in_host_ram &&
		    (cached_pc & ~0xfff) == cpu->pc_last_virtual_page) {
			/*  NOTE: This only works on the host if offset is
			    aligned correctly!  (TODO)  */
			*(uint32_t *)instr = *(uint32_t *)
			    (cpu->pc_last_host_4k_page + (cached_pc & 0xfff));

			/*  TODO:  Make sure this works with
			    dynamic binary translation...  */
                } else {
			if (!memory_rw(cpu, cpu->mem, cached_pc, &instr[0],
			    sizeof(instr), MEM_READ, CACHE_INSTRUCTION))
				return 0;
		}

		/*  Advance the program counter:  */
		cpu->pc += sizeof(instr);
		cached_pc = cpu->pc;

		/*  TODO:  If Reverse-endian is set in the status cop0 register, and
			we are in usermode, then reverse endianness!  */

		/*  The rest of the code is written for little endian, so swap if neccessary:  */
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp, tmp2;
			tmp  = instr[0]; instr[0] = instr[3]; instr[3] = tmp;
			tmp2 = instr[1]; instr[1] = instr[2]; instr[2] = tmp2;
		}

		if (instruction_trace_cached) {
			int offset;
			char *symbol = get_symbol_name(cpu->pc_last, &offset);
			if (symbol != NULL && offset==0)
				debug("<%s>\n", symbol);

			debug("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, cpu->pc_last,
			    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
		}
	}


	/*  Nullify this instruction?  (Set by previous instruction)  */
	if (cpu->nullify_next) {
		cpu->nullify_next = 0;
		if (instruction_trace_cached)
			debug("(nullified)\n");

		/*  Note: Return value is 1, even if no instruction was actually executed.  */
		return 1;
	}


	/*
	 *  Any pending interrupts?
	 *
	 *  If interrupts are enabled, and any interrupt has arrived (ie its
	 *  bit in the cause register is set) and corresponding enable bits
	 *  in the status register are set, then cause an interrupt exception
	 *  instead of executing the current instruction.
	 */
	if (cpu->cpu_type.exc_model == EXC3K) {
		/*  R3000:  */
		int enabled, mask;
		int status = cp0->reg[COP0_STATUS];

		if (cpu->last_was_rfe) {
			enabled = 0;
			cpu->last_was_rfe = 0;
		} else {
			enabled = status & MIPS_SR_INT_IE;
			mask  = status & cp0->reg[COP0_CAUSE] & STATUS_IM_MASK;
			if (enabled && mask) {
				cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0, 0);
				return 0;
			}
		}
	} else {
		/*  R4000 and others:  */
		int enabled, mask;
		int status = cp0->reg[COP0_STATUS];

		enabled = (status & STATUS_IE)
		    && !(status & STATUS_EXL)
		    && !(status & STATUS_ERL);

		mask = status & cp0->reg[COP0_CAUSE] & STATUS_IM_MASK;
		if (enabled && mask) {
			cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0, 0);
			return 0;
		}
	}


	/*
	 *  Execute the instruction:
	 */

	/*  Get the top 6 bits of the instruction:  */
	hi6 = (instr[3] >> 2) & 0x3f;

	if (show_opcode_statistics)
		cpu->stats_opcode[hi6] ++;

	switch (hi6) {
	case HI6_SPECIAL:
		special6 = instr[0] & 0x3f;

		if (show_opcode_statistics)
			cpu->stats__special[special6] ++;

		switch (special6) {
		case SPECIAL_SLL:
		case SPECIAL_SRL:
		case SPECIAL_SRA:
		case SPECIAL_DSLL:
		case SPECIAL_DSRL:
		case SPECIAL_DSRA:
		case SPECIAL_DSLL32:
		case SPECIAL_DSRL32:
		case SPECIAL_DSRA32:
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;
			sa = ((instr[1] & 7) << 2) + ((instr[0] >> 6) & 3);

			if (instruction_trace_cached) {
				instr_mnem = NULL;
				if (special6 == SPECIAL_SLL)	instr_mnem = "sll";
				if (special6 == SPECIAL_SRL)	instr_mnem = "srl";
				if (special6 == SPECIAL_SRA)	instr_mnem = "sra";
				if (special6 == SPECIAL_DSLL)	instr_mnem = "dsll";
				if (special6 == SPECIAL_DSRL)	instr_mnem = "dsrl";
				if (special6 == SPECIAL_DSRA)	instr_mnem = "dsra";
				if (special6 == SPECIAL_DSLL32)	instr_mnem = "dsll32";
				if (special6 == SPECIAL_DSRL32)	instr_mnem = "dsrl32";
				if (special6 == SPECIAL_DSRA32)	instr_mnem = "dsra32";
			}

			/*
			 *  Check for NOP:
			 *
			 *  The R4000 manual says that a shift amount of zero
			 *  is treated as a nop by some assemblers. Checking
			 *  for sa == 0 here would not be correct, though,
			 *  because instructions such as sll r3,r4,0 are
			 *  possible, and are definitely not a nop.
			 *  Instead, check if the destination register is r0.
			 *
			 *  TODO:  ssnop should wait until the _next_
			 *  cycle boundary, or something like that. The
			 *  code here is incorrect.
			 */
			if (rd == 0 && special6 == SPECIAL_SLL) {
				if (instruction_trace_cached) {
					if (sa == 0)
						debug("nop\n");
					else if (sa == 1) {
						debug("ssnop\n");
#ifdef ENABLE_INSTRUCTION_DELAYS
						cpu->instruction_delay +=
						    cpu->cpu_type.
						    instrs_per_cycle - 1;
#endif
					} else
						debug("nop (weird, sa=%i)\n",
						    sa);
				}
				break;
			} else
				if (instruction_trace_cached)
					debug("%s\tr%i,r%i,%i\n", instr_mnem, rd, rt, sa);

			if (special6 == SPECIAL_SLL) {
				cpu->gpr[rd] = cpu->gpr[rt] << sa;
				/*  Sign-extend rd:  */
				cpu->gpr[rd] = (int64_t) (int32_t) cpu->gpr[rd];
			}
			if (special6 == SPECIAL_DSLL) {
				cpu->gpr[rd] = cpu->gpr[rt] << sa;
			}
			if (special6 == SPECIAL_DSRL) {
				cpu->gpr[rd] = cpu->gpr[rt] >> sa;
			}
			if (special6 == SPECIAL_DSLL32) {
				cpu->gpr[rd] = cpu->gpr[rt] << (sa + 32);
			}
			if (special6 == SPECIAL_SRL) {
				cpu->gpr[rd] = cpu->gpr[rt];
				/*  Zero-extend rd:  */
				cpu->gpr[rd] &= 0xffffffffULL;
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
				}
				cpu->gpr[rd] = (int64_t)(int32_t)cpu->gpr[rd];
			}
			if (special6 == SPECIAL_SRA) {
				/*  rd = sign-extend of rt:  */
				cpu->gpr[rd] = (int64_t) (int32_t) cpu->gpr[rt];
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
				}
				cpu->gpr[rd] = (int64_t) (int32_t) cpu->gpr[rd];
			}
			if (special6 == SPECIAL_DSRL32) {
				cpu->gpr[rd] = cpu->gpr[rt] >> (sa + 32);
			}
			if (special6 == SPECIAL_DSRA32 || special6 == SPECIAL_DSRA) {
				if (special6 == SPECIAL_DSRA32)
					sa += 32;
				cpu->gpr[rd] = cpu->gpr[rt];
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
					if (cpu->gpr[rd] & ((uint64_t)1 << 62))		/*  old signbit  */
						cpu->gpr[rd] |= ((uint64_t)1 << 63);
				}
			}
			break;
		case SPECIAL_DSRLV:
		case SPECIAL_DSRAV:
		case SPECIAL_DSLLV:
		case SPECIAL_SLLV:
		case SPECIAL_SRAV:
		case SPECIAL_SRLV:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;

			if (instruction_trace_cached) {
				instr_mnem = NULL;
				if (special6 == SPECIAL_DSRLV)	instr_mnem = "dsrlv";
				if (special6 == SPECIAL_DSRAV)	instr_mnem = "dsrav";
				if (special6 == SPECIAL_DSLLV)	instr_mnem = "dsllv";
				if (special6 == SPECIAL_SLLV)	instr_mnem = "sllv";
				if (special6 == SPECIAL_SRAV)	instr_mnem = "srav";
				if (special6 == SPECIAL_SRLV)	instr_mnem = "srlv";

				debug("%s\tr%i,r%i,r%i\n", instr_mnem, rd, rt, rs);
			}

			if (special6 == SPECIAL_DSRLV) {
				sa = cpu->gpr[rs] & 63;
				cpu->gpr[rd] = cpu->gpr[rt] >> sa;
			}
			if (special6 == SPECIAL_DSRAV) {
				sa = cpu->gpr[rs] & 63;
				cpu->gpr[rd] = cpu->gpr[rt];
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
					if (cpu->gpr[rd] & ((uint64_t)1 << 62))		/*  old sign-bit  */
						cpu->gpr[rd] |= ((uint64_t)1 << 63);
				}
			}
			if (special6 == SPECIAL_DSLLV) {
				sa = cpu->gpr[rs] & 63;
				cpu->gpr[rd] = cpu->gpr[rt];
				cpu->gpr[rd] = cpu->gpr[rd] << sa;
			}
			if (special6 == SPECIAL_SLLV) {
				sa = cpu->gpr[rs] & 31;
				cpu->gpr[rd] = cpu->gpr[rt];
				cpu->gpr[rd] = cpu->gpr[rd] << sa;
				/*  Sign-extend rd:  */
				cpu->gpr[rd] &= 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
			}
			if (special6 == SPECIAL_SRAV) {
				sa = cpu->gpr[rs] & 31;
				cpu->gpr[rd] = cpu->gpr[rt];
				/*  Sign-extend rd:  */
				cpu->gpr[rd] &= 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
				}
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
			}
			if (special6 == SPECIAL_SRLV) {
				sa = cpu->gpr[rs] & 31;
				cpu->gpr[rd] = cpu->gpr[rt];
				cpu->gpr[rd] &= 0xffffffffULL;
				cpu->gpr[rd] = cpu->gpr[rd] >> sa;
				/*  And finally sign-extend rd:  */
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
			}
			break;
		case SPECIAL_JR:
			if (cpu->delay_slot) {
				fatal("jr: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}

			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			if (instruction_trace_cached) {
				int offset;
				char *symbol = get_symbol_name(cpu->gpr[rs], &offset);
				if (symbol != NULL)
					debug("jr\tr%i\t\t<%s>\n", rs, symbol);
				else
					debug("jr\tr%i\n", rs);
			}

			cpu->delay_slot = TO_BE_DELAYED;
			cpu->delay_jmpaddr = cpu->gpr[rs];

			if (rs == 31) {
#if 0
				/*  TODO:  This should be done _after_ the instruction following the JR,
					that is, the branch delay, as GPR_V0 can be modified there.  */
				int x;
				for (x=0; x<cpu->trace_tree_depth; x++)
					debug("  ");
				debug("retval 0x%llx\n", cpu->gpr[GPR_V0]);
#endif
				cpu->trace_tree_depth --;
			}

			break;
		case SPECIAL_JALR:
			if (cpu->delay_slot) {
				fatal("jalr: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}

			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rd = (instr[1] >> 3) & 31;
			if (instruction_trace_cached) {
				int offset;
				char *symbol = get_symbol_name(cpu->gpr[rs], &offset);
				if (symbol != NULL)
					debug("jalr\tr%i,r%i\t\t<%s>\n", rd, rs, symbol);
				else
					debug("jalr\tr%i,r%i\n", rd, rs);
			}

			tmpvalue = cpu->gpr[rs];
			cpu->gpr[rd] = cached_pc + 4;	/*  already increased by 4 earlier  */

			if (!quiet_mode_cached && rd == 31) {
				cpu->show_trace_delay = 2;
				cpu->show_trace_addr = tmpvalue;
			}

			cpu->delay_slot = TO_BE_DELAYED;
			cpu->delay_jmpaddr = tmpvalue;
			break;
		case SPECIAL_MFHI:
		case SPECIAL_MFLO:
			rd = (instr[1] >> 3) & 31;

			if (instruction_trace_cached) {
				instr_mnem = NULL;
				if (special6 == SPECIAL_MFHI)	instr_mnem = "mfhi";
				if (special6 == SPECIAL_MFLO)	instr_mnem = "mflo";
				debug("%s\tr%i\n", instr_mnem, rd);
			}

			if (special6 == SPECIAL_MFHI) {
				cpu->gpr[rd] = cpu->hi;
#ifdef MFHILO_DELAY
				cpu->mfhi_delay = 3;
#endif
			}
			if (special6 == SPECIAL_MFLO) {
				cpu->gpr[rd] = cpu->lo;
#ifdef MFHILO_DELAY
				cpu->mflo_delay = 3;
#endif
			}
			break;
		case SPECIAL_ADD:
		case SPECIAL_ADDU:
		case SPECIAL_SUB:
		case SPECIAL_SUBU:
		case SPECIAL_AND:
		case SPECIAL_OR:
		case SPECIAL_XOR:
		case SPECIAL_NOR:
		case SPECIAL_SLT:
		case SPECIAL_SLTU:
		case SPECIAL_MTLO:
		case SPECIAL_MTHI:
		case SPECIAL_MULT:
		case SPECIAL_MULTU:
		case SPECIAL_DMULT:
		case SPECIAL_DMULTU:
		case SPECIAL_DIV:
		case SPECIAL_DIVU:
		case SPECIAL_DDIV:
		case SPECIAL_DDIVU:
		case SPECIAL_TGE:
		case SPECIAL_TGEU:
		case SPECIAL_TLT:
		case SPECIAL_TLTU:
		case SPECIAL_TEQ:
		case SPECIAL_TNE:
		case SPECIAL_DADD:
		case SPECIAL_DADDU:
		case SPECIAL_DSUB:
		case SPECIAL_DSUBU:
		case SPECIAL_MOVZ:
		case SPECIAL_MOVN:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;

#ifdef MFHILO_DELAY
			if (cpu->mflo_delay > 0 && (
			    special6 == SPECIAL_DDIV ||   special6 == SPECIAL_DDIVU ||
			    special6 == SPECIAL_DIV ||    special6 == SPECIAL_DIVU ||
			    special6 == SPECIAL_DMULT ||  special6 == SPECIAL_DMULTU ||
			    special6 == SPECIAL_MTLO ||   special6 == SPECIAL_MULT
			    || special6 == SPECIAL_MULTU
			    ) )
				debug("warning: instruction modifying LO too early after mflo!\n");

			if (cpu->mfhi_delay > 0 && (
			    special6 == SPECIAL_DDIV ||  special6 == SPECIAL_DDIVU ||
			    special6 == SPECIAL_DIV ||   special6 == SPECIAL_DIVU ||
			    special6 == SPECIAL_DMULT || special6 == SPECIAL_DMULTU ||
			    special6 == SPECIAL_MTHI ||  special6 == SPECIAL_MULT
			    || special6 == SPECIAL_MULTU
			    ) )
				debug("warning: instruction modifying HI too early after mfhi!\n");
#endif

			if (instruction_trace_cached) {
				instr_mnem = NULL;
				if (special6 == SPECIAL_MTLO)	instr_mnem = "mtlo";
				if (special6 == SPECIAL_MTHI)	instr_mnem = "mthi";
				if (instr_mnem)
					debug("%s\tr%i\n", instr_mnem, rs);

				instr_mnem = NULL;
				if (special6 == SPECIAL_MULT) {
					if (rd!=0)
						debug("mult_xx\tr%i,r%i,r%i\n", rd, rs, rt);
					else
						instr_mnem = "mult";
				}
				if (special6 == SPECIAL_MULTU)	instr_mnem = "multu";
				if (special6 == SPECIAL_DMULT)	instr_mnem = "dmult";
				if (special6 == SPECIAL_DMULTU)	instr_mnem = "dmultu";
				if (special6 == SPECIAL_DIV)	instr_mnem = "div";
				if (special6 == SPECIAL_DIVU)	instr_mnem = "divu";
				if (special6 == SPECIAL_DDIV)	instr_mnem = "ddiv";
				if (special6 == SPECIAL_DDIVU)	instr_mnem = "ddivu";
				if (special6 == SPECIAL_TGE)	instr_mnem = "tge";
				if (special6 == SPECIAL_TGEU)	instr_mnem = "tgeu";
				if (special6 == SPECIAL_TLT)	instr_mnem = "tlt";
				if (special6 == SPECIAL_TLTU)	instr_mnem = "tltu";
				if (special6 == SPECIAL_TEQ)	instr_mnem = "teq";
				if (special6 == SPECIAL_TNE)	instr_mnem = "tne";
				if (instr_mnem)
					debug("%s\tr%i,r%i\n", instr_mnem, rs, rt);

				instr_mnem = NULL;
				if (special6 == SPECIAL_ADD)	instr_mnem = "add";
				if (special6 == SPECIAL_ADDU)	instr_mnem = "addu";
				if (special6 == SPECIAL_SUB)	instr_mnem = "sub";
				if (special6 == SPECIAL_SUBU)	instr_mnem = "subu";
				if (special6 == SPECIAL_AND)	instr_mnem = "and";
				if (special6 == SPECIAL_OR)	instr_mnem = "or";
				if (special6 == SPECIAL_XOR)	instr_mnem = "xor";
				if (special6 == SPECIAL_NOR)	instr_mnem = "nor";
				if (special6 == SPECIAL_SLT)	instr_mnem = "slt";
				if (special6 == SPECIAL_SLTU)	instr_mnem = "sltu";
				if (special6 == SPECIAL_DADD)	instr_mnem = "dadd";
				if (special6 == SPECIAL_DADDU)	instr_mnem = "daddu";
				if (special6 == SPECIAL_DSUB)	instr_mnem = "dsub";
				if (special6 == SPECIAL_DSUBU)	instr_mnem = "dsubu";
				if (special6 == SPECIAL_MOVZ)	instr_mnem = "movz";
				if (special6 == SPECIAL_MOVN)	instr_mnem = "movn";
				if (instr_mnem)
					debug("%s\tr%i,r%i,r%i\n", instr_mnem, rd, rs, rt);
			}

			/*  TODO:  trap on overflow, and stuff like that  */
			if (special6 == SPECIAL_ADD ||
			    special6 == SPECIAL_ADDU) {
				cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
				cpu->gpr[rd] &= 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_SUB ||
			    special6 == SPECIAL_SUBU) {
				cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rt];
				cpu->gpr[rd] &= 0xffffffffULL;
				if (cpu->gpr[rd] & 0x80000000ULL)
					cpu->gpr[rd] |= 0xffffffff00000000ULL;
				break;
			}

			if (special6 == SPECIAL_AND) {
				cpu->gpr[rd] = cpu->gpr[rs] & cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_OR) {
				cpu->gpr[rd] = cpu->gpr[rs] | cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_XOR) {
				cpu->gpr[rd] = cpu->gpr[rs] ^ cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_NOR) {
				cpu->gpr[rd] = ~(cpu->gpr[rs] | cpu->gpr[rt]);		/*  TODO:  is this corrent NOR?  */
				break;
			}
			if (special6 == SPECIAL_SLT) {
				cpu->gpr[rd] = (int64_t)cpu->gpr[rs] < (int64_t)cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_SLTU) {
				cpu->gpr[rd] = cpu->gpr[rs] < cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_MTLO) {
				cpu->lo = cpu->gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MTHI) {
				cpu->hi = cpu->gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MULT) {
				int64_t f1, f2, sum;
				f1 = cpu->gpr[rs] & 0xffffffffULL;
				/*  sign extend f1  */
				if (f1 & 0x80000000ULL)
					f1 |= 0xffffffff00000000ULL;
				f2 = cpu->gpr[rt] & 0xffffffffULL;
				/*  sign extend f2  */
				if (f2 & 0x80000000ULL)
					f2 |= 0xffffffff00000000ULL;
				sum = f1 * f2;

				cpu->lo = sum & 0xffffffffULL;
				cpu->hi = ((uint64_t)sum >> 32) & 0xffffffffULL;

				/*  sign-extend:  */
				if (cpu->lo & 0x80000000ULL)
					cpu->lo |= 0xffffffff00000000ULL;
				if (cpu->hi & 0x80000000ULL)
					cpu->hi |= 0xffffffff00000000ULL;

				/*
				 *  NOTE:  The stuff about rd!=0 is just a
				 *  guess, judging from how some NetBSD code
				 *  seems to execute.  It is not documented in
				 *  the MIPS64 ISA docs :-/
				 */

				if (rd != 0) {
					if (cpu->cpu_type.rev != MIPS_R5900)
						debug("WARNING! mult_xx is an undocumented instruction!");
					cpu->gpr[rd] = cpu->lo;
				}
				break;
			}
			if (special6 == SPECIAL_MULTU) {
				uint64_t f1, f2, sum;
				/*  zero extend f1 and f2  */
				f1 = cpu->gpr[rs] & 0xffffffffULL;
				f2 = cpu->gpr[rt] & 0xffffffffULL;
				sum = f1 * f2;
				cpu->lo = sum & 0xffffffffULL;
				cpu->hi = (sum >> 32) & 0xffffffffULL;

				/*  sign-extend:  */
				if (cpu->lo & 0x80000000ULL)
					cpu->lo |= 0xffffffff00000000ULL;
				if (cpu->hi & 0x80000000ULL)
					cpu->hi |= 0xffffffff00000000ULL;
				break;
			}
			/*
			 *  TODO:  I'm too tired to think now.  DMULT is probably
			 *  correct, but is DMULTU?  (Unsigned 64x64 multiply.)
			 *  Or, hm, perhaps it is dmult which is incorrect.
			 */
			if (special6 == SPECIAL_DMULT || special6 == SPECIAL_DMULTU) {
				/*  64x64 = 128 bit multiplication:  SLOW!!!  TODO  */
				uint64_t i, low_add, high_add;

				cpu->lo = cpu->hi = 0;
				for (i=0; i<64; i++) {
					uint64_t bit = cpu->gpr[rt] & ((uint64_t)1 << i);
					if (bit) {
						/*  Add cpu->gpr[rs] to hi and lo:  */
						low_add = (cpu->gpr[rs] << i);
						high_add = (cpu->gpr[rs] >> (64-i));
						if (i==0)			/*  WEIRD BUG in the compiler? Or maybe I'm just stupid  */
							high_add = 0;		/*  these lines are neccessary, a >> 64 doesn't seem to do anything  */
						if (cpu->lo + low_add < cpu->lo)
							cpu->hi ++;
						cpu->lo += low_add;
						cpu->hi += high_add;
					}
				}
				break;
			}
			if (special6 == SPECIAL_DIV) {
				int64_t a, b;
				/*  Signextend rs and rt:  */
				a = cpu->gpr[rs] & 0xffffffffULL;
				if (a & 0x80000000ULL)
					a |= 0xffffffff00000000ULL;
				b = cpu->gpr[rt] & 0xffffffffULL;
				if (b & 0x80000000ULL)
					b |= 0xffffffff00000000ULL;

				if (b == 0) {
					/*  undefined  */
					cpu->lo = cpu->hi = 0;
				} else {
					cpu->lo = a / b;
					cpu->hi = a % b;
				}
				/*  Sign-extend lo and hi:  */
				cpu->lo &= 0xffffffffULL;
				if (cpu->lo & 0x80000000ULL)
					cpu->lo |= 0xffffffff00000000ULL;
				cpu->hi &= 0xffffffffULL;
				if (cpu->hi & 0x80000000ULL)
					cpu->hi |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_DIVU) {
				int64_t a, b;
				/*  Zero-extend rs and rt:  */
				a = cpu->gpr[rs] & 0xffffffffULL;
				b = cpu->gpr[rt] & 0xffffffffULL;
				if (b == 0) {
					/*  undefined  */
					cpu->lo = cpu->hi = 0;
				} else {
					cpu->lo = a / b;
					cpu->hi = a % b;
				}
				/*  Sign-extend lo and hi:  */
				cpu->lo &= 0xffffffffULL;
				if (cpu->lo & 0x80000000ULL)
					cpu->lo |= 0xffffffff00000000ULL;
				cpu->hi &= 0xffffffffULL;
				if (cpu->hi & 0x80000000ULL)
					cpu->hi |= 0xffffffff00000000ULL;
				break;
			}
			if (special6 == SPECIAL_DDIV) {
				if (cpu->gpr[rt] == 0) {
					cpu->lo = cpu->hi = 0;		/*  undefined  */
				} else {
					cpu->lo = (int64_t)cpu->gpr[rs] / (int64_t)cpu->gpr[rt];
					cpu->hi = (int64_t)cpu->gpr[rs] % (int64_t)cpu->gpr[rt];
				}
				break;
			}
			if (special6 == SPECIAL_DDIVU) {
				if (cpu->gpr[rt] == 0) {
					cpu->lo = cpu->hi = 0;		/*  undefined  */
				} else {
					cpu->lo = cpu->gpr[rs] / cpu->gpr[rt];
					cpu->hi = cpu->gpr[rs] % cpu->gpr[rt];
				}
				break;
			}
			if (special6 == SPECIAL_TGE) {
				if ((int64_t)cpu->gpr[rs] >= (int64_t)cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TGEU) {
				if (cpu->gpr[rs] >= cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TLT) {
				if ((int64_t)cpu->gpr[rs] < (int64_t)cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TLTU) {
				if (cpu->gpr[rs] < cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TEQ) {
				if (cpu->gpr[rs] == cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_TNE) {
				if (cpu->gpr[rs] != cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
				break;
			}
			if (special6 == SPECIAL_DADD) {
				cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
				/*  TODO:  exception on overflow  */
				break;
			}
			if (special6 == SPECIAL_DADDU) {
				cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_DSUB) {
				cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rt];
				/*  TODO:  exception on overflow  */
				break;
			}
			if (special6 == SPECIAL_DSUBU) {
				cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rt];
				break;
			}
			if (special6 == SPECIAL_MOVZ) {
				if (cpu->gpr[rt] == 0)
					cpu->gpr[rd] = cpu->gpr[rs];
				break;
			}
			if (special6 == SPECIAL_MOVN) {
				if (cpu->gpr[rt] != 0)
					cpu->gpr[rd] = cpu->gpr[rs];
				break;
			}
			break;
		case SPECIAL_SYNC:
			stype = ((instr[1] & 7) << 2) + (instr[0] >> 6);
			if (instruction_trace_cached)
				debug("sync\t0x%02x\n", stype);
			/*  TODO: actually sync  */
			break;
		case SPECIAL_SYSCALL:
			imm = ((instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0]) >> 6;
			imm &= 0xfffff;
			if (instruction_trace_cached)
				debug("syscall\t0x%05x\n", imm);

			if (userland_emul) {
				useremul_syscall(cpu, imm);
			} else
				cpu_exception(cpu, EXCEPTION_SYS, 0, 0, 0, 0, 0, 0);
			break;
		case SPECIAL_BREAK:
			if (instruction_trace_cached)
				debug("break\n");
			cpu_exception(cpu, EXCEPTION_BP, 0, 0, 0, 0, 0, 0);
			break;
		case SPECIAL_MFSA:
			/*  R5900? What on earth does this thing do?  */
			rd = (instr[1] >> 3) & 31;
			if (instruction_trace_cached)
				debug("mfsa\tr%i\n", rd);
			/*  TODO  */
			break;
		case SPECIAL_MTSA:
			/*  R5900? What on earth does this thing do?  */
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			if (instruction_trace_cached)
				debug("mtsa\tr%i\n", rs);
			/*  TODO  */
			break;
		default:
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented special6 = 0x%02x\n", special6);
			cpu->running = 0;
			return 1;
		}
		break;
	case HI6_BEQ:
	case HI6_BEQL:
	case HI6_BNE:
	case HI6_BGTZ:
	case HI6_BGTZL:
	case HI6_BLEZ:
	case HI6_BLEZL:
	case HI6_BNEL:
	case HI6_ADDI:
	case HI6_ADDIU:
	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_SLTI:
	case HI6_SLTIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
	case HI6_LUI:
	case HI6_LB:
	case HI6_LBU:
	case HI6_LH:
	case HI6_LHU:
	case HI6_LW:
	case HI6_LWU:
	case HI6_LD:
	case HI6_LQ_MDMX:
	case HI6_LWC1:
	case HI6_LWC2:
	case HI6_LWC3:
	case HI6_LDC1:
	case HI6_LDC2:
	case HI6_LL:
	case HI6_LLD:
	case HI6_SB:
	case HI6_SH:
	case HI6_SW:
	case HI6_SD:
	case HI6_SQ:
	case HI6_SC:
	case HI6_SCD:
	case HI6_SWC1:
	case HI6_SWC2:
	case HI6_SWC3:
	case HI6_SDC1:
	case HI6_LWL:	/*  Unaligned load/store  */
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		imm = (instr[1] << 8) + instr[0];
		if (imm >= 32768)		/*  signed 16-bit  */
			imm -= 65536;

		if (instruction_trace_cached) {
			instr_mnem = NULL;
			if (hi6 == HI6_BEQ)	instr_mnem = "beq";
			if (hi6 == HI6_BEQL)	instr_mnem = "beql";
			if (hi6 == HI6_BNE)	instr_mnem = "bne";
			if (hi6 == HI6_BGTZ)	instr_mnem = "bgtz";
			if (hi6 == HI6_BGTZL)	instr_mnem = "bgtzl";
			if (hi6 == HI6_BLEZ)	instr_mnem = "blez";
			if (hi6 == HI6_BLEZL)	instr_mnem = "blezl";
			if (hi6 == HI6_BNEL)	instr_mnem = "bnel";
			if (instr_mnem != NULL)
				debug("%s\tr%i,r%i,%016llx\n", instr_mnem, rt, rs, cached_pc + (imm << 2));

			instr_mnem = NULL;
			if (hi6 == HI6_ADDI)	instr_mnem = "addi";
			if (hi6 == HI6_ADDIU)	instr_mnem = "addiu";
			if (hi6 == HI6_DADDI)	instr_mnem = "daddi";
			if (hi6 == HI6_DADDIU)	instr_mnem = "daddiu";
			if (hi6 == HI6_SLTI)	instr_mnem = "slti";
			if (hi6 == HI6_SLTIU)	instr_mnem = "sltiu";
			if (hi6 == HI6_ANDI)	instr_mnem = "andi";
			if (hi6 == HI6_ORI)	instr_mnem = "ori";
			if (hi6 == HI6_XORI)	instr_mnem = "xori";
			if (instr_mnem != NULL)
				debug("%s\tr%i,r%i,%i\n", instr_mnem, rt, rs, imm);

			instr_mnem = NULL;
			if (hi6 == HI6_LUI)	instr_mnem = "lui";
			if (instr_mnem != NULL)
				debug("%s\tr%i,0x%x\n", instr_mnem, rt, imm & 0xffff);

			instr_mnem = NULL;
			if (hi6 == HI6_LB)	instr_mnem = "lb";
			if (hi6 == HI6_LBU)	instr_mnem = "lbu";
			if (hi6 == HI6_LH)	instr_mnem = "lh";
			if (hi6 == HI6_LHU)	instr_mnem = "lhu";
			if (hi6 == HI6_LW)	instr_mnem = "lw";
			if (hi6 == HI6_LWU)	instr_mnem = "lwu";
			if (hi6 == HI6_LD)	instr_mnem = "ld";
			if (hi6 == HI6_LQ_MDMX)	instr_mnem = "lq";	/*  R5900 only, otherwise MDMX (TODO)  */
			if (hi6 == HI6_LWC1)	instr_mnem = "lwc1";
			if (hi6 == HI6_LWC2)	instr_mnem = "lwc2";
			if (hi6 == HI6_LWC3)	instr_mnem = "pref";	/* "lwc3"; */
			if (hi6 == HI6_LDC1)	instr_mnem = "ldc1";
			if (hi6 == HI6_LDC2)	instr_mnem = "ldc2";
			if (hi6 == HI6_LL)	instr_mnem = "ll";
			if (hi6 == HI6_LLD)	instr_mnem = "lld";
			if (hi6 == HI6_LWL)	instr_mnem = "lwl";
			if (hi6 == HI6_LWR)	instr_mnem = "lwr";
			if (hi6 == HI6_LDL)	instr_mnem = "ldl";
			if (hi6 == HI6_LDR)	instr_mnem = "ldr";
			if (hi6 == HI6_SB)	instr_mnem = "sb";
			if (hi6 == HI6_SH)	instr_mnem = "sh";
			if (hi6 == HI6_SW)	instr_mnem = "sw";
			if (hi6 == HI6_SD)	instr_mnem = "sd";
			if (hi6 == HI6_SQ)	instr_mnem = "sq";	/*  R5900 ?  */
			if (hi6 == HI6_SC)	instr_mnem = "sc";
			if (hi6 == HI6_SCD)	instr_mnem = "scd";
			if (hi6 == HI6_SWC1)	instr_mnem = "swc1";
			if (hi6 == HI6_SWC2)	instr_mnem = "swc2";
			if (hi6 == HI6_SWC3)	instr_mnem = "swc3";
			if (hi6 == HI6_SDC1)	instr_mnem = "sdc1";
			if (hi6 == HI6_SWL)	instr_mnem = "swl";
			if (hi6 == HI6_SWR)	instr_mnem = "swr";
			if (hi6 == HI6_SDL)	instr_mnem = "sdl";
			if (hi6 == HI6_SDR)	instr_mnem = "sdr";
			dataflag = 0;
			if (instr_mnem != NULL) {
				int offset;
		                char *symbol = get_symbol_name(cpu->gpr[rs] + imm, &offset);

				if (hi6 == HI6_LWC3)
					debug("%s\t0x%x,%i(r%i)\t\t[0x%016llx = %s]\n", instr_mnem,
					    rt, imm, rs, (long long)(cpu->gpr[rs] + imm), symbol);
				else {
					if (symbol != NULL)
						debug("%s\tr%i,%i(r%i)\t\t[0x%016llx = %s, data=", instr_mnem,
						    rt, imm, rs, (long long)(cpu->gpr[rs] + imm), symbol);
					else
						debug("%s\tr%i,%i(r%i)\t\t[0x%016llx, data=", instr_mnem,
						    rt, imm, rs, (long long)(cpu->gpr[rs] + imm));
				}

				dataflag = 1;
			}
		}

		switch (hi6) {
		case HI6_ADDI:
		case HI6_ADDIU:
		case HI6_DADDI:
		case HI6_DADDIU:
			tmpvalue = cpu->gpr[rs];
			result_value = cpu->gpr[rs] + imm;

			/*
			 *  addi and daddi should trap on overflow:
			 *
			 *  TODO:  This is incorrect? The R4000 manual says
			 *  that overflow occurs if the carry bits out of bit
			 *  62 and 63 differ.   The destination register should
			 *  not be modified on overflow.
			 */
			if (imm >= 0) {
				/*  Turn around from 0x7fff.. to 0x800 ?  Then overflow.  */
				if (   ((hi6 == HI6_ADDI && (result_value &
				    0x80000000ULL) && (tmpvalue &
				    0x80000000ULL)==0))
				    || ((hi6 == HI6_DADDI && (result_value &
				    0x8000000000000000ULL) && (tmpvalue &
				    0x8000000000000000ULL)==0)) ) {
					cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
					break;
				}
			} else {
				/*  Turn around from 0x8000.. to 0x7fff.. ?  Then overflow.  */
				if (   ((hi6 == HI6_ADDI && (result_value &
				    0x80000000ULL)==0 && (tmpvalue &
				    0x80000000ULL)))
				    || ((hi6 == HI6_DADDI && (result_value &
				    0x8000000000000000ULL)==0 && (tmpvalue &
				    0x8000000000000000ULL))) ) {
					cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
					break;
				}
			}
			cpu->gpr[rt] = result_value;

			/*
			 *  Super-ugly speed-hack:  (only if speed_tricks != 0)
			 *  NOTE: This makes the emulation less correct.
			 *
			 *  If we encounter a loop such as:
			 *
			 *	8012f5f4: 1c40ffff      bgtz r0,r2,ffffffff8012f5f4
			 *	8012f5f8: 2442ffff (d)  addiu r2,r2,-1
			 *
			 *  then it is a small loop which simply waits for r2
			 *  to become zero.
			 *
			 *  TODO:  increaste the count register, and cause
			 *  interrupts!!!  For now: return as if we just
			 *  executed 1 instruction.
			 */
			if (speed_tricks && cpu->delay_slot && cpu->last_was_jumptoself &&
			    cpu->jump_to_self_reg == rt && cpu->jump_to_self_reg == rs) {
				if ((int64_t)cpu->gpr[rt] > 5 && imm == -1) {
					if (instruction_trace_cached)
						debug("changing r%i from %016llx to", rt, (long long)cpu->gpr[rt]);

					cpu->gpr[rt] = 0;
					if (instruction_trace_cached)
						debug(" %016llx\n", (long long)cpu->gpr[rt]);

					/*  TODO: return value, cpu->gpr[rt] * 2;  */
				}
				if ((int64_t)cpu->gpr[rt] < -5 && imm == 1) {
					if (instruction_trace_cached)
						debug("changing r%i from %016llx to", rt, (long long)cpu->gpr[rt]);
					cpu->gpr[rt] = 0;
					if (instruction_trace_cached)
						debug(" %016llx\n", (long long)cpu->gpr[rt]);

					/*  TODO: return value, -cpu->gpr[rt]*2;  */
				}
			}

			if (hi6 == HI6_ADDI || hi6 == HI6_ADDIU) {
				/*  Sign-extend:  */
				cpu->gpr[rt] = (int64_t) (int32_t) cpu->gpr[rt];
			}
			break;
		case HI6_BEQ:
		case HI6_BNE:
		case HI6_BGTZ:
		case HI6_BGTZL:
		case HI6_BLEZ:
		case HI6_BLEZL:
		case HI6_BEQL:
		case HI6_BNEL:
			if (cpu->delay_slot) {
				fatal("b*: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 1;
			}
			likely = 0;
			switch (hi6) {
			case HI6_BNEL:	likely = 1;
			case HI6_BNE:	cond = (cpu->gpr[rt] != cpu->gpr[rs]);
					break;
			case HI6_BEQL:	likely = 1;
			case HI6_BEQ:	cond = (cpu->gpr[rt] == cpu->gpr[rs]);
					break;
			case HI6_BLEZL:	likely = 1;
			case HI6_BLEZ:	cond = ((int64_t)cpu->gpr[rs] <= 0);
					break;
			case HI6_BGTZL:	likely = 1;
			case HI6_BGTZ:	cond = ((int64_t)cpu->gpr[rs] > 0);
					break;
			}

			if (cond) {
				cpu->delay_slot = TO_BE_DELAYED;
				cpu->delay_jmpaddr = cached_pc + (imm << 2);
			} else {
				if (likely)
					cpu->nullify_next = 1;		/*  nullify delay slot  */
			}

			if (imm==-1 && (hi6 == HI6_BGTZ || hi6 == HI6_BLEZ ||
			    (hi6 == HI6_BNE && (rt==0 || rs==0)) ||
			    (hi6 == HI6_BEQ && (rt==0 || rs==0)))) {
				cpu->last_was_jumptoself = 2;
				if (rs == 0)
					cpu->jump_to_self_reg = rt;
				else
					cpu->jump_to_self_reg = rs;
			}
			break;
		case HI6_LUI:
			cpu->gpr[rt] = (imm << 16);
			/*  No sign-extending neccessary, as imm already
			    was sign-extended if it was negative.  */
			break;
		case HI6_SLTI:
			tmpvalue = imm;
			cpu->gpr[rt] = ((int64_t)cpu->gpr[rs] < (int64_t)tmpvalue) ? 1 : 0;
			break;
		case HI6_SLTIU:
			tmpvalue = imm;
			cpu->gpr[rt] = (cpu->gpr[rs] < tmpvalue) ? 1 : 0;
			break;
		case HI6_ANDI:
			tmpvalue = imm;
			cpu->gpr[rt] = cpu->gpr[rs] & (tmpvalue & 0xffff);
			break;
		case HI6_ORI:
			tmpvalue = imm;
			cpu->gpr[rt] = cpu->gpr[rs] | (tmpvalue & 0xffff);
			break;
		case HI6_XORI:
			tmpvalue = imm;
			cpu->gpr[rt] = cpu->gpr[rs] ^ (tmpvalue & 0xffff);
			break;
		case HI6_LB:
		case HI6_LBU:
		case HI6_LH:
		case HI6_LHU:
		case HI6_LW:
		case HI6_LWU:
		case HI6_LD:
		case HI6_LQ_MDMX:
		case HI6_LWC1:
		case HI6_LWC2:
		case HI6_LWC3:	/*  pref  */
		case HI6_LDC1:
		case HI6_LDC2:
		case HI6_LL:
		case HI6_LLD:
		case HI6_SB:
		case HI6_SH:
		case HI6_SW:
		case HI6_SD:
		case HI6_SQ:
		case HI6_SC:
		case HI6_SCD:
		case HI6_SWC1:
		case HI6_SWC2:
		case HI6_SWC3:
		case HI6_SDC1:
			/*  These are the default "assumptions".  */
			linked = 0;
			st = 1;
			signd = 1;
			wlen = 4;

			switch (hi6) {
			/*  The most common ones:  */
			case HI6_LW:	{           st = 0;            }  break;
			case HI6_SW:	{                   signd = 0; }  break;

			case HI6_LB:	{ wlen = 1; st = 0;            }  break;
			case HI6_LBU:	{ wlen = 1; st = 0; signd = 0; }  break;
			case HI6_SB:	{ wlen = 1;         signd = 0; }  break;

			case HI6_LD:	{ wlen = 8; st = 0; signd = 0; }  break;
			case HI6_SD:	{ wlen = 8;         signd = 0; }  break;

			case HI6_LQ_MDMX:	{ wlen = 16; st = 0; signd = 0; }  break;	/*  R5900, otherwise MDMX (TODO)  */
			case HI6_SQ:		{ wlen = 16;         signd = 0; }  break;	/*  R5900 ?  */

			/*  The rest:  */
			case HI6_LH:	{ wlen = 2; st = 0;            }  break;
			case HI6_LHU:	{ wlen = 2; st = 0; signd = 0; }  break;
			case HI6_LWU:	{           st = 0; signd = 0; }  break;
			case HI6_LWC1:	{           st = 0;            }  break;
			case HI6_LWC2:	{           st = 0;            }  break;
			case HI6_LWC3:	{           st = 0;            }  break;
			case HI6_LDC1:	{ wlen = 8; st = 0; signd = 0; }  break;
			case HI6_LDC2:	{ wlen = 8; st = 0; signd = 0; }  break;

			case HI6_SH:	{ wlen = 2;         signd = 0; }  break;
			case HI6_SWC1:	{                   signd = 0; }  break;
			case HI6_SWC2:	{                   signd = 0; }  break;
			case HI6_SWC3:	{                   signd = 0; }  break;
			case HI6_SDC1:	{ wlen = 8;         signd = 0; }  break;

			case HI6_LL:	{           st = 0; signd = 0; linked = 1; }  break;
			case HI6_LLD:	{ wlen = 8; st = 0; signd = 0; linked = 1; }  break;

			case HI6_SC:	{                   signd = 0; linked = 1; }  break;
			case HI6_SCD:	{ wlen = 8;         signd = 0; linked = 1; }  break;

			default:
				fatal("cannot be here\n");
				wlen = 4; st = 0; signd = 0;
			}

			/*
			 *  In the MIPS IV ISA, the 'lwc3' instruction is changed into 'pref'.
			 *  The pref instruction is emulated by not doing anything. :-)  TODO
			 */
			if (hi6 == HI6_LWC3)
				break;

			addr = cpu->gpr[rs] + imm;

			/*  Check for natural alignment:  */
			if ((addr & (wlen - 1)) != 0) {
				cpu_exception(cpu, st? EXCEPTION_ADES : EXCEPTION_ADEL,
				    0, addr, 0, 0, 0, 0);
				break;
			}

#if 0
			if (cpu->cpu_type.isa_level == 4 && (imm & (wlen - 1)) != 0)
				debug("WARNING: low bits of imm value not zero! (MIPS IV) "
				    "pc=%016llx", (long long)cpu->pc_last);
#endif

			/*
			 *  Load Linked: This initiates a Read-Modify-Write
			 *  sequence.
			 */
			if (linked) {
				if (st==0) {
					/*  st == 0:  Load  */
					cpu->rmw      = 1;
					cpu->rmw_addr = addr;
					cpu->rmw_len  = wlen;

					/*
					 *  COP0_LLADDR is updated for
					 *  diagnostic purposes, except for
					 *  CPUs in the R10000 family.
					 */
					if (cpu->cpu_type.exc_model != MMU10K)
						cp0->reg[COP0_LLADDR] =
						    (addr >> 4) & 0xffffffffULL;
				} else {
					/*
					 *  st == 1:  Store
					 *  If rmw is 0, then the store failed.
					 *  (This cache-line was written to by
					 *  someone else.)
					 */
					if (cpu->rmw == 0) {
						/*  The store failed:  */
						cpu->gpr[rt] = 0;

						/*
						 *  Operating systems that make use of ll/sc for
						 *  synchronization should implement back-off protocols
						 *  of their own, so this code should NOT be used:
						 *
						 *	cpu->instruction_delay = random() % (ncpus + 1);
						 *
						 *  Search for ENABLE_INSTRUCTION_DELAYS.
						 */
						break;
					}
				}
			} else {
				/*
				 *  If any kind of load or store occurs between an ll and an sc,
				 *  then the ll-sc sequence should fail.  (This is local to
				 *  each cpu.)
				 */
				cpu->rmw = 0;
			}

			if (st) {
				/*  store:  */
				int cpnr, success;

				if (hi6 == HI6_SWC3 || hi6 == HI6_SWC2 ||
				    hi6 == HI6_SDC1 || hi6 == HI6_SWC1) {
					cpnr = 1;
					switch (hi6) {
					case HI6_SWC3:	cpnr++;		/*  fallthrough  */
					case HI6_SWC2:	cpnr++;
					case HI6_SDC1:
					case HI6_SWC1:	if (cpu->coproc[cpnr] == NULL ||
							    (cached_pc <= 0x7fffffff && !(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)))
							    ) {
								cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
								cpnr = -1;
								break;
							} else {
								coproc_register_read(cpu, cpu->coproc[cpnr], rt, &value);
							}
							break;
					default:
							;
					}
					if (cpnr < 0)
						break;
				} else
					value = cpu->gpr[rt];

				if (wlen == 4) {
					/*  Special case for 32-bit stores... (perhaps not worth it)  */
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						d[0] = value & 0xff;         d[1] = (value >> 8) & 0xff;
						d[2] = (value >> 16) & 0xff; d[3] = (value >> 24) & 0xff;
					} else {
						d[3] = value & 0xff;         d[2] = (value >> 8) & 0xff;
						d[1] = (value >> 16) & 0xff; d[0] = (value >> 24) & 0xff;
					}
				} else if (wlen == 16) {
					value_hi = cpu->gpr_quadhi[rt];
					/*  Special case for R5900 128-bit stores:  */
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
						for (i=0; i<8; i++) {
							d[i] = (value >> (i*8)) & 255;
							d[i+8] = (value_hi >> (i*8)) & 255;
						}
					else
						for (i=0; i<8; i++) {
							d[i] = (value >> ((wlen-1-i)*8)) & 255;
							d[i + 8] = (value_hi >> ((wlen-1-i)*8)) & 255;
						}
				} else if (wlen == 1) {
					d[0] = value & 0xff;
				} else {
					/*  General case:  */
					uint64_t v = value;
					if (cpu->byte_order ==
					    EMUL_LITTLE_ENDIAN)
						for (i=0; i<wlen; i++) {
							d[i] = v & 255;
							v >>= 8;
						}
					else
						for (i=0; i<wlen; i++) {
							d[wlen-1-i] = v & 255;
							v >>= 8;
						}
				}
				success = memory_rw(cpu, cpu->mem, addr, d, wlen, MEM_WRITE, CACHE_DATA);
				if (!success) {
					/*  The store failed, and might have caused an exception.  */
					if (instruction_trace_cached && dataflag)
						debug("(failed)]\n");
					break;
				}
			} else {
				/*  load:  */
				int cpnr = 1;
				int success;

				success = memory_rw(cpu, cpu->mem, addr, d, wlen, MEM_READ, CACHE_DATA);
				if (!success) {
					/*  The load failed, and might have caused an exception.  */
					if (instruction_trace_cached && dataflag)
						debug("(failed)]\n");
					break;
				}

				if (wlen == 1)
					value = d[0] | (signd && (d[0]&128)? (-1 << 8) : 0);
				else if (wlen != 16) {
					/*  General case (except for 128-bit):  */
					int i;
					value = 0;
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						if (signd && (d[wlen-1] & 128)!=0)	/*  sign extend  */
							value = -1;
						for (i=wlen-1; i>=0; i--) {
							value <<= 8;
							value += d[i];
						}
					} else {
						if (signd && (d[0] & 128)!=0)		/*  sign extend  */
							value = -1;
						for (i=0; i<wlen; i++) {
							value <<= 8;
							value += d[i];
						}
					}
				} else {
					/*  R5900 128-bit quadword:  */
					int i;
					value_hi = 0;
					value = 0;
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
						for (i=wlen-1; i>=0; i--) {
							value_hi <<= 8;
							value_hi += (value >> 56) & 255;
							value <<= 8;
							value += d[i];
						}
					} else {
						for (i=0; i<wlen; i++) {
							value_hi <<= 8;
							value_hi += (value >> 56) & 255;
							value <<= 8;
							value += d[i];
						}
					}
					cpu->gpr_quadhi[rt] = value_hi;
				}

				switch (hi6) {
				case HI6_LWC3:	cpnr++;		/*  fallthrough  */
				case HI6_LDC2:
				case HI6_LWC2:	cpnr++;
				case HI6_LDC1:
				case HI6_LWC1:	if (cpu->coproc[cpnr] == NULL ||
						    (cached_pc <= 0x7fffffff && !(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)))
						    ) {
							cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
						} else {
							coproc_register_write(cpu, cpu->coproc[cpnr], rt, &value);
						}
						break;
				default:	cpu->gpr[rt] = value;
				}
			}

			if (linked && st==1) {
				/*
				 *  The store succeeded. Invalidate any other
				 *  cpu's store to this cache line, and then
				 *  return 1 in gpr rt:
				 *
				 *  (this is a semi-ugly hack using global
				 * 'cpus')
				 */
				for (i=0; i<ncpus; i++) {
					if (cpus[i]->rmw) {
						uint64_t yaddr = addr;
						uint64_t xaddr =
						    cpus[i]->rmw_addr;
						uint64_t mask;
						mask = ~(cpus[i]->
						    cache_linesize[CACHE_DATA]
						    - 1);
						xaddr &= mask;
						yaddr &= mask;
						if (xaddr == yaddr) {
							cpus[i]->rmw = 0;
							cpus[i]->rmw_addr = 0;
						}
					}
				}

				cpu->gpr[rt] = 1;
				cpu->rmw = 0;
			}

			if (instruction_trace_cached && dataflag) {
				char *t;
				switch (wlen) {
				case 2:		t = "0x%04llx"; break;
				case 4:		t = "0x%08llx"; break;
				case 8:		t = "0x%016llx"; break;
				case 16:	debug("0x%016llx", (long long)value_hi);
						t = "%016llx"; break;
				default:	t = "0x%02llx";
				}
				debug(t, (long long)value);
				debug("]\n");
			}
			break;
		case HI6_LWL:	/*  Unaligned load/store  */
		case HI6_LWR:
		case HI6_LDL:
		case HI6_LDR:
		case HI6_SWL:
		case HI6_SWR:
		case HI6_SDL:
		case HI6_SDR:
			/*  For L (Left):   address is the most significant byte  */
			/*  For R (Right):  address is the least significant byte  */
			addr = cpu->gpr[rs] + imm;

			is_left = 0;
			if (hi6 == HI6_SWL || hi6 == HI6_LWL ||
			    hi6 == HI6_SDL || hi6 == HI6_LDL)
				is_left = 1;

			wlen = 0; st = 0;
			signd = 0;	/*  sign extend after Load Word Left only  */
			if (hi6 == HI6_LWL)
				signd = 1;

			if (hi6 == HI6_LWL || hi6 == HI6_LWR)	{ wlen = 4; st = 0; }
			if (hi6 == HI6_SWL || hi6 == HI6_SWR)	{ wlen = 4; st = 1; }
			if (hi6 == HI6_LDL || hi6 == HI6_LDR)	{ wlen = 8; st = 0; }
			if (hi6 == HI6_SDL || hi6 == HI6_SDR)	{ wlen = 8; st = 1; }

			dir = 1;		/*  big endian, Left  */
			reg_dir = -1;
			reg_ofs = wlen - 1;	/*  byte offset in the register  */
			if (!is_left) {
				dir = -1 * dir;
				reg_ofs = 0;
				reg_dir = 1;
			}
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				dir = -1 * dir;

			result_value = cpu->gpr[rt];

			/*  Try to load and store, to make sure that all bytes in this store
			    will be allowed to go through:  */
			if (st) {
				for (i=0; i<wlen; i++) {
					unsigned char databyte;
					int ok;

					tmpaddr = addr + i*dir;
					/*  Have we moved into another word/dword? Then stop:  */
					if ( (tmpaddr & ~(wlen-1)) != (addr & ~(wlen-1)) )
						break;

					/*  Load and store one byte:  */
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_READ, CACHE_DATA);
					if (!ok)
						return 1;
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_WRITE, CACHE_DATA);
					if (!ok)
						return 1;
				}
			}

			for (i=0; i<wlen; i++) {
				unsigned char databyte;
				int ok;

				tmpaddr = addr + i*dir;
				/*  Have we moved into another word/dword? Then stop:  */
				if ( (tmpaddr & ~(wlen-1)) != (addr & ~(wlen-1)) )
					break;

				/*  debug("unaligned byte at %016llx, reg_ofs=%i reg=0x%016llx\n",
				    tmpaddr, reg_ofs, (long long)result_value);  */

				/*  Load or store one byte:  */
				if (st) {
					databyte = (result_value >> (reg_ofs * 8)) & 255;
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_WRITE, CACHE_DATA);
					/*  if (instruction_trace_cached && dataflag)
						debug("%02x ", databyte);  */
				} else {
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_READ, CACHE_DATA);
					/*  if (instruction_trace_cached && dataflag)
						debug("%02x ", databyte);  */
					result_value &= ~((uint64_t)0xff << (reg_ofs * 8));
					result_value |= (uint64_t)databyte << (reg_ofs * 8);
				}

				/*  Return immediately if exception.  */
				if (!ok)
					return 1;

				reg_ofs += reg_dir;
			}

			if (!st)
				cpu->gpr[rt] = result_value;

			/*  Sign extend for 32-bit load lefts:  */
			if (!st && signd && wlen == 4) {
				cpu->gpr[rt] &= 0xffffffffULL;
				if (cpu->gpr[rt] & 0x80000000ULL)
					cpu->gpr[rt] |= 0xffffffff00000000ULL;
			}

			if (instruction_trace_cached && dataflag) {
				char *t;
				switch (wlen) {
				case 2:		t = "0x%04llx"; break;
				case 4:		t = "0x%08llx"; break;
				case 8:		t = "0x%016llx"; break;
				default:	t = "0x%02llx";
				}
				debug(t, (long long)cpu->gpr[rt]);
				debug("]\n");
			}

			break;
		}
		break;
	case HI6_REGIMM:
		regimm5 = instr[2] & 0x1f;

		if (show_opcode_statistics)
			cpu->stats__regimm[regimm5] ++;

		switch (regimm5) {
		case REGIMM_BLTZ:
		case REGIMM_BGEZ:
		case REGIMM_BLTZL:
		case REGIMM_BGEZL:
		case REGIMM_BLTZAL:
		case REGIMM_BLTZALL:
		case REGIMM_BGEZAL:
		case REGIMM_BGEZALL:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			imm = (instr[1] << 8) + instr[0];
			if (imm >= 32768)		/*  signed 16-bit  */
				imm -= 65536;

			if (instruction_trace_cached) {
				instr_mnem = NULL;
				if (regimm5 == REGIMM_BLTZ)	instr_mnem = "bltz";
				if (regimm5 == REGIMM_BGEZ)	instr_mnem = "bgez";
				if (regimm5 == REGIMM_BLTZL)	instr_mnem = "bltzl";
				if (regimm5 == REGIMM_BGEZL)	instr_mnem = "bgezl";
				if (regimm5 == REGIMM_BLTZAL)	instr_mnem = "bltzal";
				if (regimm5 == REGIMM_BLTZALL)	instr_mnem = "bltzall";
				if (regimm5 == REGIMM_BGEZAL)	instr_mnem = "bgezal";
				if (regimm5 == REGIMM_BGEZALL)	instr_mnem = "bgezall";
				debug("%s\tr%i,%016llx\n", instr_mnem, rs, cached_pc + (imm << 2));
			}

			cond = 0;
			and_link = 0;
			likely = 0;

			switch (regimm5) {
			case REGIMM_BLTZL:	likely = 1;
			case REGIMM_BLTZ:	cond = (cpu->gpr[rs] & ((uint64_t)1 << 63)) != 0;
						break;
			case REGIMM_BGEZL:	likely = 1;
			case REGIMM_BGEZ:	cond = (cpu->gpr[rs] & ((uint64_t)1 << 63)) == 0;
						break;

			case REGIMM_BLTZALL:	likely = 1;
			case REGIMM_BLTZAL:	and_link = 1;
						cond = (cpu->gpr[rs] & ((uint64_t)1 << 63)) != 0;
						break;
			case REGIMM_BGEZALL:	likely = 1;
			case REGIMM_BGEZAL:	and_link = 1;
						cond = (cpu->gpr[rs] & ((uint64_t)1 << 63)) == 0;
						break;
			}

			if (and_link)
				cpu->gpr[31] = cached_pc + 4;

			if (cond) {
				cpu->delay_slot = TO_BE_DELAYED;
				cpu->delay_jmpaddr = cached_pc + (imm << 2);
			} else {
				if (likely)
					cpu->nullify_next = 1;		/*  nullify delay slot  */
			}

			break;
		default:
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented regimm5 = 0x%02x\n", regimm5);
			cpu->running = 0;
			return 1;
		}
		break;
	case HI6_J:
	case HI6_JAL:
		if (cpu->delay_slot) {
			fatal("j/jal: jump inside a jump's delay slot, or similar. TODO\n");
			cpu->running = 0;
			return 1;
		}
		imm = ((instr[3] & 3) << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		imm <<= 2;

		if (hi6 == HI6_JAL)
			cpu->gpr[31] = cached_pc + 4;		/*  pc already increased by 4 earlier  */

		addr = cached_pc & ~((1 << 28) - 1);
		addr |= imm;

		if (instruction_trace_cached) {
			int offset;
			char *symbol = get_symbol_name(addr, &offset);

			instr_mnem = NULL;
			if (hi6 == HI6_J)	instr_mnem = "j";
			if (hi6 == HI6_JAL)	instr_mnem = "jal";

			if (symbol != NULL)
				debug("%s\t0x%016llx\t\t<%s>\n", instr_mnem, addr, symbol);
			else
				debug("%s\t0x%016llx\n", instr_mnem, addr);
		}

		cpu->delay_slot = TO_BE_DELAYED;
		cpu->delay_jmpaddr = addr;

		if (!quiet_mode_cached && hi6 == HI6_JAL) {
			cpu->show_trace_delay = 2;
			cpu->show_trace_addr = addr;
		}

		break;
	case HI6_COP0:
	case HI6_COP1:
	case HI6_COP2:
	case HI6_COP3:
		imm = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		imm &= ((1 << 26) - 1);

		cpnr = 0;
		if (hi6 == HI6_COP0)	cpnr = 0;
		if (hi6 == HI6_COP1)	cpnr = 1;
		if (hi6 == HI6_COP2)	cpnr = 2;
		if (hi6 == HI6_COP3)	cpnr = 3;

		/*
		 *  If there is no coprocessor nr cpnr, or we are running in
		 *  userland and the coprocessor is not marked as Useable in
		 *  the status register of CP0, then we get an exception:
		 *
		 *  TODO:  More robust checking for user code (ie R4000 stuff)
		 */
		if (cpu->coproc[cpnr] == NULL ||
		    (cached_pc <= 0x7fffffff && !(cp0->reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)))
		    ) {
			if (instruction_trace_cached)
				debug("cop%i\t0x%08x => coprocessor unusable\n", cpnr, (int)imm);

			cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
		} else {
			/*  The coproc_function code should output instruction trace.  */

			coproc_function(cpu, cpu->coproc[cpnr], imm);
		}
		break;
	case HI6_CACHE:
		rt   = ((instr[3] & 3) << 3) + (instr[2] >> 5);	/*  base  */
		copz = instr[2] & 31;
		imm  = (instr[1] << 8) + instr[0];

		cache_op    = copz >> 2;
		which_cache = copz & 3;

		if (instruction_trace_cached) {
			int showtag = 0;

			debug("cache\t0x%02x,0x%04x(r%i)", copz, imm, rt);
			if (which_cache==0)	debug("  [ primary I-cache");
			if (which_cache==1)	debug("  [ primary D-cache");
			if (which_cache==2)	debug("  [ secondary I-cache");
			if (which_cache==3)	debug("  [ secondary D-cache");
			debug(", ");
			if (cache_op==0)	debug("index invalidate");
			if (cache_op==1)	debug("index load tag");
			if (cache_op==2)	debug("index store tag"), showtag=1;
			if (cache_op==3)	debug("create dirty exclusive");
			if (cache_op==4)	debug("hit invalidate");
			if (cache_op==5)	debug("fill OR hit writeback invalidate");
			if (cache_op==6)	debug("hit writeback");
			if (cache_op==7)	debug("hit set virtual");
			debug(", r%i=0x%016llx", rt, (long long)cpu->gpr[rt]);

			if (showtag)
				debug(", taghi=%08lx lo=%08lx",
				    (long)cp0->reg[COP0_TAGDATA_HI],
				    (long)cp0->reg[COP0_TAGDATA_LO]);

			debug(" ]\n");
		}

		/*
		 *  TODO:  The cache instruction is implementation dependant.
		 *  This is really ugly.
		 */

/*		if (cpu->cpu_type.mmu_model == MMU10K) {  */
/*			printf("taghi=%08lx taglo=%08lx\n",
			    (long)cp0->reg[COP0_TAGDATA_HI],
			    (long)cp0->reg[COP0_TAGDATA_LO]);
*/
			if (cp0->reg[COP0_TAGDATA_HI] == 0 &&
			    cp0->reg[COP0_TAGDATA_LO] == 0) {
				/*  Normal cache operation:  */
				cpu->r10k_cache_disable_TODO = 0;
			} else {
				/*  Dislocate the cache:  */
				cpu->r10k_cache_disable_TODO = 1;
			}
/*		}  */

		break;
	case HI6_SPECIAL2:
		special6 = instr[0] & 0x3f;

		if (show_opcode_statistics)
			cpu->stats__special2[special6] ++;

		instrword = (instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];

		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		rd = (instr[1] >> 3) & 31;

		/*  printf("special2 %08x  rs=0x%02x rt=0x%02x rd=0x%02x\n", instrword, rs,rt,rd);  */

		/*
		 *  Many of these can be found in the R5000 docs, or figured out
		 *  by studying binutils source code for MIPS instructions.
		 */

		if ((instrword & 0xfc0007ffULL) == 0x70000000) {
			if (instruction_trace_cached)
				debug("madd\tr(r%i,)r%i,r%i\n", rd, rs, rt);
			{
				int32_t a, b;
				int64_t c;
				a = (int32_t)cpu->gpr[rs];
				b = (int32_t)cpu->gpr[rt];
				c = a * b;
				c += (cpu->lo & 0xffffffffULL)
				    + (cpu->hi << 32);
				cpu->lo = (int64_t)((int32_t)c);
				cpu->hi = (int64_t)((int32_t)(c >> 32));

				/*
				 *  The R5000 manual says that rd should be all zeros,
				 *  but it isn't on R5900.   I'm just guessing here that
				 *  it stores the value in register rd, in addition to hi/lo.
				 *  TODO
				 */
				if (rd != 0)
					cpu->gpr[rd] = cpu->lo;
			}
		} else if ((instrword & 0xffff07ffULL) == 0x70000209
		    || (instrword & 0xffff07ffULL) == 0x70000249) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	pmfhi/pmflo rd
			 *
			 *  If the lowest 8 bits of the instruction word are 0x09, it's a pmfhi.
			 *  If the lowest bits are 0x49, it's a pmflo.
			 *
			 *  A wild guess is that this is a 128-bit version of mfhi/mflo.
			 *  For now, this is implemented as 64-bit only.  (TODO)
			 */
			if (instr[0] == 0x49) {
				if (instruction_trace_cached)
					debug("pmflo\tr%i rs=%i\n", rd);
				cpu->gpr[rd] = cpu->lo;
			} else {
				if (instruction_trace_cached)
					debug("pmfhi\tr%i rs=%i\n", rd);
				cpu->gpr[rd] = cpu->hi;
			}
		} else if ((instrword & 0xfc1fffff) == 0x70000269 || (instrword & 0xfc1fffff) == 0x70000229) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	pmthi/pmtlo rs		(pmtlo = 269, pmthi = 229)
			 *
			 *  A wild guess is that this is a 128-bit version of mthi/mtlo.
			 *  For now, this is implemented as 64-bit only.  (TODO)
			 */
			if (instr[0] == 0x69) {
				if (instruction_trace_cached)
					debug("pmtlo\tr%i rs=%i\n", rs);
				cpu->lo = cpu->gpr[rs];
			} else {
				if (instruction_trace_cached)
					debug("pmthi\tr%i rs=%i\n", rs);
				cpu->hi = cpu->gpr[rs];
			}
		} else if ((instrword & 0xfc0007ff) == 0x700004a9) {
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	por dst,src,src2  ==> rs=src rt=src2 rd=dst
			 *
			 *  A wild guess is that this is a 128-bit "or" between two registers.
			 *  For now, let's just or using 64-bits.  (TODO)
			 */
			if (instruction_trace_cached)
				debug("por\tr%i,r%i,r%i\n", rd, rs, rt);
			cpu->gpr[rd] = cpu->gpr[rs] | cpu->gpr[rt];
		} else if ((instrword & 0xfc0007ff) == 0x70000488) {
			/*
			 *  R5900 "undocumented" pextlw. TODO: find out if this is correct.
			 *  It seems that this instruction is used to combine two 32-bit
			 *  words into a 64-bit dword, typically before a sd (store dword).
			 */
			if (instruction_trace_cached)
				debug("pextlw\tr%i,r%i,r%i\n", rd, rs, rt);
			cpu->gpr[rd] =
			    ((cpu->gpr[rs] & 0xffffffffULL) << 32)		/*  TODO: switch rt and rs?  */
			    | (cpu->gpr[rt] & 0xffffffffULL);
		} else {
			if (!instruction_trace_cached) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented special_2 = 0x%02x, rs=0x%02x rt=0x%02x rd=0x%02x\n",
			    special6, rs, rt, rd);
			cpu->running = 0;
			return 1;
		}
		break;
	default:
		if (!instruction_trace_cached) {
			fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, cpu->pc_last,
			    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
		}
		fatal("unimplemented hi6 = 0x%02x\n", hi6);
		cpu->running = 0;
		return 1;
	}

	/*  Don't put any code here, after the switch statement!  */

	/*  One instruction executed.  */
	return 1;
}


/*
 *  cpu_show_cycles():
 *
 *  If automatic adjustment of clock interrupts is turned on, then recalculate
 *  emulated_hz.  Also, if show_nr_of_instructions is on, then print a
 *  line to stdout about how many instructions/cycles have been executed so
 *  far.
 */
void cpu_show_cycles(struct timeval *starttime, int64_t ncycles, int forced)
{
	int offset;
	char *symbol;
	int64_t mseconds, ninstrs;
	struct timeval tv;
	int h, m, s, ms, d;

	static int64_t mseconds_last = 0;
	static int64_t ninstrs_last = -1;

	gettimeofday(&tv, NULL);
	mseconds = (tv.tv_sec - starttime->tv_sec) * 1000
	         + (tv.tv_usec - starttime->tv_usec) / 1000;

	if (mseconds == 0)
		mseconds = 1;

	if (mseconds - mseconds_last == 0)
		mseconds ++;

	ninstrs = ncycles * cpus[bootstrap_cpu]->cpu_type.instrs_per_cycle;

	if (automatic_clock_adjustment) {
		static int first_adjustment = 1;

		/*  Current nr of cycles per second:  */
		int64_t cur_cycles_per_second = 1000 *
		    (ninstrs-ninstrs_last) / (mseconds-mseconds_last)
		    / cpus[bootstrap_cpu]->cpu_type.instrs_per_cycle;

		if (cur_cycles_per_second < 1000000)
			cur_cycles_per_second = 1000000;

		if (first_adjustment) {
			emulated_hz = cur_cycles_per_second;
			first_adjustment = 0;
		} else
			emulated_hz = (7 * emulated_hz +
			    cur_cycles_per_second) / 8;

		debug("[ updating emulated_hz to %lli Hz ]\n",
		    (long long)emulated_hz);
	}


	/*  RETURN here, unless show_nr_of_instructions (-N) is turned on:  */
	if (!show_nr_of_instructions && !forced)
		goto do_return;


	printf("[ ");

	if (!automatic_clock_adjustment) {
		d = emulated_hz / 1000;
		if (d < 1)
			d = 1;
		ms = ncycles / d;
		h = ms / 3600000;
		ms -= 3600000 * h;
		m = ms / 60000;
		ms -= 60000 * m;
		s = ms / 1000;
		ms -= 1000 * s;

		printf("emulated time = %02i:%02i:%02i.%03i; ", h, m, s, ms);
	}

	printf("total nr of cycles = %lli", (long long) ncycles);

	if (cpus[bootstrap_cpu]->cpu_type.instrs_per_cycle > 1)
		printf(" (%lli instructions)", (long long) ninstrs);

	printf(", instr/sec: %lli cur, %lli avg, ",
	    (long long) ((long long)1000 * (ninstrs-ninstrs_last)
		/ (mseconds-mseconds_last)),
	    (long long) ((long long)1000 * ninstrs / mseconds));

	symbol = get_symbol_name(cpus[bootstrap_cpu]->pc, &offset);

	printf(" pc=%016llx <%s> ]\n",
	    (long long)cpus[bootstrap_cpu]->pc, symbol? symbol : "no symbol");

do_return:
	ninstrs_last = ninstrs;
	mseconds_last = mseconds;
}


/*
 *  cpu_show_full_statistics():
 *
 *  Show detailed statistics on opcode usage on each cpu.
 */
void cpu_show_full_statistics(struct cpu **cpus)
{
	int i, s1, s2;

	for (i=0; i<ncpus; i++) {
		printf("cpu%i opcode statistics:\n", i);
		for (s1=0; s1<N_HI6; s1++) {
			if (cpus[i]->stats_opcode[s1] > 0)
				printf("  opcode %02x (%7s): %li\n", s1,
				    hi6_names[s1], cpus[i]->stats_opcode[s1]);
			if (s1 == HI6_SPECIAL)
				for (s2=0; s2<N_SPECIAL; s2++)
					if (cpus[i]->stats__special[s2] > 0)
						printf("      special %02x (%7s): %li\n",
						    s2, special_names[s2], cpus[i]->stats__special[s2]);
			if (s1 == HI6_REGIMM)
				for (s2=0; s2<N_REGIMM; s2++)
					if (cpus[i]->stats__regimm[s2] > 0)
						printf("      regimm %02x (%7s): %li\n",
						    s2, regimm_names[s2], cpus[i]->stats__regimm[s2]);
			if (s1 == HI6_SPECIAL2)
				for (s2=0; s2<N_SPECIAL; s2++)
					if (cpus[i]->stats__special2[s2] > 0)
						printf("      special2 %02x (%7s): %li\n",
						    s2, special2_names[s2], cpus[i]->stats__special2[s2]);
		}
	}
}


/*
 *  cpu_run():
 *
 *  Run instructions from all CPUs, until all CPUs have halted.
 */
int cpu_run(struct cpu **cpus, int ncpus)
{
	int te;
	int64_t max_instructions_cached = max_instructions;
	int64_t ncycles = 0, ncycles_chunk_end, ncycles_show = 0;
	int64_t ncycles_flush = 0, ncycles_flushx11 = 0;
		/*  TODO: how about overflow of ncycles?  */
	int running, ncpus_cached = ncpus;
	struct timeval starttime;
	int a_few_cycles = 1048576, a_few_instrs;

	/*
	 *  Instead of doing { one cycle, check hardware ticks }, we
	 *  can do { n cycles, check hardware ticks }, as long as
	 *  n is at most as much as the lowest number of cycles/tick
	 *  for any hardware device.
	 */
	for (te=0; te<cpus[0]->n_tick_entries; te++) {
		if (cpus[0]->ticks_reset_value[te] < a_few_cycles)
			a_few_cycles = cpus[0]->ticks_reset_value[te];
	}

	/*  debug("cpu_run(): a_few_cycles = %i\n", a_few_cycles);  */

	/*  For performance measurement:  */
	gettimeofday(&starttime, NULL);

	/*  The main loop:  */
	running = 1;
	while (running) {
		ncycles_chunk_end = ncycles + (1 << 15);

		a_few_instrs = a_few_cycles *
		    cpus[0]->cpu_type.instrs_per_cycle;

		/*  Do a chunk of cycles:  */
		do {
			int i, j, te, cpu0instrs, a_few_instrs2;

			running = 0;
			cpu0instrs = 0;

			/*
			 *  Run instructions from each CPU:
			 */

			/*  Is any cpu alive?  */
			for (i=0; i<ncpus_cached; i++)
				if (cpus[i]->running)
					running = 1;

			/*  CPU 0 is special, cpu0instr must be updated.  */
			for (j=0; j<a_few_instrs; j++) {
				int instrs_run;
				if (!cpus[0]->running)
					break;
				do {
					instrs_run =
					    cpu_run_instr(cpus[0]);
				} while (instrs_run == 0);
				cpu0instrs += instrs_run;
			}

			/*  CPU 1 and up:  */
			for (i=1; i<ncpus_cached; i++) {
				a_few_instrs2 = a_few_cycles *
				    cpus[i]->cpu_type.instrs_per_cycle;
				for (j=0; j<a_few_instrs2; j++)
					if (cpus[i]->running) {
						int instrs_run = 0;
						while (!instrs_run)
							instrs_run = cpu_run_instr(cpus[i]);
					}
			}

			/*
			 *  Hardware 'ticks':  (clocks, interrupt sources...)
			 *
			 *  Here, cpu0instrs is the number of instructions
			 *  executed on cpu0.  (TODO: don't use cpu 0 for this,
			 *  use some kind of "mainbus" instead.)  Hardware
			 *  ticks are not per instruction, but per cycle,
			 *  so we divide by the number of
			 *  instructions_per_cycle for cpu0.
			 *
			 *  TODO:  This doesn't work in a machine with, say,
			 *  a mixture of R3000, R4000, and R10000 CPUs, if
			 *  there ever was such a thing.
			 *
			 *  TODO 2:  A small bug occurs if cpu0instrs isn't
			 *  evenly divisible by instrs_per_cycle. We then
			 *  cause hardware ticks a fraction of a cycle too
			 *  often.
			 */
			i = cpus[0]->cpu_type.instrs_per_cycle;
			switch (i) {
			case 1:	break;
			case 2:	cpu0instrs >>= 1; break;
			case 4:	cpu0instrs >>= 2; break;
			default:
				cpu0instrs /= i;
			}

			for (te=0; te<cpus[0]->n_tick_entries; te++) {
				cpus[0]->ticks_till_next[te] -= cpu0instrs;

				if (cpus[0]->ticks_till_next[te] <= 0) {
					while (cpus[0]->ticks_till_next[te] <= 0)
						cpus[0]->ticks_till_next[te] +=
						    cpus[0]->ticks_reset_value[te];

					cpus[0]->tick_func[te](cpus[0], cpus[0]->tick_extra[te]);
				}
			}

			ncycles += cpu0instrs;
		} while (running && (ncycles < ncycles_chunk_end));

		/*  Check for X11 events:  */
		if (use_x11) {
			if (ncycles > ncycles_flushx11 + (1<<16)) {
				x11_check_event();
				ncycles_flushx11 = ncycles;
			}
		}

		/*  If we've done buffered console output,
		    the flush stdout every now and then:  */
		if (ncycles > ncycles_flush + (1<<16)) {
			console_flush();
			ncycles_flush = ncycles;
		}

		if (ncycles > ncycles_show + (1<<22)) {
			cpu_show_cycles(&starttime, ncycles, 0);
			ncycles_show = ncycles;
		}

		if (max_instructions_cached != 0 &&
		    ncycles >= max_instructions_cached)
			running = 0;
	}

	/*
	 *  Two last ticks of every hardware device.  This will allow
	 *  framebuffers to draw the last updates to the screen before
	 *  halting.
	 *  (TODO: do this per cpu?)
	 */
        for (te=0; te<cpus[0]->n_tick_entries; te++) {
		cpus[0]->tick_func[te](cpus[0], cpus[0]->tick_extra[te]);
		cpus[0]->tick_func[te](cpus[0], cpus[0]->tick_extra[te]);
	}

	debug("All CPUs halted.\n");

	if (show_nr_of_instructions || !quiet_mode)
		cpu_show_cycles(&starttime, ncycles, 1);

	if (show_opcode_statistics)
		cpu_show_full_statistics(cpus);

	return 0;
}

