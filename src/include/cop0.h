#ifndef	COP0_H
#define	COP0_H

/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: cop0.h,v 1.1 2004-12-06 13:15:05 debug Exp $
 *
 *  Misc. definitions for coprocessor 0.
 */


/*  TODO:  Coproc registers are actually CPU dependant, so an R4000
	has other bits/registers than an R3000...
    TODO 2: CPUs like the R10000 are probably even a bit more different.  */

/*  Coprocessor 0's registers:  */
#define	COP0_NAMES	{ "INDEX", "RANDOM", "ENTRYLO0", "ENTRYLO1", \
			  "CONTEXT", "PAGEMASK", "WIRED", "RESERVED_7", \
			  "BADVADDR", "COUNT", "ENTRYHI", "COMPARE", \
			  "STATUS", "CAUSE", "EPC", "PRID", \
			  "CONFIG", "LLADDR", "WATCHLO", "WATCHHI", \
			  "XCONTEXT", "RESERVED_21", "RESERVED_22", "DEBUG", \
			  "DEPC", "PERFCNT", "ERRCTL", "CACHEERR", \
			  "TAGDATA_LO", "TAGDATA_HI", "ERROREPC", "DESAVE" \
			}
#define	COP0_INDEX		0
#define	   INDEX_P		    0x80000000UL	/*  Probe failure bit. Set by tlbp  */
#define	   INDEX_MASK		    0x3f
#define	   R2K3K_INDEX_P	    0x80000000UL
#define	   R2K3K_INDEX_MASK	    0x3f00
#define	   R2K3K_INDEX_SHIFT	    8
#define	COP0_RANDOM		1
#define	   RANDOM_MASK		    0x3f
#define	   R2K3K_RANDOM_MASK	    0x3f00
#define	   R2K3K_RANDOM_SHIFT	    8
#define	COP0_ENTRYLO0		2
#define	COP0_ENTRYLO1		3
/*  R4000 ENTRYLO:  */
#define	   ENTRYLO_PFN_MASK	    0x3fffffc0
#define	   ENTRYLO_PFN_SHIFT	    6
#define	   ENTRYLO_C_MASK	    0x00000038		/*  Coherency attribute  */
#define	   ENTRYLO_C_SHIFT	    3
#define	   ENTRYLO_D		    0x04		/*  Dirty bit  */
#define	   ENTRYLO_V		    0x02		/*  Valid bit  */
#define	   ENTRYLO_G		    0x01		/*  Global bit  */
/*  R2000/R3000 ENTRYLO:  */
#define	   R2K3K_ENTRYLO_PFN_MASK   0xfffff000UL
#define	   R2K3K_ENTRYLO_PFN_SHIFT  12
#define	   R2K3K_ENTRYLO_N	    0x800
#define	   R2K3K_ENTRYLO_D	    0x400
#define	   R2K3K_ENTRYLO_V	    0x200
#define	   R2K3K_ENTRYLO_G	    0x100
#define	COP0_CONTEXT		4
#define	   CONTEXT_BADVPN2_MASK	    0x007ffff0
#define	   CONTEXT_BADVPN2_SHIFT    4
#define	   R2K3K_CONTEXT_BADVPN_MASK	 0x001ffffc
#define	   R2K3K_CONTEXT_BADVPN_SHIFT    2
#define	COP0_PAGEMASK		5
#define	   PAGEMASK_MASK	    0x01ffe000
#define	   PAGEMASK_SHIFT	    13
#define	COP0_WIRED		6
#define	COP0_RESERVED_7		7
#define	COP0_BADVADDR		8
#define	COP0_COUNT		9
#define	COP0_ENTRYHI		10
/*  R4000 ENTRYHI:  */
#define	   ENTRYHI_R_MASK	    0xc000000000000000ULL
#define	   ENTRYHI_R_SHIFT	    62
#define	   ENTRYHI_VPN2_MASK_R10K   0x00000fffffffe000ULL
#define	   ENTRYHI_VPN2_MASK	    0x000000ffffffe000ULL
#define	   ENTRYHI_VPN2_SHIFT	    13
#define	   ENTRYHI_ASID		    0xff
#define	   TLB_G		    (1 << 12)
/*  R2000/R3000 ENTRYHI:  */
#define	   R2K3K_ENTRYHI_VPN_MASK   0xfffff000UL
#define	   R2K3K_ENTRYHI_VPN_SHIFT  12
#define	   R2K3K_ENTRYHI_ASID_MASK  0xfc0
#define	   R2K3K_ENTRYHI_ASID_SHIFT 6
#define	COP0_COMPARE		11
#define	COP0_STATUS		12
#define	   STATUS_CU_MASK	    0xf0000000UL	/*  coprocessor usable bits  */
#define	   STATUS_CU_SHIFT	    28
#define	   STATUS_RP		    0x08000000		/*  reduced power  */
#define	   STATUS_FR		    0x04000000		/*  1=32 float regs, 0=16  */
#define	   STATUS_RE		    0x02000000		/*  reverse endian bit  */
#define	   STATUS_BEV		    0x00400000		/*  boot exception vectors (?)  */
/*  STATUS_DS: TODO  */
#define	   STATUS_IM_MASK	    0xff00
#define	   STATUS_IM_SHIFT	    8
#define	   STATUS_KX		    0x80
#define	   STATUS_SX		    0x40
#define	   STATUS_UX		    0x20
#define	   STATUS_KSU_MASK	    0x18
#define	   STATUS_KSU_SHIFT	    3
#define	   STATUS_ERL		    0x04
#define	   STATUS_EXL		    0x02
#define	   STATUS_IE		    0x01
#define	   R5900_STATUS_EIE	    0x10000
#define	COP0_CAUSE		13
#define	   CAUSE_BD		    0x80000000UL	/*  branch delay flag  */
#define	   CAUSE_CE_MASK	    0x30000000		/*  which coprocessor  */
#define	   CAUSE_CE_SHIFT	    28
#define	   CAUSE_IP_MASK	    0xff00		/*  interrupt pending  */
#define	   CAUSE_IP_SHIFT	    8
#define    CAUSE_EXCCODE_MASK	    0x7c		/*  exception code  */
#define    R2K3K_CAUSE_EXCCODE_MASK 0x3c
#define	   CAUSE_EXCCODE_SHIFT	    2
#define	COP0_EPC		14
#define	COP0_PRID		15
#define	COP0_CONFIG		16
#define	COP0_LLADDR		17
#define	COP0_WATCHLO		18
#define	COP0_WATCHHI		19
#define	COP0_XCONTEXT		20
#define	   XCONTEXT_R_MASK          0x180000000ULL
#define	   XCONTEXT_R_SHIFT         31
#define	   XCONTEXT_BADVPN2_MASK    0x7ffffff0
#define	   XCONTEXT_BADVPN2_SHIFT   4
#define	COP0_FRAMEMASK		21		/*  R10000  */
#define	COP0_RESERVED_22	22
#define	COP0_DEBUG		23
#define	COP0_DEPC		24
#define	COP0_PERFCNT		25
#define	COP0_ERRCTL		26
#define	COP0_CACHEERR		27
#define	COP0_TAGDATA_LO		28
#define	COP0_TAGDATA_HI		29
#define	COP0_ERROREPC		30
#define	COP0_DESAVE		31

/*  Coprocessor 1's registers:  */
#define	COP1_REVISION		0
#define	  COP1_REVISION_MIPS3D	    0x80000		/*  MIPS3D support  */
#define	  COP1_REVISION_PS	    0x40000		/*  Paired-single support  */
#define	  COP1_REVISION_DOUBLE	    0x20000		/*  double precision support  */
#define	  COP1_REVISION_SINGLE	    0x10000		/*  single precision support  */
#define	COP1_CONTROLSTATUS	31

/*  CP0's STATUS KSU values:  */
#define	KSU_KERNEL		0
#define	KSU_SUPERVISOR		1
#define	KSU_USER		2

#define	EXCEPTION_NAMES		{ \
	"INT", "MOD", "TLBL", "TLBS", "ADEL", "ADES", "IBE", "DBE",	\
	"SYS", "BP", "RI", "CPU", "OV", "TR", "VCEI", "FPE",		\
	"16?", "17?", "18?", "19?", "20?", "21?", "22?", "WATCH",	\
	"24?", "25?", "26?", "27?", "28?", "29?", "30?", "VCED" }

/*  CP0's CAUSE exception codes:  */
#define	EXCEPTION_INT		0	/*  Interrupt  */
#define	EXCEPTION_MOD		1	/*  TLB modification exception  */
#define	EXCEPTION_TLBL		2	/*  TLB exception (load or instruction fetch)  */
#define	EXCEPTION_TLBS		3	/*  TLB exception (store)  */
#define	EXCEPTION_ADEL		4	/*  Address Error Exception (load/instr. fetch)  */
#define	EXCEPTION_ADES		5	/*  Address Error Exception (store)  */
#define	EXCEPTION_IBE		6	/*  Bus Error Exception (instruction fetch)  */
#define	EXCEPTION_DBE		7	/*  Bus Error Exception (data: load or store)  */
#define	EXCEPTION_SYS		8	/*  Syscall  */
#define	EXCEPTION_BP		9	/*  Breakpoint  */
#define	EXCEPTION_RI		10	/*  Reserved instruction  */
#define	EXCEPTION_CPU		11	/*  CoProcessor Unusable  */
#define	EXCEPTION_OV		12	/*  Arithmetic Overflow  */
#define	EXCEPTION_TR		13	/*  Trap exception  */
#define	EXCEPTION_VCEI		14	/*  Virtual Coherency Exception, Instruction  */
#define	EXCEPTION_FPE		15	/*  Floating point exception  */
/*  16..22: Unused  */
#define	EXCEPTION_WATCH		23	/*  Reference to WatchHi/WatchLo address  */
/*  24..30: Unused  */
#define	EXCEPTION_VCED		31	/*  Virtual Coherency Exception, Data  */


#endif	/*  COP0_H  */

