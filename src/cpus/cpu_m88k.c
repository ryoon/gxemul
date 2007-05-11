/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_m88k.c,v 1.21 2007-05-11 01:31:20 debug Exp $
 *
 *  Motorola M881x0 CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cpu.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "settings.h"
#include "symbol.h"

#define DYNTRANS_32
#define DYNTRANS_DELAYSLOT
#include "tmp_m88k_head.c"


static char *memop[4] = { ".d", "", ".h", ".b" };

void m88k_irq_interrupt_assert(struct interrupt *interrupt);
void m88k_irq_interrupt_deassert(struct interrupt *interrupt);


static char *m88k_cr_names[] = M88K_CR_NAMES;
static char *m88k_cr_197_names[] = M88K_CR_NAMES_197;

static char *m88k_cr_name(struct cpu *cpu, int i)
{
	char **cr_names = m88k_cr_names;

	/*  Hm. Is this really MVME197 specific? TODO  */
	if (cpu->machine->machine_subtype == MACHINE_MVME88K_197)
		cr_names = m88k_cr_197_names;

	return cr_names[i];
}

static char *m88k_fcr_name(struct cpu *cpu, int fi)
{
	/*  TODO  */
	static char fcr_name[10];
	snprintf(fcr_name, sizeof(fcr_name), "FCR%i", fi);
	return fcr_name;
}



/*
 *  m88k_cpu_new():
 *
 *  Create a new M88K cpu object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid M88K processor.
 */
int m88k_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i, found;
	struct m88k_cpu_type_def cpu_type_defs[] = M88K_CPU_TYPE_DEFS;

	/*  Scan the list for this cpu type:  */
	i = 0; found = -1;
	while (i >= 0 && cpu_type_defs[i].name != NULL) {
		if (strcasecmp(cpu_type_defs[i].name, cpu_type_name) == 0) {
			found = i;
			break;
		}
		i++;
	}
	if (found == -1)
		return 0;

	cpu->run_instr = m88k_run_instr;
	cpu->memory_rw = m88k_memory_rw;
	cpu->update_translation_table = m88k_update_translation_table;
	cpu->invalidate_translation_caches =
	    m88k_invalidate_translation_caches;
	cpu->invalidate_code_translation = m88k_invalidate_code_translation;
	/*  cpu->translate_v2p = m88k_translate_v2p;  */

	cpu->cd.m88k.cpu_type = cpu_type_defs[found];
	cpu->name            = cpu->cd.m88k.cpu_type.name;
	cpu->is_32bit        = 1;
	cpu->byte_order      = EMUL_BIG_ENDIAN;

	cpu->instruction_has_delayslot = m88k_cpu_instruction_has_delayslot;

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}


	/*
	 *  Add register names as settings:
	 */

	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);

	for (i=0; i<N_M88K_REGS - 1; i++) {
		char name[10];
		snprintf(name, sizeof(name), "r%i", i);
		CPU_SETTINGS_ADD_REGISTER32(name, cpu->cd.m88k.r[i]);
	}

	for (i=0; i<N_M88K_CONTROL_REGS; i++) {
		char name[10];
		snprintf(name, sizeof(name), "%s", m88k_cr_name(cpu, i));
		CPU_SETTINGS_ADD_REGISTER32(name, cpu->cd.m88k.cr[i]);
	}

	for (i=0; i<N_M88K_FPU_CONTROL_REGS; i++) {
		char name[10];
		snprintf(name, sizeof(name), "%s", m88k_fcr_name(cpu, i));
		CPU_SETTINGS_ADD_REGISTER32(name, cpu->cd.m88k.fcr[i]);
	}


	/*  Register the CPU interrupt pin:  */
	{
		struct interrupt template;
		char name[50];
		snprintf(name, sizeof(name), "%s", cpu->path);

                memset(&template, 0, sizeof(template));
                template.line = 0;
                template.name = name;
                template.extra = cpu;
                template.interrupt_assert = m88k_irq_interrupt_assert;
                template.interrupt_deassert = m88k_irq_interrupt_deassert;
                interrupt_handler_register(&template);
        }

	/*  Set the Processor ID:  */
	cpu->cd.m88k.cr[M88K_CR_PID] = cpu->cd.m88k.cpu_type.pid | M88K_PID_MC;

	/*  Start in supervisor mode, with interrupts disabled.  */
	cpu->cd.m88k.cr[M88K_CR_PSR] = M88K_PSR_MODE | M88K_PSR_IND;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		cpu->cd.m88k.cr[M88K_CR_PSR] |= M88K_PSR_BO;

	/*  Initial stack pointer:  */
	cpu->cd.m88k.r[31] = 1048576 * cpu->machine->physical_ram_in_mb - 1024;

	return 1;
}


/*
 *  m88k_cpu_dumpinfo():
 */
void m88k_cpu_dumpinfo(struct cpu *cpu)
{
	/*  struct m88k_cpu_type_def *ct = &cpu->cd.m88k.cpu_type;  */

	debug(", %s-endian",
	    cpu->byte_order == EMUL_BIG_ENDIAN? "Big" : "Little");

	debug("\n");
}


/*
 *  m88k_cpu_list_available_types():
 *
 *  Print a list of available M88K CPU types.
 */
void m88k_cpu_list_available_types(void)
{
	int i, j;
	struct m88k_cpu_type_def tdefs[] = M88K_CPU_TYPE_DEFS;

	i = 0;
	while (tdefs[i].name != NULL) {
		debug("%s", tdefs[i].name);
		for (j=13 - strlen(tdefs[i].name); j>0; j--)
			debug(" ");
		i++;
		if ((i % 5) == 0 || tdefs[i].name == NULL)
			debug("\n");
	}
}


/*
 *  m88k_cpu_instruction_has_delayslot():
 *
 *  Returns 1 if an opcode has a delay slot after it, 0 otherwise.
 */
int m88k_cpu_instruction_has_delayslot(struct cpu *cpu, unsigned char *ib)
{
	uint32_t iword = *((uint32_t *)&ib[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iword = LE32_TO_HOST(iword);
	else
		iword = BE32_TO_HOST(iword);

	switch (iword >> 26) {
	case 0x31:	/*  br.n  */
	case 0x33:	/*  bsr.n  */
	case 0x35:	/*  bb0.n  */
	case 0x37:	/*  bb1.n  */
	case 0x3b:	/*  bcnd.n  */
		return 1;
	case 0x3d:
		switch ((iword >> 8) & 0xff) {
		case 0xc4:	/*  jmp.n  */
		case 0xcc:	/*  jsr.n  */
			return 1;
		}
	}

	return 0;
}


/*
 *  m88k_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void m88k_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);
		debug("cpu%i:  pc  = 0x%08"PRIx32, x, (uint32_t)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_M88K_REGS; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			if (i == 0)
				debug("                  ");
			else
				debug("  r%-2i = 0x%08"PRIx32,
				    i, cpu->cd.m88k.r[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}

	if (coprocs & 1) {
		int n_control_regs = 32;

		/*  Hm. Is this really MVME197 specific? TODO  */
		if (cpu->machine->machine_subtype == MACHINE_MVME88K_197)
			n_control_regs = 64;

		for (i=0; i<n_control_regs; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			debug("  %4s=0x%08"PRIx32,
			    m88k_cr_name(cpu, i), cpu->cd.m88k.cr[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}

	if (coprocs & 2) {
		int n_fpu_control_regs = 64;

		for (i=0; i<n_fpu_control_regs; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			debug("  %5s=0x%08"PRIx32,
			    m88k_fcr_name(cpu, i), cpu->cd.m88k.fcr[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}
}


/*
 *  m88k_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void m88k_cpu_tlbdump(struct machine *m, int x, int rawflag)
{
}


/*
 *  m88k_irq_interrupt_assert():
 *  m88k_irq_interrupt_deassert():
 */
void m88k_irq_interrupt_assert(struct interrupt *interrupt)
{
	struct cpu *cpu = (struct cpu *) interrupt->extra;
	cpu->cd.m88k.irq_asserted = 1;
}
void m88k_irq_interrupt_deassert(struct interrupt *interrupt)
{
	struct cpu *cpu = (struct cpu *) interrupt->extra;
	cpu->cd.m88k.irq_asserted = 0;
}


/*
 *  m88k_ldcr():
 *
 *  Read from a control register. Store the resulting value in a register (pointed
 *  to by r32ptr).
 */
void m88k_ldcr(struct cpu *cpu, uint32_t *r32ptr, int cr)
{
	uint32_t retval = cpu->cd.m88k.cr[cr];

	switch (cr) {

	case M88K_CR_PID:
	case M88K_CR_PSR:
	case M88K_CR_VBR:
	case M88K_CR_SR0:
	case M88K_CR_SR1:
	case M88K_CR_SR2:
	case M88K_CR_SR3:
		break;

	default:fatal("m88k_ldcr: UNIMPLEMENTED cr = 0x%02x (%s)\n",
		    cr, m88k_cr_name(cpu, cr));
		exit(1);
	}

	*r32ptr = retval;
}


/*
 *  m88k_stcr():
 *
 *  Write to a control register.
 *  (Used by both the stcr and rte instructions.)
 */
void m88k_stcr(struct cpu *cpu, uint32_t value, int cr, int rte)
{
	uint32_t old = cpu->cd.m88k.cr[cr];

	switch (cr) {

	case M88K_CR_PSR:	/*  Processor Status Regoster  */
		if ((cpu->byte_order == EMUL_LITTLE_ENDIAN
		    && !(value & M88K_PSR_BO)) ||
		    (cpu->byte_order == EMUL_BIG_ENDIAN
		    && (value & M88K_PSR_BO))) {
			fatal("TODO: attempt to change endianness by flipping"
			    " the endianness bit in the PSR. How should this"
			    " be handled? Aborting.\n");
			exit(1);
		}

		if (!rte && old & M88K_PSR_MODE && !(value & M88K_PSR_MODE))
			fatal("[ m88k_stcr: WARNING! the PSR_MODE bit is being"
			    " cleared; this should be done using the RTE "
			    "instruction only, according to the M88100 "
			    "manual! Continuing anyway. ]\n");

		if (value & M88K_PSR_MXM) {
			fatal("m88k_stcr: TODO: MXM support\n");
			exit(1);
		}

		cpu->cd.m88k.cr[cr] = value;
		break;

	case M88K_CR_SSBR:	/*  Shadow ScoreBoard Register  */
		if (value & 1)
			fatal("[ m88k_stcr: WARNING! bit 0 non-zero when"
			    " writing to SSBR (?) ]\n");
		cpu->cd.m88k.cr[cr] = value;
		break;

	case M88K_CR_VBR:
		if (value & 0x00000fff)
			fatal("[ m88k_stcr: WARNING! bits 0..11 non-zero when"
			    " writing to VBR (?) ]\n");
		cpu->cd.m88k.cr[cr] = value;
		break;

	case M88K_CR_SR0:	/*  Supervisor Storage Registers 0..3  */
	case M88K_CR_SR1:
	case M88K_CR_SR2:
	case M88K_CR_SR3:
		cpu->cd.m88k.cr[cr] = value;
		break;

	default:fatal("m88k_stcr: UNIMPLEMENTED cr = 0x%02x (%s)\n",
		    cr, m88k_cr_name(cpu, cr));
		exit(1);
	}
}


/*
 *  m88k_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *              
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and dumpaddr will be used instead of
 *  cpu->pc for relative addresses.
 */                     
int m88k_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr)
{
	uint32_t iw;
	char *symbol, *mnem = NULL;
	uint64_t offset;
	uint32_t op26, op10, op11, d, s1, s2, w5, cr6, imm16;
	int32_t d16, d26, simm16;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%08"PRIx32":  ", (uint32_t) dumpaddr);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iw = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);

	debug("%08"PRIx32, (uint32_t) iw);

	if (running && cpu->delay_slot)
		debug(" (d)");

	debug("\t");

	op26   = (iw >> 26) & 0x3f;
	op11   = (iw >> 11) & 0x1f;
	op10   = (iw >> 10) & 0x3f;
	d      = (iw >> 21) & 0x1f;
	s1     = (iw >> 16) & 0x1f;
	s2     =  iw        & 0x1f;
	imm16  = iw & 0xffff;
	simm16 = (int16_t) (iw & 0xffff);
	w5     = (iw >>  5) & 0x1f;
	cr6    = (iw >>  5) & 0x3f;
	d16    = ((int16_t) (iw & 0xffff)) * 4;
	d26    = ((int32_t)((iw & 0x03ffffff) << 6)) >> 4;

	switch (op26) {

	case 0x02:	/*  ld.hu  */
	case 0x03:	/*  ld.bu  */
	case 0x04:	/*  ld.d  */
	case 0x05:	/*  ld    */
	case 0x06:	/*  ld.h  */
	case 0x07:	/*  ld.b  */
	case 0x08:	/*  st.d  */
	case 0x09:	/*  st    */
	case 0x0a:	/*  st.h  */
	case 0x0b:	/*  st.b  */
		switch (op26) {
		case 0x02:  debug("ld.hu"); break;
		case 0x03:  debug("ld.bu"); break;
		default:    debug("%s%s", op26 >= 0x08? "st" : "ld",
				memop[op26 & 3]);
		}
		debug("\tr%i,r%i,0x%x", d, s1, imm16);
		if (running) {
			uint32_t tmpaddr = cpu->cd.m88k.r[s1] + imm16;
			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    tmpaddr, &offset);
			if (symbol != NULL)
				debug("\t; [<%s>]", symbol);
			else
				debug("\t; [0x%08"PRIx32"]", tmpaddr);
			if (op26 >= 0x08) {
				/*  Store:  */
				debug(" = ");
				switch (op26 & 3) {
				case 0:	debug("0x%016"PRIx64, (uint64_t)
					    ((((uint64_t) cpu->cd.m88k.r[d])
					    << 32) + ((uint64_t)
					    cpu->cd.m88k.r[d+1])) );
					break;
				case 1:	debug("0x%08"PRIx32,
					    (uint32_t) cpu->cd.m88k.r[d]);
					break;
				case 2:	debug("0x%08"PRIx16,
					    (uint16_t) cpu->cd.m88k.r[d]);
					break;
				case 3:	debug("0x%08"PRIx8,
					    (uint8_t) cpu->cd.m88k.r[d]);
					break;
				}
			} else {
				/*  Load:  */
				/*  TODO  */
			}
		} else {
			/*
			 *  Not running, but the following instruction
			 *  sequence is quite common:
			 *
			 *  or.u      rX,r0,A
			 *  st_or_ld  rY,rX,B
			 */

			/*  Try loading the instruction before the
			    current one.  */
			uint32_t iw2 = 0;
			cpu->memory_rw(cpu, cpu->mem,
			    dumpaddr - sizeof(uint32_t), (unsigned char *)&iw2,
			    sizeof(iw2), MEM_READ, CACHE_INSTRUCTION
			    | NO_EXCEPTIONS);
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				 iw2 = LE32_TO_HOST(iw2);
			else
				 iw2 = BE32_TO_HOST(iw2);
			if ((iw2 >> 26) == 0x17 &&	/*  or.u  */
			    ((iw2 >> 21) & 0x1f) == s1) {
				uint32_t tmpaddr = (iw2 << 16) + imm16;
				symbol = get_symbol_name(
				    &cpu->machine->symbol_context,
				    tmpaddr, &offset);
				if (symbol != NULL)
					debug("\t; [<%s>]", symbol);
				else
					debug("\t; [0x%08"PRIx32"]", tmpaddr);
			}
		}
		debug("\n");
		break;

	case 0x10:	/*  and     */
	case 0x11:	/*  and.u   */
	case 0x12:	/*  mask    */
	case 0x13:	/*  mask.u  */
	case 0x14:	/*  xor     */
	case 0x15:	/*  xor.u   */
	case 0x16:	/*  or      */
	case 0x17:	/*  or.u    */
		switch (op26) {
		case 0x10:
		case 0x11:	mnem = "and"; break;
		case 0x12:
		case 0x13:	mnem = "mask"; break;
		case 0x14:
		case 0x15:	mnem = "xor"; break;
		case 0x16:
		case 0x17:	mnem = "or"; break;
		}
		debug("%s%s\t", mnem, op26 & 1? ".u" : "");
		debug("r%i,r%i,0x%x", d, s1, imm16);

		if (op26 == 0x16 && d != M88K_ZERO_REG) {
			/*
			 *  The following instruction sequence is common:
			 *
			 *  or.u   rX,r0,A
			 *  or     rY,rX,B	; rY = AAAABBBB
			 */

			/*  Try loading the instruction before the
			    current one.  */
			uint32_t iw2 = 0;
			cpu->memory_rw(cpu, cpu->mem,
			    dumpaddr - sizeof(uint32_t), (unsigned char *)&iw2,
			    sizeof(iw2), MEM_READ, CACHE_INSTRUCTION
			    | NO_EXCEPTIONS);
			if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
				 iw2 = LE32_TO_HOST(iw2);
			else
				 iw2 = BE32_TO_HOST(iw2);
			if ((iw2 >> 26) == 0x17 &&	/*  or.u  */
			    ((iw2 >> 21) & 0x1f) == s1) {
				uint32_t tmpaddr = (iw2 << 16) + imm16;
				symbol = get_symbol_name(
				    &cpu->machine->symbol_context,
				    tmpaddr, &offset);
				debug("\t; ");
				if (symbol != NULL)
					debug("<%s>", symbol);
				else
					debug("0x%08"PRIx32, tmpaddr);
			}
		}

		debug("\n");
		break;

	case 0x18:	/*  addu    */
	case 0x19:	/*  subu    */
	case 0x1a:	/*  divu    */
	case 0x1b:	/*  mulu    */
	case 0x1c:	/*  add    */
	case 0x1d:	/*  sub    */
	case 0x1e:	/*  div    */
	case 0x1f:	/*  cmp    */
		switch (op26) {
		case 0x18:	mnem = "addu"; break;
		case 0x19:	mnem = "subu"; break;
		case 0x1a:	mnem = "divu"; break;
		case 0x1b:	mnem = "mulu"; break;
		case 0x1c:	mnem = "add"; break;
		case 0x1d:	mnem = "sub"; break;
		case 0x1e:	mnem = "div"; break;
		case 0x1f:	mnem = "cmp"; break;
		}
		debug("%s\tr%i,r%i,%i\n", mnem, d, s1, imm16);
		break;

	case 0x20:
		if ((iw & 0x001ff81f) == 0x00004000) {
			debug("ldcr\tr%i,%s\n", d,
			    m88k_cr_name(cpu, cr6));
		} else if ((iw & 0x001ff81f) == 0x00004800) {
			debug("fldcr\tr%i,%s\n", d,
			    m88k_fcr_name(cpu, cr6));
		} else if ((iw & 0x03e0f800) == 0x00008000) {
			debug("stcr\tr%i,%s", s1,
			    m88k_cr_name(cpu, cr6));
			if (s1 != s2)
				debug("\t\t; NOTE: weird encoding: "
				    "low 5 bits = 0x%02x", s2);
			debug("\n");
		} else if ((iw & 0x03e0f800) == 0x00008800) {
			debug("fstcr\tr%i,%s", s1,
			    m88k_fcr_name(cpu, cr6));
			if (s1 != s2)
				debug("\t\t; NOTE: weird encoding: "
				    "low 5 bits = 0x%02x", s2);
			debug("\n");
		} else if ((iw & 0x0000f81f) == 0x0000c000) {
			debug("xcr\tr%i,r%i,%s\n", d, s1,
			    m88k_cr_name(cpu, cr6));
		} else if ((iw & 0x0000f81f) == 0x0000c800) {
			debug("fxcr\tr%i,r%i,%s\n", d, s1,
			    m88k_fcr_name(cpu, cr6));
		} else {
			debug("UNIMPLEMENTED 0x20\n");
		}
		break;

	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
		debug("b%sr%s\t",
		    op26 >= 0x32? "s" : "",
		    op26 & 1? ".n" : "");
		debug("0x%08"PRIx32, (uint32_t) (dumpaddr + d26));
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    dumpaddr + d26, &offset);
		if (symbol != NULL)
			debug("\t; <%s>", symbol);
		debug("\n");
		break;

	case 0x34:	/*  bb0    */
	case 0x35:	/*  bb0.n  */
	case 0x36:	/*  bb1    */
	case 0x37:	/*  bb1.n  */
	case 0x3a:	/*  bcnd    */
	case 0x3b:	/*  bcnd.n  */
		switch (op26) {
		case 0x34:
		case 0x35: mnem = "bb0"; break;
		case 0x36:
		case 0x37: mnem = "bb1"; break;
		case 0x3a:
		case 0x3b: mnem = "bcnd"; break;
		}
		debug("%s%s\t", mnem, op26 & 1? ".n" : "");
		if (op26 == 0x3a || op26 == 0x3b) {
			/*  Attempt to decode bcnd condition:  */
			switch (d) {
			case 0x1: debug("gt0"); break;
			case 0x2: debug("eq0"); break;
			case 0x3: debug("ge0"); break;
			case 0xc: debug("lt0"); break;
			case 0xd: debug("ne0"); break;
			case 0xe: debug("le0"); break;
			default:  debug("%i", d);
			}
		} else {
			debug("%i", d);
		}
		debug(",r%i,0x%08"PRIx32, s1, (uint32_t) (dumpaddr + d16));
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    dumpaddr + d16, &offset);
		if (symbol != NULL)
			debug("\t; <%s>", symbol);
		debug("\n");
		break;

	case 0x3c:
		if ((iw & 0x0000f000)==0x1000 || (iw & 0x0000f000)==0x2000) {
			/*  Load/store:  */
			debug("%s", (iw & 0x0000f000) == 0x1000? "ld" : "st");
			switch (iw & 0x00000c00) {
			case 0x000: debug(".d"); break;
			case 0x400: break;
			case 0x800: debug(".x"); break;
			default: debug(".UNIMPLEMENTED");
			}
			if (iw & 0x100)
				debug(".usr");
			if (iw & 0x80)
				debug(".wt");
			debug("\tr%i,r%i", d, s1);
			if (iw & 0x200)
				debug("[r%i]", s2);
			else
				debug(",r%i", s2);
			debug("\n");
		} else switch (op10) {
		case 0x20:	/*  clr  */
		case 0x22:	/*  set  */
		case 0x24:	/*  ext  */
		case 0x26:	/*  extu  */
		case 0x28:	/*  mak  */
		case 0x2a:	/*  rot  */
			switch (op10) {
			case 0x20: mnem = "clr"; break;
			case 0x22: mnem = "set"; break;
			case 0x24: mnem = "ext"; break;
			case 0x26: mnem = "extu"; break;
			case 0x28: mnem = "mak"; break;
			case 0x2a: mnem = "rot"; break;
			}
			debug("%s\tr%i,r%i,", mnem, d, s1);
			/*  Don't include w5 for the rot instruction:  */
			if (op10 != 0x2a)
				debug("%i", w5);
			/*  Note: o5 = s2:  */
			debug("<%i>\n", s2);
			break;
		case 0x34:	/*  tb0  */
		case 0x36:	/*  tb1  */
			switch (op10) {
			case 0x34: mnem = "tb0"; break;
			case 0x36: mnem = "tb1"; break;
			}
			debug("%s\t%i,r%i,0x%x\n", mnem, d, s1, iw & 0x1ff);
			break;
		default:debug("UNIMPLEMENTED 0x3c, op10=0x%02x\n", op10);
		}
		break;

	case 0x3d:
		if ((iw & 0xf000) <= 0x3fff) {
			/*  Load, Store, xmem, and lda:  */
			switch (iw & 0xf000) {
			case 0x2000: debug("st"); break;
			case 0x3000: debug("lda"); break;
			default:     if ((iw & 0xf800) >= 0x0800)
					  debug("ld");
				     else
					  debug("xmem");
			}
			if ((iw & 0xf000) >= 0x1000)	/*  ld, st, lda  */
				debug("%s", memop[(iw >> 10) & 3]);
			else if ((iw & 0xf800) == 0x0000)/*  xmem  */
				debug("%s", (iw & 0x800) <= 0x300? ".bu" : "");
			else				/*  ld  */
				debug("%s", (iw & 0xf00) < 0xc00? ".hu":".bu");
			if (iw & 0x100)
				debug(".usr");
			if (iw & 0x80)
				debug(".wt");
			debug("\tr%i,r%i", d, s1);
			if (iw & 0x200)
				debug("[r%i]", s2);
			else
				debug(",r%i", s2);
			debug("\n");
		} else switch ((iw >> 8) & 0xff) {
		case 0x40:	/*  and  */
		case 0x44:	/*  and.c  */
		case 0x50:	/*  xor  */
		case 0x54:	/*  xor.c  */
		case 0x58:	/*  or  */
		case 0x5c:	/*  or.c  */
		case 0x60:	/*  addu  */
		case 0x61:	/*  addu.co  */
		case 0x62:	/*  addu.ci  */
		case 0x63:	/*  addu.cio  */
		case 0x64:	/*  subu  */
		case 0x65:	/*  subu.co  */
		case 0x66:	/*  subu.ci  */
		case 0x67:	/*  subu.cio  */
		case 0x68:	/*  divu  */
		case 0x69:	/*  divu.d  */
		case 0x6c:	/*  mul  */
		case 0x6d:	/*  mulu.d  */
		case 0x6e:	/*  muls  */
		case 0x70:	/*  add  */
		case 0x71:	/*  add.co  */
		case 0x72:	/*  add.ci  */
		case 0x73:	/*  add.cio  */
		case 0x74:	/*  sub  */
		case 0x75:	/*  sub.co  */
		case 0x76:	/*  sub.ci  */
		case 0x77:	/*  sub.cio  */
		case 0x78:	/*  div  */
		case 0x7c:	/*  cmp  */
		case 0x80:	/*  clr  */
		case 0x88:	/*  set  */
		case 0x90:	/*  ext  */
		case 0x98:	/*  extu  */
		case 0xa0:	/*  mak  */
		case 0xa8:	/*  rot  */
			/*  Three-register opcodes:  */
			switch ((iw >> 8) & 0xff) {
			case 0x40: mnem = "and"; break;
			case 0x44: mnem = "and.c"; break;
			case 0x50: mnem = "xor"; break;
			case 0x54: mnem = "xor.c"; break;
			case 0x58: mnem = "or"; break;
			case 0x5c: mnem = "or.c"; break;
			case 0x60: mnem = "addu"; break;
			case 0x61: mnem = "addu.co"; break;
			case 0x62: mnem = "addu.ci"; break;
			case 0x63: mnem = "addu.cio"; break;
			case 0x64: mnem = "subu"; break;
			case 0x65: mnem = "subu.co"; break;
			case 0x66: mnem = "subu.ci"; break;
			case 0x67: mnem = "subu.cio"; break;
			case 0x68: mnem = "divu"; break;
			case 0x69: mnem = "divu.d"; break;
			case 0x6c: mnem = "mul"; break;
			case 0x6d: mnem = "mulu.d"; break;
			case 0x6e: mnem = "muls"; break;
			case 0x70: mnem = "add"; break;
			case 0x71: mnem = "add.co"; break;
			case 0x72: mnem = "add.ci"; break;
			case 0x73: mnem = "add.cio"; break;
			case 0x74: mnem = "sub"; break;
			case 0x75: mnem = "sub.co"; break;
			case 0x76: mnem = "sub.ci"; break;
			case 0x77: mnem = "sub.cio"; break;
			case 0x78: mnem = "div"; break;
			case 0x7c: mnem = "cmp"; break;
			case 0x80: mnem = "clr"; break;
			case 0x88: mnem = "set"; break;
			case 0x90: mnem = "ext"; break;
			case 0x98: mnem = "extu"; break;
			case 0xa0: mnem = "mak"; break;
			case 0xa8: mnem = "rot"; break;
			}
			debug("%s\tr%i,r%i,r%i\n", mnem, d, s1, s2);
			break;
		case 0xc0:	/*  jmp  */
		case 0xc4:	/*  jmp.n  */
		case 0xc8:	/*  jsr  */
		case 0xcc:	/*  jsr.n  */
			debug("%s%s\t(r%i)",
			    op11 & 1? "jsr" : "jmp",
			    iw & 0x400? ".n" : "",
			    s2);
			if (running) {
				uint32_t tmpaddr = cpu->cd.m88k.r[s2];
				symbol = get_symbol_name(&cpu->machine->
				    symbol_context, tmpaddr, &offset);
				debug("\t\t; ");
				if (symbol != NULL)
					debug("<%s>", symbol);
				else
					debug("0x%08"PRIx32, tmpaddr);
			}
			debug("\n");
			break;
		case 0xe8:	/*  ff1  */
		case 0xec:	/*  ff0  */
			debug("%s\tr%i,r%i\n",
			    ((iw >> 8) & 0xff) == 0xe8 ? "ff1" : "ff0", d, s2);
			break;
		case 0xf8:	/*  tbnd  */
			debug("tbnd\tr%i,r%i\n", s1, s2);
			break;
		case 0xfc:
			switch (iw & 0xff) {
			case 0x00:
				debug("rte\n");
				break;
			case 0x01:
			case 0x02:
			case 0x03:
				debug("illop%i\n", iw & 0xff);
				break;
			default:debug("UNIMPLEMENTED 0x3d,0xfc: 0x%02x\n",
				    iw & 0xff);
			}
			break;
		default:debug("UNIMPLEMENTED 0x3d, opbyte = 0x%02x\n",
			    (iw >> 8) & 0xff);
		}
		break;

	case 0x3e:
		debug("tbnd\tr%i,0x%x\n", s1, imm16);
		break;

	default:debug("UNIMPLEMENTED op26=0x%02x\n", op26);
	}

	return sizeof(uint32_t);
}


#include "tmp_m88k_tail.c"


