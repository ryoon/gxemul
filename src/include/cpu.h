#ifndef	CPU_H
#define	CPU_H

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
 *  $Id: cpu.h,v 1.27 2005-06-27 10:43:17 debug Exp $
 *
 *  See cpu.c.
 */


#include <sys/types.h>
#include <inttypes.h>
#include <sys/time.h>

/*  This is needed for undefining 'mips' or 'ppc', on weird systems:  */
#include "../../config.h"

#include "cpu_arm.h"
#include "cpu_mips.h"
#include "cpu_ppc.h"
#include "cpu_urisc.h"
#include "cpu_x86.h"

struct cpu;
struct emul;
struct machine;
struct memory;


struct cpu_family {
	struct cpu_family	*next;
	int			arch;

	/*  These are filled in by each CPU family's init function:  */
	char			*name;
	int			(*cpu_new)(struct cpu *cpu, struct memory *mem,
				    struct machine *machine,
				    int cpu_id, char *cpu_type_name);
	void			(*list_available_types)(void);
	void			(*register_match)(struct machine *m,
				    char *name, int writeflag,
				    uint64_t *valuep, int *match_register);
	int			(*disassemble_instr)(struct cpu *cpu,
				    unsigned char *instr, int running,
				    uint64_t dumpaddr, int bintrans);
	void			(*register_dump)(struct cpu *cpu,
				    int gprs, int coprocs);
	int			(*run)(struct emul *emul,
				    struct machine *machine);
	void			(*dumpinfo)(struct cpu *cpu);
	void			(*show_full_statistics)(struct machine *m);
	void			(*tlbdump)(struct machine *m, int x,
				    int rawflag);
	int			(*interrupt)(struct cpu *cpu, uint64_t irq_nr);
	int			(*interrupt_ack)(struct cpu *cpu,
				    uint64_t irq_nr);
};

#ifdef TRACE_NULL_CRASHES
#define	TRACE_NULL_N_ENTRIES		16
#endif

struct cpu {
	/*  Pointer back to the machine this CPU is in:  */
	struct machine	*machine;

	int		byte_order;
	int		running;
	int		dead;
	int		bootstrap_cpu_flag;
	int		cpu_id;
	char		*name;

	struct memory	*mem;
	int		(*memory_rw)(struct cpu *cpu,
			    struct memory *mem, uint64_t vaddr,
			    unsigned char *data, size_t len,
			    int writeflag, int cache_flags);
	int		(*translate_address)(struct cpu *, uint64_t vaddr,
			    uint64_t *return_addr, int flags);
	void		(*useremul_syscall)(struct cpu *cpu,
			    uint32_t code);

	/*  Things that all CPU families have:  */
	uint64_t	pc;

#ifdef TRACE_NULL_CRASHES
	uint64_t	trace_null_addr[TRACE_NULL_N_ENTRIES];
	int		trace_null_index;
#endif  

	/*  CPU-family dependant:  */
	union {
		struct arm_cpu     arm;
		struct mips_cpu    mips;
		struct ppc_cpu     ppc;
		struct urisc_cpu   urisc;
		struct x86_cpu     x86;
	} cd;
};


/*  cpu.c:  */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *cpu_type_name);
void cpu_show_full_statistics(struct machine *m);
void cpu_tlbdump(struct machine *m, int x, int rawflag);
void cpu_register_match(struct machine *m, char *name, 
	int writeflag, uint64_t *valuep, int *match_register);
void cpu_register_dump(struct machine *m, struct cpu *cpu,
	int gprs, int coprocs);
int cpu_disassemble_instr(struct machine *m, struct cpu *cpu,
	unsigned char *instr, int running, uint64_t addr, int bintrans);
int cpu_interrupt(struct cpu *cpu, uint64_t irq_nr);
int cpu_interrupt_ack(struct cpu *cpu, uint64_t irq_nr);
void cpu_run_init(struct emul *emul, struct machine *machine);
int cpu_run(struct emul *emul, struct machine *machine);
void cpu_run_deinit(struct emul *emul, struct machine *machine);
void cpu_dumpinfo(struct machine *m, struct cpu *cpu);
void cpu_list_available_types(void);
void cpu_show_cycles(struct machine *machine, int forced);
struct cpu_family *cpu_family_ptr_by_number(int arch);
void cpu_init(void);


#endif	/*  CPU_H  */
