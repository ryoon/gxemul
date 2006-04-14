#ifndef	OPCODES_H
#define	OPCODES_H

/*
 *  Copyright (C) 2003-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: opcodes_mips.h,v 1.4 2006-04-14 18:00:30 debug Exp $
 *
 *  MIPS opcodes, gathered from various sources.
 */


/*  Opcodes:  (see page 191 in MIPS_IV_Instruction_Set_v3.2.pdf)  */

#define	HI6_NAMES	{	\
	"special", "regimm", "j", "jal", "beq", "bne", "blez", "bgtz", 			/*  0x00 - 0x07  */	\
	"addi", "addiu", "slti", "sltiu", "andi", "ori", "xori", "lui",			/*  0x08 - 0x0f  */	\
	"cop0", "cop1", "cop2", "cop3", "beql", "bnel", "blezl", "bgtzl",		/*  0x10 - 0x17  */	\
	"daddi", "daddiu", "ldl", "ldr", "special2", "opcode_1d", "lq_mdmx", "special3",	/*  0x18 - 0x1f  */	\
	"lb", "lh", "lwl", "lw", "lbu", "lhu", "lwr", "lwu",				/*  0x20 - 0x27  */	\
	"sb", "sh", "swl", "sw", "sdl", "sdr", "swr", "cache",				/*  0x28 - 0x2f  */	\
	"ll", "lwc1", "lwc2", "lwc3", "lld", "ldc1", "ldc2", "ld",			/*  0x30 - 0x37  */	\
	"sc", "swc1", "swc2", "swc3", "scd", "sdc1", "sdc2", "sd"			/*  0x38 - 0x3f  */	}

#define	REGIMM_NAMES	{	\
	"bltz", "bgez", "bltzl", "bgezl", "regimm_04", "regimm_05", "regimm_06", "regimm_07",			/*  0x00 - 0x07  */	\
	"regimm_08", "regimm_09", "regimm_0a", "regimm_0b", "regimm_0c", "regimm_0d", "regimm_0e", "regimm_0f",	/*  0x08 - 0x0f  */	\
	"bltzal", "bgezal", "bltzall", "bgezall", "regimm_14", "regimm_15", "regimm_16", "regimm_17",		/*  0x10 - 0x17  */	\
	"regimm_18", "regimm_19", "regimm_1a", "regimm_1b", "regimm_1c", "regimm_1d", "regimm_1e", "regimm_1f" 	/*  0x18 - 0x1f  */ }

#define	SPECIAL_NAMES	{	\
	"sll", "special_01", "srl", "sra", "sllv", "special_05", "srlv", "srav",	/*  0x00 - 0x07  */	\
	"jr", "jalr", "movz", "movn", "syscall", "break", "special_0e", "sync",		/*  0x08 - 0x0f  */	\
	"mfhi", "mthi", "mflo", "mtlo", "dsllv", "special_15", "dsrlv", "dsrav",	/*  0x10 - 0x17  */	\
	"mult", "multu", "div", "divu", "dmult", "dmultu", "ddiv", "ddivu",		/*  0x18 - 0x1f  */	\
	"add", "addu", "sub", "subu", "and", "or", "xor", "nor",			/*  0x20 - 0x27  */	\
	"mfsa", "mtsa", "slt", "sltu", "special_2c", "daddu", "special_2e", "dsubu",  /*  0x28 - 0x2f  */	\
	"special_30", "special_31", "special_32", "special_33", "teq", "special_35", "special_36", "special_37", /*  0x30 - 0x37  */	\
	"dsll", "special_39", "dsrl", "dsra", "dsll32", "special_3d", "dsrl32", "dsra32"/*  0x38 - 0x3f  */	}

#define	SPECIAL2_NAMES	{	\
	"madd",        "maddu",       "mul",         "special2_03", "msub",        "msubu",       "special2_06", "special2_07", /*  0x00 - 0x07  */	\
	"mov_xxx",     "pmfhi_lo",    "special2_0a", "special2_0b", "special2_0c", "special2_0d", "special2_0e", "special2_0f",	/*  0x08 - 0x0f  */	\
	"special2_10", "special2_11", "special2_12", "special2_13", "special2_14", "special2_15", "special2_16", "special2_17", /*  0x10 - 0x17  */	\
	"special2_18", "special2_19", "special2_1a", "special2_1b", "special2_1c", "special2_1d", "special2_1e", "special2_1f",	/*  0x18 - 0x1f  */	\
	"clz",         "clo",         "special2_22", "special2_23", "dclz",        "dclo",        "special2_26", "special2_27", /*  0x20 - 0x27  */	\
	"special2_28", "por", 	      "special2_2a", "special2_2b", "special2_2c", "special2_2d", "special2_2e", "special2_2f",	/*  0x28 - 0x2f  */	\
	"special2_30", "special2_31", "special2_32", "special2_33", "special2_34", "special2_35", "special2_36", "special2_37", /*  0x30 - 0x37  */	\
	"special2_38", "special2_39", "special2_3a", "special2_3b", "special2_3c", "special2_3d", "special2_3e", "sdbbp"	/*  0x38 - 0x3f  */  }

#define	HI6_SPECIAL			0x00	/*  000000  */
#define	    SPECIAL_SLL			    0x00    /*  000000  */	/*  MIPS I  */
/*					    0x01	000001  */
#define	    SPECIAL_SRL			    0x02    /*	000010  */	/*  MIPS I  */
#define	    SPECIAL_SRA			    0x03    /*  000011  */	/*  MIPS I  */
#define	    SPECIAL_SLLV		    0x04    /*  000100  */	/*  MIPS I  */
/*					    0x05	000101  */
#define	    SPECIAL_SRLV		    0x06    /*  000110  */
#define	    SPECIAL_SRAV		    0x07    /*  000111  */	/*  MIPS I  */
#define	    SPECIAL_JR			    0x08    /*  001000  */	/*  MIPS I  */
#define	    SPECIAL_JALR		    0x09    /*  001001  */	/*  MIPS I  */
#define	    SPECIAL_MOVZ		    0x0a    /*	001010  */	/*  MIPS IV  */
#define	    SPECIAL_MOVN		    0x0b    /*	001011  */	/*  MIPS IV  */
#define	    SPECIAL_SYSCALL		    0x0c    /*	001100  */	/*  MIPS I  */
#define	    SPECIAL_BREAK		    0x0d    /*	001101  */	/*  MIPS I  */
/*					    0x0e	001110  */
#define	    SPECIAL_SYNC		    0x0f    /*	001111  */	/*  MIPS II  */
#define	    SPECIAL_MFHI		    0x10    /*  010000  */	/*  MIPS I  */
#define	    SPECIAL_MTHI		    0x11    /*	010001  */	/*  MIPS I  */
#define	    SPECIAL_MFLO		    0x12    /*  010010  */	/*  MIPS I  */
#define	    SPECIAL_MTLO		    0x13    /*	010011  */	/*  MIPS I  */
#define	    SPECIAL_DSLLV		    0x14    /*	010100  */
/*					    0x15	010101  */
#define	    SPECIAL_DSRLV		    0x16    /*  010110  */	/*  MIPS III  */
#define	    SPECIAL_DSRAV		    0x17    /*  010111  */	/*  MIPS III  */
#define	    SPECIAL_MULT		    0x18    /*  011000  */	/*  MIPS I  */
#define	    SPECIAL_MULTU		    0x19    /*	011001  */	/*  MIPS I  */
#define	    SPECIAL_DIV			    0x1a    /*  011010  */	/*  MIPS I  */
#define	    SPECIAL_DIVU		    0x1b    /*	011011  */	/*  MIPS I  */
#define	    SPECIAL_DMULT		    0x1c    /*  011100  */	/*  MIPS III  */
#define	    SPECIAL_DMULTU		    0x1d    /*  011101  */	/*  MIPS III  */
#define	    SPECIAL_DDIV		    0x1e    /*  011110  */	/*  MIPS III  */
#define	    SPECIAL_DDIVU		    0x1f    /*  011111  */	/*  MIPS III  */
#define	    SPECIAL_ADD			    0x20    /*	100000  */	/*  MIPS I  */
#define	    SPECIAL_ADDU		    0x21    /*  100001  */	/*  MIPS I  */
#define	    SPECIAL_SUB			    0x22    /*  100010  */	/*  MIPS I  */
#define	    SPECIAL_SUBU		    0x23    /*  100011  */	/*  MIPS I  */
#define	    SPECIAL_AND			    0x24    /*  100100  */	/*  MIPS I  */
#define	    SPECIAL_OR			    0x25    /*  100101  */	/*  MIPS I  */
#define	    SPECIAL_XOR			    0x26    /*  100110  */	/*  MIPS I  */
#define	    SPECIAL_NOR			    0x27    /*  100111  */	/*  MIPS I  */
#define	    SPECIAL_MFSA		    0x28    /*  101000  */  	/*  Undocumented R5900 ?  */
#define	    SPECIAL_MTSA		    0x29    /*  101001  */  	/*  Undocumented R5900 ?  */
#define	    SPECIAL_SLT			    0x2a    /*  101010  */	/*  MIPS I  */
#define	    SPECIAL_SLTU		    0x2b    /*  101011  */	/*  MIPS I  */
#define	    SPECIAL_DADD		    0x2c    /*  101100  */	/*  MIPS III  */
#define	    SPECIAL_DADDU		    0x2d    /*	101101  */	/*  MIPS III  */
#define	    SPECIAL_DSUB		    0x2e    /*	101110  */
#define	    SPECIAL_DSUBU		    0x2f    /*	101111  */	/*  MIPS III  */
#define	    SPECIAL_TGE			    0x30    /*	110000  */
#define	    SPECIAL_TGEU		    0x31    /*	110001  */
#define	    SPECIAL_TLT			    0x32    /*	110010  */
#define	    SPECIAL_TLTU		    0x33    /*	110011  */
#define	    SPECIAL_TEQ			    0x34    /*	110100  */
/*					    0x35	110101  */
#define	    SPECIAL_TNE			    0x36    /*	110110  */
/*					    0x37	110111  */
#define	    SPECIAL_DSLL		    0x38    /*  111000  */	/*  MIPS III  */
/*					    0x39	111001  */
#define	    SPECIAL_DSRL		    0x3a    /*  111010  */	/*  MIPS III  */
#define	    SPECIAL_DSRA		    0x3b    /*  111011  */	/*  MIPS III  */
#define	    SPECIAL_DSLL32		    0x3c    /*  111100  */	/*  MIPS III  */
/*					    0x3d	111101  */
#define	    SPECIAL_DSRL32		    0x3e    /*  111110  */	/*  MIPS III  */
#define	    SPECIAL_DSRA32		    0x3f    /*  111111  */	/*  MIPS III  */

#define	HI6_REGIMM			0x01	/*  000001  */
#define	    REGIMM_BLTZ			    0x00    /*  00000  */	/*  MIPS I  */
#define	    REGIMM_BGEZ			    0x01    /*  00001  */	/*  MIPS I  */
#define	    REGIMM_BLTZL		    0x02    /*  00010  */	/*  MIPS II  */
#define	    REGIMM_BGEZL		    0x03    /*  00011  */	/*  MIPS II  */
#define	    REGIMM_BLTZAL		    0x10    /*  10000  */
#define	    REGIMM_BGEZAL		    0x11    /*  10001  */
#define	    REGIMM_BLTZALL		    0x12    /*  10010  */
#define	    REGIMM_BGEZALL		    0x13    /*  10011  */
/*  regimm ...............  */

#define	HI6_J				0x02	/*  000010  */	/*  MIPS I  */
#define	HI6_JAL				0x03	/*  000011  */	/*  MIPS I  */
#define	HI6_BEQ				0x04	/*  000100  */	/*  MIPS I  */
#define	HI6_BNE				0x05	/*  000101  */
#define	HI6_BLEZ			0x06	/*  000110  */	/*  MIPS I  */
#define	HI6_BGTZ			0x07	/*  000111  */	/*  MIPS I  */
#define	HI6_ADDI			0x08	/*  001000  */	/*  MIPS I  */
#define	HI6_ADDIU			0x09	/*  001001  */	/*  MIPS I  */
#define	HI6_SLTI			0x0a	/*  001010  */	/*  MIPS I  */
#define	HI6_SLTIU			0x0b	/*  001011  */	/*  MIPS I  */
#define	HI6_ANDI			0x0c	/*  001100  */	/*  MIPS I  */
#define	HI6_ORI				0x0d	/*  001101  */	/*  MIPS I  */
#define	HI6_XORI			0x0e    /*  001110  */	/*  MIPS I  */
#define	HI6_LUI				0x0f	/*  001111  */	/*  MIPS I  */
#define	HI6_COP0			0x10	/*  010000  */
#define	    COPz_MFCz			    0x00    /*  00000  */
#define	    COPz_DMFCz			    0x01    /*  00001  */
#define	    COPz_MTCz			    0x04    /*  00100  */
#define	    COPz_DMTCz			    0x05    /*  00101  */
/*  COP1 fmt codes = bits 25..21 (only if COP1):  */
#define	    COPz_CFCz			    0x02    /*  00010  */  /*  MIPS I  */
#define	    COPz_CTCz			    0x06    /*  00110  */  /*  MIPS I  */
/*  COP0 opcodes = bits 4..0 (only if COP0 and CO=1):  */
#define	    COP0_TLBR			    0x01    /*  00001  */
#define	    COP0_TLBWI			    0x02    /*  00010  */
#define	    COP0_TLBWR			    0x06    /*  00110  */
#define	    COP0_TLBP			    0x08    /*  01000  */
#define	    COP0_RFE			    0x10    /*  10000  */
#define	    COP0_ERET			    0x18    /*  11000  */
#define	    COP0_STANDBY		    0x21
#define	    COP0_SUSPEND		    0x22
#define	    COP0_HIBERNATE		    0x23
#define	HI6_COP1			0x11	/*  010001  */
#define	HI6_COP2			0x12	/*  010010  */
#define	HI6_COP3			0x13	/*  010011  */
#define	HI6_BEQL			0x14	/*  010100  */	/*  MIPS II  */
#define	HI6_BNEL			0x15	/*  010101  */
#define	HI6_BLEZL			0x16	/*  010110  */	/*  MIPS II  */
#define	HI6_BGTZL			0x17	/*  010111  */	/*  MIPS II  */
#define	HI6_DADDI			0x18	/*  011000  */	/*  MIPS III  */
#define	HI6_DADDIU			0x19	/*  011001  */	/*  MIPS III  */
#define	HI6_LDL				0x1a	/*  011010  */	/*  MIPS III  */
#define	HI6_LDR				0x1b	/*  011011  */	/*  MIPS III  */
#define	HI6_SPECIAL2			0x1c	/*  011100  */
#define	    SPECIAL2_MADD		    0x00    /*  000000  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MADDU		    0x01    /*  000001  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MUL		    0x02    /*  000010  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MSUB		    0x04    /*  000100  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MSUBU		    0x05    /*  000001  */  /*  MIPS32 (?) TODO  */
#define	    SPECIAL2_MOV_XXX		    0x08    /*  001000  */  /*  Undocumented R5900 ?  */
#define	    SPECIAL2_PMFHI		    0x09    /*  001001  */  /*  Undocumented R5900 ?  */
#define	    SPECIAL2_CLZ		    0x20    /*  100100  */  /*  MIPS32  */
#define	    SPECIAL2_CLO		    0x21    /*  100101  */  /*  MIPS32  */
#define	    SPECIAL2_DCLZ		    0x24    /*  100100  */  /*  MIPS64  */
#define	    SPECIAL2_DCLO		    0x25    /*  100101  */  /*  MIPS64  */
#define	    SPECIAL2_POR		    0x29    /*  101001  */  /*  Undocumented R5900 ?  */
#define	    SPECIAL2_SDBBP		    0x3f    /*  111111  */  /*  EJTAG (?)  TODO  */
/*	JALX (TODO)			0x1d	    011101  */
#define	HI6_LQ_MDMX			0x1e	/*  011110  */	/*  lq on R5900, MDMX on others?  */
#define	HI6_SQ_SPECIAL3			0x1f	/*  011111  */	/*  sq on R5900, SPECIAL3 on MIPS32/64 rev 2  */
#define	HI6_LB				0x20	/*  100000  */	/*  MIPS I  */
#define	HI6_LH				0x21	/*  100001  */	/*  MIPS I  */
#define	HI6_LWL				0x22	/*  100010  */	/*  MIPS I  */
#define	HI6_LW				0x23	/*  100011  */	/*  MIPS I  */
#define	HI6_LBU				0x24	/*  100100  */	/*  MIPS I  */
#define	HI6_LHU				0x25	/*  100101  */	/*  MIPS I  */
#define	HI6_LWR				0x26	/*  100110  */	/*  MIPS I  */
#define	HI6_LWU				0x27	/*  100111  */	/*  MIPS III  */
#define	HI6_SB				0x28	/*  101000  */	/*  MIPS I  */
#define	HI6_SH				0x29	/*  101001  */	/*  MIPS I  */
#define	HI6_SWL				0x2a	/*  101010  */	/*  MIPS I  */
#define	HI6_SW				0x2b	/*  101011  */	/*  MIPS I  */
#define	HI6_SDL				0x2c	/*  101100  */	/*  MIPS III  */
#define	HI6_SDR				0x2d	/*  101101  */	/*  MIPS III  */
#define	HI6_SWR				0x2e	/*  101110  */	/*  MIPS I  */
#define	HI6_CACHE			0x2f	/*  101111  */	/*  ??? R4000  */
#define	HI6_LL				0x30	/*  110000  */	/*  MIPS II  */
#define	HI6_LWC1			0x31	/*  110001  */	/*  MIPS I  */
#define	HI6_LWC2			0x32	/*  110010  */	/*  MIPS I  */
#define	HI6_LWC3			0x33	/*  110011  */	/*  MIPS I  */
#define	HI6_LLD				0x34	/*  110100  */	/*  MIPS III  */
#define	HI6_LDC1			0x35	/*  110101  */	/*  MIPS II  */
#define	HI6_LDC2			0x36	/*  110110  */	/*  MIPS II  */
#define	HI6_LD				0x37	/*  110111  */	/*  MIPS III  */
#define	HI6_SC				0x38	/*  111000  */	/*  MIPS II  */
#define	HI6_SWC1			0x39	/*  111001  */	/*  MIPS I  */
#define	HI6_SWC2			0x3a	/*  111010  */	/*  MIPS I  */
#define	HI6_SWC3			0x3b	/*  111011  */	/*  MIPS I  */
#define	HI6_SCD				0x3c	/*  111100  */	/*  MIPS III  */
#define	HI6_SDC1			0x3d	/*  111101  */  /*  MIPS II  */
#define	HI6_SDC2			0x3e	/*  111110  */  /*  MIPS II  */
#define	HI6_SD				0x3f	/*  111111  */	/*  MIPS III  */


#endif	/*  OPCODES_H  */

