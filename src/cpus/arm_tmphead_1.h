#ifndef ARM_TMPHEAD_1
#define ARM_TMPHEAD_1
#define Y(n) void arm_instr_ ## n ## __eq(struct cpu *cpu,		\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_Z)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ne(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __cs(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_C)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __cc(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_C))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __mi(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_N)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __pl(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_N))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __vs(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_V)				\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __vc(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (!(cpu->cd.arm.cpsr & ARM_FLAG_V))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __hi(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_C &&				\
		!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ls(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (cpu->cd.arm.cpsr & ARM_FLAG_Z ||				\
		!(cpu->cd.arm.cpsr & ARM_FLAG_C))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __ge(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==			\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __lt(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=			\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __gt(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) ==			\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) &&		\
		!(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}		\
	void arm_instr_ ## n ## __le(struct cpu *cpu,			\
			struct arm_instr_call *ic)			\
	{  if (((cpu->cd.arm.cpsr & ARM_FLAG_N)?1:0) !=			\
		((cpu->cd.arm.cpsr & ARM_FLAG_V)?1:0) ||		\
		(cpu->cd.arm.cpsr & ARM_FLAG_Z))			\
		arm_instr_ ## n (cpu, ic);		}
#endif	/*  ARM_TMPHEAD_1  */
