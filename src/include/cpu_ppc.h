#ifndef	CPU_PPC_H
#define	CPU_PPC_H

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
 *  $Id: cpu_ppc.h,v 1.15 2005-02-14 21:44:36 debug Exp $
 */

#include "misc.h"


struct cpu_family;

#define	MODE_PPC		0
#define	MODE_POWER		1

/*  PPC CPU types:  */
struct ppc_cpu_type_def { 
	char		*name;
	int		bits;
	int		icache_shift;
	int		iway;
	int		dcache_shift;
	int		dway;
	int		l2cache_shift;
	int		l2way;

	/*  TODO: 64-bit-ness? POWER vs PowerPC?  */
};

/*  TODO: Most of these just bogus  */

#define PPC_CPU_TYPE_DEFS	{				\
	{ "G4e", 32, 15, 8, 15, 8, 18, 8 },			\
	{ "PPC405GP", 32, 15, 2, 15, 2, 20, 1, },	 	\
	{ "PPC750", 32, 15, 2, 15, 2, 20, 1 },			\
	{ "PPC970", 64, 16, 1, 15, 2, 19, 1 },			\
	{ NULL, 0, 0,0, 0,0, 0,0 }				\
	};

#define	PPC_NGPRS		32
#define	PPC_NFPRS		32

struct ppc_cpu {
	struct ppc_cpu_type_def cpu_type;

	uint64_t	pc;		/*  Program Counter (TODO: CIA?)  */
	uint64_t	pc_last;

	int		mode;		/*  MODE_PPC or MODE_POWER  */
	int		bits;		/*  32 or 64  */

	int		ca;		/*  Carry bit  */
	uint32_t	cr;		/*  Condition Register  */
	uint32_t	fpscr;		/*  FP Status and Control Register  */
	uint64_t	lr;		/*  Link Register  */
	uint64_t	ctr;		/*  Count Register  */
	uint64_t	gpr[PPC_NGPRS];	/*  General Purpose Registers  */
	uint64_t	xer;		/*  FP Exception Register  */
	uint64_t	fpr[PPC_NFPRS];	/*  Floating-Point Registers  */

	uint32_t	tbl;		/*  Time Base Lower  */
	uint32_t	tbu;		/*  Time Base Upper  */
	uint32_t	dec;		/*  Decrementer  */
	uint32_t	hdec;		/*  Hypervisor Decrementer  */
	uint64_t	ssr0;		/*  Machine status save/restore
					    register 0  */
	uint64_t	ssr1;		/*  Machine status save/restore
					    register 1  */
	uint64_t	msr;		/*  Machine state register  */
	uint64_t	sprg0;		/*  Special Purpose Register G0  */
	uint64_t	sprg1;		/*  Special Purpose Register G1  */
	uint64_t	sprg2;		/*  Special Purpose Register G2  */
	uint64_t	sprg3;		/*  Special Purpose Register G3  */
	uint32_t	pvr;		/*  Processor Version Register  */
	uint32_t	pir;		/*  Processor ID  */
};


/*  Machine status word bits: (according to Book 3)  */
#define	PPC_MSR_SF	(1ULL << 63)	/*  Sixty-Four-Bit Mode  */
/*  bits 62..61 are reserved  */
#define	PPC_MSR_HV	(1ULL << 60)	/*  Hypervisor  */
/*  bits 59..17  are reserved  */
#define	PPC_MSR_ILE	(1 << 16)	/*  Interrupt Little-Endian Mode  */
#define	PPC_MSR_EE	(1 << 15)	/*  External Interrupt Enable  */
#define	PPC_MSR_PR	(1 << 14)	/*  Problem State  */
#define	PPC_MSR_FP	(1 << 13)	/*  Floating-Point Available  */
#define	PPC_MSR_ME	(1 << 12)	/*  Machine Check Interrupt Enable  */
#define	PPC_MSR_FE0	(1 << 11)	/*  Floating-Point Exception Mode 0  */
#define	PPC_MSR_SE	(1 << 10)	/*  Single-Step Trace Enable  */
#define	PPC_MSR_BE	(1 << 9)	/*  Branch Trace Enable  */
#define	PPC_MSR_FE1	(1 << 8)	/*  Floating-Point Exception Mode 1  */
#define	PPC_MSR_IR	(1 << 5)	/*  Instruction Relocate  */
#define	PPC_MSR_DR	(1 << 4)	/*  Data Relocate  */
#define	PPC_MSR_PMM	(1 << 2)	/*  Performance Monitor Mark  */
#define	PPC_MSR_RI	(1 << 1)	/*  Recoverable Interrupt  */
#define	PPC_MSR_LE	(1)		/*  Little-Endian Mode  */


/*  cpu_ppc.c:  */
struct cpu *ppc_cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *cpu_type_name);
void ppc_cpu_show_full_statistics(struct machine *m);
void ppc_cpu_register_match(struct machine *m, char *name, 
	int writeflag, uint64_t *valuep, int *match_register);
void ppc_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs);
int ppc_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
        int running, uint64_t addr, int bintrans);
int ppc_cpu_interrupt(struct cpu *cpu, uint64_t irq_nr);
int ppc_cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr);
int ppc_cpu_run(struct emul *emul, struct machine *machine);
void ppc_cpu_dumpinfo(struct cpu *cpu);
void ppc_cpu_list_available_types(void);
int ppc_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
int ppc_cpu_family_init(struct cpu_family *);


#endif	/*  CPU_PPC_H  */
