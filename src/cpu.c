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
 *  $Id: cpu.c,v 1.36 2004-03-06 12:55:20 debug Exp $
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
extern int max_instructions;
extern struct cpu **cpus;
extern int ncpus;
extern int show_opcode_statistics;

extern int n_dumppoints;
extern uint64_t dumppoint_pc[MAX_PC_DUMPPOINTS];
extern int dumppoint_flag_r[MAX_PC_DUMPPOINTS];

char *exception_names[] = EXCEPTION_NAMES;

static char *hi6_names[] = HI6_NAMES;
static char *regimm_names[] = REGIMM_NAMES;
static char *special_names[] = SPECIAL_NAMES;
static char *special2_names[] = SPECIAL2_NAMES;

/*  Ugly, but needed for kn230 and kn02 "shared" interrupts:  */
extern struct kn230_csr *kn230_csr;
extern struct kn02_csr *kn02_csr;


/*
 *  cpu_new():
 *
 *  Create a new cpu object.
 */
struct cpu *cpu_new(struct memory *mem, int cpu_id, char *cpu_type_name)
{
	struct cpu *cpu;
	int i;
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
		fprintf(stderr, "cpu_new(): unknown cpu type '%s'\n", cpu_type_name);
		exit(1);
	}

	for (i=CACHE_DATA; i<=CACHE_INSTRUCTION; i++) {
		cpu->cache_size[i] = 65536;
		cpu->cache[i] = malloc(cpu->cache_size[i]);
		if (cpu->cache[i] == NULL) {
			fprintf(stderr, "out of memory\n");
		}
	}

	cpu->coproc[0] = coproc_new(cpu, 0);	/*  System control, MMU  */
	cpu->coproc[1] = coproc_new(cpu, 1);	/*  FPU  */

	return cpu;
}


/*
 *  cpu_add_tickfunction():
 *
 *  Adds a tick function (a function called every now and then,
 *  depending on clock cycle count).
 */
void cpu_add_tickfunction(struct cpu *cpu, void (*func)(struct cpu *, void *), void *extra, int clockshift)
{
	int n = cpu->n_tick_entries;

	if (n >= MAX_TICK_FUNCTIONS) {
		fprintf(stderr, "cpu_add_tickfunction(): too many tick functions\n");
		exit(1);
	}

	cpu->tick_shift[n] = clockshift;
	cpu->tick_func[n]  = func;
	cpu->tick_extra[n] = extra;

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
 */
void show_trace(struct cpu *cpu, uint64_t addr)
{
	int offset, x;
	int n_args_to_print;
	char strbuf[50];
	char *symbol;

	if (!show_trace_tree)
		return;

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

	if (irq_nr < 2 || irq_nr >= 8)
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

	if (irq_nr < 2 || irq_nr >= 8)
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
 *	pagemask	pagemask (for some exceptions)
 *	coproc_nr	coprocessor number (for some exceptions)
 *	vaddr_vpn2	vpn2 (for some exceptions)
 *	vaddr_asid	asid (for some exceptions)
 *	x_64		64-bit mode for R4000-style tlb misses
 */
void cpu_exception(struct cpu *cpu, int exccode, int tlb, uint64_t vaddr,
	uint64_t pagemask, int coproc_nr, uint64_t vaddr_vpn2, int vaddr_asid, int x_64)
{
	int offset, x;
	char *symbol = "";

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

	if (tlb && vaddr == 0) {
		fatal("warning: NULL reference, exception %s, pc->last=%08llx <%s>\n",
		    exception_names[exccode], (long long)cpu->pc_last, symbol? symbol : "(no symbol)");
/*		tlb_dump = 1;  */
	}

	if (vaddr > 0 && vaddr < 0x1000) {
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
		if ((vaddr >> 32) == 0 && (vaddr & 0x80000000))
			cpu->coproc[0]->reg[COP0_BADVADDR] |= (uint64_t) 0xffffffff00000000;
	}
#endif

	if ((exccode >= EXCEPTION_MOD && exccode <= EXCEPTION_ADES) ||
	    exccode == EXCEPTION_VCEI || exccode == EXCEPTION_VCED || tlb) {
		cpu->coproc[0]->reg[COP0_BADVADDR] = vaddr;
		/*  sign-extend vaddr, if it is 32-bit  */
		if ((vaddr >> 32) == 0 && (vaddr & 0x80000000))
			cpu->coproc[0]->reg[COP0_BADVADDR] |= (uint64_t) 0xffffffff00000000;

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
		if (tlb && !(vaddr & 0x80000000) &&
		    (exccode == EXCEPTION_TLBL || exccode == EXCEPTION_TLBS) )
			cpu->pc = 0xffffffff80000000;
		else
			cpu->pc = 0xffffffff80000080;
	} else {
		/*  R4000:  */
		if (tlb && (exccode == EXCEPTION_TLBL || exccode == EXCEPTION_TLBS)
		    && !(cpu->coproc[0]->reg[COP0_STATUS] & STATUS_EXL)) {
			if (x_64)
				cpu->pc = 0xffffffff80000080;
			else
				cpu->pc = 0xffffffff80000000;
		} else
			cpu->pc = 0xffffffff80000180;
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
 *  Execute one instruction on a cpu.  If we are in a delay slot, set cpu->pc
 *  to cpu->delay_jmpaddr after the instruction is executed.
 */
int cpu_run_instr(struct cpu *cpu, long *instrcount)
{
	struct coproc *cp0 = cpu->coproc[0];
	int instr_fetched;
	int i;
	int dcount;
	unsigned char instr[4];
	int hi6, special6, regimm5, rd, rs, rt, sa, imm;
	int copz, stype, which_cache, cache_op;
	char *instr_mnem = NULL;			/*  for instruction trace  */

	int enabled, mask, statusmask;			/*  interrupt delivery  */

	int cond=0, likely, and_link;

	uint64_t dir, is_left, reg_ofs, reg_dir;	/*  for unaligned load/store  */
	uint64_t tmpvalue, tmpaddr;

	int cpnr;					/*  coprocessor nr  */

	uint64_t addr, result_value;			/*  for load/store  */
	int wlen, st, signd, linked, dataflag = 0;
	unsigned char d[8];


	/*
	 *  Hardware 'ticks':  (clocks, interrupt sources...)
	 */
	dcount = cpu->cpu_type.flags & DCOUNT? 1 : 0;
	for (i=0; i<cpu->n_tick_entries; i++)
		if (((*instrcount) & ((1 << (cpu->tick_shift[i] + dcount))-1)) == 0)
			cpu->tick_func[i](cpu, cpu->tick_extra[i]);


	if (cpu->instruction_delay > 0) {
		cpu->instruction_delay --;
		return 0;
	}

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

	if (cpu->last_was_jumptoself > 0)
		cpu->last_was_jumptoself --;

	/*  Hardwire the zero register to 0:  */
	cpu->gpr[GPR_ZERO] = 0;


	/*  Check PC dumppoints:  */
	for (i=0; i<n_dumppoints; i++)
		if (cpu->pc == dumppoint_pc[i]) {
			instruction_trace = 1;
			if (dumppoint_flag_r[i])
				register_dump = 1;
		}

	/*  Dump CPU registers for debugging:  */
	if (register_dump) {
		debug("\n");
		cpu_register_dump(cpu);
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
			if (cpu->gpr[i] & 0x80000000)
				cpu->gpr[i] |= 0xffffffff00000000;
		}
		for (i=0; i<32; i++) {
			cpu->coproc[0]->reg[i] &= 0xffffffff;
			if (cpu->coproc[0]->reg[i] & 0x80000000)
				cpu->coproc[0]->reg[i] |= 0xffffffff00000000;
		}
		for (i=0; i<cpu->coproc[0]->nr_of_tlbs; i++) {
			cpu->coproc[0]->tlbs[i].hi &= 0xffffffff;
			if (cpu->coproc[0]->tlbs[i].hi & 0x80000000)
				cpu->coproc[0]->tlbs[i].hi |= 0xffffffff00000000;
			cpu->coproc[0]->tlbs[i].lo0 &= 0xffffffff;
			if (cpu->coproc[0]->tlbs[i].lo0 & 0x80000000)
				cpu->coproc[0]->tlbs[i].lo0 |= 0xffffffff00000000;
		}
	}
#endif

#ifdef HALT_IF_PC_ZERO
	/*  Halt if PC = 0:  */
	if (cpu->pc == 0) {
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
	if ((cpu->pc & 0xfff00000) == 0xbfc00000 && prom_emulation) {
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
			cpu->delay_slot = NOT_DELAYED;
			cpu->trace_tree_depth --;
			return 0;
		}
	}

	/*  Remember where we are, in case of interrupt or exception:  */
	cpu->pc_last = cpu->pc;

	if (cpu->show_trace_delay > 0) {
		cpu->show_trace_delay --;
		if (cpu->show_trace_delay == 0)
			show_trace(cpu, cpu->show_trace_addr);
	}

	/*  Decrease the MFHI/MFLO delays:  */
	if (cpu->mfhi_delay > 0)
		cpu->mfhi_delay--;
	if (cpu->mflo_delay > 0)
		cpu->mflo_delay--;

	/*  Read an instruction from memory:  */
#ifdef SUPPORT_MIPS16
	if (cpu->mips16 && (cpu->pc & 1)) {
		/*  16-bit instruction word:  */
		unsigned char instr16[2];
		int mips16_offset = 0;

		if (!memory_rw(cpu, cpu->mem, cpu->pc ^ 1, &instr16[0], sizeof(instr16), MEM_READ, CACHE_INSTRUCTION))
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
			if (instruction_trace)
				debug("cpu%i @ %016llx: %02x%02x\t\t\textend\n",
				    cpu->cpu_id, (cpu->pc_last ^ 1) + mips16_offset,
				    instr16[1], instr16[0]);

			/*  instruction with extend:  */
			mips16_offset += 2;
			if (!memory_rw(cpu, cpu->mem, (cpu->pc ^ 1) + mips16_offset, &instr16[0], sizeof(instr16), MEM_READ, CACHE_INSTRUCTION))
				return 0;

			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				int tmp;
				tmp  = instr16[0]; instr16[0] = instr16[1]; instr16[1] = tmp;
			}
		}

		/*  Advance the program counter:  */
		cpu->pc += sizeof(instr16) + mips16_offset;

		if (instruction_trace) {
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
		/*  32-bit instruction word:  */
		instr_fetched = memory_rw(cpu, cpu->mem, cpu->pc, &instr[0], sizeof(instr), MEM_READ, CACHE_INSTRUCTION);

		if (!instr_fetched)
			return 0;

		if (bintrans_enable && (cpu->delay_slot==0 && cpu->nullify_next==0)) {
			/*
			 *  Binary translation:
			 */
			int result = 0;
			uint64_t paddr = cpu->mem->bintrans_last_paddr;
			int chunk_nr = cpu->mem->bintrans_last_chunk_nr;

			if (instr_fetched != INSTR_BINTRANS) {
				/*  Cache miss:  */

				/*  debug("BINTRANS cache miss (pc = 0x%08llx, paddr = 0x%08llx)\n",
				    (long long)cpu->pc, (long long)paddr);  */

				result = bintrans_try_to_add(cpu, cpu->mem, paddr, &chunk_nr);
			}

			if (instr_fetched == INSTR_BINTRANS || result) {
				/*  Cache hit:  */

				/*  debug("BINTRANS cache hit (pc = 0x%08llx, paddr = 0x%08llx, hit chunk_nr = %i)\n",
				    (long long)cpu->pc, (long long)paddr, chunk_nr);  */

				if (instruction_trace) {
					int offset;
					char *symbol = get_symbol_name(cpu->pc_last, &offset);
					if (symbol != NULL && offset==0)
						debug("<%s>\n", symbol);

					debug("cpu%i @ %016llx: %02x%02x%02x%02x%s\tbintrans",
					    cpu->cpu_id, cpu->pc_last,
					    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
				}

				result = bintrans_try_to_run(cpu, cpu->mem, paddr, chunk_nr);

				if (result) {
					if (instruction_trace)
						printf("\n");
					/*  TODO: misc stuff?  */

					return 0;
				} else {
					if (instruction_trace)
						printf(" (failed)\n");
				}
			}
		}

		/*  Advance the program counter:  */
		cpu->pc += sizeof(instr);

		/*  TODO:  If Reverse-endian is set in the status cop0 register, and
			we are in usermode, then reverse endianness!  */

		/*  The rest of the code is written for little endian, so swap if neccessary:  */
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			int tmp, tmp2;
			tmp  = instr[0]; instr[0] = instr[3]; instr[3] = tmp;
			tmp2 = instr[1]; instr[1] = instr[2]; instr[2] = tmp2;
		}

		if (instruction_trace) {
			int offset;
			char *symbol = get_symbol_name(cpu->pc_last, &offset);
			if (symbol != NULL && offset==0)
				debug("<%s>\n", symbol);

			debug("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, cpu->pc_last,
			    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
		}
	}


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
		/*  TODO: &1 ==> double count blah blah  */
		if ((*instrcount) & 1)
			cp0->reg[COP0_COUNT] ++;

		cp0->reg[COP0_RANDOM] --;
		if ((int64_t)cp0->reg[COP0_RANDOM] >= cp0->nr_of_tlbs ||
		    (int64_t)cp0->reg[COP0_RANDOM] < (int64_t) cp0->reg[COP0_WIRED])
			cp0->reg[COP0_RANDOM] = cp0->nr_of_tlbs-1;
	}

	if (cp0->reg[COP0_COUNT] == cp0->reg[COP0_COMPARE])
		cpu_interrupt(cpu, 7);


	/*  Nullify this instruction?  (Set by previous instruction)  */
	if (cpu->nullify_next) {
		cpu->nullify_next = 0;
		if (instruction_trace)
			debug("(nullified)\n");
		return 0;
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
		enabled = cp0->reg[COP0_STATUS] & MIPS_SR_INT_IE;
		statusmask = 0xff01;
	} else {
		/*  R4000:  */
		enabled = (cp0->reg[COP0_STATUS] & STATUS_IE)
		    && !(cp0->reg[COP0_STATUS] & STATUS_EXL)
		    && !(cp0->reg[COP0_STATUS] & STATUS_ERL);
		statusmask = 0xff07;
	}

#if 0
	if (enabled && (cpu->old_status == (cpu->coproc[0]->reg[COP0_STATUS] & statusmask)))
		cpu->time_since_intr_enabling ++;
	else
		cpu->time_since_intr_enabling = 0;

	cpu->old_status = cpu->coproc[0]->reg[COP0_STATUS] & statusmask;

	if (cpu->time_since_intr_enabling < 3)
		enabled = 0;
#endif
	mask  = cp0->reg[COP0_STATUS] & cp0->reg[COP0_CAUSE];

	if (enabled && (mask & STATUS_IM_MASK) != 0) {
		cpu_exception(cpu, EXCEPTION_INT, 0, 0, 0, 0, 0, 0, 0);
		return 0;
	}


	/*
	 *  Execute the instruction:
	 */

	/*  Get the top 6 bits of the instruction:  */
	hi6 = (instr[3] >> 2) & 0x3f;

	cpu->stats_opcode[hi6] ++;

	switch (hi6) {
	case HI6_SPECIAL:
		special6 = instr[0] & 0x3f;
		cpu->stats__special[special6] ++;

		switch (special6) {
		case SPECIAL_SYNC:
			stype = ((instr[1] & 7) << 2) + (instr[0] >> 6);	/*  stype  */
			if (instruction_trace)
				debug("sync\t0x%02x\n", stype);
			/*  TODO: actually sync  */
			break;
		case SPECIAL_SYSCALL:
			imm = ((instr[3] << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0]) >> 6;
			imm &= 0xfffff;
			if (instruction_trace)
				debug("syscall\t0x%05x\n", imm);

			if (userland_emul) {
				useremul_syscall(cpu, imm);
			} else
				cpu_exception(cpu, EXCEPTION_SYS, 0, 0, 0, 0, 0, 0, 0);
			break;
		case SPECIAL_BREAK:
			if (instruction_trace)
				debug("break\n");
			cpu_exception(cpu, EXCEPTION_BP, 0, 0, 0, 0, 0, 0, 0);
			break;
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

			if (instruction_trace) {
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
			 *  The R4000 manual says that a shift amount of zero is treated
			 *  as a nop by some assemblers.  Checking for sa == 0 here would
			 *  not be correct, though, because instructions such as
			 *  sll r3,r4,0 are possible, and are definitely not a nop.
			 *  Instead, check if the destination register is r0.
			 */
			if (rd == 0 && special6 == SPECIAL_SLL) {
				if (instruction_trace)
					debug("nop\n");
				break;
			} else
				if (instruction_trace)
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
				cpu->gpr[rd] &= 0xffffffff;	/*  Zero-extend rd:  */
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
				}
			}
			if (special6 == SPECIAL_SRA) {
				/*  rd = sign-extend of rt:  */
				cpu->gpr[rd] = (int64_t) (int32_t) cpu->gpr[rt];
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
					if (cpu->gpr[rd] & (1 << 30))		/*  old sign-bit of low word */
						cpu->gpr[rd] |= ((uint64_t)1 << 63);
				}
				/*  Sign-extend of rd should not be neccessary.  */
				/*  cpu->gpr[rd] = (int64_t) (int32_t) cpu->gpr[rd];  */
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

			if (instruction_trace) {
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
				cpu->gpr[rd] &= (((uint64_t)1 << 32) - 1);
				if (cpu->gpr[rd] & 0x80000000)
					cpu->gpr[rd] |= ~(((uint64_t)1 << 32) - 1);
			}
			if (special6 == SPECIAL_SRAV) {
				sa = cpu->gpr[rs] & 31;
				cpu->gpr[rd] = cpu->gpr[rt];
				/*  Sign-extend rd:  */
				cpu->gpr[rd] &= (((uint64_t)1 << 32) - 1);
				if (cpu->gpr[rd] & 0x80000000)
					cpu->gpr[rd] |= ~(((uint64_t)1 << 32) - 1);
				while (sa > 0) {
					cpu->gpr[rd] = cpu->gpr[rd] >> 1;
					sa--;
					if (cpu->gpr[rd] & ((uint64_t)1 << 30))		/*  old sign-bit  */
						cpu->gpr[rd] |= ((uint64_t)1 << 63);
				}
			}
			if (special6 == SPECIAL_SRLV) {
				sa = cpu->gpr[rs] & 31;
				cpu->gpr[rd] = cpu->gpr[rt];
				cpu->gpr[rd] &= (((uint64_t)1 << 32) - 1);	/*  zero-extend rd:  */
				cpu->gpr[rd] = cpu->gpr[rd] >> sa;
				/*  And finally sign-extend rd:  */
				if (cpu->gpr[rd] & 0x80000000)
					cpu->gpr[rd] |= ~(((uint64_t)1 << 32) - 1);
			}
			break;
		case SPECIAL_JR:
			if (cpu->delay_slot) {
				fatal("jr: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 0;
			}

			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			if (instruction_trace) {
				int offset;
				char *symbol = get_symbol_name(cpu->gpr[rs], &offset);
				if (symbol != NULL)
					debug("jr\tr%i\t\t<%s>\n", rs, symbol);
				else
					debug("jr\tr%i\n", rs);
			}

			cpu->delay_slot = TO_BE_DELAYED;
			cpu->delay_jmpaddr = cpu->gpr[rs];

			if (rs == 31)
				cpu->trace_tree_depth --;

			break;
		case SPECIAL_JALR:
			if (cpu->delay_slot) {
				fatal("jalr: jump inside a jump's delay slot, or similar. TODO\n");
				cpu->running = 0;
				return 0;
			}

			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rd = (instr[1] >> 3) & 31;
			if (instruction_trace) {
				int offset;
				char *symbol = get_symbol_name(cpu->gpr[rs], &offset);
				if (symbol != NULL)
					debug("jalr\tr%i,r%i\t\t<%s>\n", rd, rs, symbol);
				else
					debug("jalr\tr%i,r%i\n", rd, rs);
			}

			tmpvalue = cpu->gpr[rs];
			cpu->gpr[rd] = cpu->pc + 4;	/*  already increased by 4 earlier  */

			if (rd == 31) {
				cpu->show_trace_delay = 2;
				cpu->show_trace_addr = tmpvalue;
			}

			cpu->delay_slot = TO_BE_DELAYED;
			cpu->delay_jmpaddr = tmpvalue;
			break;
		case SPECIAL_MFHI:
		case SPECIAL_MFLO:
			rd = (instr[1] >> 3) & 31;

			if (instruction_trace) {
				instr_mnem = NULL;
				if (special6 == SPECIAL_MFHI)	instr_mnem = "mfhi";
				if (special6 == SPECIAL_MFLO)	instr_mnem = "mflo";
				debug("%s\tr%i\n", instr_mnem, rd);
			}

			if (special6 == SPECIAL_MFHI) {
				cpu->gpr[rd] = cpu->hi;
				cpu->mfhi_delay = 3;
			}
			if (special6 == SPECIAL_MFLO) {
				cpu->gpr[rd] = cpu->lo;
				cpu->mflo_delay = 3;
			}
			break;
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
		case SPECIAL_TEQ:
		case SPECIAL_DADDU:
		case SPECIAL_DSUBU:
		case SPECIAL_MOVZ:
		case SPECIAL_MOVN:
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			rt = instr[2] & 31;
			rd = (instr[1] >> 3) & 31;

#if 0
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

			if (instruction_trace) {
				instr_mnem = NULL;
				if (special6 == SPECIAL_MTLO)	instr_mnem = "mtlo";
				if (special6 == SPECIAL_MTHI)	instr_mnem = "mthi";
				if (instr_mnem)
					debug("%s\tr%i\n", instr_mnem, rs);

				instr_mnem = NULL;
				if (special6 == SPECIAL_MULT && rd!=0)	instr_mnem = "mult_xx";
				if (instr_mnem)
					debug("%s\tr%i,r%i,r%i\n", instr_mnem, rd, rs, rt);

				instr_mnem = NULL;
				if (special6 == SPECIAL_MULT)	instr_mnem = "mult";
				if (special6 == SPECIAL_MULTU)	instr_mnem = "multu";
				if (special6 == SPECIAL_DMULT)	instr_mnem = "dmult";
				if (special6 == SPECIAL_DMULTU)	instr_mnem = "dmultu";
				if (special6 == SPECIAL_DIV)	instr_mnem = "div";
				if (special6 == SPECIAL_DIVU)	instr_mnem = "divu";
				if (special6 == SPECIAL_DDIV)	instr_mnem = "ddiv";
				if (special6 == SPECIAL_DDIVU)	instr_mnem = "ddivu";
				if (special6 == SPECIAL_TEQ)	instr_mnem = "teq";
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
				if (special6 == SPECIAL_DADDU)	instr_mnem = "daddu";
				if (special6 == SPECIAL_DSUBU)	instr_mnem = "dsubu";
				if (special6 == SPECIAL_MOVZ)	instr_mnem = "movz";
				if (special6 == SPECIAL_MOVN)	instr_mnem = "movn";
				if (instr_mnem)
					debug("%s\tr%i,r%i,r%i\n", instr_mnem, rd, rs, rt);
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
				f1 = cpu->gpr[rs];
				if (f1 & 0x80000000) {		/*  sign extend  */
					f1 &= 0xffffffff;
					f1 |= ((uint64_t)0xffffffff << 32);
				}
				f2 = cpu->gpr[rt];
				if (f2 & 0x80000000) {		/*  sign extend  */
					f2 &= 0xffffffff;
					f2 |= ((uint64_t)0xffffffff << 32);
				}
				sum = f1 * f2;

				/*  NOTE:  The stuff about rd!=0 is just a guess, judging
					from how some NetBSD code seems to execute.
					It is not documented in the MIPS64 ISA docs :-/  */

				if (rd!=0) {
					if (cpu->cpu_type.rev != MIPS_R5900)
						debug("WARNING! mult_xx is an undocumented instruction!");
					cpu->gpr[rd] = sum & 0xffffffff;

					/*  sign-extend:  */
					if (cpu->gpr[rd] & 0x80000000)
						cpu->gpr[rd] |= 0xffffffff00000000;
				} else {
					cpu->lo = sum & 0xffffffff;
					cpu->hi = (sum >> 32) & 0xffffffff;

					/*  sign-extend:  */
					if (cpu->lo & 0x80000000)
						cpu->lo |= 0xffffffff00000000;
					if (cpu->hi & 0x80000000)
						cpu->hi |= 0xffffffff00000000;
				}
				break;
			}
			if (special6 == SPECIAL_MULTU) {
				uint64_t f1, f2, sum;
				f1 = cpu->gpr[rs] & 0xffffffff;		/*  zero extend  */
				f2 = cpu->gpr[rt] & 0xffffffff;		/*  zero extend  */
				sum = f1 * f2;
				cpu->lo = sum & 0xffffffff;
				cpu->hi = (sum >> 32) & 0xffffffff;

				/*  sign-extend:  */
				if (cpu->lo & 0x80000000)
					cpu->lo |= 0xffffffff00000000;
				if (cpu->hi & 0x80000000)
					cpu->hi |= 0xffffffff00000000;
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
				a = cpu->gpr[rs] & (((uint64_t)1 << 32) - 1);
				if (a & 0x80000000)
					a |= ~(((uint64_t)1 << 32) - 1);
				b = cpu->gpr[rt] & (((uint64_t)1 << 32) - 1);
				if (b & 0x80000000)
					b |= ~(((uint64_t)1 << 32) - 1);

				if (b == 0) {
					cpu->lo = cpu->hi = 0;		/*  undefined  */
				} else {
					cpu->lo = a / b;
					cpu->hi = a % b;
				}
				/*  Sign-extend lo and hi:  */
				cpu->lo &= 0xffffffff;
				if (cpu->lo & 0x80000000)
					cpu->lo |= 0xffffffff00000000;
				cpu->hi &= 0xffffffff;
				if (cpu->hi & 0x80000000)
					cpu->hi |= 0xffffffff00000000;
				break;
			}
			if (special6 == SPECIAL_DIVU) {
				int64_t a, b;
				/*  Zero-extend rs and rt:  */
				a = cpu->gpr[rs] & (((uint64_t)1 << 32) - 1);
				b = cpu->gpr[rt] & (((uint64_t)1 << 32) - 1);
				if (b == 0) {
					cpu->lo = cpu->hi = 0;		/*  undefined  */
				} else {
					cpu->lo = a / b;
					cpu->hi = a % b;
				}
				/*  Sign-extend lo and hi:  */
				cpu->lo &= 0xffffffff;
				if (cpu->lo & 0x80000000)
					cpu->lo |= 0xffffffff00000000;
				cpu->hi &= 0xffffffff;
				if (cpu->hi & 0x80000000)
					cpu->hi |= 0xffffffff00000000;
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

			/*  TODO:  trap on overflow, and stuff like that  */
			if (special6 == SPECIAL_ADD || special6 == SPECIAL_ADDU) {
				cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
				cpu->gpr[rd] &= 0xffffffff;
				if (cpu->gpr[rd] & 0x80000000)
					cpu->gpr[rd] |= 0xffffffff00000000;
				break;
			}
			if (special6 == SPECIAL_SUB || special6 == SPECIAL_SUBU) {
				cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rt];
				cpu->gpr[rd] &= 0xffffffff;
				if (cpu->gpr[rd] & 0x80000000)
					cpu->gpr[rd] |= 0xffffffff00000000;
				break;
			}

			if (special6 == SPECIAL_TEQ) {
				if (cpu->gpr[rs] == cpu->gpr[rt])
					cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0, 0);
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
			if (special6 == SPECIAL_DADDU) {
				cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
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
		case SPECIAL_MFSA:
			/*  R5900? What on earth does this thing do?  */
			rd = (instr[1] >> 3) & 31;
			if (instruction_trace)
				debug("mfsa\tr%i\n", rd);
			/*  TODO  */
			break;
		case SPECIAL_MTSA:
			/*  R5900? What on earth does this thing do?  */
			rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
			if (instruction_trace)
				debug("mtsa\tr%i\n", rs);
			/*  TODO  */
			break;
		default:
			if (!instruction_trace) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented special6 = 0x%02x\n", special6);
			cpu->running = 0;
			return 0;
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

		if (instruction_trace) {
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
				debug("%s\tr%i,r%i,%016llx\n", instr_mnem, rt, rs, cpu->pc + (imm << 2));

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
				if (   ((hi6 == HI6_ADDI && (result_value & 0x80000000) && (tmpvalue & 0x80000000)==0))
				    || ((hi6 == HI6_DADDI && (result_value & 0x8000000000000000) && (tmpvalue & 0x8000000000000000)==0)) ) {
					cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0, 0);
					break;
				}
			} else {
				/*  Turn around from 0x8000.. to 0x7fff.. ?  Then overflow.  */
				if (   ((hi6 == HI6_ADDI && (result_value & 0x80000000)==0 && (tmpvalue & 0x80000000)))
				    || ((hi6 == HI6_DADDI && (result_value & 0x8000000000000000)==0 && (tmpvalue & 0x8000000000000000))) ) {
					cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0, 0);
					break;
				}
			}
			cpu->gpr[rt] = result_value;

			/*
			 *  Super-ugly speed-hack:  (only if speed_tricks != 0)
			 *
			 *  If we encounter a loop such as:
			 *
			 *	8012f5f4: 1c40ffff      bgtz r0,r2,ffffffff8012f5f4
			 *	8012f5f8: 2442ffff (d)  addiu r2,r2,-1
			 *
			 *  then it is a small loop which simply waits for r2 to
			 *  become zero.
			 *
			 *  TODO:  This should be generalized, and OPTIONAL as it
			 *  makes the emulation less correct.
			 */
			if (speed_tricks && cpu->delay_slot && cpu->last_was_jumptoself &&
			    cpu->jump_to_self_reg == rt && cpu->jump_to_self_reg == rs) {
				if ((int64_t)cpu->gpr[rt] > 5 && imm == -1) {
					if (instruction_trace)
						debug("changing r%i from %016llx to", rt, (long long)cpu->gpr[rt]);

					(*instrcount) += cpu->gpr[rt] * 2;

					/*  TODO:  increaste the count register, and cause interrupts!!!  */

					cpu->gpr[rt] = 0;
					if (instruction_trace)
						debug(" %016llx\n", (long long)cpu->gpr[rt]);
				}
				if ((int64_t)cpu->gpr[rt] < -5 && imm == 1) {
					if (instruction_trace)
						debug("changing r%i from %016llx to", rt, (long long)cpu->gpr[rt]);
					cpu->gpr[rt] = 0;
					if (instruction_trace)
						debug(" %016llx\n", (long long)cpu->gpr[rt]);
				}
			}

			if (hi6 == HI6_ADDI || hi6 == HI6_ADDIU) {
				/*  Sign-extend:  */
/*				cpu->gpr[rt] &= 0xffffffff;
				if (cpu->gpr[rt] & 0x80000000)
					cpu->gpr[rt] |= 0xffffffff00000000;
*/
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
				return 0;
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
				cpu->delay_jmpaddr = cpu->pc + (imm << 2);
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
			cpu->gpr[rt] = cpu->gpr[rs] & (imm & 0xffff);
			break;
		case HI6_ORI:
			cpu->gpr[rt] = cpu->gpr[rs] | (imm & 0xffff);
			break;
		case HI6_XORI:
			cpu->gpr[rt] = cpu->gpr[rs] ^ (imm & 0xffff);
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
			linked = 0;

			switch (hi6) {
			/*  The most common ones:  */
			case HI6_LW:	{ wlen = 4; st = 0; signd = 1; }  break;
			case HI6_SW:	{ wlen = 4; st = 1; signd = 0; }  break;

			case HI6_LB:	{ wlen = 1; st = 0; signd = 1; }  break;
			case HI6_LBU:	{ wlen = 1; st = 0; signd = 0; }  break;
			case HI6_SB:	{ wlen = 1; st = 1; signd = 0; }  break;

			case HI6_LD:	{ wlen = 8; st = 0; signd = 0; }  break;
			case HI6_SD:	{ wlen = 8; st = 1; signd = 0; }  break;

			case HI6_LQ_MDMX:	{ wlen = 16; st = 0; signd = 0; }  break;	/*  R5900, otherwise MDMX (TODO)  */
			case HI6_SQ:		{ wlen = 16; st = 1; signd = 0; }  break;	/*  R5900 ?  */

			/*  The rest:  */
			case HI6_LH:	{ wlen = 2; st = 0; signd = 1; }  break;
			case HI6_LHU:	{ wlen = 2; st = 0; signd = 0; }  break;
			case HI6_LWU:	{ wlen = 4; st = 0; signd = 0; }  break;
			case HI6_LWC1:	{ wlen = 4; st = 0; signd = 1; }  break;
			case HI6_LWC2:	{ wlen = 4; st = 0; signd = 1; }  break;
			case HI6_LWC3:	{ wlen = 4; st = 0; signd = 1; }  break;
			case HI6_LDC1:	{ wlen = 8; st = 0; signd = 0; }  break;
			case HI6_LDC2:	{ wlen = 8; st = 0; signd = 0; }  break;

			case HI6_SH:	{ wlen = 2; st = 1; signd = 0; }  break;
			case HI6_SWC1:	{ wlen = 4; st = 1; signd = 0; }  break;
			case HI6_SWC2:	{ wlen = 4; st = 1; signd = 0; }  break;
			case HI6_SWC3:	{ wlen = 4; st = 1; signd = 0; }  break;

			case HI6_LL:	{ wlen = 4; st = 0; signd = 0; linked = 1; }  break;
			case HI6_LLD:	{ wlen = 8; st = 0; signd = 0; linked = 1; }  break;

			case HI6_SC:	{ wlen = 4; st = 1; signd = 0; linked = 1; }  break;
			case HI6_SCD:	{ wlen = 8; st = 1; signd = 0; linked = 1; }  break;

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
				    0, addr, 0, 0, 0, 0, 0);
				break;
			}

#if 0
			if (cpu->cpu_type.isa_level == 4 && (imm & (wlen - 1)) != 0)
				debug("WARNING: low bits of imm value not zero! (MIPS IV) "
				    "pc=%016llx", (long long)cpu->pc_last);
#endif

			/*  Load Linked:  initiate a Read-Modify-Write sequence  */
			if (linked) {
				if (st==0) {
					/*  st == 0:  Load  */
					cpu->rmw      = 1;
					cpu->rmw_addr = addr;
					cpu->rmw_len  = wlen;

					/*  COP0_LLADDR is updated for diagnostic purposes.  */
					/*  (On R10K, this does not happen.)  */
					if (cpu->cpu_type.exc_model != MMU10K)
						cp0->reg[COP0_LLADDR] = (addr >> 4) & 0xffffffff;
				} else {
					/*  st == 1:  Store  */
					/*  If rmw is 0, then the store failed. (Cache collision.)  */
					if (cpu->rmw == 0) {
						/*  The store failed:  */
						cpu->gpr[rt] = 0;

						/*
						 *  Operating systems that make use of ll/sc for
						 *  synchronization should implement back-off protocols
						 *  of their own, so this code should NOT be used:
						 *
						 *	cpu->instruction_delay = random() % (ncpus + 1);
						 */
						break;
					}
				}
			}

			if (st) {
				/*  store:  */
				uint64_t value;
				int cpnr = 1, success;

				switch (hi6) {
				case HI6_SWC3:	cpnr++;		/*  fallthrough  */
				case HI6_SWC2:	cpnr++;
				case HI6_SWC1:	if (cpu->coproc[cpnr] == NULL) {
							cpu_exception(cpu, EXCEPTION_CPU, 0, 0, 0, cpnr, 0, 0, 0);
							break;
						} else
							coproc_register_read(cpu, cpu->coproc[cpnr], rt, &value); break;
				default:	value = cpu->gpr[rt];
				}

				if (wlen == 4) {
					/*  Special case for 32-bit stores... (perhaps not worth it)  */
					d[0] = value & 0xff;         d[1] = (value >> 8) & 0xff;
					d[2] = (value >> 16) & 0xff; d[3] = (value >> 24) & 0xff;
					if (cpu->byte_order == EMUL_BIG_ENDIAN) {
						int tmp1, tmp2;
						tmp1 = d[0]; tmp2 = d[1];
						d[0] = d[3]; d[1] = d[2];
						d[3] = tmp1; d[2] = tmp2;
					}
				} else if (wlen == 1)
					d[0] = value & 0xff;
				else {
					if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
						for (i=0; i<wlen; i++)
							d[i] = (value >> (i*8)) & 255;
					else
						for (i=0; i<wlen; i++)
							d[i] = (value >> ((wlen-1-i)*8)) & 255;
				}
				success = memory_rw(cpu, cpu->mem, addr, d, wlen, MEM_WRITE, CACHE_DATA);
				if (!success) {
					/*  The store failed, and might have caused an exception.  */
					if (instruction_trace && dataflag)
						debug("(failed)]\n");
					break;
				}
			} else {
				/*  load:  */
				uint64_t value;
				int cpnr = 1;
				int success;

				success = memory_rw(cpu, cpu->mem, addr, d, wlen, MEM_READ, CACHE_DATA);
				if (!success) {
					/*  The load failed, and might have caused an exception.  */
					if (instruction_trace && dataflag)
						debug("(failed)]\n");
					break;
				}

				if (wlen == 1)
					value = d[0] | (signd && (d[0]&128)? (-1 << 8) : 0);
				else {
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
				}

				switch (hi6) {
				case HI6_LWC3:	cpnr++;		/*  fallthrough  */
				case HI6_LWC2:	cpnr++;
				case HI6_LWC1:	if (cpu->coproc[cpnr] == NULL) {
							cpu_exception(cpu, EXCEPTION_CPU, 0, 0, 0, cpnr, 0, 0, 0);
							break;
						} else
							coproc_register_write(cpu, cpu->coproc[cpnr], rt, &value); break;
				default:	cpu->gpr[rt] = value;
				}
			}

			if (linked && st==1) {
				/*  The store succeeded. Invalidate any other cpu's store
					near this address, and then return 1 in gpr rt:  */
				/*  (this is a semi-ugly hack using global 'cpus')  */
				for (i=0; i<ncpus; i++) {
					/*  TODO:  check length too  */
					if (cpus[i]->rmw_addr == addr) {
						cpus[i]->rmw = 0;
						cpus[i]->rmw_addr = 0;
					}
				}

				cpu->gpr[rt] = 1;
				cpu->rmw = 0;
			}

			if (instruction_trace && dataflag) {
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

			/*  TODO:  if a store fails because of an exception, the memory
				may be partly written to. This is probably not good...  */

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
					/*  if (instruction_trace && dataflag)
						debug("%02x ", databyte);  */
				} else {
					ok = memory_rw(cpu, cpu->mem, tmpaddr, &databyte, 1, MEM_READ, CACHE_DATA);
					/*  if (instruction_trace && dataflag)
						debug("%02x ", databyte);  */
					result_value &= ~((uint64_t)0xff << (reg_ofs * 8));
					result_value |= (uint64_t)databyte << (reg_ofs * 8);
				}

				/*  Return immediately if exception.  */
				if (!ok)
					return 0;

				reg_ofs += reg_dir;
			}

			if (!st)
				cpu->gpr[rt] = result_value;

			/*  Sign extend for 32-bit load lefts:  */
			if (!st && signd && wlen == 4) {
				cpu->gpr[rt] &= 0xffffffff;
				if (cpu->gpr[rt] & 0x80000000)
					cpu->gpr[rt] |= 0xffffffff00000000;
			}

			if (instruction_trace && dataflag) {
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

			if (instruction_trace) {
				instr_mnem = NULL;
				if (regimm5 == REGIMM_BLTZ)	instr_mnem = "bltz";
				if (regimm5 == REGIMM_BGEZ)	instr_mnem = "bgez";
				if (regimm5 == REGIMM_BLTZL)	instr_mnem = "bltzl";
				if (regimm5 == REGIMM_BGEZL)	instr_mnem = "bgezl";
				if (regimm5 == REGIMM_BLTZAL)	instr_mnem = "bltzal";
				if (regimm5 == REGIMM_BLTZALL)	instr_mnem = "bltzall";
				if (regimm5 == REGIMM_BGEZAL)	instr_mnem = "bgezal";
				if (regimm5 == REGIMM_BGEZALL)	instr_mnem = "bgezall";
				debug("%s\tr%i,%016llx\n", instr_mnem, rs, cpu->pc + (imm << 2));
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
				cpu->gpr[31] = cpu->pc + 4;

			if (cond) {
				cpu->delay_slot = TO_BE_DELAYED;
				cpu->delay_jmpaddr = cpu->pc + (imm << 2);
			} else {
				if (likely)
					cpu->nullify_next = 1;		/*  nullify delay slot  */
			}

			break;
		default:
			if (!instruction_trace) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented regimm5 = 0x%02x\n", regimm5);
			cpu->running = 0;
			return 0;
		}
		break;
	case HI6_J:
	case HI6_JAL:
		if (cpu->delay_slot) {
			fatal("j/jal: jump inside a jump's delay slot, or similar. TODO\n");
			cpu->running = 0;
			return 0;
		}
		imm = ((instr[3] & 3) << 24) + (instr[2] << 16) + (instr[1] << 8) + instr[0];
		imm <<= 2;

		if (hi6 == HI6_JAL)
			cpu->gpr[31] = cpu->pc + 4;		/*  pc already increased by 4 earlier  */

		addr = cpu->pc & ~((1 << 28) - 1);
		addr |= imm;

		if (instruction_trace) {
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

		if (hi6 == HI6_JAL) {
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

		instr_mnem = NULL;

		copz = ((instr[3] & 3) << 3) + (instr[2] >> 5);
		rt = instr[2] & 31;
		rd = (instr[1] >> 3) & 31;
		if (copz == COPz_MFCz)	instr_mnem = "mfc";
		if (copz == COPz_DMFCz)	instr_mnem = "dmfc";
		if (copz == COPz_MTCz)	instr_mnem = "mtc";
		if (copz == COPz_DMTCz)	instr_mnem = "dmtc";

		if (cpnr == 1) {
			if (copz == COPz_CFCz)	instr_mnem = "cfc";
			if (copz == COPz_CTCz)	instr_mnem = "ctc";
		}

		if (instruction_trace && instr_mnem!=NULL)
			debug("%s%i\tr%i,r%i\n", instr_mnem, cpnr, rt, rd);

		if (instruction_trace && instr_mnem == NULL) {
			if ((imm & 0xff) == COP0_TLBR)   instr_mnem = "tlbr";
			if ((imm & 0xff) == COP0_TLBWI)  instr_mnem = "tlbwi";
			if ((imm & 0xff) == COP0_TLBWR)  instr_mnem = "tlbwr";
			if ((imm & 0xff) == COP0_TLBP)   instr_mnem = "tlbp";
			if ((imm & 0xff) == COP0_RFE)    instr_mnem = "rfe";
			if ((imm & 0xff) == COP0_ERET)   instr_mnem = "eret";

			if (instr_mnem==NULL)
				debug("cop%i\t%08lx\n", cpnr, imm);
			else
				debug("%s\n", instr_mnem);
		}

		if (cpu->coproc[cpnr] == NULL) {
			/*  If there is no coprocessor nr cpnr, then we get an exception:  */
			cpu_exception(cpu, EXCEPTION_CPU, 0, 0, 0, cpnr, 0, 0, 0);
		} else {
			switch (copz) {
			case COPz_MFCz:
			case COPz_DMFCz:
				coproc_register_read(cpu, cpu->coproc[cpnr], rd, &tmpvalue);
				cpu->gpr[rt] = tmpvalue;
				if (copz == COPz_MFCz) {
					/*  Sign-extend:  */
					cpu->gpr[rt] &= 0xffffffff;
					if (cpu->gpr[rt] & 0x80000000)
						cpu->gpr[rt] |= 0xffffffff00000000;
				}
				break;
			case COPz_MTCz:
			case COPz_DMTCz:
				tmpvalue = cpu->gpr[rt];
				coproc_register_write(cpu, cpu->coproc[cpnr], rd, &tmpvalue);
				break;
			default:
				coproc_function(cpu, cpu->coproc[cpnr], imm);
			}
		}
		break;
	case HI6_CACHE:
		rt   = ((instr[3] & 3) << 3) + (instr[2] >> 5);	/*  base  */
		copz = instr[2] & 31;
		imm  = (instr[1] << 8) + instr[0];

		cache_op    = copz >> 2;
		which_cache = copz & 3;

		if (instruction_trace) {
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
		cpu->stats__special2[special6] ++;

		rs = ((instr[3] & 3) << 3) + ((instr[2] >> 5) & 7);
		rt = instr[2] & 31;
		rd = (instr[1] >> 3) & 31;

		/*  Many of these can be found in the R5000 docs.  */

		switch (special6) {
		case SPECIAL2_MADD:
			/*
			 *  The R5000 manual says that rd should be all zeros,
			 *  but it isn't on R5900.   I'm just guessing here that
			 *  it uses register rd instead of hi/lo.
			 *  TODO
			 */
			if (instruction_trace)
				debug("madd\tr(r%i,)r%i,r%i\n", rd, rs, rt);
			{
				int32_t a, b;
				int64_t c;
				a = cpu->gpr[rs];
				b = cpu->gpr[rt];
				c = (int64_t)a * (int64_t)b;
				if (rd != 0) {
					c += (int32_t)cpu->gpr[rd];
					cpu->gpr[rd] = (int64_t)(int32_t)c;
				} else {
					c += cpu->lo + (cpu->hi << 32);
					cpu->lo = (int64_t)((int32_t)c);
					cpu->hi = (int64_t)((int32_t)(c >> 32));
				}
			}
			break;
		case SPECIAL2_PMFHI:
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
				if (instruction_trace)
					debug("pmflo\tr%i rs=%i\n", rd);
				cpu->gpr[rd] = cpu->lo;
			} else {
				if (instruction_trace)
					debug("pmfhi\tr%i rs=%i\n", rd);
				cpu->gpr[rd] = cpu->hi;
			}
			break;
		case SPECIAL2_POR:
			/*
			 *  This is just a guess for R5900, I've not found any docs on this one yet.
			 *
			 *	por dst,src,src2  ==> special_2 = 0x29, rs=src rt=src2 rd=dst
			 *
			 *  A wild guess is that this is a 128-bit "or" between two registers.
			 *  For now, let's just or using 64-bits.  (TODO)
			 */
			if (instruction_trace)
				debug("por\tr%i,r%i,r%i\n", rd, rs, rt);
			cpu->gpr[rd] = cpu->gpr[rs] | cpu->gpr[rt];
			break;
		case SPECIAL2_MOV_XXX:		/*  Undocumented  TODO  */
			/*  What in the world does this thing do? And what is rs?  */
			/*  It _SEEMS_ like two 32-bit registers are glued
				together to form a 64-bit register, but it might
				be doing something else too  */
			if (instruction_trace)
				debug("mov_xxx\tr%i,r%i,0x%x\n", rd, rt, rs);

			cpu->gpr[rd] =
			    ((cpu->gpr[rt+1] & 0xffffffff) << 32)
			    | (cpu->gpr[rt] & 0xffffffff);

			break;
		default:
			if (!instruction_trace) {
				fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
				    cpu->cpu_id, cpu->pc_last,
				    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
			}
			fatal("unimplemented special_2 = 0x%02x, rs=0x%02x rt=0x%02x rd=0x%02x\n",
			    special6, rs, rt, rd);
			cpu->running = 0;
			return 0;
		}
		break;
	default:
		if (!instruction_trace) {
			fatal("cpu%i @ %016llx: %02x%02x%02x%02x%s\t",
			    cpu->cpu_id, cpu->pc_last,
			    instr[3], instr[2], instr[1], instr[0], cpu_flags(cpu));
		}
		fatal("unimplemented hi6 = 0x%02x\n", hi6);
		cpu->running = 0;
		return 0;
	}

	/*  Don't put any code here, after the switch statement!  */

	return 0;
}


/*
 *  cpu_show_cycles():
 */
void cpu_show_cycles(struct timeval *starttime, long ncycles)
{
	int offset;
	char *symbol;
	long long mseconds;
	struct rusage rusage;

	symbol = get_symbol_name(cpus[bootstrap_cpu]->pc, &offset);

	getrusage(RUSAGE_SELF, &rusage);
	mseconds = (rusage.ru_utime.tv_sec -
		    starttime->tv_sec) * 1000
	    + (rusage.ru_utime.tv_usec - starttime->tv_usec) / 1000;

	if (mseconds == 0)
		mseconds = 1;

	printf("[ %li cycles, %lli instr/sec average, cpu%i->pc = %016llx <%s> ]\n",
	    (long) ncycles,
	    (long long) ((long long)1000 * ncycles / mseconds),
	    bootstrap_cpu,
	    (long long)cpus[bootstrap_cpu]->pc, symbol? symbol : "no symbol");
}


/*
 *  cpu_run():
 *
 *  Run instructions from all cpus.
 */
int cpu_run(struct cpu **cpus, int ncpus)
{
	int i, s1, s2;
	long ncycles = 0, ncycles_chunk_end, ncycles_show = 0;
	long ncycles_flush = 0, ncycles_flushx11 = 0;	/*  TODO: overflow?  */
	int running;
	struct rusage rusage;
	struct timeval starttime;

	getrusage(RUSAGE_SELF, &rusage);
	starttime = rusage.ru_utime;

	running = 1;
	while (running) {
		ncycles_chunk_end = ncycles + (1 << 14);

		/*  Run instructions from each CPU:  */
		do {
			running = 0;
			for (i=0; i<ncpus; i++)
				if (cpus[i]->running) {
					cpu_run_instr(cpus[i], &ncycles);
					running = 1;
				}
			ncycles++;
		} while (running && (ncycles < ncycles_chunk_end));

		/*  Check for X11 events:  */
		if (use_x11) {
			if (ncycles > ncycles_flushx11 + (1<<16)) {
				x11_check_event();
				ncycles_flushx11 = ncycles;
			}
		}

		/*  If we've done buffered console output, the flush it every now and then:  */
		if (ncycles > ncycles_flush + (1<<17)) {
			console_flush();
			ncycles_flush = ncycles;
		}

		if (show_nr_of_instructions && (ncycles > ncycles_show + (1<<21))) {
			cpu_show_cycles(&starttime, ncycles);
			ncycles_show = ncycles;
		}

		if (max_instructions!=0 && ncycles >= max_instructions)
			running = 0;
	}

	/*  One last tick of every hardware device:  */
	/*  (TODO: per cpu?)  */
        for (i=0; i<cpus[0]->n_tick_entries; i++)
		cpus[0]->tick_func[i](cpus[0], cpus[0]->tick_extra[i]);

	debug("All CPUs halted.\n");

	if (show_nr_of_instructions || !quiet_mode)
		cpu_show_cycles(&starttime, ncycles);

	if (show_opcode_statistics) {
		for (i=0; i<ncpus; i++) {
			printf("cpu%i opcode statistics:\n", i);
			for (s1=0; s1<N_HI6; s1++) {
				if (cpus[i]->stats_opcode[s1] > 0)
					printf("  opcode %02x (%7s): %li\n",
					    s1, hi6_names[s1], cpus[i]->stats_opcode[s1]);
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

	return 0;
}


