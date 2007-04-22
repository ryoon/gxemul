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
 *  $Id: cpu_m88k.c,v 1.4 2007-04-22 14:12:57 debug Exp $
 *
 *  M88K CPU emulation.
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
#include "tmp_m88k_head.c"


static char *memop[4] = { ".d", "", ".h", ".b" };

void m88k_irq_interrupt_assert(struct interrupt *interrupt);
void m88k_irq_interrupt_deassert(struct interrupt *interrupt);


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

	/*  Only show name and caches etc for CPU nr 0:  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);
	for (i=0; i<N_M88K_REGS - 1; i++) {
		char name[10];
		snprintf(name, sizeof(name), "r%i", i);
		CPU_SETTINGS_ADD_REGISTER32(name, cpu->cd.m88k.r[i]);
	}

	/*  Register the CPU interrupt pin:  */
	{
		struct interrupt template;
		char name[50];
		snprintf(name, sizeof(name), "%s.irq", cpu->path);

                memset(&template, 0, sizeof(template));
                template.line = 0;
                template.name = name;
                template.extra = cpu;
                template.interrupt_assert = m88k_irq_interrupt_assert;
                template.interrupt_deassert = m88k_irq_interrupt_deassert;
                interrupt_handler_register(&template);
        }


	return 1;
}


/*
 *  m88k_cpu_dumpinfo():
 */
void m88k_cpu_dumpinfo(struct cpu *cpu)
{
	/*  struct m88k_cpu_type_def *ct = &cpu->cd.m88k.cpu_type;  */
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
				debug("  r%-2i = 0x%08x", i,
				    (int)cpu->cd.m88k.r[i]);
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
 *  m88k_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *              
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */                     
int m88k_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr)
{
	uint32_t iw;
	char *symbol, *mnem = NULL;
	uint64_t offset;
	int32_t op26, op10, op11, d, s1, s2, w5, d16, d26, imm16, simm16;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%08x:  ", (int)dumpaddr);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iw = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);

	debug("%08"PRIx32"\t", (uint32_t) iw);

	op26   = (iw >> 26) & 0x3f;
	op11   = (iw >> 11) & 0x1f;
	op10   = (iw >> 10) & 0x3f;
	d      = (iw >> 21) & 0x1f;
	s1     = (iw >> 16) & 0x1f;
	s2     =  iw        & 0x1f;
	imm16  = iw & 0xffff;
	simm16 = (int16_t) (iw & 0xffff);
	w5     = (iw >>  5) & 0x1f;
	d16    = ((int16_t) (iw & 0xffff)) * 4;
	d26    = ((int32_t)((iw & 0x03ffffff) << 6)) >> 4;

	switch (op26) {

	case 0x04:	/*  ld.d  */
	case 0x05:	/*  ld    */
	case 0x06:	/*  ld.h  */
	case 0x07:	/*  ld.b  */
	case 0x08:	/*  st.d  */
	case 0x09:	/*  st    */
	case 0x0a:	/*  st.h  */
	case 0x0b:	/*  st.b  */
		debug("%s%s\tr%i,r%i,%i",
		    op26 >= 0x08? "st" : "ld",
		    memop[op26 & 3], d, s1, imm16);
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
				case 0:	/*  TODO: Endianness!!!  */
					debug("0x%016"PRIx64, (uint64_t)
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
		debug("r%i,r%i,0x%x\n", d, s1, imm16);
		break;

	case 0x18:	/*  addu    */
	case 0x19:	/*  subu    */
	case 0x1a:	/*  divu    */
	case 0x1b:	/*  mulu    */
		switch (op26) {
		case 0x18:	mnem = "addu"; break;
		case 0x19:	mnem = "subu"; break;
		case 0x1a:	mnem = "divu"; break;
		case 0x1b:	mnem = "mulu"; break;
		}
		debug("%s\tr%i,r%i,%i\n", mnem, d, s1, imm16);
		break;

	case 0x1c:	/*  add    */
	case 0x1d:	/*  sub    */
	case 0x1e:	/*  div    */
	case 0x1f:	/*  cmp    */
		switch (op26) {
		case 0x1c:	mnem = "add"; break;
		case 0x1d:	mnem = "sub"; break;
		case 0x1e:	mnem = "div"; break;
		case 0x1f:	mnem = "cmp"; break;
		}
		debug("%s\tr%i,r%i,%i\n", mnem, d, s1, simm16);
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
		debug("bb%s%s\t",
		    op26 >= 0x36? "1" : "0",
		    op26 & 1? ".n" : "");
		debug("%i,r%i,0x%08"PRIx32, d, s1, (uint32_t) (dumpaddr + d16));
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    dumpaddr + d16, &offset);
		if (symbol != NULL)
			debug("\t; <%s>", symbol);
		debug("\n");
		break;

	case 0x3d:
		if ((iw & 0x03fff3e0) == 0x0000c000) {
			debug("%s%s\t(r%i)\n",
			    op11 & 1? "jsr" : "jmp",
			    iw & 0x400? ".n" : "",
			    s2);
			if (running) {
				uint32_t tmpaddr = cpu->cd.m88k.r[s2];
				symbol = get_symbol_name(&cpu->machine->
				    symbol_context, tmpaddr, &offset);
				debug("\t; ");
				if (symbol != NULL)
					debug("<%s>", symbol);
				else
					debug("0x%08"PRIx32, tmpaddr);
			}
		} else if ((iw & 0x0000ffe0) == 0x00000000) {
			debug("xmem\tr%i,r%i,r%i\n", d, s1, s2);
		} else {
			debug("UNIMPLEMENTED 0x3d\n");
		}
		break;

	default:debug("UNIMPLEMENTED\n");
	}

	return sizeof(uint32_t);
}


#include "tmp_m88k_tail.c"


