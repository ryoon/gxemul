/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_x86.c,v 1.164 2005-07-28 13:29:36 debug Exp $
 *
 *  x86 (and amd64) CPU emulation.
 *
 *
 *  TODO:  Pretty much everything that has to do with 64-bit and 32-bit modes,
 *  memory translation, flag bits, and so on.
 *
 *  See http://www.amd.com/us-en/Processors/DevelopWithAMD/
 *	0,,30_2252_875_7044,00.html for more info on AMD64.
 *
 *  http://www.cs.ucla.edu/~kohler/class/04f-aos/ref/i386/appa.htm has a
 *  nice overview of the standard i386 opcodes.
 *
 *  HelpPC (http://members.tripod.com/~oldboard/assembly/) is also useful.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_X86


#include "cpu_x86.h"

/*  (Bogus, when ENABLE_X86 isn't defined.)  */
int x86_cpu_family_init(struct cpu_family *fp) { return 0; }


#else	/*  ENABLE_X86  */


#include "cpu.h"
#include "cpu_x86.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


static struct x86_model models[] = x86_models;
static char *reg_names[N_X86_REGS] = x86_reg_names;
static char *reg_names_bytes[8] = x86_reg_names_bytes;
static char *seg_names[N_X86_SEGS] = x86_seg_names;
static char *cond_names[N_X86_CONDS] = x86_cond_names;

#define	REP_REP		1
#define	REP_REPNE	2


/*
 *  x86_cpu_new():
 *
 *  Create a new x86 cpu object.
 */
int x86_cpu_new(struct cpu *cpu, struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	int i = 0;

	/*  Try to find a match:  */
	while (models[i].model_number != 0) {
		if (strcasecmp(cpu_type_name, models[i].name) == 0)
			break;
		i++;
	}

	if (models[i].name == NULL)
		return 0;

	memset(&cpu->cd.x86, 0, sizeof(struct x86_cpu));

	cpu->memory_rw  = x86_memory_rw;
	cpu->byte_order = EMUL_LITTLE_ENDIAN;

	cpu->cd.x86.model = models[i];

	/*  Initial startup is in 16-bit real mode:  */
	cpu->pc = 0xfff0;

	/*  Initial segments:  */
	cpu->cd.x86.descr_cache[X86_S_CS].valid = 1;
	cpu->cd.x86.descr_cache[X86_S_CS].default_op_size = 16;
	cpu->cd.x86.descr_cache[X86_S_CS].access_rights = 0x93;
	cpu->cd.x86.descr_cache[X86_S_CS].base = 0xf0000; /* ffff0000  */
	cpu->cd.x86.descr_cache[X86_S_CS].limit = 0xffff;
	cpu->cd.x86.descr_cache[X86_S_CS].descr_type = DESCR_TYPE_CODE;
	cpu->cd.x86.descr_cache[X86_S_CS].readable = 1;
	cpu->cd.x86.descr_cache[X86_S_CS].writable = 1;
	cpu->cd.x86.descr_cache[X86_S_CS].granularity = 0;
	cpu->cd.x86.s[X86_S_CS] = 0xf000;

	cpu->cd.x86.idtr = 0;
	cpu->cd.x86.idtr_limit = 0x3ff;

	cpu->translate_address = translate_address_x86;

	cpu->cd.x86.rflags = 0x0002;
	if (cpu->cd.x86.model.model_number == X86_MODEL_8086)
		cpu->cd.x86.rflags |= 0xf000;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return 1;
}


/*
 *  x86_cpu_dumpinfo():
 */
void x86_cpu_dumpinfo(struct cpu *cpu)
{
	debug(", currently in %s mode", PROTECTED_MODE? "protected" : "real");
	debug("\n");
}


/*
 *  x86_cpu_list_available_types():
 *
 *  Print a list of available x86 CPU types.
 */
void x86_cpu_list_available_types(void)
{
	int i = 0, j;

	while (models[i].model_number != 0) {
		debug("%s", models[i].name);

		for (j=0; j<10-strlen(models[i].name); j++)
			debug(" ");
		i++;
		if ((i % 6) == 0 || models[i].name == NULL)
			debug("\n");
	}
}


/*
 *  x86_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  (gprs and coprocs are mostly useful for the MIPS version of this function.)
 */
void x86_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (REAL_MODE) {
		/*  Real-mode:  */
		debug("cpu%i:  cs:ip = 0x%04x:0x%04x\n", x,
		    cpu->cd.x86.s[X86_S_CS], (int)cpu->pc);

		debug("cpu%i:  ax = 0x%04x  bx = 0x%04x  cx = 0x%04x  dx = "
		    "0x%04x\n", x,
		    (int)cpu->cd.x86.r[X86_R_AX], (int)cpu->cd.x86.r[X86_R_BX],
		    (int)cpu->cd.x86.r[X86_R_CX], (int)cpu->cd.x86.r[X86_R_DX]);
		debug("cpu%i:  si = 0x%04x  di = 0x%04x  bp = 0x%04x  sp = "
		    "0x%04x\n", x,
		    (int)cpu->cd.x86.r[X86_R_SI], (int)cpu->cd.x86.r[X86_R_DI],
		    (int)cpu->cd.x86.r[X86_R_BP], (int)cpu->cd.x86.r[X86_R_SP]);

		debug("cpu%i:  ds = 0x%04x  es = 0x%04x  ss = 0x%04x  flags "
		    "= 0x%04x\n", x,
		    (int)cpu->cd.x86.s[X86_S_DS], (int)cpu->cd.x86.s[X86_S_ES],
		    (int)cpu->cd.x86.s[X86_S_SS], (int)cpu->cd.x86.rflags);
	} else {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i:  eip=0x", x);
	        debug("%08x", (int)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i:  eax=0x%08x  ebx=0x%08x  ecx=0x%08x  edx="
		    "0x%08x\n", x,
		    (int)cpu->cd.x86.r[X86_R_AX], (int)cpu->cd.x86.r[X86_R_BX],
		    (int)cpu->cd.x86.r[X86_R_CX], (int)cpu->cd.x86.r[X86_R_DX]);
		debug("cpu%i:  esi=0x%08x  edi=0x%08x  ebp=0x%08x  esp="
		    "0x%08x\n", x,
		    (int)cpu->cd.x86.r[X86_R_SI], (int)cpu->cd.x86.r[X86_R_DI],
		    (int)cpu->cd.x86.r[X86_R_BP], (int)cpu->cd.x86.r[X86_R_SP]);
#if 0
	} else {
		/*  64-bit  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i:  rip = 0x", x);
	        debug("%016llx", (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_X86_REGS; i++) {
			if ((i & 1) == 0)
				debug("cpu%i:", x);
			debug("  r%s = 0x%016llx", reg_names[i],
			    (long long)cpu->cd.x86.r[i]);
			if ((i & 1) == 1)
				debug("\n");
		}
#endif
	}

	if (coprocs != 0) {
		for (i=0; i<6; i++) {
			debug("cpu%i:  %s=0x%04x (", x, seg_names[i],
			    cpu->cd.x86.s[i]);
			if (cpu->cd.x86.descr_cache[i].valid) {
				debug("base=0x%08x, limit=0x%08x, ",
				    (int)cpu->cd.x86.descr_cache[i].base,
				    (int)cpu->cd.x86.descr_cache[i].limit);
				debug("%s", cpu->cd.x86.descr_cache[i].
				    descr_type==DESCR_TYPE_CODE?"CODE":"DATA");
				debug(", %i-bit", cpu->cd.x86.descr_cache[i].
				    default_op_size);
				debug(", %s%s", cpu->cd.x86.descr_cache[i].
				    readable? "R" : "-", cpu->cd.x86.
				    descr_cache[i].writable? "W" : "-");
			} else
				debug("invalid");
			debug(")\n");
		}
		debug("cpu%i:  gdtr=0x%08llx:0x%04x  idtr=0x%08llx:0x%04x "
		    " ldtr=0x%08x:0x%04x\n", x, (long long)cpu->cd.x86.gdtr,
		    (int)cpu->cd.x86.gdtr_limit, (long long)cpu->cd.x86.idtr,
		    (int)cpu->cd.x86.idtr_limit, (long long)cpu->cd.x86.
		    ldtr_base, (int)cpu->cd.x86.ldtr_limit);
		debug("cpu%i:  pic1: irr=0x%02x ier=0x%02x isr=0x%02x "
		    "base=0x%02x\n", x, cpu->machine->md.pc.pic1->irr,
		    cpu->machine->md.pc.pic1->ier,cpu->machine->md.pc.pic1->isr,
		    cpu->machine->md.pc.pic1->irq_base);
		debug("cpu%i:  pic2: irr=0x%02x ier=0x%02x isr=0x%02x "
		    "base=0x%02x\n", x, cpu->machine->md.pc.pic2->irr,
		    cpu->machine->md.pc.pic2->ier,cpu->machine->md.pc.pic2->isr,
		    cpu->machine->md.pc.pic2->irq_base);
	} else if (PROTECTED_MODE) {
		/*  Protected mode:  */
		debug("cpu%i:  cs=0x%04x  ds=0x%04x  es=0x%04x  "
		    "fs=0x%04x  gs=0x%04x  ss=0x%04x\n", x,
		    (int)cpu->cd.x86.s[X86_S_CS], (int)cpu->cd.x86.s[X86_S_DS],
		    (int)cpu->cd.x86.s[X86_S_ES], (int)cpu->cd.x86.s[X86_S_FS],
		    (int)cpu->cd.x86.s[X86_S_GS], (int)cpu->cd.x86.s[X86_S_SS]);
	}

	if (PROTECTED_MODE) {
		/*  Protected mode:  */
		debug("cpu%i:  cr0=0x%08x  cr2=0x%08x  cr3=0x%08x  eflags="
		    "0x%08x\n", x, (int)cpu->cd.x86.cr[0],
		    (int)cpu->cd.x86.cr[2], (int)cpu->cd.x86.cr[3],
		    (int)cpu->cd.x86.rflags);
		debug("cpu%i:  tr = 0x%04x (base=0x%llx, limit=0x%x)\n",
		    x, (int)cpu->cd.x86.tr, (long long)cpu->cd.x86.tr_base,
		    (int)cpu->cd.x86.tr_limit);
	}
}


/*
 *  x86_cpu_register_match():
 */
void x86_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *mr)
{
	int cpunr = 0;
	int r;

	/*  CPU number:  TODO  */

	if (strcasecmp(name, "pc") == 0 || strcasecmp(name, "rip") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
			m->cpus[cpunr]->cd.x86.halted = 0;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*mr = 1;
		return;
	}
	if (strcasecmp(name, "ip") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = (m->cpus[cpunr]->pc & ~0xffff)
			    | (*valuep & 0xffff);
			m->cpus[cpunr]->cd.x86.halted = 0;
		} else
			*valuep = m->cpus[cpunr]->pc & 0xffff;
		*mr = 1;
		return;
	}
	if (strcasecmp(name, "eip") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
			m->cpus[cpunr]->cd.x86.halted = 0;
		} else
			*valuep = m->cpus[cpunr]->pc & 0xffffffffULL;
		*mr = 1;
		return;
	}

	if (strcasecmp(name, "rflags") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.x86.rflags = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.x86.rflags;
		*mr = 1;
		return;
	}
	if (strcasecmp(name, "eflags") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.x86.rflags = (m->cpus[cpunr]->
			    cd.x86.rflags & ~0xffffffffULL) | (*valuep &
			    0xffffffffULL);
		else
			*valuep = m->cpus[cpunr]->cd.x86.rflags & 0xffffffffULL;
		*mr = 1;
		return;
	}
	if (strcasecmp(name, "flags") == 0) {
		if (writeflag)
			m->cpus[cpunr]->cd.x86.rflags = (m->cpus[cpunr]->
			    cd.x86.rflags & ~0xffff) | (*valuep & 0xffff);
		else
			*valuep = m->cpus[cpunr]->cd.x86.rflags & 0xffff;
		*mr = 1;
		return;
	}

	/*  8-bit low:  */
	for (r=0; r<4; r++)
		if (strcasecmp(name, reg_names_bytes[r]) == 0) {
			if (writeflag)
				m->cpus[cpunr]->cd.x86.r[r] =
				    (m->cpus[cpunr]->cd.x86.r[r] & ~0xff)
				    | (*valuep & 0xff);
			else
				*valuep = m->cpus[cpunr]->cd.x86.r[r] & 0xff;
			*mr = 1;
			return;
		}

	/*  8-bit high:  */
	for (r=0; r<4; r++)
		if (strcasecmp(name, reg_names_bytes[r+4]) == 0) {
			if (writeflag)
				m->cpus[cpunr]->cd.x86.r[r] =
				    (m->cpus[cpunr]->cd.x86.r[r] & ~0xff00)
				    | ((*valuep & 0xff) << 8);
			else
				*valuep = (m->cpus[cpunr]->cd.x86.r[r] >>
				    8) & 0xff;
			*mr = 1;
			return;
		}

	/*  16-, 32-, 64-bit registers:  */
	for (r=0; r<N_X86_REGS; r++) {
		/*  16-bit:  */
		if (r<8 && strcasecmp(name, reg_names[r]) == 0) {
			if (writeflag)
				m->cpus[cpunr]->cd.x86.r[r] =
				    (m->cpus[cpunr]->cd.x86.r[r] & ~0xffff)
				    | (*valuep & 0xffff);
			else
				*valuep = m->cpus[cpunr]->cd.x86.r[r] & 0xffff;
			*mr = 1;
			return;
		}

		/*  32-bit:  */
		if (r<8 && (name[0]=='e' || name[0]=='E') &&
		    strcasecmp(name+1, reg_names[r]) == 0) {
			if (writeflag)
				m->cpus[cpunr]->cd.x86.r[r] =
				    *valuep & 0xffffffffULL;
			else
				*valuep = m->cpus[cpunr]->cd.x86.r[r] &
				    0xffffffffULL;
			*mr = 1;
			return;
		}

		/*  64-bit:  */
		if ((name[0]=='r' || name[0]=='R') &&
		    strcasecmp(name+1, reg_names[r]) == 0) {
			if (writeflag)
				m->cpus[cpunr]->cd.x86.r[r] = *valuep;
			else
				*valuep = m->cpus[cpunr]->cd.x86.r[r];
			*mr = 1;
			return;
		}
	}

	/*  segment names:  */
	for (r=0; r<N_X86_SEGS; r++) {
		if (strcasecmp(name, seg_names[r]) == 0) {
			if (writeflag)
				m->cpus[cpunr]->cd.x86.s[r] =
				    (m->cpus[cpunr]->cd.x86.s[r] & ~0xffff)
				    | (*valuep & 0xffff);
			else
				*valuep = m->cpus[cpunr]->cd.x86.s[r] & 0xffff;
			*mr = 1;
			return;
		}
	}

	/*  control registers: (TODO: 32- vs 64-bit on AMD64?)  */
	if (strncasecmp(name, "cr", 2) == 0 && atoi(name+2) < N_X86_CREGS ) {
		int r = atoi(name+2);
		if (writeflag)
			m->cpus[cpunr]->cd.x86.cr[r] = *valuep;
		else
			*valuep = m->cpus[cpunr]->cd.x86.cr[r];
		*mr = 1;
		return;
	}
}


/*  Macro which modifies the lower part of a value, or the entire value,
    depending on 'mode':  */
#define modify(old,new) (					\
		mode==16? (					\
			((old) & ~0xffff) + ((new) & 0xffff)	\
		) : ((new) & 0xffffffffULL) )

/*  "volatile" here, because some versions of gcc with -O3 on i386 are buggy  */
#define	HEXPRINT(x,n) { volatile int j; for (j=0; j<(n); j++) \
	debug("%02x",(x)[j]); }
#define	HEXSPACES(i) { int j; j = (i)>10? 10:(i); while (j++<10) debug("  "); \
	debug(" "); }
#define	SPACES	HEXSPACES(ilen)


static uint32_t read_imm_common(unsigned char **instrp, uint64_t *ilenp,
	int len, int printflag)
{
	uint32_t imm;
	unsigned char *instr = *instrp;

	if (len == 8)
		imm = instr[0];
	else if (len == 16)
		imm = instr[0] + (instr[1] << 8);
	else
		imm = instr[0] + (instr[1] << 8) +
		    (instr[2] << 16) + (instr[3] << 24);

	if (printflag)
		HEXPRINT(instr, len / 8);

	if (ilenp != NULL)
		(*ilenp) += len/8;

	(*instrp) += len/8;
	return imm;
}


static uint32_t read_imm_and_print(unsigned char **instrp, uint64_t *ilenp,
	int mode)
{
	return read_imm_common(instrp, ilenp, mode, 1);
}


static uint32_t read_imm(unsigned char **instrp, uint64_t *newpcp,
	int mode)
{
	return read_imm_common(instrp, newpcp, mode, 0);
}


static void print_csip(struct cpu *cpu)
{
	fatal("0x%04x:", cpu->cd.x86.s[X86_S_CS]);
	if (PROTECTED_MODE)
		fatal("0x%llx", (long long)cpu->pc);
	else
		fatal("0x%04x", (int)cpu->pc);
}


/*
 *  x86_cpu_interrupt():
 *
 *  NOTE: Interacting with the 8259 PIC is done in src/machine.c.
 */
int x86_cpu_interrupt(struct cpu *cpu, uint64_t nr)
{
	if (cpu->machine->md_interrupt != NULL)
		cpu->machine->md_interrupt(cpu->machine, cpu, nr, 1);
	else {
		fatal("x86_cpu_interrupt(): no md_interrupt()?\n");
		return 1;
	}

        return 1;
}


/*
 *  x86_cpu_interrupt_ack():
 *
 *  NOTE: Interacting with the 8259 PIC is done in src/machine.c.
 */
int x86_cpu_interrupt_ack(struct cpu *cpu, uint64_t nr)
{
	if (cpu->machine->md_interrupt != NULL)
		cpu->machine->md_interrupt(cpu->machine, cpu, nr, 0);
	else {
		fatal("x86_cpu_interrupt(): no md_interrupt()?\n");
		return 1;
	}

        return 1;
}


/*  (NOTE: Don't use the lowest 3 bits in these defines)  */
#define	RELOAD_TR		0x1000
#define	RELOAD_LDTR		0x1008


/*
 *  x86_task_switch():
 *
 *  Save away current state into the current task state segment, and
 *  load the new state from the new task.
 *
 *  TODO: 16-bit TSS, etc. And clean up all of this :)
 *
 *  TODO: Link word. AMD64 stuff. And lots more.
 */
void x86_task_switch(struct cpu *cpu, int new_tr, uint64_t *curpc)
{
	unsigned char old_descr[8];
	unsigned char new_descr[8];
	uint32_t value, ofs;
	int i;
	unsigned char buf[4];

	fatal("x86_task_switch():\n");
	cpu->pc = *curpc;

	if (!cpu->memory_rw(cpu, cpu->mem, cpu->cd.x86.gdtr + cpu->cd.x86.tr,
	    old_descr, sizeof(old_descr), MEM_READ, NO_SEGMENTATION)) {
		fatal("x86_task_switch(): TODO: 1\n");
		cpu->running = 0;
		return;
	}

	/*  Check the busy bit, and then clear it:  */
	if (!(old_descr[5] & 0x02)) {
		fatal("x86_task_switch(): TODO: switching FROM a non-BUSY"
		    " TSS descriptor?\n");
		cpu->running = 0;
		return;
	}
	old_descr[5] &= ~0x02;
	if (!cpu->memory_rw(cpu, cpu->mem, cpu->cd.x86.gdtr + cpu->cd.x86.tr,
	    old_descr, sizeof(old_descr), MEM_WRITE, NO_SEGMENTATION)) {
		fatal("x86_task_switch(): TODO: could not clear busy bit\n");
		cpu->running = 0;
		return;
	}

	x86_cpu_register_dump(cpu, 1, 1);

	/*  Set the task-switched bit in CR0:  */
	cpu->cd.x86.cr[0] |= X86_CR0_TS;

	/*  Save away all the old registers:  */
#define WRITE_VALUE { buf[0]=value; buf[1]=value>>8; buf[2]=value>>16; \
	buf[3]=value>>24; cpu->memory_rw(cpu, cpu->mem, \
	cpu->cd.x86.tr_base + ofs, buf, sizeof(buf), MEM_WRITE,  \
	NO_SEGMENTATION); }

	ofs = 0x1c; value = cpu->cd.x86.cr[3]; WRITE_VALUE;
	ofs = 0x20; value = cpu->pc; WRITE_VALUE;
	ofs = 0x24; value = cpu->cd.x86.rflags; WRITE_VALUE;
	for (i=0; i<N_X86_REGS; i++) {
		ofs = 0x28+i*4; value = cpu->cd.x86.r[i]; WRITE_VALUE;
	}
	for (i=0; i<6; i++) {
		ofs = 0x48+i*4; value = cpu->cd.x86.s[i]; WRITE_VALUE;
	}

	fatal("-------\n");

	if ((cpu->cd.x86.tr & 0xfffc) == 0) {
		fatal("TODO: x86_task_switch(): task switch, but old TR"
		    " was 0?\n");
		cpu->running = 0;
		return;
	}

	if (!cpu->memory_rw(cpu, cpu->mem, cpu->cd.x86.gdtr + new_tr,
	    new_descr, sizeof(new_descr), MEM_READ, NO_SEGMENTATION)) {
		fatal("x86_task_switch(): TODO: 1\n");
		cpu->running = 0;
		return;
	}
	if (new_descr[5] & 0x02) {
		fatal("x86_task_switch(): TODO: switching TO an already BUSY"
		    " TSS descriptor?\n");
		cpu->running = 0;
		return;
	}

	reload_segment_descriptor(cpu, RELOAD_TR, new_tr, NULL);

	if (cpu->cd.x86.tr_limit < 0x67)
		fatal("WARNING: tr_limit = 0x%x, must be at least 0x67!\n",
		    (int)cpu->cd.x86.tr_limit);

	/*  Read new registers:  */
#define READ_VALUE { cpu->memory_rw(cpu, cpu->mem, cpu->cd.x86.tr_base + \
	ofs, buf, sizeof(buf), MEM_READ, NO_SEGMENTATION); \
	value = buf[0] + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24); }

	ofs = 0x1c; READ_VALUE; cpu->cd.x86.cr[3] = value;
	ofs = 0x20; READ_VALUE; cpu->pc = value;
	ofs = 0x24; READ_VALUE; cpu->cd.x86.rflags = value;
	for (i=0; i<N_X86_REGS; i++) {
		ofs = 0x28+i*4; READ_VALUE; cpu->cd.x86.r[i] = value;
	}
	for (i=0; i<6; i++) {
		ofs = 0x48+i*4; READ_VALUE;
		reload_segment_descriptor(cpu, i, value, NULL);
	}
	ofs = 0x60; READ_VALUE; value &= 0xffff;
	reload_segment_descriptor(cpu, RELOAD_LDTR, value, NULL);

	if ((cpu->cd.x86.s[X86_S_CS] & X86_PL_MASK) !=
	    (cpu->cd.x86.s[X86_S_SS] & X86_PL_MASK))
		fatal("WARNING: rpl in CS and SS differ!\n");

	if ((cpu->cd.x86.s[X86_S_CS] & X86_PL_MASK) == X86_RING3 &&
	    !(cpu->cd.x86.rflags & X86_FLAGS_IF))
		fatal("WARNING (?): switching to userland task, but interrupts"
		    " are disabled?\n");

	x86_cpu_register_dump(cpu, 1, 1);
	fatal("-------\n");

	*curpc = cpu->pc;

	/*  cpu->machine->instruction_trace = 1;  */
	/*  cpu->running = 0;  */
}


/*
 *  reload_segment_descriptor():
 *
 *  Loads base, limit and other settings from the Global Descriptor Table into
 *  segment descriptors.
 *
 *  This function can also be used to reload the TR (task register).
 *
 *  And also to do a task switch, or jump into a trap handler etc.
 *  (Perhaps this function should be renamed.)
 */
void reload_segment_descriptor(struct cpu *cpu, int segnr, int selector,
	uint64_t *curpcp)
{
	int res, i, readable, writable, granularity, descr_type;
	int segment = 1, rpl, orig_selector = selector;
	unsigned char descr[8];
	char *table_name = "GDT";
	uint64_t base, limit, table_base, table_limit;

	if (segnr > 0x100)	/*  arbitrary, larger than N_X86_SEGS  */
		segment = 0;

	if (segment && (segnr < 0 || segnr >= N_X86_SEGS)) {
		fatal("reload_segment_descriptor(): segnr = %i\n", segnr);
		exit(1);
	}

	if (segment && REAL_MODE) {
		/*  Real mode:  */
		cpu->cd.x86.descr_cache[segnr].valid = 1;
		cpu->cd.x86.descr_cache[segnr].default_op_size = 16;
		cpu->cd.x86.descr_cache[segnr].access_rights = 0x93;
		cpu->cd.x86.descr_cache[segnr].descr_type =
		    segnr == X86_S_CS? DESCR_TYPE_CODE : DESCR_TYPE_DATA;
		cpu->cd.x86.descr_cache[segnr].readable = 1;
		cpu->cd.x86.descr_cache[segnr].writable = 1;
		cpu->cd.x86.descr_cache[segnr].granularity = 0;
		cpu->cd.x86.descr_cache[segnr].base = selector << 4;
		cpu->cd.x86.descr_cache[segnr].limit = 0xffff;
		cpu->cd.x86.s[segnr] = selector;
		return;
	}

	/*
	 *  Protected mode:  Load the descriptor cache from the GDT.
	 */

	table_base = cpu->cd.x86.gdtr;
	table_limit = cpu->cd.x86.gdtr_limit;
	if (selector & 4) {
		table_name = "LDT";
		/*  fatal("TODO: x86 translation via LDT: 0x%04x\n",
		    selector);  */
		table_base = cpu->cd.x86.ldtr_base;
		table_limit = cpu->cd.x86.ldtr_limit;
	}

	/*  Special case: Null-descriptor:  */
	if (segment && (selector & ~3) == 0) {
		cpu->cd.x86.descr_cache[segnr].valid = 0;
		cpu->cd.x86.s[segnr] = selector;
		return;
	}

	rpl = selector & 3;

	/*  TODO: check rpl  */

	selector &= ~7;

	if (selector + 7 > table_limit) {
		fatal("TODO: selector 0x%04x outside %s limit (0x%04x)\n",
		    selector, table_name, (int)table_limit);
		cpu->running = 0;
		return;
	}

	res = cpu->memory_rw(cpu, cpu->mem, table_base + selector,
	    descr, sizeof(descr), MEM_READ, NO_SEGMENTATION);
	if (!res) {
		fatal("reload_segment_descriptor(): TODO: "
		    "could not read the GDT\n");
		cpu->running = 0;
		return;
	}

	base = descr[2] + (descr[3] << 8) + (descr[4] << 16) +
	    (descr[7] << 24);
	limit = descr[0] + (descr[1] << 8) + ((descr[6]&15) << 16);

	descr_type = readable = writable = granularity = 0;
	granularity = (descr[6] & 0x80)? 1 : 0;
	if (limit == 0) {
		fatal("WARNING: descriptor limit = 0\n");
		limit = 0xfffff;
	}
	if (granularity)
		limit = (limit << 12) | 0xfff;

#if 0
printf("base = %llx\n",(long long)base);
for (i=0; i<8; i++)
	fatal(" %02x", descr[i]);
#endif

	if (selector != 0x0000 && (descr[5] & 0x80) == 0x00) {
		fatal("TODO: nonpresent descriptor?\n");
		goto fail_dump;
	}

	if (!segment) {
		switch (segnr) {
		case RELOAD_TR:
			/*  Check that this is indeed a TSS descriptor:  */
			if ((descr[5] & 0x15) != 0x01) {
				fatal("TODO: load TR but entry in table is"
				    " not a TSS descriptor?\n");
				goto fail_dump;
			}

			/*  Reload the task register:  */
			cpu->cd.x86.tr = selector;
			cpu->cd.x86.tr_base = base;
			cpu->cd.x86.tr_limit = limit;

			/*  Mark the TSS as busy:  */
			descr[5] |= 0x02;
			res = cpu->memory_rw(cpu, cpu->mem, cpu->cd.x86.gdtr +
			    selector, descr, sizeof(descr), MEM_WRITE,
			    NO_SEGMENTATION);
			break;
		case RELOAD_LDTR:
			/*  Reload the Local Descriptor Table register:  */
			cpu->cd.x86.ldtr = selector;
			cpu->cd.x86.ldtr_base = base;
			cpu->cd.x86.ldtr_limit = limit;
			break;
		}
		return;
	}

	if ((descr[5] & 0x18) == 0x18) {
		descr_type = DESCR_TYPE_CODE;
		readable = descr[5] & 0x02? 1 : 0;
		if ((descr[5] & 0x98) != 0x98) {
			fatal("TODO CODE\n");
			goto fail_dump;
		}
	} else if ((descr[5] & 0x18) == 0x10) {
		descr_type = DESCR_TYPE_DATA;
		readable = 1;
		writable = descr[5] & 0x02? 1 : 0;
		if ((descr[5] & 0x98) != 0x90) {
			fatal("TODO DATA\n");
			goto fail_dump;
		}
	} else if (segnr == X86_S_CS && (descr[5] & 0x15) == 0x01
	    && curpcp != NULL) {
		/*  TSS  */
		x86_task_switch(cpu, selector, curpcp);
		return;
	} else {
		fatal("TODO: other\n");
		goto fail_dump;
	}

	cpu->cd.x86.descr_cache[segnr].valid = 1;
	cpu->cd.x86.descr_cache[segnr].default_op_size =
	    (descr[6] & 0x40)? 32 : 16;
	cpu->cd.x86.descr_cache[segnr].access_rights = descr[5];
	cpu->cd.x86.descr_cache[segnr].descr_type = descr_type;
	cpu->cd.x86.descr_cache[segnr].readable = readable;
	cpu->cd.x86.descr_cache[segnr].writable = writable;
	cpu->cd.x86.descr_cache[segnr].granularity = granularity;
	cpu->cd.x86.descr_cache[segnr].base = base;
	cpu->cd.x86.descr_cache[segnr].limit = limit;
	cpu->cd.x86.s[segnr] = orig_selector;
	return;

fail_dump:
	for (i=0; i<8; i++)
		fatal(" %02x", descr[i]);
	cpu->running = 0;
}


/*
 *  x86_load():
 *
 *  Returns same error code as memory_rw().
 */
static int x86_load(struct cpu *cpu, uint64_t addr, uint64_t *data, int len)
{
	unsigned char databuf[8];
	int res;
	uint64_t d;

	res = cpu->memory_rw(cpu, cpu->mem, addr, &databuf[0], len,
	    MEM_READ, CACHE_DATA);

	d = databuf[0];
	if (len > 1) {
		d += ((uint64_t)databuf[1] << 8);
		if (len > 2) {
			d += ((uint64_t)databuf[2] << 16);
			d += ((uint64_t)databuf[3] << 24);
			if (len > 4) {
				d += ((uint64_t)databuf[4] << 32);
				d += ((uint64_t)databuf[5] << 40);
				d += ((uint64_t)databuf[6] << 48);
				d += ((uint64_t)databuf[7] << 56);
			}
		}
	}

	*data = d;
	return res;
}


/*
 *  x86_store():
 *
 *  Returns same error code as memory_rw().
 */
static int x86_store(struct cpu *cpu, uint64_t addr, uint64_t data, int len)
{
	unsigned char databuf[8];

	/*  x86 is always little-endian:  */
	databuf[0] = data;
	if (len > 1) {
		databuf[1] = data >> 8;
		if (len > 2) {
			databuf[2] = data >> 16;
			databuf[3] = data >> 24;
			if (len > 4) {
				databuf[4] = data >> 32;
				databuf[5] = data >> 40;
				databuf[6] = data >> 48;
				databuf[7] = data >> 56;
			}
		}
	}

	return cpu->memory_rw(cpu, cpu->mem, addr, &databuf[0], len,
	    MEM_WRITE, CACHE_DATA);
}


/*
 *  x86_write_cr():
 *
 *  Write to a control register.
 */
static void x86_write_cr(struct cpu *cpu, int r, uint64_t value)
{
	uint64_t new, tmp;

	switch (r) {
	case 0:	new = cpu->cd.x86.cr[r] = value;
		/*  Warn about unimplemented bits:  */
		tmp = new & ~(X86_CR0_PE | X86_CR0_PG);
		if (cpu->cd.x86.model.model_number <= X86_MODEL_80386) {
			if (tmp & X86_CR0_WP)
				fatal("WARNING: cr0 WP bit set, but this is"
				    " not an 80486 or higher (?)\n");
		}
		tmp &= ~X86_CR0_WP;
		if (tmp != 0)
			fatal("x86_write_cr(): unimplemented cr0 bits: "
			    "0x%08llx\n", (long long)tmp);
		break;
	case 2:
	case 3:	new = cpu->cd.x86.cr[r] = value;
		break;
	case 4:	new = cpu->cd.x86.cr[r] = value;
		/*  Warn about unimplemented bits:  */
		tmp = new; /*  & ~(X86_CR0_PE | X86_CR0_PG); */
		if (tmp != 0)
			fatal("x86_write_cr(): unimplemented cr4 bits: "
			    "0x%08llx\n", (long long)tmp);
		break;
	default:fatal("x86_write_cr(): write to UNIMPLEMENTED cr%i\n", r);
		cpu->running = 0;
	}
}


static char *ofs_string(int32_t imm)
{
	static char buf[25];
	buf[0] = buf[sizeof(buf)-1] = '\0';

	if (imm > 32)
		sprintf(buf, "+0x%x", imm);
	else if (imm > 0)
		sprintf(buf, "+%i", imm);
	else if (imm < -32)
		sprintf(buf, "-0x%x", -imm);
	else if (imm < 0)
		sprintf(buf, "-%i", -imm);

	return buf;
}


static char modrm_r[65];
static char modrm_rm[65];
#define MODRM_READ	0
#define MODRM_WRITE_RM	1
#define MODRM_WRITE_R	2
/*  flags:  */
#define	MODRM_EIGHTBIT		1
#define	MODRM_SEG		2
#define	MODRM_JUST_GET_ADDR	4
#define	MODRM_CR		8
#define	MODRM_DR		16
#define	MODRM_R_NONEIGHTBIT	32
#define	MODRM_RM_16BIT		64


/*
 *  modrm():
 *
 *  Yuck. I have a feeling that this function will become really ugly.
 */
static int modrm(struct cpu *cpu, int writeflag, int mode, int mode67,
	int flags, unsigned char **instrp, uint64_t *lenp,
	uint64_t *op1p, uint64_t *op2p)
{
	uint32_t imm, imm2;
	uint64_t addr = 0;
	int mod, r, rm, res = 1, z, q = mode/8, sib, s, i, b, immlen;
	char *e, *f;
	int disasm = (op1p == NULL);

	/*  e for data, f for addresses  */
	e = f = "";

	if (disasm) {
		if (mode == 32)
			e = "e";
		if (mode == 64)
			e = "r";
		if (mode67 == 32)
			f = "e";
		if (mode67 == 64)
			f = "r";
		modrm_rm[0] = modrm_rm[sizeof(modrm_rm)-1] = '\0';
		modrm_r[0] = modrm_r[sizeof(modrm_r)-1] = '\0';
	}

	immlen = mode67;
	if (immlen == 64)
		immlen = 32;

	imm = read_imm_common(instrp, lenp, 8, disasm);
	mod = (imm >> 6) & 3; r = (imm >> 3) & 7; rm = imm & 7;

	if (flags & MODRM_EIGHTBIT)
		q = 1;

	/*
	 *  R/M:
	 */

	switch (mod) {
	case 0:
		if (disasm) {
			if (mode67 >= 32) {
				if (rm == 5) {
					imm2 = read_imm_common(instrp, lenp,
					    immlen, disasm);
					sprintf(modrm_rm, "[0x%x]", imm2);
				} else if (rm == 4) {
					char tmp[20];
					sib = read_imm_common(instrp, lenp,
					    8, disasm);
					s = 1 << (sib >> 6);
					i = (sib >> 3) & 7;
					b = sib & 7;
					if (b == 5) {	/*  imm base  */
						imm2 = read_imm_common(instrp,
						    lenp, immlen, disasm);
						sprintf(tmp, ofs_string(imm2));
					} else
						sprintf(tmp, "+%s%s", f,
						    reg_names[b]);
					if (i == 4)
						sprintf(modrm_rm, "[%s]", tmp);
					else if (s == 1)
						sprintf(modrm_rm, "[%s%s%s]",
						    f, reg_names[i], tmp);
					else
						sprintf(modrm_rm, "[%s%s*%i%s"
						    "]", f, reg_names[i],
						    s, tmp);
				} else {
					sprintf(modrm_rm, "[%s%s]", f,
					    reg_names[rm]);
				}
			} else {
				switch (rm) {
				case 0:	sprintf(modrm_rm, "[bx+si]");
					break;
				case 1:	sprintf(modrm_rm, "[bx+di]");
					break;
				case 2:	sprintf(modrm_rm, "[bp+si]");
					break;
				case 3:	sprintf(modrm_rm, "[bp+di]");
					break;
				case 4:	sprintf(modrm_rm, "[si]");
					break;
				case 5:	sprintf(modrm_rm, "[di]");
					break;
				case 6:	imm2 = read_imm_common(instrp, lenp,
					    immlen, disasm);
					sprintf(modrm_rm, "[0x%x]", imm2);
					break;
				case 7:	sprintf(modrm_rm, "[bx]");
					break;
				}
			}
		} else {
			if (mode67 >= 32) {
				if (rm == 5) {
					addr = read_imm_common(instrp, lenp,
					    immlen, disasm);
				} else if (rm == 4) {
					sib = read_imm_common(instrp, lenp,
					    8, disasm);
					s = 1 << (sib >> 6);
					i = (sib >> 3) & 7;
					b = sib & 7;
					if (b == 4 &&
					    !cpu->cd.x86.seg_override)
						cpu->cd.x86.cursegment=X86_S_SS;
					if (b == 5)
						addr = read_imm_common(instrp,
						    lenp, mode67, disasm);
					else
						addr = cpu->cd.x86.r[b];
					if (i != 4)
						addr += cpu->cd.x86.r[i] * s;
				} else {
					addr = cpu->cd.x86.r[rm];
				}
			} else {
				switch (rm) {
				case 0:	addr = cpu->cd.x86.r[X86_R_BX] +
					    cpu->cd.x86.r[X86_R_SI]; break;
				case 1:	addr = cpu->cd.x86.r[X86_R_BX] +
					    cpu->cd.x86.r[X86_R_DI]; break;
				case 2:	addr = cpu->cd.x86.r[X86_R_BP] +
					    cpu->cd.x86.r[X86_R_SI];
					if (!cpu->cd.x86.seg_override)
						cpu->cd.x86.cursegment=X86_S_SS;
					break;
				case 3:	addr = cpu->cd.x86.r[X86_R_BP] +
					    cpu->cd.x86.r[X86_R_DI];
					if (!cpu->cd.x86.seg_override)
						cpu->cd.x86.cursegment=X86_S_SS;
					break;
				case 4:	addr = cpu->cd.x86.r[X86_R_SI]; break;
				case 5:	addr = cpu->cd.x86.r[X86_R_DI]; break;
				case 6:	addr = read_imm_common(instrp, lenp,
					    immlen, disasm); break;
				case 7:	addr = cpu->cd.x86.r[X86_R_BX]; break;
				}
			}

			if (mode67 == 16)
				addr &= 0xffff;
			if (mode67 == 32)
				addr &= 0xffffffffULL;

			switch (writeflag) {
			case MODRM_WRITE_RM:
				res = x86_store(cpu, addr, *op1p, q);
				break;
			case MODRM_READ:	/*  read  */
				if (flags & MODRM_JUST_GET_ADDR)
					*op1p = addr;
				else
					res = x86_load(cpu, addr, op1p, q);
			}
		}
		break;
	case 1:
	case 2:
		z = (mod == 1)? 8 : immlen;
		if (disasm) {
			if (mode67 >= 32) {
				if (rm == 4) {
					sib = read_imm_common(instrp, lenp,
					    8, disasm);
					s = 1 << (sib >> 6);
					i = (sib >> 3) & 7;
					b = sib & 7;
					imm2 = read_imm_common(instrp, lenp,
					    z, disasm);
					if (z == 8)  imm2 = (signed char)imm2;
					if (i == 4)
						sprintf(modrm_rm, "[%s%s%s]",
						    f, reg_names[b],
						    ofs_string(imm2));
					else if (s == 1)
						sprintf(modrm_rm, "[%s%s%s"
						    "%s%s]", f, reg_names[i],
						    f, reg_names[b],
						    ofs_string(imm2));
					else
						sprintf(modrm_rm, "[%s%s*%i+%s"
						    "%s%s]", f, reg_names[i], s,
						    f, reg_names[b],
						    ofs_string(imm2));
				} else {
					imm2 = read_imm_common(instrp, lenp,
					    z, disasm);
					if (z == 8)  imm2 = (signed char)imm2;
					sprintf(modrm_rm, "[%s%s%s]", f,
					    reg_names[rm], ofs_string(imm2));
				}
			} else
			switch (rm) {
			case 0:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)  imm2 = (signed char)imm2;
				sprintf(modrm_rm, "[bx+si%s]",ofs_string(imm2));
				break;
			case 1:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)  imm2 = (signed char)imm2;
				sprintf(modrm_rm, "[bx+di%s]",ofs_string(imm2));
				break;
			case 2:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)  imm2 = (signed char)imm2;
				sprintf(modrm_rm, "[bp+si%s]",ofs_string(imm2));
				break;
			case 3:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)  imm2 = (signed char)imm2;
				sprintf(modrm_rm, "[bp+di%s]",ofs_string(imm2));
				break;
			case 4:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)  imm2 = (signed char)imm2;
				sprintf(modrm_rm, "[si%s]", ofs_string(imm2));
				break;
			case 5:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)  imm2 = (signed char)imm2;
				sprintf(modrm_rm, "[di%s]", ofs_string(imm2));
				break;
			case 6:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)  imm2 = (signed char)imm2;
				sprintf(modrm_rm, "[bp%s]", ofs_string(imm2));
				break;
			case 7:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)  imm2 = (signed char)imm2;
				sprintf(modrm_rm, "[bx%s]", ofs_string(imm2));
				break;
			}
		} else {
			if (mode67 >= 32) {
				if (rm == 4) {
					sib = read_imm_common(instrp, lenp,
					    8, disasm);
					s = 1 << (sib >> 6);
					i = (sib >> 3) & 7;
					b = sib & 7;
					addr = read_imm_common(instrp, lenp,
					    z, disasm);
					if ((b == 4 || b == 5) &&
					    !cpu->cd.x86.seg_override)
						cpu->cd.x86.cursegment=X86_S_SS;
					if (z == 8)
						addr = (signed char)addr;
					if (i == 4)
						addr = cpu->cd.x86.r[b] + addr;
					else
						addr = cpu->cd.x86.r[i] * s +
						    cpu->cd.x86.r[b] + addr;
				} else {
					addr = read_imm_common(instrp, lenp,
					    z, disasm);
					if (z == 8)
						addr = (signed char)addr;
					addr = cpu->cd.x86.r[rm] + addr;
				}
			} else {
				addr = read_imm_common(instrp, lenp, z, disasm);
				if (z == 8)
					addr = (signed char)addr;
				switch (rm) {
				case 0:	addr += cpu->cd.x86.r[X86_R_BX]
					    + cpu->cd.x86.r[X86_R_SI];
					break;
				case 1:	addr += cpu->cd.x86.r[X86_R_BX]
					    + cpu->cd.x86.r[X86_R_DI];
					break;
				case 2:	addr += cpu->cd.x86.r[X86_R_BP]
					    + cpu->cd.x86.r[X86_R_SI];
					if (!cpu->cd.x86.seg_override)
						cpu->cd.x86.cursegment=X86_S_SS;
					break;
				case 3:	addr += cpu->cd.x86.r[X86_R_BP]
					    + cpu->cd.x86.r[X86_R_DI];
					if (!cpu->cd.x86.seg_override)
						cpu->cd.x86.cursegment=X86_S_SS;
					break;
				case 4:	addr += cpu->cd.x86.r[X86_R_SI];
					break;
				case 5:	addr += cpu->cd.x86.r[X86_R_DI];
					break;
				case 6:	addr += cpu->cd.x86.r[X86_R_BP];
					if (!cpu->cd.x86.seg_override)
						cpu->cd.x86.cursegment=X86_S_SS;
					break;
				case 7:	addr += cpu->cd.x86.r[X86_R_BX];
					break;
				}
			}

			if (mode67 == 16)
				addr &= 0xffff;
			if (mode67 == 32)
				addr &= 0xffffffffULL;

			switch (writeflag) {
			case MODRM_WRITE_RM:
				res = x86_store(cpu, addr, *op1p, q);
				break;
			case MODRM_READ:	/*  read  */
				if (flags & MODRM_JUST_GET_ADDR)
					*op1p = addr;
				else
					res = x86_load(cpu, addr, op1p, q);
			}
		}
		break;
	case 3:
		if (flags & MODRM_EIGHTBIT) {
			if (disasm) {
				strlcpy(modrm_rm, reg_names_bytes[rm],
				    sizeof(modrm_rm));
			} else {
				switch (writeflag) {
				case MODRM_WRITE_RM:
					if (rm < 4)
						cpu->cd.x86.r[rm] =
						    (cpu->cd.x86.r[rm] &
						    ~0xff) | (*op1p & 0xff);
					else
						cpu->cd.x86.r[rm&3] = (cpu->
						    cd.x86.r[rm&3] & ~0xff00) |
						    ((*op1p & 0xff) << 8);
					break;
				case MODRM_READ:
					if (rm < 4)
						*op1p = cpu->cd.x86.r[rm] &
						    0xff;
					else
						*op1p = (cpu->cd.x86.r[rm&3] &
						     0xff00) >> 8;
				}
			}
		} else {
			if (disasm) {
				if (mode == 16 || flags & MODRM_RM_16BIT)
					strlcpy(modrm_rm, reg_names[rm],
					    sizeof(modrm_rm));
				else
					sprintf(modrm_rm, "%s%s", e,
					    reg_names[rm]);
			} else {
				switch (writeflag) {
				case MODRM_WRITE_RM:
					if (mode == 16 ||
					    flags & MODRM_RM_16BIT)
						cpu->cd.x86.r[rm] = (
						    cpu->cd.x86.r[rm] & ~0xffff)
						    | (*op1p & 0xffff);
					else
						cpu->cd.x86.r[rm] =
						    modify(cpu->cd.x86.r[rm],
						    *op1p);
					break;
				case MODRM_READ:	/*  read  */
					if (mode == 16 ||
					    flags & MODRM_RM_16BIT)
						*op1p = cpu->cd.x86.r[rm]
						    & 0xffff;
					else
						*op1p = cpu->cd.x86.r[rm];
				}
			}
		}
		break;
	default:
		fatal("modrm(): unimplemented mod %i\n", mod);
		exit(1);
	}


	/*
	 *  R:
	 */

	if (flags & MODRM_EIGHTBIT && !(flags & MODRM_R_NONEIGHTBIT)) {
		if (disasm) {
			strlcpy(modrm_r, reg_names_bytes[r],
			    sizeof(modrm_r));
		} else {
			switch (writeflag) {
			case MODRM_WRITE_R:
				if (r < 4)
					cpu->cd.x86.r[r] = (cpu->cd.x86.r[r] &
					    ~0xff) | (*op2p & 0xff);
				else
					cpu->cd.x86.r[r&3] = (cpu->cd.x86.r[r&3]
					    & ~0xff00) | ((*op2p & 0xff) << 8);
				break;
			case MODRM_READ:
				if (r < 4)
					*op2p = cpu->cd.x86.r[r] & 0xff;
				else
					*op2p = (cpu->cd.x86.r[r&3] &
					    0xff00) >>8;
			}
		}
	} else {
		if (disasm) {
			if (flags & MODRM_SEG)
				strlcpy(modrm_r, seg_names[r],
				    sizeof(modrm_r));
			else if (flags & MODRM_CR)
				sprintf(modrm_r, "cr%i", r);
			else if (flags & MODRM_DR)
				sprintf(modrm_r, "dr%i", r);
			else {
				if (mode >= 32)
					sprintf(modrm_r, "%s%s", e,
					    reg_names[r]);
				else
					strlcpy(modrm_r, reg_names[r],
					    sizeof(modrm_r));
			}
		} else {
			switch (writeflag) {
			case MODRM_WRITE_R:
				if (flags & MODRM_SEG)
					cpu->cd.x86.s[r] = *op2p;
				else if (flags & MODRM_CR)
					x86_write_cr(cpu, r, *op2p);
				else if (flags & MODRM_DR)
					cpu->cd.x86.dr[r] = *op2p;
				else
					cpu->cd.x86.r[r] =
					    modify(cpu->cd.x86.r[r], *op2p);
				break;
			case MODRM_READ:
				if (flags & MODRM_SEG)
					*op2p = cpu->cd.x86.s[r];
				else if (flags & MODRM_CR)
					*op2p = cpu->cd.x86.cr[r];
				else if (flags & MODRM_DR)
					*op2p = cpu->cd.x86.dr[r];
				else
					*op2p = cpu->cd.x86.r[r];
			}
		}
	}

	if (!disasm) {
		switch (mode) {
		case 16:*op1p &= 0xffff; *op2p &= 0xffff; break;
		case 32:*op1p &= 0xffffffffULL; *op2p &= 0xffffffffULL; break;
		}
	}

	return res;
}


/*
 *  x86_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *
 *  If running&1 is 1, cpu->pc should be the address of the instruction.
 *
 *  If running&1 is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 *
 *  The rest of running tells us the default (code) operand size.
 */
int x86_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	int op, rep = 0, lock = 0, n_prefix_bytes = 0;
	uint64_t ilen = 0, offset;
	uint32_t imm=0, imm2;
	int mode = running & ~1;
	int mode67;
	char *symbol, *mnem = "ERROR", *e = "e", *prefix = NULL;

	if (running)
		dumpaddr = cpu->pc;

	if (mode == 0) {
		mode = cpu->cd.x86.descr_cache[X86_S_CS].default_op_size;
		if (mode == 0) {
			fatal("x86_cpu_disassemble_instr(): no mode: TODO\n");
			return 1;
		}
	}

	mode67 = mode;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (mode == 32)
		debug("%08x:  ", (int)dumpaddr);
	else if (mode == 64)
		debug("%016llx:  ", (long long)dumpaddr);
	else { /*  16-bit mode  */
		debug("%04x:%04x  ", cpu->cd.x86.s[X86_S_CS],
		    (int)dumpaddr & 0xffff);
	}

	/*
	 *  Decode the instruction:
	 */

	/*  All instructions are at least 1 byte long:  */
	HEXPRINT(instr,1);
	ilen = 1;

	/*  Any prefix?  */
	for (;;) {
		if (instr[0] == 0x66) {
			if (mode == 16)
				mode = 32;
			else
				mode = 16;
		} else if (instr[0] == 0x67) {
			if (mode67 == 16)
				mode67 = 32;
			else
				mode67 = 16;
		} else if (instr[0] == 0xf2) {
			rep = REP_REPNE;
		} else if (instr[0] == 0xf3) {
			rep = REP_REP;
		} else if (instr[0] == 0x26) {
			prefix = "es:";
		} else if (instr[0] == 0x2e) {
			prefix = "cs:";
		} else if (instr[0] == 0x36) {
			prefix = "ss:";
		} else if (instr[0] == 0x3e) {
			prefix = "ds:";
		} else if (instr[0] == 0x64) {
			prefix = "fs:";
		} else if (instr[0] == 0x65) {
			prefix = "gs:";
		} else if (instr[0] == 0xf0) {
			lock = 1;
		} else
			break;

		if (++n_prefix_bytes > 4) {
			SPACES; debug("more than 4 prefix bytes?\n");
			return 4;
		}

		/*  TODO: lock, segment overrides etc  */
		instr ++; ilen ++;
		debug("%02x", instr[0]);
	}

	if (mode == 16)
		e = "";

	op = instr[0];
	instr ++;

	if ((op & 0xf0) <= 0x30 && (op & 7) <= 5) {
		switch (op & 0x38) {
		case 0x00: mnem = "add"; break;
		case 0x08: mnem = "or"; break;
		case 0x10: mnem = "adc"; break;
		case 0x18: mnem = "sbb"; break;
		case 0x20: mnem = "and"; break;
		case 0x28: mnem = "sub"; break;
		case 0x30: mnem = "xor"; break;
		case 0x38: mnem = "cmp"; break;
		}
		switch (op & 7) {
		case 4:	imm = read_imm_and_print(&instr, &ilen, 8);
			SPACES; debug("%s\tal,0x%02x", mnem, imm);
			break;
		case 5:	imm = read_imm_and_print(&instr, &ilen, mode);
			SPACES; debug("%s\t%sax,0x%x", mnem, e, imm);
			break;
		default:modrm(cpu, MODRM_READ, mode, mode67, op&1? 0 :
			    MODRM_EIGHTBIT, &instr, &ilen, NULL, NULL);
			SPACES; debug("%s\t", mnem);
			if (op & 2)
				debug("%s,%s", modrm_r, modrm_rm);
			else
				debug("%s,%s", modrm_rm, modrm_r);
		}
	} else if (op == 0xf) {
		/*  "pop cs" on 8086  */
		if (cpu->cd.x86.model.model_number == X86_MODEL_8086) {
			SPACES; debug("pop\tcs");
		} else {
			imm = read_imm_and_print(&instr, &ilen, 8);
			if (imm == 0x00) {
				int subop = (*instr >> 3) & 0x7;
				switch (subop) {
				case 0:	modrm(cpu, MODRM_READ, mode, mode67,
					    0, &instr, &ilen, NULL, NULL);
					SPACES; debug("sldt\t%s", modrm_rm);
					break;
				case 1:	modrm(cpu, MODRM_READ, 16 /* note:16 */,
					    mode67, 0, &instr, &ilen,
					    NULL, NULL);
					SPACES; debug("str\t%s", modrm_rm);
					break;
				case 2:	modrm(cpu, MODRM_READ, 16 /* note:16 */,
					    mode67, 0, &instr, &ilen,
					    NULL, NULL);
					SPACES; debug("lldt\t%s", modrm_rm);
					break;
				case 3:	modrm(cpu, MODRM_READ, 16 /* note:16 */,
					    mode67, 0, &instr, &ilen,
					    NULL, NULL);
					SPACES; debug("ltr\t%s", modrm_rm);
					break;
				case 4:	modrm(cpu, MODRM_READ, 16 /* note:16 */,
					    mode67, 0, &instr, &ilen,
					    NULL, NULL);
					SPACES; debug("verr\t%s", modrm_rm);
					break;
				case 5:	modrm(cpu, MODRM_READ, 16 /* note:16 */,
					    mode67, 0, &instr, &ilen,
					    NULL, NULL);
					SPACES; debug("verw\t%s", modrm_rm);
					break;
				default:SPACES; debug("UNIMPLEMENTED 0x%02x,0x"
					    "%02x,0x%02x", op, imm, *instr);
				}
			} else if (imm == 0x01) {
				int subop = (*instr >> 3) & 0x7;
				switch (subop) {
				case 0:
				case 1:
				case 2:
				case 3:	modrm(cpu, MODRM_READ, mode, mode67,
					    0, &instr, &ilen, NULL, NULL);
					SPACES; debug("%s%s\t%s",
					    subop < 2? "s" : "l",
					    subop&1? "idt" : "gdt", modrm_rm);
					break;
				case 4:
				case 6:	if (((*instr >> 3) & 0x7) == 4)
						mnem = "smsw";
					else
						mnem = "lmsw";
					modrm(cpu, MODRM_READ, 16, mode67,
					    0, &instr, &ilen, NULL, NULL);
					SPACES; debug("%s\t%s", mnem, modrm_rm);
					break;
				case 7:	modrm(cpu, MODRM_READ, mode,
					    mode67, 0, &instr, &ilen,
					    NULL, NULL);
					SPACES; debug("invlpg\t%s", modrm_rm);
					break;
				default:SPACES; debug("UNIMPLEMENTED 0x%02x,0x"
					    "%02x,0x%02x", op, imm, *instr);
				}
			} else if (imm == 0x02) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &ilen, NULL, NULL);
				SPACES; debug("lar\t%s,%s", modrm_r, modrm_rm);
			} else if (imm == 0x03) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &ilen, NULL, NULL);
				SPACES; debug("lsl\t%s,%s", modrm_r, modrm_rm);
			} else if (imm == 0x05) {
				SPACES;		/* TODO: exactly which models?*/
				if (cpu->cd.x86.model.model_number >
				    X86_MODEL_80486)
					debug("syscall");
				else
					debug("loadall286");
			} else if (imm == 0x06) {
				SPACES; debug("clts");
			} else if (imm == 0x07) {
				SPACES;		/* TODO: exactly which models?*/
				if (cpu->cd.x86.model.model_number >
				    X86_MODEL_80486)
					debug("sysret");
				else
					debug("loadall");
			} else if (imm == 0x08) {
				SPACES; debug("invd");
			} else if (imm == 0x09) {
				SPACES; debug("wbinvd");
			} else if (imm == 0x0b) {
				SPACES; debug("reserved_0b");
			} else if (imm == 0x20 || imm == 0x21) {
				modrm(cpu, MODRM_READ, 32 /* note: 32  */,
				    mode67, imm == 0x20? MODRM_CR : MODRM_DR,
				    &instr, &ilen, NULL, NULL);
				SPACES; debug("mov\t%s,%s", modrm_rm, modrm_r);
			} else if (imm == 0x22 || imm == 0x23) {
				modrm(cpu, MODRM_READ, 32 /* note: 32  */,
				    mode67, imm == 0x22? MODRM_CR : MODRM_DR,
				    &instr, &ilen, NULL, NULL);
				SPACES; debug("mov\t%s,%s", modrm_r, modrm_rm);
			} else if (imm == 0x30) {
				SPACES; debug("wrmsr");
			} else if (imm == 0x31) {
				SPACES; debug("rdtsc");
			} else if (imm == 0x32) {
				SPACES; debug("rdmsr");
			} else if (imm == 0x33) {
				SPACES; debug("rdpmc");		/*  http://www
				    .x86.org/secrets/opcodes/rdpmc.htm  */
			} else if (imm == 0x34) {
				SPACES; debug("sysenter");
			} else if (imm == 0x36) {
				SPACES; debug("sysexit");
			} else if (imm >= 0x40 && imm <= 0x4f) {
				modrm(cpu, MODRM_READ, mode, mode67, 0,
				    &instr, &ilen, NULL, NULL);
				op = imm & 0xf;
				SPACES; debug("cmov%s%s\t%s,%s", op&1? "n"
				    : "", cond_names[(op/2) & 0x7],
				    modrm_r, modrm_rm);
			} else if (imm >= 0x80 && imm <= 0x8f) {
				op = imm & 0xf;
				imm = read_imm_and_print(&instr, &ilen, mode);
				imm = dumpaddr + 2 + mode/8 + imm;
				SPACES; debug("j%s%s\tnear 0x%x", op&1? "n"
				    : "", cond_names[(op/2) & 0x7], imm);
			} else if (imm >= 0x90 && imm <= 0x9f) {
				op = imm;
				modrm(cpu, MODRM_READ, mode,
				    mode67, MODRM_EIGHTBIT, &instr, &ilen,
				    NULL, NULL);
				SPACES; debug("set%s%s\t%s", op&1? "n"
				    : "", cond_names[(op/2) & 0x7], modrm_rm);
			} else if (imm == 0xa0) {
				SPACES; debug("push\tfs");
			} else if (imm == 0xa1) {
				SPACES; debug("pop\tfs");
			} else if (imm == 0xa2) {
				SPACES; debug("cpuid");
			} else if (imm == 0xa3 || imm == 0xab
			    || imm == 0xb3 || imm == 0xbb) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &ilen, NULL, NULL);
				switch (imm) {
				case 0xa3: mnem = "bt"; break;
				case 0xab: mnem = "bts"; break;
				case 0xb3: mnem = "btr"; break;
				case 0xbb: mnem = "btc"; break;
				}
				SPACES; debug("%s\t%s,%s",
				    mnem, modrm_rm, modrm_r);
			} else if (imm == 0xa4 || imm == 0xa5 ||
			    imm == 0xac || imm == 0xad) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &ilen, NULL, NULL);
				if (!(imm & 1))
					imm2 = read_imm_and_print(&instr,
					    &ilen, 8);
				else
					imm2 = 0;
				SPACES; debug("sh%sd\t%s,%s,",
				    imm <= 0xa5? "l" : "r",
				    modrm_rm, modrm_r);
				if (imm & 1)
					debug("cl");
				else
					debug("%i", imm2);
			} else if (imm == 0xa8) {
				SPACES; debug("push\tgs");
			} else if (imm == 0xa9) {
				SPACES; debug("pop\tgs");
			} else if (imm == 0xaa) {
				SPACES; debug("rsm");
			} else if (imm == 0xaf) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &ilen, NULL, NULL);
				SPACES; debug("imul\t%s,%s", modrm_r, modrm_rm);
			} else if (imm == 0xb0 || imm == 0xb1) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    imm == 0xb0? MODRM_EIGHTBIT : 0,
				    &instr, &ilen, NULL, NULL);
				SPACES; debug("cmpxchg\t%s,%s",
				    modrm_rm, modrm_r);
			} else if (imm == 0xb2 || imm == 0xb4 || imm == 0xb5) {
				modrm(cpu, MODRM_READ, mode, mode67, 0,
				    &instr, &ilen, NULL, NULL);
				switch (imm) {
				case 0xb2: mnem = "lss"; break;
				case 0xb4: mnem = "lfs"; break;
				case 0xb5: mnem = "lgs"; break;
				}
				SPACES; debug("%s\t%s,%s", mnem,
				    modrm_r, modrm_rm);
			} else if (imm == 0xb6 || imm == 0xb7 ||
			    imm == 0xbe || imm == 0xbf) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    (imm&1)==0? (MODRM_EIGHTBIT |
				    MODRM_R_NONEIGHTBIT) : MODRM_RM_16BIT,
				    &instr, &ilen, NULL, NULL);
				mnem = "movsx";
				if (imm <= 0xb7)
					mnem = "movzx";
				SPACES; debug("%s\t%s,%s", mnem,
				    modrm_r, modrm_rm);
			} else if (imm == 0xba) {
				int subop = (*instr >> 3) & 0x7;
				switch (subop) {
				case 4:	modrm(cpu, MODRM_READ, mode, mode67,
					    0, &instr, &ilen, NULL, NULL);
					imm2 = read_imm_and_print(&instr,
					    &ilen, 8);
					SPACES; debug("bt\t%s,%i",
					    modrm_rm, imm2);
					break;
				case 5: modrm(cpu, MODRM_READ, mode, mode67,
					    0, &instr, &ilen, NULL, NULL);
					imm2 = read_imm_and_print(&instr,
					    &ilen, 8);
					SPACES; debug("bts\t%s,%i",
					    modrm_rm, imm2);
					break;
				case 6: modrm(cpu, MODRM_READ, mode, mode67,
					    0, &instr, &ilen, NULL, NULL);
					imm2 = read_imm_and_print(&instr,
					    &ilen, 8);
					SPACES; debug("btr\t%s,%i",
					    modrm_rm, imm2);
					break;
				case 7:	modrm(cpu, MODRM_READ, mode, mode67,
					    0, &instr, &ilen, NULL, NULL);
					imm2 = read_imm_and_print(&instr,
					    &ilen, 8);
					SPACES; debug("btc\t%s,%i",
					    modrm_rm, imm2);
					break;
				default:SPACES; debug("UNIMPLEMENTED 0x%02x,0x"
					    "%02x,0x%02x", op, imm, *instr);
				}
			} else if (imm == 0xbc || imm == 0xbd) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &ilen, NULL, NULL);
				if (imm == 0xbc)
					mnem = "bsf";
				else
					mnem = "bsr";
				SPACES; debug("%s\t%s,%s", mnem, modrm_r,
				    modrm_rm);
			} else if (imm == 0xc0 || imm == 0xc1) {
				modrm(cpu, MODRM_READ, mode, mode67,
				    imm&1? 0 : MODRM_EIGHTBIT,
				    &instr, &ilen, NULL, NULL);
				SPACES; debug("xadd\t%s,%s", modrm_rm, modrm_r);
			} else if (imm == 0xc7) {
				int subop = (*instr >> 3) & 0x7;
				switch (subop) {
				case 1:	modrm(cpu, MODRM_READ, 64, mode67,
					    0, &instr, &ilen, NULL, NULL);
					SPACES; debug("cmpxchg8b\t%s",modrm_rm);
					break;
				default:SPACES; debug("UNIMPLEMENTED 0x%02x,0x"
					    "%02x,0x%02x", op, imm, *instr);
				}
			} else if (imm >= 0xc8 && imm <= 0xcf) {
				SPACES; debug("bswap\te%s", reg_names[imm & 7]);
			} else {
				SPACES; debug("UNIMPLEMENTED 0x0f,0x%02x", imm);
			}
		}
	} else if (op < 0x20 && (op & 7) == 6) {
		SPACES; debug("push\t%s", seg_names[op/8]);
	} else if (op < 0x20 && (op & 7) == 7) {
		SPACES; debug("pop\t%s", seg_names[op/8]);
	} else if (op >= 0x20 && op < 0x40 && (op & 7) == 7) {
		SPACES; debug("%sa%s", op < 0x30? "d" : "a",
		    (op & 0xf)==7? "a" : "s");
	} else if (op >= 0x40 && op <= 0x5f) {
		switch (op & 0x38) {
		case 0x00: mnem = "inc"; break;
		case 0x08: mnem = "dec"; break;
		case 0x10: mnem = "push"; break;
		case 0x18: mnem = "pop"; break;
		}
		SPACES; debug("%s\t%s%s", mnem, e, reg_names[op & 7]);
	} else if (op == 0x60) {
		SPACES; debug("pusha%s", mode==16? "" : (mode==32? "d" : "q"));
	} else if (op == 0x61) {
		SPACES; debug("popa%s", mode==16? "" : (mode==32? "d" : "q"));
	} else if (op == 0x62) {
		modrm(cpu, MODRM_READ, mode, mode67,
		    0, &instr, &ilen, NULL, NULL);
		SPACES; debug("bound\t%s,%s", modrm_r, modrm_rm);
	} else if (op == 0x63) {
		modrm(cpu, MODRM_READ, 16, mode67,
		    0, &instr, &ilen, NULL, NULL);
		SPACES; debug("arpl\t%s,%s", modrm_rm, modrm_r);
	} else if (op == 0x68) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		SPACES; debug("push\t%sword 0x%x", mode==32?"d":"", imm);
	} else if (op == 0x69 || op == 0x6b) {
		modrm(cpu, MODRM_READ, mode, mode67,
		    0, &instr, &ilen, NULL, NULL);
		if (op == 0x69)
			imm = read_imm_and_print(&instr, &ilen, mode);
		else
			imm = (signed char)read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("imul\t%s,%s,%i", modrm_r, modrm_rm, imm);
	} else if (op == 0x6a) {
		imm = (signed char)read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("push\tbyte 0x%x", imm);
	} else if (op == 0x6c) {
		SPACES; debug("insb");
	} else if (op == 0x6d) {
		SPACES; debug("ins%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op == 0x6e) {
		SPACES; debug("outsb");
	} else if (op == 0x6f) {
		SPACES; debug("outs%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if ((op & 0xf0) == 0x70) {
		imm = (signed char)read_imm_and_print(&instr, &ilen, 8);
		imm = dumpaddr + 2 + imm;
		SPACES; debug("j%s%s\t0x%x", op&1? "n" : "",
		    cond_names[(op/2) & 0x7], imm);
	} else if (op == 0x80 || op == 0x81) {
		switch ((*instr >> 3) & 0x7) {
		case 0:	mnem = "add"; break;
		case 1:	mnem = "or"; break;
		case 2:	mnem = "adc"; break;
		case 3:	mnem = "sbb"; break;
		case 4:	mnem = "and"; break;
		case 5:	mnem = "sub"; break;
		case 6:	mnem = "xor"; break;
		case 7:	mnem = "cmp"; break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
		modrm(cpu, MODRM_READ, mode, mode67,
		    op == 0x80? MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
		imm = read_imm_and_print(&instr, &ilen, op==0x80? 8 : mode);
		SPACES; debug("%s\t%s,0x%x", mnem, modrm_rm, imm);
	} else if (op == 0x83) {
		switch ((*instr >> 3) & 0x7) {
		case 0:	mnem = "add"; break;
		case 1:	mnem = "or"; break;
		case 2:	mnem = "adc"; break;
		case 3:	mnem = "sbb"; break;
		case 4:	mnem = "and"; break;
		case 5:	mnem = "sub"; break;
		case 6:	mnem = "xor"; break;
		case 7: mnem = "cmp"; break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
		modrm(cpu, MODRM_READ, mode, mode67, 0, &instr, &ilen,
		    NULL, NULL);
		imm = (signed char)read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("%s\t%s,0x%x", mnem, modrm_rm, imm);
	} else if (op == 0x84 || op == 0x85) {
		modrm(cpu, MODRM_READ, mode, mode67,
		    op == 0x84? MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
		SPACES; debug("test\t%s,%s", modrm_rm, modrm_r);
	} else if (op == 0x86 || op == 0x87) {
		modrm(cpu, MODRM_READ, mode, mode67, op == 0x86?
		    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
		SPACES; debug("xchg\t%s,%s", modrm_rm, modrm_r);
	} else if (op == 0x88 || op == 0x89) {
		modrm(cpu, MODRM_READ, mode, mode67, op == 0x88?
		    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
		SPACES; debug("mov\t%s,%s", modrm_rm, modrm_r);
	} else if (op == 0x8a || op == 0x8b) {
		modrm(cpu, MODRM_READ, mode, mode67, op == 0x8a?
		    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
		SPACES; debug("mov\t%s,%s", modrm_r, modrm_rm);
	} else if (op == 0x8c || op == 0x8e) {
		modrm(cpu, MODRM_READ, mode, mode67, MODRM_SEG, &instr, &ilen,
		    NULL, NULL);
		SPACES; debug("mov\t");
		if (op == 0x8c)
			debug("%s,%s", modrm_rm, modrm_r);
		else
			debug("%s,%s", modrm_r, modrm_rm);
	} else if (op == 0x8d) {
		modrm(cpu, MODRM_READ, mode, mode67, 0, &instr, &ilen,
		    NULL, NULL);
		SPACES; debug("lea\t%s,%s", modrm_r, modrm_rm);
	} else if (op == 0x8f) {
		switch ((*instr >> 3) & 0x7) {
		case 0:	/*  POP m16/m32  */
			modrm(cpu, MODRM_READ, mode, mode67, 0, &instr,
			    &ilen, NULL, NULL);
			SPACES; debug("pop\t%sword %s", mode == 32? "d" : "",
			    modrm_rm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
	} else if (op == 0x90) {
		SPACES; debug("nop");
	} else if (op >= 0x91 && op <= 0x97) {
		SPACES; debug("xchg\t%sax,%s%s", e, e, reg_names[op & 7]);
	} else if (op == 0x98) {
		SPACES; debug("cbw");
	} else if (op == 0x99) {
		SPACES; debug("cwd");
	} else if (op == 0x9a) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		imm2 = read_imm_and_print(&instr, &ilen, 16);
		SPACES; debug("call\t0x%04x:", imm2);
		if (mode == 16)
			debug("0x%04x", imm);
		else
			debug("0x%08x", imm);
	} else if (op == 0x9b) {
		SPACES; debug("wait");
	} else if (op == 0x9c) {
		SPACES; debug("pushf%s", mode==16? "" : (mode==32? "d" : "q"));
	} else if (op == 0x9d) {
		SPACES; debug("popf%s", mode==16? "" : (mode==32? "d" : "q"));
	} else if (op == 0x9e) {
		SPACES; debug("sahf");
	} else if (op == 0x9f) {
		SPACES; debug("lahf");
	} else if (op == 0xa0) {
		imm = read_imm_and_print(&instr, &ilen, mode67);
		SPACES; debug("mov\tal,[0x%x]", imm);
	} else if (op == 0xa1) {
		imm = read_imm_and_print(&instr, &ilen, mode67);
		SPACES; debug("mov\t%sax,[0x%x]", e, imm);
	} else if (op == 0xa2) {
		imm = read_imm_and_print(&instr, &ilen, mode67);
		SPACES; debug("mov\t[0x%x],al", imm);
	} else if (op == 0xa3) {
		imm = read_imm_and_print(&instr, &ilen, mode67);
		SPACES; debug("mov\t[0x%x],%sax", imm, e);
	} else if (op == 0xa4) {
		SPACES; debug("movsb");
	} else if (op == 0xa5) {
		SPACES; debug("movs%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op == 0xa6) {
		SPACES; debug("cmpsb");
	} else if (op == 0xa7) {
		SPACES; debug("cmps%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op == 0xa8 || op == 0xa9) {
		imm = read_imm_and_print(&instr, &ilen, op == 0xa8? 8 : mode);
		if (op == 0xa8)
			mnem = "al";
		else if (mode == 16)
			mnem = "ax";
		else
			mnem = "eax";
		SPACES; debug("test\t%s,0x%x", mnem, imm);
	} else if (op == 0xaa) {
		SPACES; debug("stosb");
	} else if (op == 0xab) {
		SPACES; debug("stos%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op == 0xac) {
		SPACES; debug("lodsb");
	} else if (op == 0xad) {
		SPACES; debug("lods%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op == 0xae) {
		SPACES; debug("scasb");
	} else if (op == 0xaf) {
		SPACES; debug("scas%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op >= 0xb0 && op <= 0xb7) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("mov\t%s,0x%x", reg_names_bytes[op&7], imm);
	} else if (op >= 0xb8 && op <= 0xbf) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		SPACES; debug("mov\t%s%s,0x%x", e, reg_names[op & 7], imm);
	} else if (op == 0xc0 || op == 0xc1) {
		switch ((*instr >> 3) & 0x7) {
		case 0:	mnem = "rol"; break;
		case 1:	mnem = "ror"; break;
		case 2:	mnem = "rcl"; break;
		case 3:	mnem = "rcr"; break;
		case 4:	mnem = "shl"; break;
		case 5:	mnem = "shr"; break;
		case 6:	mnem = "sal"; break;
		case 7:	mnem = "sar"; break;
		}
		modrm(cpu, MODRM_READ, mode, mode67, op == 0xc0?
		    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("%s\t%s,%i", mnem, modrm_rm, imm);
	} else if (op == 0xc2) {
		imm = read_imm_and_print(&instr, &ilen, 16);
		SPACES; debug("ret\t0x%x", imm);
	} else if (op == 0xc3) {
		SPACES; debug("ret");
	} else if (op == 0xc4 || op == 0xc5) {
		modrm(cpu, MODRM_READ, mode, mode67, 0, &instr, &ilen,
		    NULL, NULL);
		switch (op) {
		case 0xc4: mnem = "les"; break;
		case 0xc5: mnem = "lds"; break;
		}
		SPACES; debug("%s\t%s,%s", mnem, modrm_r, modrm_rm);
	} else if (op == 0xc6 || op == 0xc7) {
		switch ((*instr >> 3) & 0x7) {
		case 0:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xc6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			imm = read_imm_and_print(&instr, &ilen,
			    op == 0xc6? 8 : mode);
			SPACES; debug("mov\t%s,0x%x", modrm_rm, imm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
	} else if (op == 0xc8) {
		imm = read_imm_and_print(&instr, &ilen, 16);
		imm2 = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("enter\t0x%x,%i", imm, imm2);
	} else if (op == 0xc9) {
		SPACES; debug("leave");
	} else if (op == 0xca) {
		imm = read_imm_and_print(&instr, &ilen, 16);
		SPACES; debug("retf\t0x%x", imm);
	} else if (op == 0xcb) {
		SPACES; debug("retf");
	} else if (op == 0xcc) {
		SPACES; debug("int3");
	} else if (op == 0xcd) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("int\t0x%x", imm);
	} else if (op == 0xce) {
		SPACES; debug("into");
	} else if (op == 0xcf) {
		SPACES; debug("iret");
	} else if (op >= 0xd0 && op <= 0xd3) {
		int subop = (*instr >> 3) & 0x7;
		modrm(cpu, MODRM_READ, mode, mode67, op&1? 0 :
		    MODRM_EIGHTBIT, &instr, &ilen, NULL, NULL);
		switch (subop) {
		case 0: mnem = "rol"; break;
		case 1: mnem = "ror"; break;
		case 2: mnem = "rcl"; break;
		case 3: mnem = "rcr"; break;
		case 4: mnem = "shl"; break;
		case 5: mnem = "shr"; break;
		case 6: mnem = "sal"; break;
		case 7: mnem = "sar"; break;
		}
		SPACES; debug("%s\t%s,", mnem, modrm_rm);
		if (op <= 0xd1)
			debug("1");
		else
			debug("cl");
	} else if (op == 0xd4) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("aam");
		if (imm != 10)
			debug("\t%i", imm);
	} else if (op == 0xd5) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("aad");
		if (imm != 10)
			debug("\t%i", imm);
	} else if (op == 0xd6) {
		SPACES; debug("salc");		/*  undocumented?  */
	} else if (op == 0xd7) {
		SPACES; debug("xlat");
	} else if (op == 0xd9) {
		int subop = (*instr >> 3) & 7;
		imm = *instr;
		if (subop == 5) {
			modrm(cpu, MODRM_READ, 16, mode67, 0,
			    &instr, &ilen, NULL, NULL);
			SPACES; debug("fldcw\t%s", modrm_rm);
		} else if (subop == 7) {
			modrm(cpu, MODRM_READ, 16, mode67, 0,
			    &instr, &ilen, NULL, NULL);
			SPACES; debug("fstcw\t%s", modrm_rm);
		} else {
			SPACES; debug("UNIMPLEMENTED 0x%02x,0x%02x", op, imm);
		}
	} else if (op == 0xdb) {
		imm = *instr;
		if (imm == 0xe2) {
			read_imm_and_print(&instr, &ilen, 8);
			SPACES; debug("fclex");
		} else if (imm == 0xe3) {
			read_imm_and_print(&instr, &ilen, 8);
			SPACES; debug("finit");
		} else if (imm == 0xe4) {
			read_imm_and_print(&instr, &ilen, 8);
			SPACES; debug("fsetpm");
		} else {
			SPACES; debug("UNIMPLEMENTED 0x%02x,0x%02x", op, imm);
		}
	} else if (op == 0xdd) {
		int subop = (*instr >> 3) & 7;
		imm = *instr;
		if (subop == 7) {
			modrm(cpu, MODRM_READ, 16, mode67, 0,
			    &instr, &ilen, NULL, NULL);
			SPACES; debug("fstsw\t%s", modrm_rm);
		} else {
			SPACES; debug("UNIMPLEMENTED 0x%02x,0x%02x", op, imm);
		}
	} else if (op == 0xdf) {
		imm = *instr;
		if (imm == 0xe0) {
			read_imm_and_print(&instr, &ilen, 8);
			SPACES; debug("fstsw\tax");
		} else {
			SPACES; debug("UNIMPLEMENTED 0x%02x,0x%02x", op, imm);
		}
	} else if (op == 0xe3) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		imm = dumpaddr + ilen + (signed char)imm;
		if (mode == 16)
			mnem = "jcxz";
		else
			mnem = "jecxz";
		SPACES; debug("%s\t0x%x", mnem, imm);
	} else if (op == 0xe4) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("in\tal,0x%x", imm);
	} else if (op == 0xe5) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("in\t%sax,0x%x", e, imm);
	} else if (op == 0xe6) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("out\t0x%x,al", imm);
	} else if (op == 0xe7) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("out\t0x%x,%sax", imm, e);
	} else if (op == 0xe8 || op == 0xe9) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		if (mode == 16)
			imm = (int16_t)imm;
		imm = dumpaddr + ilen + imm;
		switch (op) {
		case 0xe8: mnem = "call"; break;
		case 0xe9: mnem = "jmp"; break;
		}
		SPACES; debug("%s\t0x%x", mnem, imm);
	} else if (op == 0xea) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		imm2 = read_imm_and_print(&instr, &ilen, 16);
		SPACES; debug("jmp\t0x%04x:", imm2);
		if (mode == 16)
			debug("0x%04x", imm);
		else
			debug("0x%08x", imm);
	} else if ((op >= 0xe0 && op <= 0xe2) || op == 0xeb) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		imm = dumpaddr + ilen + (signed char)imm;
		switch (op) {
		case 0xe0: mnem = "loopnz"; break;
		case 0xe1: mnem = "loopz"; break;
		case 0xe2: mnem = "loop"; break;
		case 0xeb: mnem = "jmp"; break;
		}
		SPACES; debug("%s\t0x%x", mnem, imm);
	} else if (op == 0xec) {
		SPACES; debug("in\tal,dx");
	} else if (op == 0xed) {
		SPACES; debug("in\t%sax,dx", e);
	} else if (op == 0xee) {
		SPACES; debug("out\tdx,al");
	} else if (op == 0xef) {
		SPACES; debug("out\tdx,%sax", e);
	} else if (op == 0xf1) {
		SPACES; debug("icebp");		/*  undocumented?  */
		/*  http://www.x86.org/secrets/opcodes/icebp.htm  */
	} else if (op == 0xf4) {
		SPACES; debug("hlt");
	} else if (op == 0xf5) {
		SPACES; debug("cmc");
	} else if (op == 0xf8) {
		SPACES; debug("clc");
	} else if (op == 0xf9) {
		SPACES; debug("stc");
	} else if (op == 0xfa) {
		SPACES; debug("cli");
	} else if (op == 0xfb) {
		SPACES; debug("sti");
	} else if (op == 0xfc) {
		SPACES; debug("cld");
	} else if (op == 0xfd) {
		SPACES; debug("std");
	} else if (op == 0xf6 || op == 0xf7) {
		switch ((*instr >> 3) & 0x7) {
		case 0:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			imm = read_imm_and_print(&instr, &ilen,
			    op == 0xf6? 8 : mode);
			SPACES; debug("test\t%s,0x%x", modrm_rm, imm);
			break;
		case 2:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("not\t%s", modrm_rm);
			break;
		case 3:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("neg\t%s", modrm_rm);
			break;
		case 4:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("mul\t%s", modrm_rm);
			break;
		case 5:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("imul\t%s", modrm_rm);
			break;
		case 6:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("div\t%s", modrm_rm);
			break;
		case 7:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("idiv\t%s", modrm_rm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x,0x%02x", op,*instr);
		}
	} else if (op == 0xfe || op == 0xff) {
		/*  FE /0 = inc r/m8 */
		/*  FE /1 = dec r/m8 */
		/*  FF /2 = call near rm16/32  */
		/*  FF /3 = call far m16:32  */
		/*  FF /6 = push r/m16/32 */
		switch ((*instr >> 3) & 0x7) {
		case 0:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xfe?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("inc\t%s", modrm_rm);
			break;
		case 1:	modrm(cpu, MODRM_READ, mode, mode67, op == 0xfe?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("dec\t%s", modrm_rm);
			break;
		case 2:	if (op == 0xfe) {
				SPACES; debug("UNIMPLEMENTED "
				    "0x%02x,0x%02x", op,*instr);
			} else {
				modrm(cpu, MODRM_READ, mode, mode67, 0, &instr,
				    &ilen, NULL, NULL);
				SPACES; debug("call\t%s", modrm_rm);
			}
			break;
		case 3:	if (op == 0xfe) {
				SPACES; debug("UNIMPLEMENTED "
				    "0x%02x,0x%02x", op,*instr);
			} else {
				modrm(cpu, MODRM_READ, mode, mode67, 0, &instr,
				    &ilen, NULL, NULL);
				SPACES; debug("call\tfar %s", modrm_rm);
			}
			break;
		case 4:	if (op == 0xfe) {
				SPACES; debug("UNIMPLEMENTED "
				    "0x%02x,0x%02x", op,*instr);
			} else {
				modrm(cpu, MODRM_READ, mode, mode67, 0, &instr,
				    &ilen, NULL, NULL);
				SPACES; debug("jmp\t%s", modrm_rm);
			}
			break;
		case 5:	if (op == 0xfe) {
				SPACES; debug("UNIMPLEMENTED "
				    "0x%02x,0x%02x", op,*instr);
			} else {
				modrm(cpu, MODRM_READ, mode, mode67, 0, &instr,
				    &ilen, NULL, NULL);
				SPACES; debug("jmp\tfar %s", modrm_rm);
			}
			break;
		case 6:	if (op == 0xfe) {
				SPACES; debug("UNIMPLEMENTED "
				    "0x%02x,0x%02x", op,*instr);
			} else {
				modrm(cpu, MODRM_READ, mode, mode67, 0, &instr,
				    &ilen, NULL, NULL);
				SPACES; debug("push\t%sword %s",
				    mode == 32? "d" : "", modrm_rm);
			}
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x,0x%02x", op,*instr);
		}
	} else {
		SPACES; debug("UNIMPLEMENTED 0x%02x", op);
	}

	switch (rep) {
	case REP_REP:    debug(" (rep)"); break;
	case REP_REPNE:  debug(" (repne)"); break;
	}
	if (prefix != NULL)
		debug(" (%s)", prefix);
	if (lock)
		debug(" (lock)");

	debug("\n");
	return ilen;
}


/*
 *  x86_cpuid():
 *
 *  TODO: Level 1 and 2 info.
 */
static void x86_cpuid(struct cpu *cpu)
{
	switch (cpu->cd.x86.r[X86_R_AX]) {
	/*  Normal CPU id:  */
	case 0:	cpu->cd.x86.r[X86_R_AX] = 2;
		/*  Intel...  */
		cpu->cd.x86.r[X86_R_BX] = 0x756e6547;  /*  "Genu"  */
		cpu->cd.x86.r[X86_R_DX] = 0x49656e69;  /*  "ineI"  */
		cpu->cd.x86.r[X86_R_CX] = 0x6c65746e;  /*  "ntel"  */
		/*  ... or AMD:  */
		cpu->cd.x86.r[X86_R_BX] = 0x68747541;  /*  "Auth"  */
		cpu->cd.x86.r[X86_R_DX] = 0x69746E65;  /*  "enti"  */
		cpu->cd.x86.r[X86_R_CX] = 0x444D4163;  /*  "cAMD"  */
		break;
	case 1:	/*  TODO  */
		cpu->cd.x86.r[X86_R_AX] = 0x0623;
		cpu->cd.x86.r[X86_R_BX] = (cpu->cpu_id << 24);
		/*  TODO: are bits 8..15 the _total_ nr of cpus, or the
		    cpu id of this one?  */
		cpu->cd.x86.r[X86_R_CX] = X86_CPUID_ECX_CX16;
		cpu->cd.x86.r[X86_R_DX] = X86_CPUID_EDX_CX8 | X86_CPUID_EDX_FPU
		    | X86_CPUID_EDX_MSR | X86_CPUID_EDX_TSC | X86_CPUID_EDX_MTRR
		    | X86_CPUID_EDX_CMOV | X86_CPUID_EDX_PSE |
		    X86_CPUID_EDX_SEP | X86_CPUID_EDX_PGE |
		    X86_CPUID_EDX_MMX | X86_CPUID_EDX_FXSR;
		break;
	case 2:	/*  TODO: actual Cache info  */
		/*  This is just bogus  */
		cpu->cd.x86.r[X86_R_AX] = 0x03020101;
		cpu->cd.x86.r[X86_R_BX] = 0x00000000;
		cpu->cd.x86.r[X86_R_CX] = 0x00000000;
		cpu->cd.x86.r[X86_R_DX] = 0x06040a42;
		break;

	/*  Extended CPU id:  */
	case 0x80000000:
		cpu->cd.x86.r[X86_R_AX] = 0x80000008;
		/*  AMD...  */
		cpu->cd.x86.r[X86_R_BX] = 0x68747541;
		cpu->cd.x86.r[X86_R_DX] = 0x444D4163;
		cpu->cd.x86.r[X86_R_CX] = 0x69746E65;
		break;
	case 0x80000001:
		cpu->cd.x86.r[X86_R_AX] = 0;
		cpu->cd.x86.r[X86_R_BX] = 0;
		cpu->cd.x86.r[X86_R_CX] = 0;
		cpu->cd.x86.r[X86_R_DX] = (cpu->cd.x86.model.model_number 
		    >= X86_MODEL_AMD64)? X86_CPUID_EXT_EDX_LM : 0;
		break;
	case 0x80000002:
	case 0x80000003:
	case 0x80000004:
	case 0x80000005:
	case 0x80000006:
	case 0x80000007:
		fatal("[ CPUID 0x%08x ]\n", (int)cpu->cd.x86.r[X86_R_AX]);
		cpu->cd.x86.r[X86_R_AX] = 0;
		cpu->cd.x86.r[X86_R_BX] = 0;
		cpu->cd.x86.r[X86_R_CX] = 0;
		cpu->cd.x86.r[X86_R_DX] = 0;
		break;
	case 0x80000008:
		cpu->cd.x86.r[X86_R_AX] = 0x00003028;
		cpu->cd.x86.r[X86_R_BX] = 0;
		cpu->cd.x86.r[X86_R_CX] = 0;
		cpu->cd.x86.r[X86_R_DX] = 0;
		break;
	default:fatal("x86_cpuid(): unimplemented eax = 0x%x\n",
		    (int)cpu->cd.x86.r[X86_R_AX]);
		cpu->running = 0;
	}
}


#define	TRANSLATE_ADDRESS       translate_address_x86
#include "memory_x86.c"
#undef TRANSLATE_ADDRESS


#define MEMORY_RW	x86_memory_rw
#define MEM_X86
#include "memory_rw.c"
#undef MEM_X86
#undef MEMORY_RW


/*
 *  x86_push():
 */
static int x86_push(struct cpu *cpu, uint64_t value, int mode)
{
	int res = 1, oldseg;
	int ssize = cpu->cd.x86.descr_cache[X86_S_SS].default_op_size;
	uint64_t new_esp;
	uint64_t old_esp = cpu->cd.x86.r[X86_R_SP];
	uint16_t old_ss = cpu->cd.x86.s[X86_S_SS];
	uint64_t old_eip = cpu->pc;
	uint16_t old_cs = cpu->cd.x86.s[X86_S_CS];

	/*  TODO: up/down?  */
	/*  TODO: stacksize?  */
ssize = mode;

	oldseg = cpu->cd.x86.cursegment;
	cpu->cd.x86.cursegment = X86_S_SS;
	if (ssize == 16)
		new_esp = (cpu->cd.x86.r[X86_R_SP] & ~0xffff)
		    | ((cpu->cd.x86.r[X86_R_SP] - (ssize / 8)) & 0xffff);
	else
		new_esp = (cpu->cd.x86.r[X86_R_SP] -
		    (ssize / 8)) & 0xffffffff;
	res = x86_store(cpu, new_esp, value, ssize / 8);
	if (!res) {
		fatal("WARNING: x86_push store failed: cs:eip=0x%04x:0x%08x"
		    " ss:esp=0x%04x:0x%08x\n", (int)old_cs,
		    (int)old_eip, (int)old_ss, (int)old_esp);
		if ((old_cs & X86_PL_MASK) != X86_RING3)
			cpu->running = 0;
	} else {
		cpu->cd.x86.r[X86_R_SP] = new_esp;
	}
	cpu->cd.x86.cursegment = oldseg;
	return res;
}


/*
 *  x86_pop():
 */
static int x86_pop(struct cpu *cpu, uint64_t *valuep, int mode)
{
	int res = 1, oldseg;
	int ssize = cpu->cd.x86.descr_cache[X86_S_SS].default_op_size;

	/*  TODO: up/down?  */
	/*  TODO: stacksize?  */
ssize = mode;

	oldseg = cpu->cd.x86.cursegment;
	cpu->cd.x86.cursegment = X86_S_SS;
	res = x86_load(cpu, cpu->cd.x86.r[X86_R_SP], valuep, ssize / 8);
	if (!res) {
		fatal("WARNING: x86_pop load failed\n");
	} else {
		if (ssize == 16)
			cpu->cd.x86.r[X86_R_SP] = (cpu->cd.x86.r[X86_R_SP] &
			    ~0xffff) | ((cpu->cd.x86.r[X86_R_SP] + (ssize / 8))
			    & 0xffff);
		else
			cpu->cd.x86.r[X86_R_SP] = (cpu->cd.x86.r[X86_R_SP] +
			    (ssize / 8)) & 0xffffffff;
	}
	cpu->cd.x86.cursegment = oldseg;
	return res;
}


#define	INT_TYPE_CALLGATE		1
#define	INT_TYPE_INTGATE		2
#define	INT_TYPE_TRAPGATE		3
/*
 *  x86_interrupt():
 *
 *  Read the interrupt descriptor table (or, in real mode, the interrupt
 *  vector table), push flags/cs/eip, and jump to the interrupt handler.
 */
int x86_interrupt(struct cpu *cpu, int nr, int errcode)
{
	uint16_t seg, old_cs;
	uint32_t ofs;
	int res, mode;
	unsigned char buf[8];

	old_cs = cpu->cd.x86.s[X86_S_CS];

	debug("{ x86_interrupt %i }\n", nr);

	if (PROTECTED_MODE) {
		int i, int_type = 0;

		if (nr * 8 > cpu->cd.x86.idtr_limit) {
			fatal("TODO: protected mode int 0x%02x outside idtr"
			    " limit (%i)?\n", nr, (int)cpu->cd.x86.idtr_limit);
			cpu->running = 0;
			return 0;
		}

		/*  Read the interrupt descriptor:  */
		res = cpu->memory_rw(cpu, cpu->mem, cpu->cd.x86.idtr + nr*8,
		    buf, 8, MEM_READ, NO_SEGMENTATION);
		if (!res) {
			fatal("x86_interrupt(): could not read the"
			    " interrupt descriptor table (prot. mode)\n");
			cpu->running = 0;
			return 0;
		}

		if ((buf[5] & 0x17) == 0x04)
			int_type = INT_TYPE_CALLGATE;
		if ((buf[5] & 0x17) == 0x06)
			int_type = INT_TYPE_INTGATE;
		if ((buf[5] & 0x17) == 0x07)
			int_type = INT_TYPE_TRAPGATE;

		if (!int_type) {
			fatal("x86_interrupt(): TODO:\n");
			for (i=0; i<8; i++)
				fatal("  %02x", buf[i]);
			fatal("\n");
			cpu->running = 0;
			return 0;
		}

		seg = buf[2] + (buf[3] << 8);
		ofs = buf[0] + (buf[1] << 8) + (buf[6] << 16) + (buf[7] << 24);

		switch (int_type) {
		case INT_TYPE_INTGATE:
		case INT_TYPE_TRAPGATE:
			break;
		default:
			fatal("INT type: %i, cs:eip = 0x%04x:0x%08x\n",
			    int_type, (int)seg, (int)ofs);
			cpu->running = 0;
			return 0;
		}

		reload_segment_descriptor(cpu, X86_S_CS, seg, &cpu->pc);

		/*
		 *  If we're changing privilege level, the we should change
		 *  stack here, and push the old SS:ESP.
		 */
		if ((seg & X86_PL_MASK) < (old_cs & X86_PL_MASK)) {
			unsigned char buf[16];
			uint16_t new_ss, old_ss;
			uint32_t new_esp, old_esp;
			int pl;

			pl = seg & X86_PL_MASK;

			/*  Load SSx:ESPx from the Task State Segment:  */
			if (cpu->cd.x86.tr < 4)
				fatal("WARNING: interrupt with stack switch"
				    ", but task register = 0?\n");

			/*  fatal("::: old SS:ESP=0x%04x:0x%08x\n",
			    (int)cpu->cd.x86.s[X86_S_SS],
			    (int)cpu->cd.x86.r[X86_R_SP]);  */

			if (!cpu->memory_rw(cpu, cpu->mem, 4 + pl*8 +
			    cpu->cd.x86.tr_base, buf, sizeof(buf), MEM_READ,
			    NO_SEGMENTATION)) {
				fatal("ERROR: couldn't read tss blah blah\n");
				cpu->running = 0;
				return 0;
			}

			new_esp = buf[0] + (buf[1] << 8) +
			    (buf[2] << 16) + (buf[3] << 24);
			new_ss = buf[4] + (buf[5] << 8);

			old_ss = cpu->cd.x86.s[X86_S_SS];
			old_esp = cpu->cd.x86.r[X86_R_SP];

			reload_segment_descriptor(cpu, X86_S_SS, new_ss, NULL);
			cpu->cd.x86.r[X86_R_SP] = new_esp;

			fatal("::: Switching Stack: new SS:ESP=0x%04x:0x%08x\n",
			    (int)new_ss, (int)new_esp);

			mode = cpu->cd.x86.descr_cache[X86_S_CS].
			    default_op_size;

			if (!x86_push(cpu, old_ss, mode)) {
				fatal("TODO: problem adgsadg 1\n");
				cpu->running = 0;
			}
			if (!x86_push(cpu, old_esp, mode)) {
				fatal("TODO: problem adgsadg 2\n");
				cpu->running = 0;
			}
		}

		/*  Push flags, cs, and ip (pc):  */
		mode = cpu->cd.x86.descr_cache[X86_S_CS].default_op_size;
		if (!x86_push(cpu, cpu->cd.x86.rflags, mode)) {
			fatal("TODO: how to handle this 1 asdf\n");
			cpu->running = 0;
		}
		if (!x86_push(cpu, old_cs, mode)) {
			fatal("TODO: how to handle this 2 sdghser\n");
			cpu->running = 0;
		}
		if (!x86_push(cpu, cpu->pc, mode)) {
			fatal("TODO: how to handle this 3 we\n");
			cpu->running = 0;
		}

		/*  Push error code for some exceptions:  */
		if ((nr >= 8 && nr <=14) || nr == 17) {
			if (!x86_push(cpu, errcode, mode)) {
				fatal("x86_interrupt(): TODO: asdgblah\n");
				cpu->running = 0;
			}
		}

		/*  Only turn off interrupts for Interrupt Gates:  */
		if (int_type == INT_TYPE_INTGATE)
			cpu->cd.x86.rflags &= ~X86_FLAGS_IF;

		/*  Turn off TF for Interrupt and Trap Gates:  */
		if (int_type == INT_TYPE_INTGATE ||
		    int_type == INT_TYPE_TRAPGATE)
			cpu->cd.x86.rflags &= ~X86_FLAGS_TF;

		goto int_jump;
	}

	/*
	 *  Real mode:
	 */
	if (nr * 4 > cpu->cd.x86.idtr_limit) {
		fatal("TODO: real mode int 0x%02x outside idtr limit ("
		    "%i)?\n", nr, (int)cpu->cd.x86.idtr_limit);
		cpu->running = 0;
		return 0;
	}
	/*  Read the interrupt vector:  */
	res = cpu->memory_rw(cpu, cpu->mem, cpu->cd.x86.idtr + nr*4, buf, 4,
	    MEM_READ, NO_SEGMENTATION);
	if (!res) {
		fatal("x86_interrupt(): could not read the"
		    " interrupt descriptor table\n");
		cpu->running = 0;
		return 0;
	}
	ofs = buf[0] + (buf[1] << 8);  seg = buf[2] + (buf[3] << 8);

	reload_segment_descriptor(cpu, X86_S_CS, seg, &cpu->pc);

	/*  Push old flags, old cs, and old ip (pc):  */
	mode = cpu->cd.x86.descr_cache[X86_S_CS].default_op_size;

	if (!x86_push(cpu, cpu->cd.x86.rflags, mode)) {
		fatal("x86_interrupt(): TODO: how to handle this 4\n");
		cpu->running = 0;
	}
	if (!x86_push(cpu, old_cs, mode)) {
		fatal("x86_interrupt(): TODO: how to handle this 5\n");
		cpu->running = 0;
	}
	if (!x86_push(cpu, cpu->pc, mode)) {
		fatal("x86_interrupt(): TODO: how to handle this 6\n");
		cpu->running = 0;
	}

	/*  Turn off interrupts and the Trap Flag, and jump to the interrupt
	    handler:  */
	cpu->cd.x86.rflags &= ~(X86_FLAGS_IF | X86_FLAGS_TF);

int_jump:
	cpu->pc = ofs;

	return 1;
}


#define	CALCFLAGS_OP_ADD	1
#define	CALCFLAGS_OP_SUB	2
#define	CALCFLAGS_OP_XOR	3
/*
 *  x86_calc_flags():
 */
static void x86_calc_flags(struct cpu *cpu, uint64_t a, uint64_t b, int mode,
	int op)
{
	uint64_t c=0, mask;
	int i, count;

	if (mode == 8)
		mask = 0xff;
	else if (mode == 16)
		mask = 0xffff;
	else if (mode == 32)
		mask = 0xffffffffULL;
	else if (mode == 64)
		mask = 0xffffffffffffffffULL;
	else {
		fatal("x86_calc_flags(): Bad mode (%i)\n", mode);
		return;
	}

	a &= mask;
	b &= mask;

	/*  CF:  */
	cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
	switch (op) {
	case CALCFLAGS_OP_ADD:
		if (((a + b)&mask) < a && ((a + b)&mask) < b)
			cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case CALCFLAGS_OP_SUB:
		if (a < b)
			cpu->cd.x86.rflags |= X86_FLAGS_CF;
		break;
	case CALCFLAGS_OP_XOR:
		break;
	}

	switch (op) {
	case CALCFLAGS_OP_ADD:
		c = (a + b) & mask;
		break;
	case CALCFLAGS_OP_SUB:
		c = (a - b) & mask;
		break;
	case CALCFLAGS_OP_XOR:
		c = a;
	}

	/*  ZF:  */
	cpu->cd.x86.rflags &= ~X86_FLAGS_ZF;
	if (c == 0)
		cpu->cd.x86.rflags |= X86_FLAGS_ZF;

	/*  SF:  */
	cpu->cd.x86.rflags &= ~X86_FLAGS_SF;
	if ((mode == 8 && (c & 0x80)) ||
	    (mode == 16 && (c & 0x8000)) ||
	    (mode == 32 && (c & 0x80000000ULL)) ||
	    (mode == 64 && (c & 0x8000000000000000ULL))) {
		cpu->cd.x86.rflags |= X86_FLAGS_SF;
	}

	/*  OF:  */
	cpu->cd.x86.rflags &= ~X86_FLAGS_OF;
	switch (op) {
	case CALCFLAGS_OP_ADD:
		/*  TODO  */
		break;
	case CALCFLAGS_OP_SUB:
		if (cpu->cd.x86.rflags & X86_FLAGS_SF)
			cpu->cd.x86.rflags |= X86_FLAGS_OF;
		if (mode == 8 && (int8_t)a < (int8_t)b)
			cpu->cd.x86.rflags ^= X86_FLAGS_OF;
		if (mode == 16 && (int16_t)a < (int16_t)b)
			cpu->cd.x86.rflags ^= X86_FLAGS_OF;
		if (mode == 32 && (int32_t)a < (int32_t)b)
			cpu->cd.x86.rflags ^= X86_FLAGS_OF;
		break;
	case CALCFLAGS_OP_XOR:
		;
	}

	/*  AF:  */
	switch (op) {
	case CALCFLAGS_OP_ADD:
		if ((a & 0xf) + (b & 0xf) > 15)
			cpu->cd.x86.rflags |= X86_FLAGS_AF;
		else
			cpu->cd.x86.rflags &= ~X86_FLAGS_AF;
		break;
	case CALCFLAGS_OP_SUB:
		if ((b & 0xf) > (a & 0xf))
			cpu->cd.x86.rflags |= X86_FLAGS_AF;
		else
			cpu->cd.x86.rflags &= ~X86_FLAGS_AF;
		break;
	case CALCFLAGS_OP_XOR:
		;
	}

	/*  PF:  (NOTE: Only the lowest 8 bits)  */
	cpu->cd.x86.rflags &= ~X86_FLAGS_PF;
	count = 0;
	for (i=0; i<8; i++) {
		if (c & 1)
			count ++;
		c >>= 1;
	}
	if (!(count&1))
		cpu->cd.x86.rflags |= X86_FLAGS_PF;
}


/*
 *  x86_condition():
 *
 *  Returns 0 or 1 (false or true) depending on flag bits.
 */
static int x86_condition(struct cpu *cpu, int op)
{
	int success = 0;

	switch (op & 0xe) {
	case 0x00:	/*  o  */
		success = cpu->cd.x86.rflags & X86_FLAGS_OF;
		break;
	case 0x02:	/*  c  */
		success = cpu->cd.x86.rflags & X86_FLAGS_CF;
		break;
	case 0x04:	/*  z  */
		success = cpu->cd.x86.rflags & X86_FLAGS_ZF;
		break;
	case 0x06:	/*  be  */
		success = (cpu->cd.x86.rflags & X86_FLAGS_ZF) ||
		    (cpu->cd.x86.rflags & X86_FLAGS_CF);
		break;
	case 0x08:	/*  s  */
		success = cpu->cd.x86.rflags & X86_FLAGS_SF;
		break;
	case 0x0a:	/*  p  */
		success = cpu->cd.x86.rflags & X86_FLAGS_PF;
		break;
	case 0x0c:	/*  nge  */
		success = (cpu->cd.x86.rflags & X86_FLAGS_SF? 1 : 0)
		    != (cpu->cd.x86.rflags & X86_FLAGS_OF? 1 : 0);
		break;
	case 0x0e:	/*  ng  */
		success = (cpu->cd.x86.rflags & X86_FLAGS_SF? 1 : 0)
		    != (cpu->cd.x86.rflags & X86_FLAGS_OF? 1 : 0);
		success |= (cpu->cd.x86.rflags & X86_FLAGS_ZF ? 1 : 0);
		break;
	}

	if (op & 1)
		success = !success;

	return success? 1 : 0;
}


/*
 *  x86_shiftrotate():
 */
static void x86_shiftrotate(struct cpu *cpu, uint64_t *op1p, int op,
	int n, int mode)
{
	uint64_t op1 = *op1p;
	int cf = -1, oldcf = 0;

	n &= 31;
	if (mode != 64)
		op1 &= (((uint64_t)1 << mode) - 1);

	oldcf = cpu->cd.x86.rflags & X86_FLAGS_CF? 1 : 0;

	while (n-- > 0) {
		cf = 0;

		if (op & 1) {	/*  right  */
			if (op1 & 1)
				cf = 1;
		} else {	/*  left  */
			cf = (op1 & ((uint64_t)1 << (mode-1)))? 1 : 0;
		}

		switch (op) {
		case 0:	/*  rol  */
			op1 = (op1 << 1) | cf;
			break;
		case 1:	/*  ror  */
			op1 >>= 1;
			op1 |= ((uint64_t)cf << (mode - 1));
			break;
		case 2:	/*  rcl  */
			op1 = (op1 << 1) | oldcf;
			oldcf = cf;
			break;
		case 3:	/*  rcr  */
			op1 >>= 1;
			op1 |= ((uint64_t)oldcf << (mode - 1));
			oldcf = cf;
			break;
		case 4:	/*  shl  */
		case 6:	/*  sal  */
			op1 <<= 1;
			break;
		case 5:	/*  shr  */
			op1 >>= 1;
			break;
		case 7:	/*  sar  */
			op1 >>= 1;
			if (mode == 8 && op1 & 0x40)
				op1 |= 0x80;
			if (mode == 16 && op1 & 0x4000)
				op1 |= 0x8000;
			if (mode == 32 && op1 & 0x40000000ULL)
				op1 |= 0x80000000ULL;
			break;
		default:
			fatal("x86_shiftrotate(): unimplemented op %i\n", op);
			cpu->running = 0;
		}
		if (mode != 64)
			op1 &= (((uint64_t)1 << mode) - 1);
		x86_calc_flags(cpu, op1, 0, mode, CALCFLAGS_OP_XOR);
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		if (cf)
			cpu->cd.x86.rflags |= X86_FLAGS_CF;
	}

	/*  TODO: OF flag  */

	*op1p = op1;
}


/*
 *  x86_msr():
 *
 *  This function reads or writes the MSRs (Model Specific Registers).
 */
static void x86_msr(struct cpu *cpu, int writeflag)
{
	uint32_t regnr = cpu->cd.x86.r[X86_R_CX] & 0xffffffff;
	uint64_t odata=0, idata = (cpu->cd.x86.r[X86_R_AX] & 0xffffffff) +
	    ((cpu->cd.x86.r[X86_R_DX] & 0xffffffff) << 32);

	switch (regnr) {
	case 0xc0000080:	/*  AMD64 EFER  */
		if (writeflag) {
			if (cpu->cd.x86.efer & X86_EFER_LME &&
			    !(idata & X86_EFER_LME))
				debug("[ switching FROM 64-bit mode ]\n");
			if (!(cpu->cd.x86.efer & X86_EFER_LME) &&
			    idata & X86_EFER_LME)
				debug("[ switching to 64-bit mode ]\n");
			cpu->cd.x86.efer = idata;
		} else
			odata = cpu->cd.x86.efer;
		break;
	default:fatal("x86_msr: unimplemented MSR 0x%08x\n", (int)regnr);
		cpu->running = 0;
	}

	if (!writeflag) {
		cpu->cd.x86.r[X86_R_AX] = odata & 0xffffffff;
		cpu->cd.x86.r[X86_R_DX] = (odata >> 32) & 0xffffffff;
	}
}


/*
 *  cause_interrupt():
 *
 *  Read the registers of PIC1 (and possibly PIC2) to find out which interrupt
 *  has occured.
 *
 *  Returns 1 if an interrupt happened, 0 otherwise (for example if the
 *  in-service bit of an interrupt was already set).
 */
static int cause_interrupt(struct cpu *cpu)
{
	int i, irq_nr = -1;

	for (i=0; i<8; i++) {
		if (cpu->machine->md.pc.pic1->irr &
		    (~cpu->machine->md.pc.pic1->ier) & (1 << i))
			irq_nr = i;
	}

	if (irq_nr == 2) {
		for (i=0; i<8; i++) {
			if (cpu->machine->md.pc.pic2->irr &
			    (~cpu->machine->md.pc.pic2->ier) & (1 << i))
				irq_nr = 8+i;
		}
	}

	if (irq_nr == 2) {
		fatal("cause_interrupt(): Huh? irq 2 but no secondary irq\n");
		cpu->running = 0;
	}

	/*
	 *  TODO: How about multiple interrupt levels?
	 */

#if 0
printf("cause1: %i (irr1=%02x ier1=%02x, irr2=%02x ier2=%02x\n", irq_nr,
cpu->machine->md.pc.pic1->irr, cpu->machine->md.pc.pic1->ier,
cpu->machine->md.pc.pic2->irr, cpu->machine->md.pc.pic2->ier);
#endif

	/*  Set the in-service bit, and calculate actual INT nr:  */
	if (irq_nr < 8) {
		if (cpu->machine->md.pc.pic1->isr & (1 << irq_nr))
			return 0;
		cpu->machine->md.pc.pic1->isr |= (1 << irq_nr);
		irq_nr = cpu->machine->md.pc.pic1->irq_base + irq_nr;
	} else {
		if (cpu->machine->md.pc.pic2->isr & (1 << (irq_nr & 7)))
			return 0;
		cpu->machine->md.pc.pic2->isr |= (1 << (irq_nr&7));
		irq_nr = cpu->machine->md.pc.pic2->irq_base + (irq_nr & 7);
	}

/*  printf("cause2: %i\n", irq_nr);  */

	x86_interrupt(cpu, irq_nr, 0);
	cpu->cd.x86.halted = 0;
	return 1;
}


/*
 *  x86_cpu_run_instr():
 *
 *  Execute one instruction on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,   
 *  0 if no instruction was executed.
 */
int x86_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	int i, r, rep = 0, op, len, mode, omode, mode67;
	int nprefixbytes = 0, success, longmode;
	uint32_t imm, imm2;
	unsigned char buf[16];
	unsigned char *instr = buf, *instr_orig, *really_orig_instr;
	uint64_t newpc = cpu->pc;
	uint64_t tmp, op1, op2;
	int trap_flag_was_set = cpu->cd.x86.rflags & X86_FLAGS_TF;

	/*  Check PC against breakpoints:  */
	if (!single_step)
		for (i=0; i<cpu->machine->n_breakpoints; i++)
			if (cpu->pc == cpu->machine->breakpoint_addr[i]) {
				fatal("Breakpoint reached, 0x%04x:0x%llx\n",
				    cpu->cd.x86.s[X86_S_CS],
				   (long long)cpu->pc);
				single_step = 1;
				return 0;
			}

	if (!cpu->cd.x86.descr_cache[X86_S_CS].valid) {
		fatal("x86_cpu_run_instr(): Invalid CS descriptor?\n");
		cpu->running = 0;
		return 0;
	}

	longmode = cpu->cd.x86.efer & X86_EFER_LME;
	mode = cpu->cd.x86.descr_cache[X86_S_CS].default_op_size;
	omode = mode;
	if (mode != 16 && mode != 32) {
		fatal("x86_cpu_run_instr(): Invalid CS default op size, %i\n",
		    mode);
		cpu->running = 0;
		return 0;
	}

	if (cpu->cd.x86.interrupt_asserted &&
	    cpu->cd.x86.rflags & X86_FLAGS_IF) {
		if (cause_interrupt(cpu))
			return 0;
	}

	/*  16-bit BIOS emulation:  */
	if (mode == 16 && ((newpc + (cpu->cd.x86.s[X86_S_CS] << 4)) & 0xff000)
	    == 0xf8000 && cpu->machine->prom_emulation) {
		int addr = (newpc + (cpu->cd.x86.s[X86_S_CS] << 4)) & 0xfff;
		if (cpu->machine->instruction_trace)
			debug("(PC BIOS emulation, int 0x%02x)\n",
			    addr >> 4);
		pc_bios_emul(cpu);
		/*  Approximately equivalent to 500 instructions.  */
		return 500;
	}

	if (cpu->cd.x86.halted) {
		if (!(cpu->cd.x86.rflags & X86_FLAGS_IF)) {
			fatal("[ Halting with interrupts disabled. ]\n");
			cpu->running = 0;
		}
		/*  Treating this as more than one instruction makes us
		    wait less for devices.  */
		return 1000;
	}

	/*  Read an instruction from memory:  */
	cpu->cd.x86.cursegment = X86_S_CS;
	cpu->cd.x86.seg_override = 0;

	r = cpu->memory_rw(cpu, cpu->mem, cpu->pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION);
	if (!r) {
		/*  This could happen if, for example, there was an
		    exception while we tried to read the instruction.  */
		return 0;
	}

	really_orig_instr = instr;	/*  Used to display an error message
					    for unimplemented instructions.  */

	if (cpu->machine->instruction_trace)
		x86_cpu_disassemble_instr(cpu, instr, 1 | omode, 0, 0);

	/*  For debugging:  */
	if (instr[0] == 0 && instr[1] == 0 && instr[2] == 0 && instr[3] == 0) {
		fatal("WARNING: Running in nothingness?\n");
		cpu->running = 0;
		return 0;
	}

	/*  All instructions are at least one byte long :-)  */
	newpc ++;

	/*  Default is to use the data segment, or the stack segment:  */
	cpu->cd.x86.cursegment = X86_S_DS;
	mode67 = mode;

	/*  Any prefix?  */
	for (;;) {
		if (longmode && (instr[0] & 0xf0) == 0x40) {
			fatal("TODO: REX byte 0x%02x\n", instr[0]);
			cpu->running = 0;
		} else if (instr[0] == 0x66) {
			if (mode == 16)
				mode = 32;
			else
				mode = 16;
		} else if (instr[0] == 0x67) {
			if (mode67 == 16)
				mode67 = 32;
			else
				mode67 = 16;
		} else if (instr[0] == 0x26) {
			cpu->cd.x86.cursegment = X86_S_ES;
			cpu->cd.x86.seg_override = 1;
		} else if (instr[0] == 0x2e) {
			cpu->cd.x86.cursegment = X86_S_CS;
			cpu->cd.x86.seg_override = 1;
		} else if (instr[0] == 0x36) {
			cpu->cd.x86.cursegment = X86_S_SS;
			cpu->cd.x86.seg_override = 1;
		} else if (instr[0] == 0x3e) {
			cpu->cd.x86.cursegment = X86_S_DS;
			cpu->cd.x86.seg_override = 1;
		} else if (instr[0] == 0x64) {
			cpu->cd.x86.cursegment = X86_S_FS;
			cpu->cd.x86.seg_override = 1;
		} else if (instr[0] == 0x65) {
			cpu->cd.x86.cursegment = X86_S_GS;
			cpu->cd.x86.seg_override = 1;
		} else if (instr[0] == 0xf0) {
			/*  lock  */
		} else if (instr[0] == 0xf2) {
			rep = REP_REPNE;
		} else if (instr[0] == 0xf3) {
			rep = REP_REP;
		} else
			break;
		instr ++;
		newpc ++;
		if (++nprefixbytes > 4) {
			fatal("x86: too many prefix bytes at ");
			print_csip(cpu); fatal("\n");
			cpu->running = 0;
			return 0;
		}
	}

	op = instr[0];
	instr ++;

	if ((op & 0xf0) <= 0x30 && (op & 7) <= 5) {
		success = 1;
		instr_orig = instr;
		switch (op & 7) {
		case 4:	imm = read_imm(&instr, &newpc, 8);
			op1 = cpu->cd.x86.r[X86_R_AX] & 0xff;
			op2 = (signed char)imm;
			mode = 8;
			break;
		case 5:	imm = read_imm(&instr, &newpc, mode);
			op1 = cpu->cd.x86.r[X86_R_AX]; op2 = imm;
			break;
		default:success = modrm(cpu, MODRM_READ, mode, mode67,
			    op&1? 0 : MODRM_EIGHTBIT, &instr, &newpc,&op1,&op2);
			if (!success)
				return 0;
		}

		if ((op & 6) == 2) {
			uint64_t tmp = op1; op1 = op2; op2 = tmp;
		}

		/*  printf("op1=0x%x op2=0x%x => ", (int)op1, (int)op2);  */

		switch (mode) {
		case 16: op1 &= 0xffff; op2 &= 0xffff; break;
		case 32: op1 &= 0xffffffffULL; op2 &= 0xffffffffULL; break;
		}

		switch (op & 0x38) {
		case 0x00:	x86_calc_flags(cpu, op1, op2, !(op & 1)? 8 :
				    mode, CALCFLAGS_OP_ADD);
				op1 = op1 + op2;
				break;
		case 0x08:	op1 = op1 | op2; break;
		case 0x10:	tmp = op2;
				if (cpu->cd.x86.rflags & X86_FLAGS_CF)
					tmp ++;
				x86_calc_flags(cpu, op1, tmp, !(op & 1)? 8 : 
				    mode, CALCFLAGS_OP_ADD);
				op1 = op1 + tmp;
				break;
		case 0x18:	tmp = op2;
				if (cpu->cd.x86.rflags & X86_FLAGS_CF)
					tmp ++;
				x86_calc_flags(cpu, op1, tmp, !(op & 1)? 8 :
				    mode, CALCFLAGS_OP_SUB);
				op1 = op1 - tmp;
				break;
		case 0x20:	op1 = op1 & op2; break;
		case 0x28:	x86_calc_flags(cpu, op1, op2, !(op & 1)? 8 :
				    mode, CALCFLAGS_OP_SUB);
				op1 = op1 - op2; break;
		case 0x30:	op1 = op1 ^ op2; break;
		case 0x38:	x86_calc_flags(cpu, op1, op2, !(op & 1)? 8 :
				    mode, CALCFLAGS_OP_SUB);
				break;
		default:
			fatal("not yet\n");
			exit(1);
		}

		switch (mode) {
		case 16: op1 &= 0xffff; op2 &= 0xffff; break;
		case 32: op1 &= 0xffffffffULL; op2 &= 0xffffffffULL; break;
		}

		/*  NOTE: Manual cmp for "sbb, "sub" and "cmp" instructions.  */
		if ((op & 0x38) != 0x38 && (op & 0x38) != 0x28 &&
		    (op & 0x38) != 0x18 && (op & 0x38) != 0x00 &&
		    (op & 0x38) != 0x10)
			x86_calc_flags(cpu, op1, 0, !(op & 1)? 8 : mode,
			    CALCFLAGS_OP_XOR);

		/*  "and","or","xor" always clears CF and OF:  */
		if ((op & 0x38) == 0x08 || (op & 0x38) == 0x20 ||
		    (op & 0x38) == 0x30) {
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
			cpu->cd.x86.rflags &= ~X86_FLAGS_OF;
		}

		/*  printf("op1=0x%x op2=0x%x\n", (int)op1, (int)op2);  */

		if ((op & 6) == 2) {
			uint64_t tmp = op1; op1 = op2; op2 = tmp;
		}

		/*  Write back the result: (for all cases except CMP)  */
		if ((op & 0x38) != 0x38) {
			switch (op & 7) {
			case 4:	cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[
				    X86_R_AX] & ~0xff) | (op1 & 0xff);
				break;
			case 5:	cpu->cd.x86.r[X86_R_AX] = modify(cpu->
				    cd.x86.r[X86_R_AX], op1);
				break;
			default:success = modrm(cpu, (op & 6) == 2?
				    MODRM_WRITE_R : MODRM_WRITE_RM, mode,
				    mode67, op&1? 0 : MODRM_EIGHTBIT,
				    &instr_orig, NULL, &op1, &op2);
				if (!success)
					return 0;
			}
		}
	} else if ((op & 0xf0) < 0x20 && (op & 7) == 6) {
		success = x86_push(cpu, cpu->cd.x86.s[op / 8], mode);
		if (!success)
			return 0;
	} else if (op == 0x0f && cpu->cd.x86.model.model_number ==
	    X86_MODEL_8086) {
		uint64_t tmp;
		fatal("WARNING: pop cs\n");
		if (!x86_pop(cpu, &tmp, mode))
			return 0;
		reload_segment_descriptor(cpu, X86_S_CS, tmp, &newpc);
	} else if (op == 0x0f) {
		uint64_t tmp;
		unsigned char *instr_orig_2;
		int signflag, i;
		imm = read_imm(&instr, &newpc, 8);
		if (imm >= 0x40 && imm <= 0x4f) {	/*  CMOVxx  */
			op = imm & 0xf;
			if (!modrm(cpu, MODRM_READ, mode, mode67,
			    0, &instr, &newpc, &op1, &op2))
				return 0;
			success = x86_condition(cpu, op);
			if (success) {
				if (!modrm(cpu, MODRM_WRITE_R, mode, mode67,
				    MODRM_EIGHTBIT, &instr_orig, NULL,
				    &op2, &op1))
					return 0;
			}
		} else if (imm >= 0x80 && imm <= 0x8f) {
			/*  conditional near jump  */
			op = imm & 0xf;
			imm = read_imm(&instr, &newpc, mode);
			success = x86_condition(cpu, op);
			if (success)
				newpc += imm;
		} else if (imm >= 0x90 && imm <= 0x9f) {
			instr_orig = instr;
			if (!modrm(cpu, MODRM_READ, mode, mode67,
			    MODRM_EIGHTBIT, &instr, &newpc, &op1, &op2))
				return 0;
			op1 = x86_condition(cpu, imm & 0xf);
			if (!modrm(cpu, MODRM_WRITE_RM, mode, mode67,
			    MODRM_EIGHTBIT, &instr_orig, NULL, &op1, &op2))
				return 0;
		} else {
			int subop;
			switch (imm) {
			case 0x00:
				subop = (*instr >> 3) & 0x7;
				switch (subop) {
				case 1:	/*  str  */
					/*  TODO: Check Prot.mode?  */
					op1 = cpu->cd.x86.tr;
					if (!modrm(cpu, MODRM_WRITE_RM, 16,
					    mode67, 0, &instr, &newpc, &op1,
					    &op2))
						return 0;
					break;
				case 2:	/*  lldt  */
					/*  TODO: Check cpl? and Prot.mode  */
					if (!modrm(cpu, MODRM_READ, 16, mode67,
					    0, &instr, &newpc, &op1, &op2))
						return 0;
					reload_segment_descriptor(cpu,
					    RELOAD_LDTR, op1, &newpc);
					break;
				case 3:	/*  ltr  */
					/*  TODO: Check cpl=0 and Prot.mode  */
					if (!modrm(cpu, MODRM_READ, 16, mode67,
					    0, &instr, &newpc, &op1, &op2))
						return 0;
					reload_segment_descriptor(cpu,
					    RELOAD_TR, op1, &newpc);
					break;
				default:fatal("UNIMPLEMENTED 0x%02x,0x%02x"
					    ",0x%02x\n", op, imm, *instr);
					quiet_mode = 0;
					x86_cpu_disassemble_instr(cpu,
					    really_orig_instr, 1 | omode, 0, 0);
					cpu->running = 0;
				}
				break;
			case 0x01:
				subop = (*instr >> 3) & 0x7;
				switch (subop) {
				case 0:	/*  sgdt  */
				case 1:	/*  sidt  */
				case 2:	/*  lgdt  */
				case 3:	/*  lidt  */
					instr_orig = instr;
					if (!modrm(cpu, MODRM_READ, mode,
					    mode67, MODRM_JUST_GET_ADDR, &instr,
					    &newpc, &op1, &op2))
						return 0;
					/*  TODO/NOTE: how about errors?  */
					if (subop >= 2) {
						x86_load(cpu, op1, &tmp, 2);
						x86_load(cpu, op1 + 2, &op2, 4);
						if (mode == 16)
							op2 &= 0x00ffffffULL;
					}
					switch (subop) {
					case 0:	tmp = cpu->cd.x86.gdtr_limit;
						op2 = cpu->cd.x86.gdtr;
						break;
					case 1:	tmp = cpu->cd.x86.idtr_limit;
						op2 = cpu->cd.x86.idtr;
						break;
					case 2:	cpu->cd.x86.gdtr_limit =
						    tmp & 0xffff;
						cpu->cd.x86.gdtr = op2;
						break;
					case 3:	cpu->cd.x86.idtr_limit =
						    tmp & 0xffff;
						cpu->cd.x86.idtr = op2;
						break;
					}
					if (subop < 2) {
						if (mode == 16)
							op2 &= 0x00ffffffULL;
						x86_store(cpu, op1, tmp, 2);
						x86_store(cpu, op1+2, op2, 4);
					}
					break;
				case 4:	/*  smsw  */
				case 6:	/*  lmsw  */
					instr_orig = instr;
					if (!modrm(cpu, MODRM_READ, 16, mode67,
					    0, &instr, &newpc, &op1, &op2))
						return 0;
					if (((*instr_orig >> 3) & 0x7) == 4) {
						op1 = cpu->cd.x86.cr[0] &0xffff;
						if (!modrm(cpu, MODRM_WRITE_RM,
						    16, mode67, 0, &instr_orig,
						    NULL, &op1, &op2))
							return 0;
					} else {
						/*  lmsw cannot be used to
						    clear bit 0:  */
						op1 |= (cpu->cd.x86.cr[0] &
						    X86_CR0_PE);
						x86_write_cr(cpu, 0,
						    (cpu->cd.x86.cr[0] & ~0xf)
						    | (op1 & 0xf));
					}
					break;
				case 7:	/*  invlpg  */
					modrm(cpu, MODRM_READ, mode,
					    mode67, MODRM_JUST_GET_ADDR, &instr,
					    &newpc, &op1, &op2);
					/*  TODO  */
					break;
				default:fatal("UNIMPLEMENTED 0x%02x,0x%02x"
					    ",0x%02x\n", op, imm, *instr);
					quiet_mode = 0;
					x86_cpu_disassemble_instr(cpu,
					    really_orig_instr, 1 | omode, 0, 0);
					cpu->running = 0;
				}
				break;
			case 0x06:	/*  CLTS  */
				cpu->cd.x86.cr[0] &= ~X86_CR0_TS;
				break;
			case 0x08:	/*  INVD  */
				/*  TODO  */
				break;
			case 0x09:	/*  WBINVD  */
				/*  TODO  */
				break;
			case 0x0b:	/*  Reserved  */
				x86_interrupt(cpu, 6, 0);
				return 1;
			case 0x20:	/*  MOV r/m,CRx  */
			case 0x21:	/*  MOV r/m,DRx: TODO: is this right? */
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, 32, mode67,
				    imm==0x20? MODRM_CR : MODRM_DR, &instr,
				    &newpc, &op1, &op2))
					return 0;
				op1 = op2;
				if (!modrm(cpu, MODRM_WRITE_RM, 32, mode67,
				    imm==0x20? MODRM_CR : MODRM_DR, &instr_orig,
				    NULL, &op1, &op2))
					return 0;
				break;
			case 0x22:	/*  MOV CRx,r/m  */
			case 0x23:	/*  MOV DRx,r/m  */
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, 32, mode67,
				    imm==0x22? MODRM_CR : MODRM_DR, &instr,
				    &newpc, &op1, &op2))
					return 0;
				op2 = op1;
				if (!modrm(cpu, MODRM_WRITE_R, 32, mode67,
				    imm==0x22? MODRM_CR : MODRM_DR, &instr_orig,
				    NULL, &op1, &op2))
					return 0;
				break;
			case 0x30:	/*  WRMSR  */
			case 0x32:	/*  RDMSR  */
				x86_msr(cpu, imm==0x30? 1 : 0);
				break;
			case 0x31:	/*  RDTSC  */
				if (cpu->cd.x86.model.model_number <
				    X86_MODEL_PENTIUM)
					fatal("WARNING: rdtsc usually requires"
					    " a Pentium. continuing anyway\n");
				if (cpu->cd.x86.cr[4] & X86_CR4_TSD)
					fatal("WARNING: time stamp disable:"
					    " TODO\n");
				cpu->cd.x86.r[X86_R_DX] = cpu->cd.x86.tsc >> 32;
				cpu->cd.x86.r[X86_R_AX] = cpu->cd.x86.tsc
				    & 0xffffffff;
				/*  TODO: make this better  */
				cpu->cd.x86.tsc += 1000;
				break;
			case 0xa0:
				if (!x86_push(cpu, cpu->cd.x86.s[X86_S_FS],
				    mode))
					return 0;
				break;
			case 0xa1:
				if (!x86_pop(cpu, &tmp, mode))
					return 0;
				reload_segment_descriptor(cpu, X86_S_FS,
				    tmp, &newpc);
				break;
			case 0xa2:
				if (!(cpu->cd.x86.rflags & X86_FLAGS_ID))
					fatal("TODO: ID bit off in flags,"
					    " but CPUID attempted?\n");
				x86_cpuid(cpu);
				break;
			case 0xa4:
			case 0xa5:
			case 0xac:
			case 0xad:
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &newpc, &op1, &op2))
					return 0;
				if (imm & 1)
					imm2 = cpu->cd.x86.r[X86_R_CX];
				else
					imm2 = read_imm(&instr, &newpc, 8);
				imm2 &= 31;
				if (imm <= 0xa5) {	/*  SHLD  */
					if (mode == 16) {
						op1 <<= 16;
						op1 |= (op2 & 0xffff);
					} else {
						op1 <<= 32;
						op1 |= (op2 & 0xffffffff);
					}
					x86_shiftrotate(cpu, &op1, 4, imm2,
					    mode == 64? 64 : (mode * 2));
					op1 >>= (mode==16? 16 : 32);
				} else {		/*  SHRD  */
					if (mode == 16) {
						op2 <<= 16;
						op1 = (op1 & 0xffff) | op2;
					} else {
						op2 <<= 32;
						op1 = (op1 & 0xffffffff) | op2;
					}
					x86_shiftrotate(cpu, &op1, 5, imm2,
					    mode == 64? 64 : (mode * 2));
					op1 &= (mode==16? 0xffff : 0xffffffff);
				}
				if (!modrm(cpu, MODRM_WRITE_RM, mode, mode67,
				    0, &instr_orig, NULL, &op1, &op2))
					return 0;
				break;
			case 0xa8:
				if (!x86_push(cpu, cpu->cd.x86.s[X86_S_GS],
				    mode))
					return 0;
				break;
			case 0xa9:
				if (!x86_pop(cpu, &tmp, mode))
					return 0;
				reload_segment_descriptor(cpu, X86_S_GS,
				    tmp, &newpc);
				break;
			case 0xa3:		/*  BT  */
			case 0xab:		/*  BTS  */
			case 0xb3:		/*  BTR  */
			case 0xbb:		/*  BTC  */
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &newpc, &op1, &op2))
					return 0;
				imm2 = op2 & 31;
				if (mode == 16)
					imm2 &= 15;
				cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
				if (op1 & ((uint64_t)1 << imm2))
					cpu->cd.x86.rflags |=
					    X86_FLAGS_CF;
				switch (imm) {
				case 0xab:
					op1 |= ((uint64_t)1 << imm2);
					break;
				case 0xb3:
					op1 &= ~((uint64_t)1 << imm2);
					break;
				case 0xbb:
					op1 ^= ((uint64_t)1 << imm2);
					break;
				}
				if (imm != 0xa3) {
					if (!modrm(cpu, MODRM_WRITE_RM, mode,
					    mode67, 0, &instr_orig, NULL,
					    &op1, &op2))
						return 0;
				}
				break;
			case 0xaf:	/*  imul r16/32, rm16/32  */
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &newpc, &op1, &op2))
					return 0;
				cpu->cd.x86.rflags &= X86_FLAGS_CF;
				cpu->cd.x86.rflags &= X86_FLAGS_OF;
				if (mode == 16) {
					op2 = (int16_t)op1 * (int16_t)op2;
					if (op2 >= 0x10000)
						cpu->cd.x86.rflags |=
						    X86_FLAGS_CF | X86_FLAGS_OF;
				} else {
					op2 = (int32_t)op1 * (int32_t)op2;
					if (op2 >= 0x100000000ULL)
						cpu->cd.x86.rflags |=
						    X86_FLAGS_CF | X86_FLAGS_OF;
				}
				if (!modrm(cpu, MODRM_WRITE_R, mode, mode67,
				    0, &instr_orig, NULL, &op1, &op2))
					return 0;
				break;
			case 0xb0:
			case 0xb1:	/*  CMPXCHG  */
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, mode, mode67,
				    imm == 0xb0? MODRM_EIGHTBIT : 0,
				    &instr, &newpc, &op1, &op2))
					return 0;
				x86_calc_flags(cpu, op1, cpu->cd.x86.r[
				    X86_R_AX], imm == 0xb0? 8 : mode,
				    CALCFLAGS_OP_SUB);
				if (cpu->cd.x86.rflags & X86_FLAGS_ZF) {
					if (!modrm(cpu, MODRM_WRITE_RM, mode,
					    mode67, imm == 0xb0?
					    MODRM_EIGHTBIT : 0,
					    &instr_orig, NULL, &op2, &op1))
						return 0;
				} else {
					if (imm == 0xb0)
						cpu->cd.x86.r[X86_R_AX] =
						    (cpu->cd.x86.r[X86_R_AX] &
						    ~0xff) | (op1 & 0xff);
					else if (mode == 16)
						cpu->cd.x86.r[X86_R_AX] =
						    (cpu->cd.x86.r[X86_R_AX] &
						    ~0xffff) | (op1 & 0xffff);
					else	/*  32 bit  */
						cpu->cd.x86.r[X86_R_AX] = op1;
				}
				break;
			case 0xb2:	/*  LSS  */
			case 0xb4:	/*  LFS  */
			case 0xb5:	/*  LGS  */
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, mode, mode67,
				    MODRM_JUST_GET_ADDR, &instr, &newpc,
				    &op1, &op2))
					return 0;
				/*  op1 is the address to load from  */
				if (!x86_load(cpu, op1, &tmp, mode/8))
					return 0;
				op2 = tmp;
				if (!x86_load(cpu, op1 + mode/8, &tmp, 2))
					return 0;
				reload_segment_descriptor(cpu, imm==0xb2?
				    X86_S_SS:(imm==0xb4?X86_S_FS:X86_S_GS),
				    tmp, &newpc);
				if (!modrm(cpu, MODRM_WRITE_R, mode, mode67,
				    0, &instr_orig, NULL, &op1, &op2))
					return 0;
				break;
			case 0xb6:
			case 0xb7:	/*  movzx  */
			case 0xbe:
			case 0xbf:	/*  movsx  */
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, mode, mode67,
				    (imm&1)==0? (MODRM_EIGHTBIT |
				    MODRM_R_NONEIGHTBIT) : MODRM_RM_16BIT,
				    &instr, &newpc, &op1, &op2))
					return 0;
				signflag = 0;
				if (imm >= 0xbe)
					signflag = 1;
				op2 = op1;
				if (imm & 1) {		/*  r32 = r16  */
					op2 &= 0xffff;
					if (signflag && op2 & 0x8000)
						op2 |= 0xffff0000ULL;
				} else {		/*  r(mode) = r8  */
					op2 &= 0xff;
					if (signflag && op2 & 0x80)
						op2 |= 0xffffff00ULL;
				}
				if (!modrm(cpu, MODRM_WRITE_R, mode, mode67,
				    (imm&1)==0? (MODRM_EIGHTBIT |
				    MODRM_R_NONEIGHTBIT) : MODRM_RM_16BIT,
				    &instr_orig, NULL, &op1, &op2))
					return 0;
				break;
			case 0xba:
				subop = (*instr >> 3) & 0x7;
				switch (subop) {
				case 4:	/*  BT  */
				case 5:	/*  BTS  */
				case 6:	/*  BTR  */
				case 7:	/*  BTC  */
					instr_orig = instr;
					if (!modrm(cpu, MODRM_READ, mode,
					    mode67, 0, &instr, &newpc, &op1,
					    &op2))
						return 0;
					imm = read_imm(&instr, &newpc, 8);
					imm &= 31;
					if (mode == 16)
						imm &= 15;
					cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
					if (op1 & ((uint64_t)1 << imm))
						cpu->cd.x86.rflags |=
						    X86_FLAGS_CF;
					switch (subop) {
					case 5:	op1 |= ((uint64_t)1 << imm);
						break;
					case 6:	op1 &= ~((uint64_t)1 << imm);
						break;
					case 7:	op1 ^= ((uint64_t)1 << imm);
						break;
					}
					if (subop != 4) {
						if (!modrm(cpu, MODRM_WRITE_RM,
						    mode, mode67, 0,
						    &instr_orig, NULL,
						    &op1, &op2))
							return 0;
					}
					break;
				default:fatal("UNIMPLEMENTED 0x%02x,0x%02x"
					    ",0x%02x\n", op, imm, *instr);
					quiet_mode = 0;
					x86_cpu_disassemble_instr(cpu,
					    really_orig_instr, 1|omode, 0, 0);
					cpu->running = 0;
				}
				break;
			case 0xbc:	/*  bsf  */
			case 0xbd:	/*  bsr  */
				instr_orig = instr;
				if (!modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &newpc, &op1, &op2))
					return 0;
				cpu->cd.x86.rflags &= ~X86_FLAGS_ZF;
				if (op1 == 0)
					cpu->cd.x86.rflags |= X86_FLAGS_ZF;
				i = mode - 1;
				if (imm == 0xbc)
					i = 0;
				for (;;) {
					if (op1 & ((uint64_t)1<<i)) {
						op2 = i;
						break;
					}
					if (imm == 0xbc) {
						if (++i >= mode)
							break;
					} else {
						if (--i < 0)
							break;
					}
				}
				if (!modrm(cpu, MODRM_WRITE_R, mode, mode67,
				    0, &instr_orig, NULL, &op1, &op2))
					return 0;
				break;
			case 0xc0:	/*  xadd  */
			case 0xc1:
				instr_orig = instr_orig_2 = instr;
				if (!modrm(cpu, MODRM_READ, mode, mode67,
				    imm == 0xc0? MODRM_EIGHTBIT : 0,
				    &instr, &newpc, &op1, &op2))
					return 0;
				tmp = op1; op1 = op2; op2 = tmp;
				x86_calc_flags(cpu, op1, op2, imm==0xc0?
				    8 : mode, CALCFLAGS_OP_ADD);
				op1 += op2;
				if (!modrm(cpu, MODRM_WRITE_RM, mode, mode67,
				    imm == 0xc0? MODRM_EIGHTBIT : 0,
				    &instr_orig, NULL, &op1, &op2))
					return 0;
				if (!modrm(cpu, MODRM_WRITE_R, mode, mode67,
				    imm == 0xc0? MODRM_EIGHTBIT : 0,
				    &instr_orig_2, NULL, &op1, &op2))
					return 0;
				break;
			case 0xc7:
				subop = (*instr >> 3) & 0x7;
				switch (subop) {
				case 1:	/*  CMPXCHG8B  */
					if (!modrm(cpu, MODRM_READ, mode,
					    mode67, MODRM_JUST_GET_ADDR, &instr,
					    &newpc, &op1, &op2))
						return 0;
					if (!x86_load(cpu, op1, &tmp, 8))
						return 0;
					cpu->cd.x86.rflags &= ~X86_FLAGS_ZF;
					if ((tmp >> 32) == (0xffffffffULL &
					    cpu->cd.x86.r[X86_R_DX]) && (tmp
					    & 0xffffffffULL) == (0xffffffffULL &
					    cpu->cd.x86.r[X86_R_AX])) {
						cpu->cd.x86.rflags |=
						    X86_FLAGS_ZF;
						tmp = ((cpu->cd.x86.r[X86_R_CX]
						    & 0xffffffffULL) << 32) |
						    (cpu->cd.x86.r[X86_R_BX] &
						    0xffffffffULL);
						if (!x86_store(cpu, op1, tmp,8))
							return 0;
					} else {
						cpu->cd.x86.r[X86_R_DX] =
						    tmp >> 32;
						cpu->cd.x86.r[X86_R_AX] =
						    tmp & 0xffffffffULL;
					}
					break;
				default:fatal("UNIMPLEMENTED 0x%02x,0x%02x"
					    ",0x%02x\n", op, imm, *instr);
					quiet_mode = 0;
					x86_cpu_disassemble_instr(cpu,
					    really_orig_instr, 1|omode, 0, 0);
					cpu->running = 0;
				}
				break;
			default:fatal("TODO: 0x0f,0x%02x\n", imm);
				quiet_mode = 0;
				x86_cpu_disassemble_instr(cpu,
				    really_orig_instr, 1|omode, 0, 0);
				cpu->running = 0;
			}
		}
	} else if ((op & 0xf0) < 0x20 && (op & 7) == 7) {
		uint64_t tmp;
		success = x86_pop(cpu, &tmp, mode);
		if (!success)
			return 0;
		reload_segment_descriptor(cpu, op/8, tmp, &newpc);
	} else if (op == 0x27) {			/*  DAA  */
		int a = (cpu->cd.x86.r[X86_R_AX] >> 4) & 0xf;
		int b = cpu->cd.x86.r[X86_R_AX] & 0xf;
		if (b > 9) {
			b -= 10;
			a ++;
		} else if (cpu->cd.x86.rflags & X86_FLAGS_AF)
			b += 6;
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.rflags &= ~X86_FLAGS_AF;
		if (a*10 + b >= 100) {
			cpu->cd.x86.rflags |= X86_FLAGS_CF;
			cpu->cd.x86.rflags |= X86_FLAGS_AF;
			a %= 10;
		}
		cpu->cd.x86.r[X86_R_AX] &= ~0xff;
		cpu->cd.x86.r[X86_R_AX] |= ((a*16 + b) & 0xff);
	} else if (op == 0x2f) {			/*  DAS  */
		int tmp_al = cpu->cd.x86.r[X86_R_AX] & 0xff;
		if ((tmp_al & 0xf) > 9 || cpu->cd.x86.rflags & X86_FLAGS_AF) {
			cpu->cd.x86.r[X86_R_AX] &= ~0xff;
			cpu->cd.x86.r[X86_R_AX] |= ((tmp_al - 6) & 0xff);
			cpu->cd.x86.rflags |= X86_FLAGS_AF;
		} else
			cpu->cd.x86.rflags &= ~X86_FLAGS_AF;
		if (tmp_al > 0x9f || cpu->cd.x86.rflags & X86_FLAGS_CF) {
			cpu->cd.x86.r[X86_R_AX] &= ~0xff;
			cpu->cd.x86.r[X86_R_AX] |= ((tmp_al - 0x60) & 0xff);
			cpu->cd.x86.rflags |= X86_FLAGS_CF;
		} else
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		x86_calc_flags(cpu, cpu->cd.x86.r[X86_R_AX] & 0xff,
		    0, 8, CALCFLAGS_OP_XOR);
	} else if (op == 0x37) {			/*  AAA  */
		int b = cpu->cd.x86.r[X86_R_AX] & 0xf;
		if (b > 9) {
			cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX]
			    & ~0xff00) | ((cpu->cd.x86.r[X86_R_AX] &
			    0xff00) + 0x100);
			cpu->cd.x86.rflags |= X86_FLAGS_CF | X86_FLAGS_AF;
		} else {
			cpu->cd.x86.rflags &= ~(X86_FLAGS_CF | X86_FLAGS_AF);
		}
		cpu->cd.x86.r[X86_R_AX] &= ~0xf0;
	} else if (op >= 0x40 && op <= 0x4f) {
		int old_cf = cpu->cd.x86.rflags & X86_FLAGS_CF;
		if (op < 0x48) {
			x86_calc_flags(cpu, cpu->cd.x86.r[op & 7], 1, mode,
			    CALCFLAGS_OP_ADD);
			cpu->cd.x86.r[op & 7] = modify(cpu->cd.x86.r[op & 7],
			    cpu->cd.x86.r[op & 7] + 1);
		} else {
			x86_calc_flags(cpu, cpu->cd.x86.r[op & 7], 1, mode,
			    CALCFLAGS_OP_SUB);
			if (mode == 16)
				cpu->cd.x86.r[op & 7] = modify(cpu->cd.x86.r[op
				    & 7], (cpu->cd.x86.r[op & 7] & 0xffff) - 1);
			else {
				cpu->cd.x86.r[op & 7] --;
				cpu->cd.x86.r[op & 7] &= 0xffffffffULL;
			}
		}
		/*  preserve CF:  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.rflags |= old_cf;
	} else if (op >= 0x50 && op <= 0x57) {
		if (!x86_push(cpu, cpu->cd.x86.r[op & 7], mode))
			return 0;
	} else if (op >= 0x58 && op <= 0x5f) {
		if (!x86_pop(cpu, &tmp, mode))
			return 0;
		if (mode == 16)
			cpu->cd.x86.r[op & 7] = (cpu->cd.x86.r[op & 7] &
			    ~0xffff) | (tmp & 0xffff);
		else
			cpu->cd.x86.r[op & 7] = tmp;
	} else if (op == 0x60) {		/*  PUSHA/PUSHAD  */
		uint64_t r[8];
		int i;
		for (i=0; i<8; i++)
			r[i] = cpu->cd.x86.r[i];
		for (i=0; i<8; i++)
			if (!x86_push(cpu, r[i], mode)) {
				fatal("TODO: failed pusha\n");
				cpu->running = 0;
				return 0;
			}
	} else if (op == 0x61) {		/*  POPA/POPAD  */
		uint64_t r[8];
		int i;
		for (i=7; i>=0; i--)
			if (!x86_pop(cpu, &r[i], mode)) {
				fatal("TODO: failed popa\n");
				cpu->running = 0;
				return 0;
			}
		for (i=0; i<8; i++)
			if (i != X86_R_SP) {
				if (mode == 16)
					cpu->cd.x86.r[i] = (cpu->cd.x86.r[i]
					    & ~0xffff) | (r[i] & 0xffff);
				else
					cpu->cd.x86.r[i] = r[i];
			}
	} else if (op == 0x68) {		/*  PUSH imm16/32  */
		uint64_t imm = read_imm(&instr, &newpc, mode);
		if (!x86_push(cpu, imm, mode))
			return 0;
	} else if (op == 0x69 || op == 0x6b) {
		instr_orig = instr;
		if (!modrm(cpu, MODRM_READ, mode, mode67, 0, &instr,
		    &newpc, &op1, &op2))
			return 0;
		if (op == 0x69)
			imm = read_imm(&instr, &newpc, mode);
		else
			imm = (signed char)read_imm(&instr, &newpc, 8);
		op2 = op1 * imm;
		/*  TODO: overflow!  */
		if (!modrm(cpu, MODRM_WRITE_R, mode, mode67, 0,
		    &instr_orig, NULL, &op1, &op2))
			return 0;
	} else if (op == 0x6a) {		/*  PUSH imm8  */
		uint64_t imm = (signed char)read_imm(&instr, &newpc, 8);
		if (!x86_push(cpu, imm, mode))
			return 0;
	} else if ((op & 0xf0) == 0x70) {
		imm = read_imm(&instr, &newpc, 8);
		success = x86_condition(cpu, op);
		if (success)
			newpc = modify(newpc, newpc + (signed char)imm);
	} else if (op == 0x80 || op == 0x81) {	/*  add/and r/m, imm  */
		instr_orig = instr;
		if (!modrm(cpu, MODRM_READ, mode, mode67, op == 0x80?
		    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2))
			return 0;
		imm = read_imm(&instr, &newpc, op==0x80? 8 : mode);
		switch ((*instr_orig >> 3) & 0x7) {
		case 0:	x86_calc_flags(cpu, op1, imm, op==0x80? 8 : mode,
			    CALCFLAGS_OP_ADD);
			op1 += imm;
			break;
		case 1:	op1 |= imm; break;
		case 2:	tmp = imm + (cpu->cd.x86.rflags & X86_FLAGS_CF? 1 : 0);
			x86_calc_flags(cpu, op1, tmp, op==0x80? 8 : mode,
			    CALCFLAGS_OP_ADD);
			op1 += tmp;
			break;
		case 3:	tmp = imm + (cpu->cd.x86.rflags & X86_FLAGS_CF? 1 : 0);
			x86_calc_flags(cpu, op1, tmp, op==0x80? 8 : mode,
			    CALCFLAGS_OP_SUB);
			op1 -= tmp;
			break;
		case 4:	op1 &= imm; break;
		case 5:	x86_calc_flags(cpu, op1, imm, op==0x80? 8 : mode,
			    CALCFLAGS_OP_SUB);
			op1 -= imm; break;
		case 6:	op1 ^= imm; break;
		case 7:	x86_calc_flags(cpu, op1, imm, op==0x80? 8 : mode,
			    CALCFLAGS_OP_SUB); /* cmp */
			break;
		}

		if (((*instr_orig >> 3) & 0x7) != 7) {
			if (((*instr_orig >> 3) & 0x7) != 0 &&
			    ((*instr_orig >> 3) & 0x7) != 2 &&
			    ((*instr_orig >> 3) & 0x7) != 3 &&
			    ((*instr_orig >> 3) & 0x7) != 5)
				x86_calc_flags(cpu, op1, 0, op==0x80? 8 : mode,
				    CALCFLAGS_OP_XOR);

			/*  "and","or","xor" always clears CF and OF:  */
			if (((*instr_orig >> 3) & 0x7) == 1 ||
			    ((*instr_orig >> 3) & 0x7) == 4 ||
			    ((*instr_orig >> 3) & 0x7) == 6) {
				cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
				cpu->cd.x86.rflags &= ~X86_FLAGS_OF;
			}

			if (!modrm(cpu, MODRM_WRITE_RM, mode, mode67,
			    op == 0x80? MODRM_EIGHTBIT : 0, &instr_orig,
			    NULL, &op1, &op2))
				return 0;
		}
	} else if (op == 0x83) {	/*  add/and r/m1632, imm8  */
		instr_orig = instr;
		if (!modrm(cpu, MODRM_READ, mode, mode67, 0, &instr,
		    &newpc, &op1, &op2))
			return 0;
		imm = read_imm(&instr, &newpc, 8);
		switch ((*instr_orig >> 3) & 0x7) {
		case 0:	x86_calc_flags(cpu, op1, (signed char)imm,
			    mode, CALCFLAGS_OP_ADD);
			op1 += (signed char)imm;
			break;
		case 1:	op1 |= (signed char)imm; break;
		case 2: tmp = (signed char)imm +
			    (cpu->cd.x86.rflags & X86_FLAGS_CF? 1 : 0);
			x86_calc_flags(cpu, op1, tmp, mode, CALCFLAGS_OP_ADD);
			op1 += tmp;
			break;
		case 3: tmp = (signed char)imm +
			    (cpu->cd.x86.rflags & X86_FLAGS_CF? 1 : 0);
			x86_calc_flags(cpu, op1, tmp, mode, CALCFLAGS_OP_SUB);
			op1 -= tmp;
			break;
		case 4:	op1 &= (signed char)imm; break;
		case 5:	x86_calc_flags(cpu, op1, (signed char)imm, mode,
			    CALCFLAGS_OP_SUB);
			op1 -= (signed char)imm; break;
		case 6:	op1 ^= (signed char)imm; break;
		case 7: x86_calc_flags(cpu, op1, (signed char)imm, mode,
			    CALCFLAGS_OP_SUB);
			break;
		}
		if (((*instr_orig >> 3) & 0x7) != 7) {
			if (((*instr_orig >> 3) & 0x7) != 0 &&
			    ((*instr_orig >> 3) & 0x7) != 2 &&
			    ((*instr_orig >> 3) & 0x7) != 3 &&
			    ((*instr_orig >> 3) & 0x7) != 5)
				x86_calc_flags(cpu, op1, 0, mode,
				    CALCFLAGS_OP_XOR);

			/*  "and","or","xor" always clears CF and OF:  */
			if (((*instr_orig >> 3) & 0x7) == 1 ||
			    ((*instr_orig >> 3) & 0x7) == 4 ||
			    ((*instr_orig >> 3) & 0x7) == 6) {
				cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
				cpu->cd.x86.rflags &= ~X86_FLAGS_OF;
			}
			if (!modrm(cpu, MODRM_WRITE_RM, mode,
			    mode67, 0, &instr_orig, NULL, &op1, &op2))
				return 0;
		}
	} else if (op == 0x84 || op == 0x85) {		/*  TEST  */
		success = modrm(cpu, MODRM_READ, mode, mode67, op == 0x84?
		    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2);
		if (!success)
			return 0;
		op1 &= op2;
		x86_calc_flags(cpu, op1, 0, op==0x84? 8 : mode,
		    CALCFLAGS_OP_XOR);
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.rflags &= ~X86_FLAGS_OF;
	} else if (op >= 0x86 && op <= 0x87) {		/*  XCHG  */
		void *orig2 = instr_orig = instr;
		success = modrm(cpu, MODRM_READ, mode, mode67, op&1? 0 :
		    MODRM_EIGHTBIT, &instr, &newpc, &op1, &op2);
		if (!success)
			return 0;
		/*  Note: Update the r/m first, because it may be dependant
		    on original register values :-)  */
		success = modrm(cpu, MODRM_WRITE_RM, mode, mode67,
		    op == 0x86? MODRM_EIGHTBIT : 0, &instr_orig,
		    NULL, &op2, &op1);
		instr_orig = orig2;
		success = modrm(cpu, MODRM_WRITE_R, mode, mode67,
		    op == 0x86? MODRM_EIGHTBIT : 0, &instr_orig,
		    NULL, &op2, &op1);
		if (!success)
			return 0;
	} else if (op >= 0x88 && op <= 0x8b) {		/*  MOV  */
		instr_orig = instr;
		success = modrm(cpu, MODRM_READ, mode, mode67, (op & 1) == 0?
		    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2);
		if (!success)
			return 0;
		if (op < 0x8a) {
			success = modrm(cpu, MODRM_WRITE_RM, mode, mode67,
			    op == 0x88? MODRM_EIGHTBIT : 0, &instr_orig,
			    NULL, &op2, &op1);
		} else {
			success = modrm(cpu, MODRM_WRITE_R, mode, mode67,
			    op == 0x8a? MODRM_EIGHTBIT : 0, &instr_orig,
			    NULL, &op2, &op1);
		}
		if (!success)
			return 0;
	} else if (op == 0x8c || op == 0x8e) {		/*  MOV seg  */
		instr_orig = instr;
		if (!modrm(cpu, MODRM_READ, 16, mode67, MODRM_SEG,
		    &instr, &newpc, &op1, &op2))
			return 0;
		if (op == 0x8c) {
			if (!modrm(cpu, MODRM_WRITE_RM, 16, mode67, MODRM_SEG,
			    &instr_orig, NULL, &op2, &op1))
				return 0;
		} else {
			reload_segment_descriptor(cpu, (*instr_orig >> 3) & 7,
			    op1 & 0xffff, &newpc);
		}
	} else if (op == 0x8d) {			/*  LEA  */
		instr_orig = instr;
		if (!modrm(cpu, MODRM_READ, mode, mode67,
		    MODRM_JUST_GET_ADDR, &instr, &newpc, &op1, &op2))
			return 0;
		op2 = op1;
		if (!modrm(cpu, MODRM_WRITE_R, mode, mode67,
		    0, &instr_orig, NULL, &op1, &op2))
			return 0;
	} else if (op == 0x8f) {
		switch ((*instr >> 3) & 0x7) {
		case 0:	/*  POP m16/m32  */
			if (!x86_pop(cpu, &op1, mode))
				return 0;
			if (!modrm(cpu, MODRM_WRITE_RM, mode, mode67,
			    0, &instr, &newpc, &op1, &op2))
				return 0;
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op, *instr);
			quiet_mode = 0;
			x86_cpu_disassemble_instr(cpu,
			    really_orig_instr, 1|omode, 0, 0);
			cpu->running = 0;
		}
	} else if (op == 0x90) {		/*  NOP  */
	} else if (op >= 0x91 && op <= 0x97) {	/*  XCHG  */
		uint64_t tmp;
		if (mode == 16) {
			tmp = cpu->cd.x86.r[X86_R_AX];
			cpu->cd.x86.r[X86_R_AX] = modify(
			    cpu->cd.x86.r[X86_R_AX], cpu->cd.x86.r[op & 7]);
			cpu->cd.x86.r[op & 7] = modify(
			    cpu->cd.x86.r[op & 7], tmp);
		} else {
			tmp = cpu->cd.x86.r[X86_R_AX];
			cpu->cd.x86.r[X86_R_AX] = cpu->cd.x86.r[op & 7];
			cpu->cd.x86.r[op & 7] = tmp;
		}
	} else if (op == 0x98) {		/*  CBW/CWDE  */
		if (mode == 16) {
			cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
			if (cpu->cd.x86.r[X86_R_AX] & 0x80)
				cpu->cd.x86.r[X86_R_AX] |= 0xff00;
		} else {
			cpu->cd.x86.r[X86_R_AX] &= 0xffff;
			if (cpu->cd.x86.r[X86_R_AX] & 0x8000)
				cpu->cd.x86.r[X86_R_AX] |= 0xffff0000ULL;
		}
	} else if (op == 0x99) {		/*  CWD/CDQ  */
		if (mode == 16) {
			cpu->cd.x86.r[X86_R_DX] &= ~0xffff;
			if (cpu->cd.x86.r[X86_R_AX] & 0x8000)
				cpu->cd.x86.r[X86_R_DX] |= 0xffff;
		} else {
			cpu->cd.x86.r[X86_R_DX] = 0;
			if (cpu->cd.x86.r[X86_R_AX] & 0x80000000ULL)
				cpu->cd.x86.r[X86_R_DX] = 0xffffffff;
		}
	} else if (op == 0x9a) {	/*  CALL seg:ofs  */
		uint16_t old_tr = cpu->cd.x86.tr;
		imm = read_imm(&instr, &newpc, mode);
		imm2 = read_imm(&instr, &newpc, 16);
		if (!x86_push(cpu, cpu->cd.x86.s[X86_S_CS], mode))
			return 0;
		if (!x86_push(cpu, newpc, mode)) {
			fatal("TODO: push failed in CALL seg:ofs\n");
			cpu->running = 0;
			return 0;
		}
		reload_segment_descriptor(cpu, X86_S_CS, imm2, &newpc);
		if (cpu->cd.x86.tr == old_tr)
			newpc = imm;
	} else if (op == 0x9b) {		/*  WAIT  */
	} else if (op == 0x9c) {		/*  PUSHF  */
		if (!x86_push(cpu, cpu->cd.x86.rflags, mode))
			return 0;
	} else if (op == 0x9d) {		/*  POPF  */
		if (!x86_pop(cpu, &tmp, mode))
			return 0;
		if (mode == 16)
			cpu->cd.x86.rflags = (cpu->cd.x86.rflags & ~0xffff)
			    | (tmp & 0xffff);
		else if (mode == 32)
			cpu->cd.x86.rflags = (cpu->cd.x86.rflags & ~0xffffffff)
			    | (tmp & 0xffffffff);
		else
			cpu->cd.x86.rflags = tmp;
		/*  TODO: only affect some bits?  */
		cpu->cd.x86.rflags |= 0x0002;
		if (cpu->cd.x86.model.model_number == X86_MODEL_8086)
			cpu->cd.x86.rflags |= 0xf000;
		/*  TODO: all these bits aren't really cleared on a 286:  */
		if (cpu->cd.x86.model.model_number == X86_MODEL_80286)
			cpu->cd.x86.rflags &= ~0xf000;
		if (cpu->cd.x86.model.model_number == X86_MODEL_80386)
			cpu->cd.x86.rflags &= ~X86_FLAGS_AC;
		if (cpu->cd.x86.model.model_number == X86_MODEL_80486)
			cpu->cd.x86.rflags &= ~X86_FLAGS_ID;
	} else if (op == 0x9e) {		/*  SAHF  */
		int mask = (X86_FLAGS_SF | X86_FLAGS_ZF
		    | X86_FLAGS_AF | X86_FLAGS_PF | X86_FLAGS_CF);
		cpu->cd.x86.rflags &= ~mask;
		mask &= ((cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff);
		cpu->cd.x86.rflags |= mask;
	} else if (op == 0x9f) {		/*  LAHF  */
		int b = cpu->cd.x86.rflags & (X86_FLAGS_SF | X86_FLAGS_ZF
		    | X86_FLAGS_AF | X86_FLAGS_PF | X86_FLAGS_CF);
		b |= 2;
		cpu->cd.x86.r[X86_R_AX] &= ~0xff00;
		cpu->cd.x86.r[X86_R_AX] |= (b << 8);
	} else if (op == 0xa0) {		/*  MOV AL,[addr]  */
		imm = read_imm(&instr, &newpc, mode67);
		if (!x86_load(cpu, imm, &tmp, 1))
			return 0;
		cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] & ~0xff)
		    | (tmp & 0xff);
	} else if (op == 0xa1) {		/*  MOV AX,[addr]  */
		imm = read_imm(&instr, &newpc, mode67);
		if (!x86_load(cpu, imm, &tmp, mode/8))
			return 0;
		cpu->cd.x86.r[X86_R_AX] = modify(cpu->cd.x86.r[X86_R_AX], tmp);
	} else if (op == 0xa2) {		/*  MOV [addr],AL  */
		imm = read_imm(&instr, &newpc, mode67);
		if (!x86_store(cpu, imm, cpu->cd.x86.r[X86_R_AX], 1))
			return 0;
	} else if (op == 0xa3) {		/*  MOV [addr],AX  */
		imm = read_imm(&instr, &newpc, mode67);
		if (!x86_store(cpu, imm, cpu->cd.x86.r[X86_R_AX], mode/8))
			return 0;
	} else if (op == 0xa4 || op == 0xa5 ||		/*  MOVS  */
	    op == 0xa6 || op == 0xa7 ||			/*  CMPS  */
	    op == 0xaa || op == 0xab ||			/*  STOS  */
	    op == 0xac || op == 0xad ||			/*  LODS  */
	    op == 0xae || op == 0xaf) {			/*  SCAS  */
		int dir = 1, movs = 0, lods = 0, cmps = 0, stos = 0, scas = 0;
		int origcursegment = cpu->cd.x86.cursegment;

		len = 1;
		if (op & 1)
			len = mode / 8;
		if (op >= 0xa4 && op <= 0xa5)
			movs = 1;
		if (op >= 0xa6 && op <= 0xa7)
			cmps = 1;
		if (op >= 0xaa && op <= 0xab)
			stos = 1;
		if (op >= 0xac && op <= 0xad)
			lods = 1;
		if (op >= 0xae && op <= 0xaf)
			scas = 1;
		if (cpu->cd.x86.rflags & X86_FLAGS_DF)
			dir = -1;

		do {
			uint64_t value;

			if (rep) {
				/*  Abort if [e]cx already 0:  */
				if (mode == 16 && (cpu->cd.x86.r[X86_R_CX] &
				    0xffff) == 0)
					break;
				if (mode != 16 && cpu->cd.x86.r[X86_R_CX] == 0)
					break;
			}

			if (!stos && !scas) {
				uint64_t addr = cpu->cd.x86.r[X86_R_SI];
				if (mode67 == 16)
					addr &= 0xffff;
				if (mode67 == 32)
					addr &= 0xffffffff;
				cpu->cd.x86.cursegment = origcursegment;
				if (!x86_load(cpu, addr, &value, len))
					return 0;
			} else
				value = cpu->cd.x86.r[X86_R_AX];
			if (lods) {
				if (op == 0xac)
					cpu->cd.x86.r[X86_R_AX] =
					    (cpu->cd.x86.r[X86_R_AX] & ~0xff)
					    | (value & 0xff);
				else if (mode == 16)
					cpu->cd.x86.r[X86_R_AX] =
					    (cpu->cd.x86.r[X86_R_AX] & ~0xffff)
					    | (value & 0xffff);
				else
					cpu->cd.x86.r[X86_R_AX] = value;
			}

			if (stos || movs) {
				uint64_t addr = cpu->cd.x86.r[X86_R_DI];
				if (mode67 == 16)
					addr &= 0xffff;
				if (mode67 == 32)
					addr &= 0xffffffff;
				cpu->cd.x86.cursegment = X86_S_ES;
				if (!x86_store(cpu, addr, value, len))
					return 0;
			}
			if (cmps || scas) {
				uint64_t addr = cpu->cd.x86.r[X86_R_DI];
				if (mode67 == 16)
					addr &= 0xffff;
				if (mode67 == 32)
					addr &= 0xffffffff;
				cpu->cd.x86.cursegment = X86_S_ES;
				if (!x86_load(cpu, addr, &tmp, len))
					return 0;

				x86_calc_flags(cpu, value, tmp, len*8,
				    CALCFLAGS_OP_SUB);
			}

			if (movs || lods || cmps) {
				/*  Modify esi:  */
				if (mode67 == 16)
					cpu->cd.x86.r[X86_R_SI] =
					    (cpu->cd.x86.r[X86_R_SI] & ~0xffff)
					    | ((cpu->cd.x86.r[X86_R_SI]+len*dir)
					    & 0xffff);
				else {
					cpu->cd.x86.r[X86_R_SI] += len*dir;
					if (mode67 == 32)
						cpu->cd.x86.r[X86_R_SI] &=
						    0xffffffff;
				}
			}

			if (!lods) {
				/*  Modify edi:  */
				if (mode67 == 16)
					cpu->cd.x86.r[X86_R_DI] =
					    (cpu->cd.x86.r[X86_R_DI] & ~0xffff)
					    | ((cpu->cd.x86.r[X86_R_DI]+len*dir)
					    & 0xffff);
				else {
					cpu->cd.x86.r[X86_R_DI] += len*dir;
					if (mode67 == 32)
						cpu->cd.x86.r[X86_R_DI] &=
						    0xffffffff;
				}
			}

			if (rep) {
				/*  Decrement ecx:  */
				if (mode67 == 16)
					cpu->cd.x86.r[X86_R_CX] =
					    (cpu->cd.x86.r[X86_R_CX] & ~0xffff)
					    | ((cpu->cd.x86.r[X86_R_CX] - 1)
					    & 0xffff);
				else {
					cpu->cd.x86.r[X86_R_CX] --;
					cpu->cd.x86.r[X86_R_CX] &= 0xffffffff;
				}
				if (mode67 == 16 && (cpu->cd.x86.r[X86_R_CX] &
				    0xffff) == 0)
					rep = 0;
				if (mode67 != 16 &&
				    cpu->cd.x86.r[X86_R_CX] == 0)
					rep = 0;

				if (cmps || scas) {
					if (rep == REP_REP && !(
					    cpu->cd.x86.rflags & X86_FLAGS_ZF))
						rep = 0;
					if (rep == REP_REPNE &&
					    cpu->cd.x86.rflags & X86_FLAGS_ZF)
						rep = 0;
				}
			}
		} while (rep);
	} else if (op >= 0xa8 && op <= 0xa9) {		/* TEST al/[e]ax,imm */
		op1 = cpu->cd.x86.r[X86_R_AX];
		op2 = read_imm(&instr, &newpc, op==0xa8? 8 : mode);
		op1 &= op2;
		x86_calc_flags(cpu, op1, 0, op==0xa8? 8 : mode,
		    CALCFLAGS_OP_XOR);
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
		cpu->cd.x86.rflags &= ~X86_FLAGS_OF;
	} else if (op >= 0xb0 && op <= 0xb3) {		/*  MOV Xl,imm  */
		imm = read_imm(&instr, &newpc, 8);
		cpu->cd.x86.r[op & 3] = (cpu->cd.x86.r[op & 3] & ~0xff)
		    | (imm & 0xff);
	} else if (op >= 0xb4 && op <= 0xb7) {		/*  MOV Xh,imm  */
		imm = read_imm(&instr, &newpc, 8);
		cpu->cd.x86.r[op & 3] = (cpu->cd.x86.r[op & 3] & ~0xff00)
		    | ((imm & 0xff) << 8);
	} else if (op >= 0xb8 && op <= 0xbf) {		/*  MOV Xx,imm  */
		imm = read_imm(&instr, &newpc, mode);
		cpu->cd.x86.r[op & 7] = modify(cpu->cd.x86.r[op & 7], imm);
	} else if (op == 0xc0 || op == 0xc1) {		/*  Shift/Rotate  */
		int n = 1;
		instr_orig = instr;
		success = modrm(cpu, MODRM_READ, mode, mode67,
		    op&1? 0 : MODRM_EIGHTBIT, &instr, &newpc, &op1, &op2);
		if (!success)
			return 0;
		n = read_imm(&instr, &newpc, 8);
		x86_shiftrotate(cpu, &op1, (*instr_orig >> 3) & 0x7,
		    n, op&1? mode : 8);
		success = modrm(cpu, MODRM_WRITE_RM, mode, mode67,
		    op&1? 0 : MODRM_EIGHTBIT, &instr_orig, NULL, &op1, &op2);
		if (!success)
			return 0;
	} else if (op == 0xc2 || op == 0xc3) {	/*  RET near  */
		uint64_t popped_pc;
		if (!x86_pop(cpu, &popped_pc, mode))
			return 0;
		if (op == 0xc2) {
			imm = read_imm(&instr, &newpc, 16);
			cpu->cd.x86.r[X86_R_SP] = modify(cpu->cd.x86.r[
			    X86_R_SP], cpu->cd.x86.r[X86_R_SP] + imm);
		}
		newpc = popped_pc;
	} else if (op == 0xc4 || op == 0xc5) {		/*  LDS,LES  */
		instr_orig = instr;
		if (!modrm(cpu, MODRM_READ, mode, mode67,
		    MODRM_JUST_GET_ADDR, &instr, &newpc, &op1, &op2))
			return 0;
		/*  op1 is the address to load from  */
		if (!x86_load(cpu, op1, &tmp, mode/8))
			return 0;
		op2 = tmp;
		if (!x86_load(cpu, op1 + mode/8, &tmp, 2))
			return 0;
		reload_segment_descriptor(cpu, op==0xc4? X86_S_ES:X86_S_DS,
		    tmp, &newpc);
		if (!modrm(cpu, MODRM_WRITE_R, mode, mode67,
		    0, &instr_orig, NULL, &op1, &op2))
			return 0;
	} else if (op >= 0xc6 && op <= 0xc7) {
		switch ((*instr >> 3) & 0x7) {
		case 0:	instr_orig = instr;		/*  MOV r/m, imm  */
			success = modrm(cpu, MODRM_READ, mode, mode67,
			    op == 0xc6? MODRM_EIGHTBIT : 0, &instr,
			    &newpc, &op1, &op2);
			if (!success)
				return 0;
			imm = read_imm(&instr, &newpc, op == 0xc6? 8 : mode);
			op1 = imm;
			success = modrm(cpu, MODRM_WRITE_RM, mode, mode67,
			    op == 0xc6? MODRM_EIGHTBIT : 0, &instr_orig,
			    NULL, &op1, &op2);
			if (!success)
				return 0;
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op, *instr);
			quiet_mode = 0;
			x86_cpu_disassemble_instr(cpu,
			    really_orig_instr, 1|omode, 0, 0);
			cpu->running = 0;
		}
	} else if (op == 0xc8) {	/*  ENTER  */
		uint64_t tmp_frame_ptr;
		int level;
		imm = read_imm(&instr, &newpc, 16);
		level = read_imm(&instr, &newpc, 8);
		if (!x86_push(cpu, cpu->cd.x86.r[X86_R_BP], mode))
			return 0;
		tmp_frame_ptr = cpu->cd.x86.r[X86_R_SP];
		if (level > 0) {
			while (level-- > 1) {
				uint64_t tmpword;
				cpu->cd.x86.r[X86_R_BP] = modify(
				    cpu->cd.x86.r[X86_R_BP],
				    cpu->cd.x86.r[X86_R_BP] - mode/8);
				cpu->cd.x86.cursegment = X86_S_SS;
				if (!x86_load(cpu, cpu->cd.x86.r[X86_R_BP], 
				    &tmpword, mode/8)) {
					fatal("TODO: load error inside"
					    " ENTER\n");
					cpu->running = 0;
					return 0;
				}
				if (!x86_push(cpu, tmpword, mode)) {
					fatal("TODO: push error inside"
					    " ENTER\n");
					cpu->running = 0;
					return 0;
				}
			}
			if (!x86_push(cpu, tmp_frame_ptr, mode))
				return 0;
		}
		cpu->cd.x86.r[X86_R_BP] = modify(cpu->cd.x86.r[X86_R_BP],
		    tmp_frame_ptr);
		if (mode == 16)
			cpu->cd.x86.r[X86_R_SP] = (cpu->cd.x86.r[X86_R_SP] &
			    ~0xffff) | ((cpu->cd.x86.r[X86_R_SP] & 0xffff)
			    - imm);
		else
			cpu->cd.x86.r[X86_R_SP] -= imm;
	} else if (op == 0xc9) {	/*  LEAVE  */
		cpu->cd.x86.r[X86_R_SP] = cpu->cd.x86.r[X86_R_BP];
		if (!x86_pop(cpu, &tmp, mode)) {
			fatal("TODO: pop error inside LEAVE\n");
			cpu->running = 0;
			return 0;
		}
		cpu->cd.x86.r[X86_R_BP] = tmp;
	} else if (op == 0xca || op == 0xcb) {	/*  RET far  */
		uint64_t tmp2;
		uint16_t old_tr = cpu->cd.x86.tr;
		if (op == 0xca)
			imm = read_imm(&instr, &newpc, 16);
		else
			imm = 0;
		if (!x86_pop(cpu, &tmp, mode))
			return 0;
		if (!x86_pop(cpu, &tmp2, mode)) {
			fatal("TODO: pop error inside RET\n");
			cpu->running = 0;
			return 0;
		}
		cpu->cd.x86.r[X86_R_SP] = modify(cpu->cd.x86.r[X86_R_SP],
		    cpu->cd.x86.r[X86_R_SP] + imm);
		reload_segment_descriptor(cpu, X86_S_CS, tmp2, &newpc);
		if (cpu->cd.x86.tr == old_tr)
			newpc = tmp;
	} else if (op == 0xcc) {	/*  INT3  */
		cpu->pc = newpc;
		return x86_interrupt(cpu, 3, 0);
	} else if (op == 0xcd) {	/*  INT  */
		imm = read_imm(&instr, &newpc, 8);
		cpu->pc = newpc;
		return x86_interrupt(cpu, imm, 0);
	} else if (op == 0xcf) {	/*  IRET  */
		uint64_t tmp2, tmp3;
		uint16_t old_tr = cpu->cd.x86.tr;
		if (!x86_pop(cpu, &tmp, mode))
			return 0;
		if (!x86_pop(cpu, &tmp2, mode))
			return 0;
		if (!x86_pop(cpu, &tmp3, mode))
			return 0;
debug("{ iret to 0x%04x:0x%08x }\n", (int)tmp2,(int)tmp);
		tmp2 &= 0xffff;
		/*  TODO: only affect some bits?  */
		if (mode == 16)
			cpu->cd.x86.rflags = (cpu->cd.x86.rflags & ~0xffff)
			    | (tmp3 & 0xffff);
		else
			cpu->cd.x86.rflags = tmp3;
		/*
		 *  In protected mode, if we're switching back from, say, an
		 *  interrupt handler, then we should pop the old ss:esp too:
		 */
		if (PROTECTED_MODE && (tmp2 & X86_PL_MASK) >
		    (cpu->cd.x86.s[X86_S_CS] & X86_PL_MASK)) {
			uint64_t old_ss, old_esp;
			if (!x86_pop(cpu, &old_esp, mode))
				return 0;
			if (!x86_pop(cpu, &old_ss, mode))
				return 0;
			old_ss &= 0xffff;

printf(": : : BEFORE tmp=%016llx tmp2=%016llx tmp3=%016llx\n",
(long long)tmp, (long long)tmp2, (long long)tmp3);
/* x86_cpu_register_dump(cpu, 1, 1); */

			reload_segment_descriptor(cpu, X86_S_SS, old_ss,
			    &newpc);
			cpu->cd.x86.r[X86_R_SP] = old_esp;

/* printf(": : : AFTER\n");
x86_cpu_register_dump(cpu, 1, 1); */

			/*  AFTER popping ss, check that the pl of
			    the popped ss is the same as tmp2 (the new
			    pl in cs)!  */
			if ((old_ss & X86_PL_MASK) != (tmp2 & X86_PL_MASK))
				fatal("WARNING: iret: popped ss' pl = %i,"
				    " different from cs' pl = %i\n",
				    (int)(old_ss & X86_PL_MASK),
				    (int)(tmp2 & X86_PL_MASK));
		}
		reload_segment_descriptor(cpu, X86_S_CS, tmp2, &newpc);
		if (cpu->cd.x86.tr == old_tr)
			newpc = tmp;
	} else if (op >= 0xd0 && op <= 0xd3) {
		int n = 1;
		instr_orig = instr;
		success = modrm(cpu, MODRM_READ, mode, mode67,
		    op&1? 0 : MODRM_EIGHTBIT, &instr, &newpc, &op1, &op2);
		if (!success)
			return 0;
		if (op >= 0xd2)
			n = cpu->cd.x86.r[X86_R_CX];
		x86_shiftrotate(cpu, &op1, (*instr_orig >> 3) & 0x7,
		    n, op&1? mode : 8);
		success = modrm(cpu, MODRM_WRITE_RM, mode, mode67,
		    op&1? 0 : MODRM_EIGHTBIT, &instr_orig, NULL, &op1, &op2);
		if (!success)
			return 0;
	} else if (op == 0xd4) {	/*  AAM  */
		int al = cpu->cd.x86.r[X86_R_AX] & 0xff;
		/*  al should be in the range 0..81  */
		int high;
		imm = read_imm(&instr, &newpc, 8);
		if (imm == 0) {
			fatal("[ x86: \"aam 0\" ]\n");
			cpu->running = 0;
		} else {
			high = al / imm;
			al %= imm;
			cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] &
			    ~0xffff) | al | ((high & 0xff) << 8);
			x86_calc_flags(cpu, cpu->cd.x86.r[X86_R_AX],
			    0, 8, CALCFLAGS_OP_XOR);
		}
	} else if (op == 0xd5) {	/*  AAD  */
		int al = cpu->cd.x86.r[X86_R_AX] & 0xff;
		int ah = (cpu->cd.x86.r[X86_R_AX] >> 8) & 0xff;
		imm = read_imm(&instr, &newpc, 8);
		cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] & ~0xffff)
		    | ((al + 10*ah) & 0xff);
		x86_calc_flags(cpu, cpu->cd.x86.r[X86_R_AX],
		    0, 8, CALCFLAGS_OP_XOR);
	} else if (op == 0xd7) {		/*  XLAT  */
		uint64_t addr = cpu->cd.x86.r[X86_R_BX];
		if (mode == 16)
			addr &= 0xffff;
		addr += (cpu->cd.x86.r[X86_R_AX] & 0xff);
		if (!x86_load(cpu, addr, &tmp, 1))
			return 0;
		cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] & ~0xff)
		    | (tmp & 0xff);
	} else if (op == 0xd9) {
		int subop = (*instr >> 3) & 7;
		imm = *instr;
		if (subop == 5) {			/*  FLDCW mem16  */
			if (!modrm(cpu, MODRM_READ, 16, mode67, 0, &instr,
			    &newpc, &op1, &op2))
				return 0;
			cpu->cd.x86.fpu_cw = op1;
		} else if (subop == 7) {		/*  FSTCW mem16  */
			op1 = cpu->cd.x86.fpu_cw;
			if (!modrm(cpu, MODRM_WRITE_RM, 16, mode67, 0, &instr,
			    &newpc, &op1, &op2))
				return 0;
		} else {
			fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op, *instr);
			quiet_mode = 0;
			x86_cpu_disassemble_instr(cpu, really_orig_instr,
			    1 | omode, 0, 0);
			cpu->running = 0;
		}
	} else if (op == 0xdb) {
		imm = *instr;
		if (imm == 0xe2) {			/*  FCLEX  */
			read_imm(&instr, &newpc, 8);
			/*  TODO: actually clear exceptions  */
		} else if (imm == 0xe3) {		/*  FINIT  */
			read_imm(&instr, &newpc, 8);
			/*  TODO: actually init?  */
		} else if (imm == 0xe4) {		/*  FSETPM  */
			read_imm(&instr, &newpc, 8);
		} else {
			fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op, *instr);
			quiet_mode = 0;
			x86_cpu_disassemble_instr(cpu, really_orig_instr,
			    1 | omode, 0, 0);
			cpu->running = 0;
		}
	} else if (op == 0xdd) {
		int subop = (*instr >> 3) & 7;
		imm = *instr;
		if (subop == 7) {		/*  FSTSW mem16  */
			op1 = cpu->cd.x86.fpu_sw;
			if (!modrm(cpu, MODRM_WRITE_RM, 16, mode67, 0, &instr,
			    &newpc, &op1, &op2))
				return 0;
		} else {
			fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op, *instr);
			quiet_mode = 0;
			x86_cpu_disassemble_instr(cpu, really_orig_instr,
			    1 | omode, 0, 0);
			cpu->running = 0;
		}
	} else if (op == 0xdf) {
		imm = *instr;
		if (imm == 0xe0) {		/*  FSTSW  */
			read_imm(&instr, &newpc, 8);
			cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] &
			    ~0xffff) | (cpu->cd.x86.fpu_sw & 0xffff);
		} else {
			fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op, *instr);
			quiet_mode = 0;
			x86_cpu_disassemble_instr(cpu, really_orig_instr,
			    1 | omode, 0, 0);
			cpu->running = 0;
		}
	} else if (op == 0xe4 || op == 0xe5	/*  IN imm,AL or AX/EAX  */
	    || op == 0x6c || op == 0x6d) {	/*  INSB or INSW/INSD  */
		unsigned char databuf[8];
		int port_nr, ins = 0, len = 1, dir = 1;
		if (op == 0x6c || op == 0x6d) {
			port_nr = cpu->cd.x86.r[X86_R_DX] & 0xffff;
			ins = 1;
		} else
			port_nr = read_imm(&instr, &newpc, 8);
		if (op & 1)
			len = mode/8;
		if (cpu->cd.x86.rflags & X86_FLAGS_DF)
			dir = -1;
		do {
			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE+port_nr,
			    &databuf[0], len, MEM_READ, CACHE_NONE | PHYSICAL);

			if (ins) {
				uint64_t addr = cpu->cd.x86.r[X86_R_DI];
				uint32_t value = databuf[0] + (databuf[1] << 8)
				    + (databuf[2] << 16) + (databuf[3] << 24);
				if (mode67 == 16)
					addr &= 0xffff;
				if (mode67 == 32)
					addr &= 0xffffffff;
				cpu->cd.x86.cursegment = X86_S_ES;
				if (!x86_store(cpu, addr, value, len))
					return 0;

				/*  Advance (e)di:  */
				if (mode67 == 16)
					cpu->cd.x86.r[X86_R_DI] =
					    (cpu->cd.x86.r[X86_R_DI] & ~0xffff)
					    | ((cpu->cd.x86.r[X86_R_DI]+len*dir)
					    & 0xffff);
				else {
					cpu->cd.x86.r[X86_R_DI] += len*dir;
					if (mode67 == 32)
						cpu->cd.x86.r[X86_R_DI] &=
						    0xffffffff;
				}

				if (rep) {
					/*  Decrement ecx:  */
					if (mode67 == 16)
						cpu->cd.x86.r[X86_R_CX] =
						    (cpu->cd.x86.r[X86_R_CX] &
						    ~0xffff) | ((cpu->cd.x86.r[
						    X86_R_CX] - 1) & 0xffff);
					else {
						cpu->cd.x86.r[X86_R_CX] --;
						cpu->cd.x86.r[X86_R_CX] &=
						    0xffffffff;
					}
					if (mode67 == 16 && (cpu->cd.x86.r[
					    X86_R_CX] & 0xffff) == 0)
						rep = 0;
					if (mode67 != 16 &&
					    cpu->cd.x86.r[X86_R_CX] == 0)
						rep = 0;
				}
			} else {
				if (len == 1)
					cpu->cd.x86.r[X86_R_AX] =
					    (cpu->cd.x86.r[X86_R_AX] &
					    ~0xff) | databuf[0];
				else if (len == 2)
					cpu->cd.x86.r[X86_R_AX] =
					    (cpu->cd.x86.r[X86_R_AX] & ~0xffff)
					    | databuf[0] | (databuf[1] << 8);
				else if (len == 4)
					cpu->cd.x86.r[X86_R_AX] = databuf[0] |
					    (databuf[1] << 8) | (databuf[2]
					    << 16) | (databuf[3] << 24);
			}
		} while (rep);
	} else if (op == 0xe6 || op == 0xe7	/*  OUT imm,AL or AX/EAX  */
	    || op == 0x6e || op == 0x6f) {	/*  OUTSB or OUTSW/OUTSD  */
		unsigned char databuf[8];
		int port_nr, outs = 0, len = 1, dir = 1;
		if (op == 0x6e || op == 0x6f) {
			port_nr = cpu->cd.x86.r[X86_R_DX] & 0xffff;
			outs = 1;
		} else
			port_nr = read_imm(&instr, &newpc, 8);
		if (op & 1)
			len = mode/8;
		if (cpu->cd.x86.rflags & X86_FLAGS_DF)
			dir = -1;
		do {
			if (outs) {
				int i;
				uint64_t addr = cpu->cd.x86.r[X86_R_DI];
				uint64_t value;
				if (mode67 == 16)
					addr &= 0xffff;
				if (mode67 == 32)
					addr &= 0xffffffff;
				cpu->cd.x86.cursegment = X86_S_ES;
				if (!x86_load(cpu, addr, &value, len))
					return 0;

				/*  Advance (e)di:  */
				if (mode67 == 16)
					cpu->cd.x86.r[X86_R_DI] =
					    (cpu->cd.x86.r[X86_R_DI] & ~0xffff)
					    | ((cpu->cd.x86.r[X86_R_DI]+len*dir)
					    & 0xffff);
				else {
					cpu->cd.x86.r[X86_R_DI] += len*dir;
					if (mode67 == 32)
						cpu->cd.x86.r[X86_R_DI] &=
						    0xffffffff;
				}

				for (i=0; i<8; i++)
					databuf[i] = value >> (i*8);

				if (rep) {
					/*  Decrement ecx:  */
					if (mode67 == 16)
						cpu->cd.x86.r[X86_R_CX] =
						    (cpu->cd.x86.r[X86_R_CX] &
						    ~0xffff) | ((cpu->cd.x86.r[
						    X86_R_CX] - 1) & 0xffff);
					else {
						cpu->cd.x86.r[X86_R_CX] --;
						cpu->cd.x86.r[X86_R_CX] &=
						    0xffffffff;
					}
					if (mode67 == 16 && (cpu->cd.x86.r[
					    X86_R_CX] & 0xffff) == 0)
						rep = 0;
					if (mode67 != 16 &&
					    cpu->cd.x86.r[X86_R_CX] == 0)
						rep = 0;
				}
			} else {
				int i;
				for (i=0; i<8; i++)
					databuf[i] = cpu->cd.x86.r[X86_R_AX]
					    >> (i*8);
			}

			cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE+port_nr,
			    &databuf[0], len, MEM_WRITE, CACHE_NONE | PHYSICAL);
		} while (rep);
	} else if (op == 0xe8 || op == 0xe9) {	/*  CALL/JMP near  */
		imm = read_imm(&instr, &newpc, mode);
		if (mode == 16)
			imm = (int16_t)imm;
		if (mode == 32)
			imm = (int32_t)imm;
		if (op == 0xe8) {
			if (!x86_push(cpu, newpc, mode))
				return 0;
		}
		newpc += imm;
	} else if (op == 0xea) {	/*  JMP seg:ofs  */
		uint16_t old_tr = cpu->cd.x86.tr;
		imm = read_imm(&instr, &newpc, mode);
		imm2 = read_imm(&instr, &newpc, 16);
		reload_segment_descriptor(cpu, X86_S_CS, imm2, &newpc);
		if (cpu->cd.x86.tr == old_tr)
			newpc = imm;
	} else if ((op >= 0xe0 && op <= 0xe3) || op == 0xeb) {	/*  LOOP,JMP */
		int perform_jump = 0;
		imm = read_imm(&instr, &newpc, 8);
		switch (op) {
		case 0xe0:	/*  loopnz  */
		case 0xe1:	/*  loopz  */
		case 0xe2:	/*  loop  */
			/*  NOTE: address size attribute, not operand size?  */
			if (mode67 == 16)
				cpu->cd.x86.r[X86_R_CX] = (~0xffff &
				    cpu->cd.x86.r[X86_R_CX]) |
				    ((cpu->cd.x86.r[X86_R_CX] - 1) & 0xffff);
			else
				cpu->cd.x86.r[X86_R_CX] --;
			if (mode67 == 16 && (cpu->cd.x86.r[X86_R_CX] &
			    0xffff) != 0)
				perform_jump = 1;
			if (mode67 == 32 && cpu->cd.x86.r[X86_R_CX] != 0)
				perform_jump = 1;
			if (op == 0xe0 && cpu->cd.x86.rflags & X86_FLAGS_ZF)
				perform_jump = 0;
			if (op == 0xe1 && (!cpu->cd.x86.rflags & X86_FLAGS_ZF))
				perform_jump = 0;
			break;
		case 0xe3:	/*  jcxz/jecxz  */
			if (mode67 == 16 && (cpu->cd.x86.r[X86_R_CX] & 0xffff)
			    == 0)
				perform_jump = 1;
			if (mode67 != 16 && (cpu->cd.x86.r[X86_R_CX] &
			    0xffffffffULL) == 0)
				perform_jump = 1;
			break;
		case 0xeb:	/*  jmp  */
			perform_jump = 1;
			break;
		}
		if (perform_jump)
			newpc += (signed char)imm;
	} else if (op == 0xec || op == 0xed) {	/*  IN DX,AL or AX/EAX  */
		unsigned char databuf[8];
		cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE +
		    (cpu->cd.x86.r[X86_R_DX] & 0xffff), &databuf[0],
		    op == 0xec? 1 : (mode/8), MEM_READ, CACHE_NONE | PHYSICAL);
		if (op == 0xec)
			cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] &
			    ~0xff) | databuf[0];
		else if (op == 0xed && mode == 16)
			cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[X86_R_AX] &
			    ~0xffff) | databuf[0] | (databuf[1] << 8);
		else if (op == 0xed && mode == 32)
			cpu->cd.x86.r[X86_R_AX] = databuf[0] |
			    (databuf[1] << 8) | (databuf[2] << 16) |
			    (databuf[3] << 24);
	} else if (op == 0xee || op == 0xef) {	/*  OUT DX,AL or AX/EAX  */
		unsigned char databuf[8];
		databuf[0] = cpu->cd.x86.r[X86_R_AX];
		if (op == 0xef) {
			databuf[1] = cpu->cd.x86.r[X86_R_AX] >> 8;
			if (mode >= 32) {
				databuf[2] = cpu->cd.x86.r[X86_R_AX] >> 16;
				databuf[3] = cpu->cd.x86.r[X86_R_AX] >> 24;
			}
		}
		cpu->memory_rw(cpu, cpu->mem, X86_IO_BASE +
		    (cpu->cd.x86.r[X86_R_DX] & 0xffff), &databuf[0],
		    op == 0xee? 1 : (mode/8), MEM_WRITE, CACHE_NONE | PHYSICAL);
	} else if (op == 0xf4) {	/*  HLT  */
		cpu->cd.x86.halted = 1;
	} else if (op == 0xf5) {	/*  CMC  */
		cpu->cd.x86.rflags ^= X86_FLAGS_CF;
	} else if (op == 0xf8) {	/*  CLC  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
	} else if (op == 0xf9) {	/*  STC  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
	} else if (op == 0xfa) {	/*  CLI  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_IF;
	} else if (op == 0xfb) {	/*  STI  */
		cpu->cd.x86.rflags |= X86_FLAGS_IF;
	} else if (op == 0xfc) {	/*  CLD  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_DF;
	} else if (op == 0xfd) {	/*  STD  */
		cpu->cd.x86.rflags |= X86_FLAGS_DF;
	} else if (op == 0xf6 || op == 0xf7) {		/*  MUL, DIV etc  */
		uint64_t res;
		int unsigned_op = 1;
		switch ((*instr >> 3) & 0x7) {
		case 0:	/*  test  */
			success = modrm(cpu, MODRM_READ, mode, mode67,
			    op == 0xf6? MODRM_EIGHTBIT : 0, &instr,
			    &newpc, &op1, &op2);
			if (!success)
				return 0;
			op2 = read_imm(&instr, &newpc, op==0xf6? 8 : mode);
			op1 &= op2;
			x86_calc_flags(cpu, op1, 0, op==0xf6? 8 : mode,
			    CALCFLAGS_OP_XOR);
			break;
		case 2:	/*  not  */
		case 3:	/*  neg  */
			instr_orig = instr;
			success = modrm(cpu, MODRM_READ, mode, mode67,
			    op == 0xf6? MODRM_EIGHTBIT : 0, &instr,
			    &newpc, &op1, &op2);
			if (!success)
				return 0;
			switch ((*instr_orig >> 3) & 0x7) {
			case 2:	op1 ^= 0xffffffffffffffffULL; break;
			case 3:	x86_calc_flags(cpu, 0, op1,
				    op == 0xf6? 8 : mode, CALCFLAGS_OP_SUB);
				op1 = 0 - op1;
				cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
				if (op1 != 0)
					cpu->cd.x86.rflags |= X86_FLAGS_CF;
				break;
			}
			success = modrm(cpu, MODRM_WRITE_RM, mode, mode67,
			    op == 0xf6? MODRM_EIGHTBIT : 0, &instr_orig,
			    NULL, &op1, &op2);
			if (!success)
				return 0;
			break;
		case 5:	/*  imul  */
			unsigned_op = 0;
		case 4:	/*  mul  */
			success = modrm(cpu, MODRM_READ, mode, mode67,
			    op == 0xf6? MODRM_EIGHTBIT : 0, &instr,
			    &newpc, &op1, &op2);
			if (!success)
				return 0;
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
			cpu->cd.x86.rflags &= ~X86_FLAGS_OF;
			if (op == 0xf6) {
				if (unsigned_op)
					res = (cpu->cd.x86.r[X86_R_AX] & 0xff)
					    * (op1 & 0xff);
				else
					res = (int16_t)(signed char)(cpu->cd.
					    x86.r[X86_R_AX] & 0xff) * (int16_t)
					    (signed char)(op1 & 0xff);
				cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[
				    X86_R_AX] & ~0xffff) | (res & 0xffff);
				if ((res & 0xffff) >= 0x100)
					cpu->cd.x86.rflags |= X86_FLAGS_CF
					    | X86_FLAGS_OF;
			} else if (mode == 16) {
				if (unsigned_op)
					res = (cpu->cd.x86.r[X86_R_AX] & 0xffff)
					    * (op1 & 0xffff);
				else
					res = (int32_t)(int16_t)(cpu->cd.x86.r[
					    X86_R_AX] & 0xffff) * (int32_t)
					    (int16_t)(op1 & 0xffff);
				cpu->cd.x86.r[X86_R_AX] = modify(cpu->
				    cd.x86.r[X86_R_AX], res & 0xffff);
				cpu->cd.x86.r[X86_R_DX] = modify(cpu->cd.x86
				    .r[X86_R_DX], (res>>16) & 0xffff);
				if ((res & 0xffffffff) >= 0x10000)
					cpu->cd.x86.rflags |= X86_FLAGS_CF
					    | X86_FLAGS_OF;
			} else if (mode == 32) {
				if (unsigned_op)
					res = (cpu->cd.x86.r[X86_R_AX] &
					    0xffffffff) * (op1 & 0xffffffff);
				else
					res = (int64_t)(int32_t)(cpu->cd.x86.r[
					    X86_R_AX] & 0xffffffff) * (int64_t)
					    (int32_t)(op1 & 0xffffffff);
				cpu->cd.x86.r[X86_R_AX] = res & 0xffffffff;
				cpu->cd.x86.r[X86_R_DX] = (res >> 32) &
				    0xffffffff;
				if (res >= 0x100000000ULL)
					cpu->cd.x86.rflags |= X86_FLAGS_CF
					    | X86_FLAGS_OF;
			}
			break;
		case 7:	/*  idiv  */
			unsigned_op = 0;
		case 6:	/*  div  */
			success = modrm(cpu, MODRM_READ, mode, mode67,
			    op == 0xf6? MODRM_EIGHTBIT : 0, &instr,
			    &newpc, &op1, &op2);
			if (!success)
				return 0;
			if (op1 == 0) {
				fatal("TODO: division by zero\n");
				cpu->running = 0;
				break;
			}
			if (op == 0xf6) {
				int al, ah;
				if (unsigned_op) {
					al = (cpu->cd.x86.r[X86_R_AX] &
					    0xffff) / op1;
					ah = (cpu->cd.x86.r[X86_R_AX] &
					    0xffff) % op1;
				} else {
					al = (int16_t)(cpu->cd.x86.r[
					    X86_R_AX] & 0xffff) / (int16_t)op1;
					ah = (int16_t)(cpu->cd.x86.r[
					    X86_R_AX] & 0xffff) % (int16_t)op1;
				}
				cpu->cd.x86.r[X86_R_AX] = modify(
				    cpu->cd.x86.r[X86_R_AX], (ah<<8) + al);
			} else if (mode == 16) {
				uint64_t a = (cpu->cd.x86.r[X86_R_AX] & 0xffff)
				    + ((cpu->cd.x86.r[X86_R_DX] & 0xffff)<<16);
				uint32_t ax, dx;
				if (unsigned_op) {
					ax = a / op1, dx = a % op1;
				} else {
					ax = (int32_t)a / (int32_t)op1;
					dx = (int32_t)a % (int32_t)op1;
				}
				cpu->cd.x86.r[X86_R_AX] = modify(
				    cpu->cd.x86.r[X86_R_AX], ax);
				cpu->cd.x86.r[X86_R_DX] = modify(
				    cpu->cd.x86.r[X86_R_DX], dx);
			} else if (mode == 32) {
				uint64_t a = (cpu->cd.x86.r[X86_R_AX] &
				    0xffffffffULL) + ((cpu->cd.x86.r[
				    X86_R_DX] & 0xffffffffULL) << 32);
				uint32_t eax, edx;
				if (unsigned_op) {
					eax = (uint64_t)a / (uint32_t)op1;
					edx = (uint64_t)a % (uint32_t)op1;
				} else {
					eax = (int64_t)a / (int32_t)op1;
					edx = (int64_t)a % (int32_t)op1;
				}
				cpu->cd.x86.r[X86_R_AX] = eax;
				cpu->cd.x86.r[X86_R_DX] = edx;
			}
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op, *instr);
			quiet_mode = 0;
			x86_cpu_disassemble_instr(cpu,
			    really_orig_instr, 1|omode, 0, 0);
			cpu->running = 0;
		}
	} else if (op == 0xfe || op == 0xff) {		/*  INC, DEC etc  */
		int old_cf;
		switch ((*instr >> 3) & 0x7) {
		case 0:
		case 1:	instr_orig = instr;
			success = modrm(cpu, MODRM_READ, mode, mode67,
			    op == 0xfe? MODRM_EIGHTBIT : 0, &instr,
			    &newpc, &op1, &op2);
			if (!success)
				return 0;
			old_cf = cpu->cd.x86.rflags & X86_FLAGS_CF;
			switch ((*instr_orig >> 3) & 0x7) {
			case 0:	x86_calc_flags(cpu, op1, 1, op==0xfe? 8 : mode,
				    CALCFLAGS_OP_ADD);
				op1 ++;
				break; /* inc */
			case 1:	x86_calc_flags(cpu, op1, 1, op==0xfe? 8 : mode,
				    CALCFLAGS_OP_SUB);
				op1 --;
				break; /* dec */
			}
			success = modrm(cpu, MODRM_WRITE_RM, mode, mode67,
			    op == 0xfe? MODRM_EIGHTBIT : 0, &instr_orig,
			    NULL, &op1, &op2);
			if (!success)
				return 0;
			/*  preserve CF:  */
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
			cpu->cd.x86.rflags |= old_cf;
			break;
		case 2:	if (op == 0xfe) {
				fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op,
				    *instr);
				quiet_mode = 0;
				x86_cpu_disassemble_instr(cpu,
				    really_orig_instr, 1|omode, 0, 0);
				cpu->running = 0;
			} else {
				success = modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &newpc, &op1, &op2);
				if (!success)
					return 0;
				/*  Push return [E]IP  */
				if (!x86_push(cpu, newpc, mode))
					return 0;
				newpc = op1;
			}
			break;
		case 3:	if (op == 0xfe) {
				fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op,
				    *instr);
				quiet_mode = 0;
				x86_cpu_disassemble_instr(cpu,
				    really_orig_instr, 1|omode, 0, 0);
				cpu->running = 0;
			} else {
				uint16_t old_tr = cpu->cd.x86.tr;
				uint64_t tmp1, tmp2;
				success = modrm(cpu, MODRM_READ, mode, mode67,
				    MODRM_JUST_GET_ADDR, &instr,
				    &newpc, &op1, &op2);
				if (!success)
					return 0;
				/*  Load a far address from op1:  */
				if (!x86_load(cpu, op1, &tmp1, mode/8))
					return 0;
				if (!x86_load(cpu, op1 + (mode/8), &tmp2, 2))
					return 0;
				/*  Push return CS:[E]IP  */
				if (!x86_push(cpu, cpu->cd.x86.s[X86_S_CS],
				    mode))
					return 0;
				if (!x86_push(cpu, newpc, mode)) {
					fatal("TODO: push failed, call "
					    "far indirect?\n");
					cpu->running = 0;
					return 0;
				}
				reload_segment_descriptor(cpu, X86_S_CS,
				    tmp2, &newpc);
				if (cpu->cd.x86.tr == old_tr)
					newpc = tmp1;
			}
			break;
		case 4:	if (op == 0xfe) {
				fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op,
				    *instr);
				quiet_mode = 0;
				x86_cpu_disassemble_instr(cpu,
				    really_orig_instr, 1|omode, 0, 0);
				cpu->running = 0;
			} else {
				success = modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &newpc, &op1, &op2);
				if (!success)
					return 0;
				newpc = op1;
			}
			break;
		case 5:	if (op == 0xfe) {
				fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op,
				    *instr);
				quiet_mode = 0;
				x86_cpu_disassemble_instr(cpu,
				    really_orig_instr, 1|omode, 0, 0);
				cpu->running = 0;
			} else {
				uint16_t old_tr = cpu->cd.x86.tr;
				uint64_t tmp1, tmp2;
				success = modrm(cpu, MODRM_READ, mode, mode67,
				    MODRM_JUST_GET_ADDR, &instr,
				    &newpc, &op1, &op2);
				if (!success)
					return 0;
				/*  Load a far address from op1:  */
				if (!x86_load(cpu, op1, &tmp1, mode/8))
					return 0;
				if (!x86_load(cpu, op1 + (mode/8), &tmp2, 2))
					return 0;
				reload_segment_descriptor(cpu, X86_S_CS,
				    tmp2, &newpc);
				if (cpu->cd.x86.tr == old_tr)
					newpc = tmp1;
			}
			break;
		case 6:	if (op == 0xfe) {
				fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op,
				    *instr);
				quiet_mode = 0;
				x86_cpu_disassemble_instr(cpu,
				    really_orig_instr, 1|omode, 0, 0);
				cpu->running = 0;
			} else {
				instr_orig = instr;
				success = modrm(cpu, MODRM_READ, mode, mode67,
				    0, &instr, &newpc, &op1, &op2);
				if (!success)
					return 0;
				if (!x86_push(cpu, op1, mode))
					return 0;
			}
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x,0x%02x\n", op, *instr);
			quiet_mode = 0;
			x86_cpu_disassemble_instr(cpu,
			    really_orig_instr, 1|omode, 0, 0);
			cpu->running = 0;
		}
	} else {
		fatal("x86_cpu_run_instr(): unimplemented opcode 0x%02x"
		    " at ", op); print_csip(cpu); fatal("\n");
		quiet_mode = 0;
		x86_cpu_disassemble_instr(cpu,
		    really_orig_instr, 1|omode, 0, 0);
		cpu->running = 0;
		return 0;
	}

	/*  Wrap-around and update [E]IP:  */
	cpu->pc = newpc & (((uint64_t)1 << (cpu->cd.x86.descr_cache[
	    X86_S_CS].default_op_size)) - 1);

	if (trap_flag_was_set) {
		if (REAL_MODE) {
			x86_interrupt(cpu, 1, 0);
		} else {
			fatal("TRAP flag in protected mode?\n");
			cpu->running = 0;
		}
	}

	return 1;
}


#define CPU_RUN         x86_cpu_run
#define CPU_RINSTR      x86_cpu_run_instr
#define CPU_RUN_X86
#include "cpu_run.c"
#undef CPU_RINSTR
#undef CPU_RUN_X86
#undef CPU_RUN


/*
 *  x86_cpu_family_init():
 *
 *  Fill in the cpu_family struct for x86.
 */
int x86_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "x86";
	fp->cpu_new = x86_cpu_new;
	fp->list_available_types = x86_cpu_list_available_types;
	fp->register_match = x86_cpu_register_match;
	fp->disassemble_instr = x86_cpu_disassemble_instr;
	fp->register_dump = x86_cpu_register_dump;
	fp->run = x86_cpu_run;
	fp->dumpinfo = x86_cpu_dumpinfo;
	/*  fp->show_full_statistics = x86_cpu_show_full_statistics;  */
	/*  fp->tlbdump = x86_cpu_tlbdump;  */
	fp->interrupt = x86_cpu_interrupt;
	fp->interrupt_ack = x86_cpu_interrupt_ack;
	return 1;
}

#endif	/*  ENABLE_X86  */
