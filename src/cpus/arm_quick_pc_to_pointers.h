#ifndef ARM_QUICK_PC_TO_POINTERS_H
#define ARM_QUICK_PC_TO_POINTERS_H
#define	quick_pc_to_pointers(cpu) {					\
        uint32_t pc = cpu->cd.arm.r[ARM_PC];				\
        struct arm_tc_physpage *ppp;					\
        ppp = cpu->cd.arm.phys_page[pc >> 12];				\
        if (ppp != NULL) {						\
                cpu->cd.arm.cur_ic_page = &ppp->ics[0];			\
                cpu->cd.arm.next_ic = cpu->cd.arm.cur_ic_page +		\
                    ARM_PC_TO_IC_ENTRY(pc);				\
        } else								\
		arm_pc_to_pointers_generic(cpu);			\
}
#endif
