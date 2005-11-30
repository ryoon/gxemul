/*  $Id: ppc_quick_pc_to_pointers.h,v 1.1 2005-11-30 08:52:44 debug Exp $  */

#ifdef quick_pc_to_pointers
#undef quick_pc_to_pointers
#endif

#ifdef MODE32
#define	quick_pc_to_pointers(cpu) {					\
	uint32_t pc = cpu->pc;						\
	struct ppc_tc_physpage *ppp;					\
	ppp = cpu->cd.ppc.phys_page[pc >> 12];				\
	if (ppp != NULL) {						\
		cpu->cd.ppc.cur_ic_page = &ppp->ics[0];			\
		cpu->cd.ppc.next_ic = cpu->cd.ppc.cur_ic_page +		\
		    PPC_PC_TO_IC_ENTRY(pc);				\
	} else								\
		DYNTRANS_PC_TO_POINTERS(cpu);				\
}
#else
#define quick_pc_to_pointers(cpu)	DYNTRANS_PC_TO_POINTERS(cpu)
#endif

