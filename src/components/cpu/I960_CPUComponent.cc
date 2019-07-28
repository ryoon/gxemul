/*
 *  Copyright (C) 2018  Anders Gavare.  All rights reserved.
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
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <iomanip>

#include "ComponentFactory.h"
#include "GXemul.h"
#include "components/I960_CPUComponent.h"


struct reg_instruction {
	int opcode;
	const char* mnemonic;
	bool has_src1;
	bool has_src2;
	bool has_dst;
	bool has_src3;	// true if the dst/src field is used as a source
};

struct reg_instruction reg_instructions[] = {
	{ 0x580, "notbit",      true,  true,  true,  false },
	{ 0x581, "and",         true,  true,  true,  false },
	{ 0x582, "andnot",      true,  true,  true,  false },
	{ 0x583, "setbit",      true,  true,  true,  false },
	{ 0x584, "notand",      true,  true,  true,  false },
	{ 0x586, "xor",         true,  true,  true,  false },
	{ 0x587, "or",          true,  true,  true,  false },
	{ 0x588, "nor",         true,  true,  true,  false },
	{ 0x589, "xnor",        true,  true,  true,  false },
	{ 0x58a, "not",         true,  false, true,  false },
	{ 0x58b, "ornot",       true,  true,  true,  false },
	{ 0x58c, "clrbit",      true,  true,  true,  false },
	{ 0x58d, "notor",       true,  true,  true,  false },
	{ 0x58e, "nand",        true,  true,  true,  false },
	{ 0x58f, "alterbit",    true,  true,  true,  false },

	{ 0x590, "addo",        true,  true,  true,  false },
	{ 0x591, "addi",        true,  true,  true,  false },
	{ 0x592, "subo",        true,  true,  true,  false },
	{ 0x593, "subi",        true,  true,  true,  false },
	{ 0x598, "shro",        true,  true,  true,  false },
	{ 0x59a, "shrdi",       true,  true,  true,  false },
	{ 0x59b, "shri" ,       true,  true,  true,  false },
	{ 0x59c, "shlo",        true,  true,  true,  false },
	{ 0x59d, "rotate",      true,  true,  true,  false },
	{ 0x59e, "shli",        true,  true,  true,  false },

	{ 0x5a0, "cmpo",        true,  true,  false, false },
	{ 0x5a1, "cmpi",        true,  true,  false, false },
	{ 0x5a2, "concmpo",     true,  true,  false, false },
	{ 0x5a3, "concmpi",     true,  true,  false, false },
	{ 0x5a4, "cmpinco",     true,  true,  true,  false },
	{ 0x5a5, "cmpinci",     true,  true,  true,  false },
	{ 0x5a6, "cmpdeco",     true,  true,  true,  false },
	{ 0x5a7, "cmpdeci",     true,  true,  true,  false },

	{ 0x5ac, "scanbyte",    true,  true,  false, false },
	{ 0x5ae, "chkbit",      true,  true,  false, false },

	{ 0x5b0, "addc",        true,  true,  true,  false },
	{ 0x5b2, "subc",        true,  true,  true,  false },

	{ 0x5cc, "mov",         true,  false, true,  false },
	{ 0x5d8, "eshro",       true,  true,  true,  false },
	{ 0x5dc, "movl",        true,  false, true,  false },
	{ 0x5ec, "movt",        true,  false, true,  false },
	{ 0x5fc, "movq",        true,  false, true,  false },

	{ 0x630, "sdma",        true,  true,  true,  true  },
	{ 0x631, "udma",        false, false, false, false },

	{ 0x640, "spanbit",     true,  false, true,  false },
	{ 0x641, "scanbit",     true,  false, true,  false },
	{ 0x645, "modac",       true,  true,  true,  true  },

	{ 0x650, "modify",      true,  true,  true,  true  },
	{ 0x651, "extract",     true,  true,  true,  true  },
	{ 0x654, "modtc",       true,  true,  true,  true  },
	{ 0x655, "modpc",       true,  true,  true,  true  },
	{ 0x659, "sysctl",      true,  true,  true,  true  },

	{ 0x660, "calls",       true,  false, false, false },
	{ 0x66b, "mark",        false, false, false, false },
	{ 0x66c, "fmark",       false, false, false, false },
	{ 0x66d, "flushreg",    false, false, false, false },
	{ 0x66f, "syncf",       false, false, false, false },

	{ 0x670, "emul",        true,  true,  true,  false },
	{ 0x671, "ediv",        true,  true,  true,  false },

	{ 0x701, "mulo",        true,  true,  true,  false },
	{ 0x708, "remo",        true,  true,  true,  false },
	{ 0x70b, "divo",        true,  true,  true,  false },

	{ 0x741, "muli",        true,  true,  true,  false },
	{ 0x748, "remi",        true,  true,  true,  false },
	{ 0x749, "modi",        true,  true,  true,  false },
	{ 0x74b, "divi",        true,  true,  true,  false },

	{ 0,     NULL,          false, false, false, false }
};


I960_CPUComponent::I960_CPUComponent()
	: CPUDyntransComponent("i960_cpu", "i960")
{
	m_frequency = 25e6;
	m_isBigEndian = false;
	m_model = "i960CA";

	ResetState();

	AddVariable("model", &m_model);

	for (size_t i = 0; i < N_I960_REGS; i++) {
		AddVariable(i960_regnames[i], &m_r[i]);
	}

	for (size_t i = 0; i < N_I960_SFRS; i++) {
		stringstream ss;
		ss << "sfr" << i;
		AddVariable(ss.str(), &m_sfr[i]);
	}

	AddVariable("i960_ac", &m_i960_ac);
	AddVariable("i960_pc", &m_i960_pc);
	AddVariable("i960_tc", &m_i960_tc);
	AddVariable("nr_of_valid_sfrs", &m_nr_of_valid_sfrs);
}


refcount_ptr<Component> I960_CPUComponent::Create(const ComponentCreateArgs& args)
{
	// Defaults:
	ComponentCreationSettings settings;
	settings["model"] = "i960CA";

	if (!ComponentFactory::GetCreationArgOverrides(settings, args))
		return NULL;

	refcount_ptr<Component> cpu = new I960_CPUComponent();
	if (!cpu->SetVariableValue("model", "\"" + settings["model"] + "\""))
		return NULL;

	return cpu;
}


static string regname_or_literal(int reg, int m, int s)
{
	// Regular g or r registers
	if (m == 0 && s == 0)
		return i960_regnames[reg];

	stringstream ss;

	if (m != 0 && s == 0) {
		// Literal
		ss << reg;
	} else if (m == 0 && s != 0) {
		// Special Function Register
		ss << "sfr" << reg;
	} else {
		ss << "reserved" << reg;
	}
	
	return ss.str();
}


void I960_CPUComponent::ResetState()
{
	m_pageSize = 4096;

	for (size_t i=0; i<N_I960_REGS; i++)
		m_r[i] = 0;

	for (size_t i=0; i<N_I960_SFRS; i++)
		m_sfr[i] = 0;

	m_pc = 0;

	// 0 for most (?) i960 implementations. 3 for i960CA. (TODO: CF etc)
	m_nr_of_valid_sfrs = 0;
	if (m_model == "i960CA")
		m_nr_of_valid_sfrs = 3;

	CPUDyntransComponent::ResetState();
}


bool I960_CPUComponent::PreRunCheckForComponent(GXemul* gxemul)
{
	if (m_pc & 0x3) {
		gxemul->GetUI()->ShowDebugMessage(this, "the pc register"
		    " can not have bit 0 or 1 set!\n");
		return false;
	}

	return CPUDyntransComponent::PreRunCheckForComponent(gxemul);
}


bool I960_CPUComponent::CheckVariableWrite(StateVariable& var, const string& oldValue)
{
	// UI* ui = GetUI();

	return CPUDyntransComponent::CheckVariableWrite(var, oldValue);
}


void I960_CPUComponent::ShowRegisters(GXemul* gxemul, const vector<string>& arguments) const
{
	stringstream ss;

	ss.flags(std::ios::hex);
	ss << "  ip = 0x" << std::setfill('0') << std::setw(8) << (uint32_t)m_pc;

	string symbol = GetSymbolRegistry().LookupAddress(m_pc, true);
	if (symbol != "")
		ss << " <" << symbol << ">";
	ss << "\n";

	for (size_t i = 0; i < N_I960_REGS; i++) {
		ss << std::setfill(' ') << std::setw(4) << i960_regnames[i]
			<< " = 0x" << std::setfill('0') << std::setw(8) << m_r[i];
		if ((i&3) == 3)
			ss << "\n";
		else
			ss << " ";
	}

	for (size_t i = 0; i < m_nr_of_valid_sfrs; i++) {
		stringstream name;
		name << "sfr" << i;
		ss << std::setfill(' ') << std::setw(6) << name.str()
			<< " = 0x" << std::setfill('0') << std::setw(8) << m_sfr[i];
		if ((i&3) == 3)
			ss << "\n";
		else
			ss << " ";
	}

	gxemul->GetUI()->ShowDebugMessage(ss.str());
}


int I960_CPUComponent::FunctionTraceArgumentCount()
{
	return 8;
}


int64_t I960_CPUComponent::FunctionTraceArgument(int n)
{
	return m_r[I960_G0 + n];
}


bool I960_CPUComponent::FunctionTraceReturnImpl(int64_t& retval)
{
	retval = m_r[I960_G0];
	return true;
}


int I960_CPUComponent::GetDyntransICshift() const
{
	// 4 bytes per instruction means 2 bits shift.
	return 2;
}


void (*I960_CPUComponent::GetDyntransToBeTranslated())(CPUDyntransComponent*, DyntransIC*)
{
	return instr_ToBeTranslated;
}


bool I960_CPUComponent::VirtualToPhysical(uint64_t vaddr, uint64_t& paddr,
	bool& writable)
{
	paddr = vaddr;
	writable = true;
	return true;
}


uint64_t I960_CPUComponent::PCtoInstructionAddress(uint64_t pc)
{
	return pc;
}


size_t I960_CPUComponent::DisassembleInstruction(uint64_t vaddr, size_t maxLen,
	unsigned char *instruction, vector<string>& result)
{
	size_t instrSize = sizeof(uint32_t);

	if (maxLen < instrSize) {
		assert(false);
		return 0;
	}

	// Read the instruction word:
	uint32_t instructionWord = ((uint32_t *) (void *) instruction)[0];
	if (m_isBigEndian)
		instructionWord = BE32_TO_HOST(instructionWord);
	else
		instructionWord = LE32_TO_HOST(instructionWord);

	const uint32_t iword = instructionWord;

	const int opcode = iword >> 24;
	
	const int REG_src_dst  = (iword >> 19) & 0x1f;
	const int REG_src2     = (iword >> 14) & 0x1f;
	const int REG_m3       = (iword >> 13) & 0x1;
	const int REG_m2       = (iword >> 12) & 0x1;
	const int REG_m1       = (iword >> 11) & 0x1;
	const int REG_opcode2  = (iword >> 7) & 0xf;
	const int REG_sfr2     = (iword >> 6) & 0x1;
	const int REG_sfr1     = (iword >> 5) & 0x1;
	const int REG_src1     = (iword >> 0) & 0x1f;
	
	const int COBR_src_dst = (iword >> 19) & 0x1f;
	const int COBR_src_2   = (iword >> 14) & 0x1f;
	const int COBR_m1      = (iword >> 13) & 0x1;
	const int COBR_disp    = (iword >> 2) & 0x7ff;
	const int COBR_t       = (iword >> 1) & 0x1;
	const int COBR_s2      = (iword >> 0) & 0x1;
	
	const int CTRL_disp    = (iword >> 2) & 0x3fffff;
	const int CTRL_t       = (iword >> 1) & 0x1;

	// const int MEMA_src_dst = (iword >> 19) & 0x1f; Same as MEMB_src_dst
	const int MEMA_abase   = (iword >> 14) & 0x1f;
	const int MEMA_md      = (iword >> 13) & 0x1;
	// const int MEMA_zero    = (iword >> 12) & 0x1;  0 for MEMA, 1 for MEMB
	const int MEMA_offset  = (iword >> 0) & 0xfff;

	const int MEMB_src_dst = (iword >> 19) & 0x1f;
	const int MEMB_abase   = (iword >> 14) & 0x1f;
	const int MEMB_mode    = (iword >> 10) & 0xf;
	const int MEMB_scale   = (iword >> 7) & 0x7;
	// const int MEMB_sfr     = (iword >> 5) & 0x3;  Should be 00?
	const int MEMB_index   = (iword >> 0) & 0x1f;

	bool hasDisplacementWord = false;

	if (opcode >= 0x80 && iword & 0x1000) {
		/*  Only some MEMB instructions have displacement words:  */
		int mode = (iword >> 10) & 0xf;
		if (mode == 0x5 || mode >= 0xc)
			hasDisplacementWord = true;		
	}

	uint32_t displacementWord = 0;
	if (hasDisplacementWord) {
		instrSize += sizeof(uint32_t);
		if (maxLen < instrSize)
			return 0;

		displacementWord = ((uint32_t *) (void *) instruction)[1];
		if (m_isBigEndian)
			displacementWord = BE32_TO_HOST(displacementWord);
		else
			displacementWord = LE32_TO_HOST(displacementWord);
	}

	stringstream ssHex;
	ssHex.flags(std::ios::hex);
	ssHex << std::setfill('0') << std::setw(8) << (uint32_t) iword;
	if (hasDisplacementWord)
		ssHex << " " << std::setfill('0') << std::setw(8) << (uint32_t) displacementWord;
	else
		ssHex << "         ";

	result.push_back(ssHex.str());


	stringstream ssOpcode;
	stringstream ssArgs;
	stringstream ssComments;
	
	if (opcode >= 0x08 && opcode <= 0x1f) {
		/*  CTRL:  */
		const char* mnemonics[] = {
				"b",			/*  0x08  */
				"call",			/*  0x09  */
				"ret",			/*  0x0a  */
				"bal",			/*  0x0b  */
				"unknown_ctrl_0x0c",	/*  0x0c  */
				"unknown_ctrl_0x0d",	/*  0x0d  */
				"unknown_ctrl_0x0e",	/*  0x0e  */
				"unknown_ctrl_0x0f",	/*  0x0f  */
				"bno",			/*  0x10  */
				"bg",			/*  0x11  */
				"be",			/*  0x12  */
				"bge",			/*  0x13  */
				"bl",			/*  0x14  */
				"bne",			/*  0x15  */
				"ble",			/*  0x16  */
				"bo",			/*  0x17  */
				"faultno",		/*  0x18  */
				"faultg",		/*  0x19  */
				"faulte",		/*  0x1a  */
				"faultge",		/*  0x1b  */
				"faultl",		/*  0x1c  */
				"faultne",		/*  0x1d  */
				"faultle",		/*  0x1e  */
				"faulto"		/*  0x1f  */
			};

		ssOpcode << mnemonics[opcode - 0x08];
		if (CTRL_t)
			ssOpcode << ".f";

		bool hasDisplacement = opcode < 0x18 && opcode != 0x0a;
		if (hasDisplacement) {
			uint32_t disp = CTRL_disp << 2;
			if (disp & 0x00800000)
				disp |= 0xff000000;

			uint32_t addr = vaddr + disp;
			ssArgs << "0x";
			ssArgs.flags(std::ios::hex);
			ssArgs << std::setfill('0') << std::setw(8) << addr;
		}
	} else if (opcode >= 0x20 && opcode <= 0x3f) {
		/*  COBR:  */
		const char* mnemonics[] = {
				"testno",		/*  0x20  */
				"testg",		/*  0x21  */
				"teste",		/*  0x22  */
				"testge",		/*  0x23  */
				"testl",		/*  0x24  */
				"testne",		/*  0x25  */
				"testle",		/*  0x26  */
				"testo",		/*  0x27  */

				"unknown_cobr_0x28",	/*  0x28  */
				"unknown_cobr_0x29",	/*  0x29  */
				"unknown_cobr_0x2a",	/*  0x2a  */
				"unknown_cobr_0x2b",	/*  0x2b  */
				"unknown_cobr_0x2c",	/*  0x2c  */
				"unknown_cobr_0x2d",	/*  0x2d  */
				"unknown_cobr_0x2e",	/*  0x2e  */
				"unknown_cobr_0x2f",	/*  0x2f  */

				"bbc",			/*  0x30  */
				"cmpobg",		/*  0x31  */
				"cmpobe",		/*  0x32  */
				"cmpobge",		/*  0x33  */
				"cmpobl",		/*  0x34  */
				"cmpobne",		/*  0x35  */
				"cmpobne",		/*  0x36  */
				"bbs",			/*  0x37  */

				"cmpibno",		/*  0x38  */
				"cmpibg",		/*  0x39  */
				"cmpibe",		/*  0x3a  */
				"cmpibge",		/*  0x3b  */
				"cmpibl",		/*  0x3c  */
				"cmpibne",		/*  0x3d  */
				"cmpible",		/*  0x3e  */
				"cmpibo",		/*  0x3f  */
			};

		ssOpcode << mnemonics[opcode - 0x20];
		if (COBR_t)
			ssOpcode << ".f";

		bool src1isBitpos = opcode == 0x30 || opcode == 0x37;

		if (opcode <= 0x27) {
			ssArgs << regname_or_literal(COBR_src_dst, 0, COBR_s2);
		} else {
			uint32_t targ = COBR_disp << 2;
			if (targ & 0x00001000)
				targ |= 0xffffe000;
			targ += vaddr;

			ssArgs << regname_or_literal(COBR_src_dst, src1isBitpos ? 1 : COBR_m1, 0) << ",";
			ssArgs << regname_or_literal(COBR_src_2, 0, COBR_s2) << ",";
			ssArgs << "0x";
			ssArgs.flags(std::ios::hex);
			ssArgs << std::setfill('0') << std::setw(8) << targ;
		}
	} else if (opcode >= 0x58 && opcode <= 0x7f) {
		/*  REG:  */
		struct reg_instruction *rinstr = NULL;
		for (int i = 0; ; ++i) {
			if (reg_instructions[i].mnemonic == NULL)
				break;
			if (reg_instructions[i].opcode == (opcode << 4) + REG_opcode2) {
				rinstr = &reg_instructions[i];
				break;
			}
		}

		bool has_src1 = true, has_src2 = true, has_dst = true, has_src3 = false;

		if (rinstr == NULL) {
			ssOpcode << "unknown_reg_";
			ssOpcode.flags(std::ios::hex);
			ssOpcode << std::setfill('0') << std::setw(2) << opcode;
			ssOpcode << ":" << std::setw(1) << REG_opcode2;
		} else {
			ssOpcode << rinstr->mnemonic;
			has_src1 = rinstr->has_src1;
			has_src2 = rinstr->has_src2;
			has_dst = rinstr->has_dst;
			has_src3 = rinstr->has_src3;
		}

		if (has_src1)
			ssArgs << regname_or_literal(REG_src1, REG_m1, REG_sfr1);

		if (has_src2) {
			if (ssArgs.str().length() > 0)
				ssArgs << ",";
			ssArgs << regname_or_literal(REG_src2, REG_m2, REG_sfr2);
		}
		
		if (has_dst) {
			if (ssArgs.str().length() > 0)
				ssArgs << ",";
			if (REG_m3) {
				/*
				 *  The manual for i960CA says (when M3 = 1):
				 *
				 *  "src/dst is a literal when used as a source
				 *   or a special function register when used
				 *   as a destination. M3 may not be 1 when
				 *   src/dst is used both as a source and 
				 *   destination in an instruction (atmod,
				 *   modify, extract, modpc)."
				 */
				if (has_src3)
					ssArgs << regname_or_literal(REG_src_dst, 1, 0);
				else
					ssArgs << regname_or_literal(REG_src_dst, 0, 1);
			} else
				ssArgs << regname_or_literal(REG_src_dst, 0, 0);
		}
	} else if (opcode >= 0x80 && opcode <= 0xcf) {
		/*  MEM:  */
		
		/*  NOTE: These are for i960CA. When implementing support for
		    other CPU variants, include an enum indicating which CPU
		    it is for so that a warning can be printed for instructions
		    that will cause faults on another CPU.  */
		const char* mnemonics[] = {
				"ldob",			/*  0x80  */
				"unknown_mem_0x81",	/*  0x81 BiiN ldvob  */
				"stob",			/*  0x82  */
				"unknown_mem_0x83",	/*  0x83 BiiN stvob  */
				"bx",			/*  0x84  */
				"balx",			/*  0x85  */
				"callx",		/*  0x86  */
				"unknown_mem_0x87",	/*  0x87  */

				"ldos",			/*  0x88  */
				"unknown_mem_0x89",	/*  0x89 BiiN ldvos  */
				"stos",			/*  0x8a  */
				"unknown_mem_0x8b",	/*  0x8b BiiN stvos  */
				"lda",			/*  0x8c  */
				"unknown_mem_0x8d",	/*  0x8d  */
				"unknown_mem_0x8e",	/*  0x8e  */
				"unknown_mem_0x8f",	/*  0x8f  */

				"ld",			/*  0x90  */
				"unknown_mem_0x91",	/*  0x91 BiiN ldv  */
				"st",			/*  0x92  */
				"unknown_mem_0x93",	/*  0x93 Biin stv  */
				"unknown_mem_0x94",	/*  0x94  */
				"unknown_mem_0x95",	/*  0x95  */
				"unknown_mem_0x96",	/*  0x96  */
				"unknown_mem_0x97",	/*  0x97  */

				"ldl",			/*  0x98  */
				"unknown_mem_0x99",	/*  0x99 BiiN ldvl  */
				"stl",			/*  0x9a  */
				"unknown_mem_0x9b",	/*  0x9b BiiN stvl  */
				"unknown_mem_0x9c",	/*  0x9c  */
				"unknown_mem_0x9d",	/*  0x9d  */
				"unknown_mem_0x9e",	/*  0x9e  */
				"unknown_mem_0x9f",	/*  0x9f  */

				"ldt",			/*  0xa0  */
				"unknown_mem_0xa1",	/*  0xa1 BiiN ldvt  */
				"stt",			/*  0xa2  */
				"unknown_mem_0xa3",	/*  0xa3 Biin stvt  */
				"unknown_mem_0xa4",	/*  0xa4  */
				"unknown_mem_0xa5",	/*  0xa5  */
				"unknown_mem_0xa6",	/*  0xa6  */
				"unknown_mem_0xa7",	/*  0xa7  */

				"unknown_mem_0xa8",	/*  0xa8  */
				"unknown_mem_0xa9",	/*  0xa9  */
				"unknown_mem_0xaa",	/*  0xaa  */
				"unknown_mem_0xab",	/*  0xab  */
				"unknown_mem_0xac",	/*  0xac  */
				"unknown_mem_0xad",	/*  0xad  */
				"unknown_mem_0xae",	/*  0xae  */
				"unknown_mem_0xaf",	/*  0xaf  */

				"ldq",			/*  0xb0  */
				"unknown_mem_0xb1",	/*  0xb1 BiiN ldvq  */
				"stq",			/*  0xb2  */
				"unknown_mem_0xb3",	/*  0xb3 BiiN stvq  */
				"unknown_mem_0xb4",	/*  0xb4  */
				"unknown_mem_0xb5",	/*  0xb5  */
				"unknown_mem_0xb6",	/*  0xb6  */
				"unknown_mem_0xb7",	/*  0xb7  */

				"unknown_mem_0xb8",	/*  0xb8  */
				"unknown_mem_0xb9",	/*  0xb9  */
				"unknown_mem_0xba",	/*  0xba  */
				"unknown_mem_0xbb",	/*  0xbb  */
				"unknown_mem_0xbc",	/*  0xbc  */
				"unknown_mem_0xbd",	/*  0xbd  */
				"unknown_mem_0xbe",	/*  0xbe  */
				"unknown_mem_0xbf",	/*  0xbf  */

				"ldib",			/*  0xc0  */
				"unknown_mem_0xc1",	/*  0xc1 BiiN ldvib  */
				"stib",			/*  0xc2  */
				"unknown_mem_0xc3",	/*  0xc3 Biin stvib  */
				"unknown_mem_0xc4",	/*  0xc4  */
				"unknown_mem_0xc5",	/*  0xc5  */
				"unknown_mem_0xc6",	/*  0xc6  */
				"unknown_mem_0xc7",	/*  0xc7  */

				"ldis",			/*  0xc8  */
				"unknown_mem_0xc9",	/*  0xc9 BiiN ldvis  */
				"stis",			/*  0xca  */
				"unknown_mem_0xcb",	/*  0xcb BiiN stvis  */
				"unknown_mem_0xcc",	/*  0xcc  */
				"unknown_mem_0xcd",	/*  0xcd  */
				"unknown_mem_0xce",	/*  0xce  */
				"unknown_mem_0xcf",	/*  0xcf  */
				
				/*  BiiN:
					d0 = ldm
					d1 = ldvm
					d2 = stm
					d3 = stvm
					d8 = ldml
					d9 = ldvml
					da = stml
					db = stvml */
			};

		ssOpcode << mnemonics[opcode - 0x80];

		bool usesDst = opcode != 0x84 && opcode != 0x86;
		bool isStore = !!(opcode & 2);

		if (usesDst && isStore) {
			ssArgs << regname_or_literal(MEMB_src_dst, 0, 0) << ",";
		}

		if (iword & 0x1000) {
			/*  MEMB:  */
			int scale = 1 << MEMB_scale;
			switch (MEMB_mode) {
			case 0x4:
				ssArgs << "(" << regname_or_literal(MEMB_abase, 0, 0) << ")";
				break;
			case 0x5:
				{
					uint32_t offset = displacementWord + 8;
					ssArgs << "0x";
					ssArgs.flags(std::ios::hex);
					ssArgs << std::setfill('0') << std::setw(8) << offset;
					ssArgs << "(ip)";
				}
				break;
			case 0x7:
				// (reg1)[reg2 * scale]
				ssArgs << "(" << regname_or_literal(MEMB_abase, 0, 0) << ")";
				ssArgs << "[" << regname_or_literal(MEMB_index, 0, 0) << "*" << scale << "]";
				break;
			case 0xc:
			case 0xd:
				{
					uint32_t offset = displacementWord;
					ssArgs << "0x";
					ssArgs.flags(std::ios::hex);
					ssArgs << std::setfill('0') << std::setw(8) << offset;
					if (MEMB_mode == 0xd)
						ssArgs << "(" << regname_or_literal(MEMB_abase, 0, 0) << ")";
				}
				break;
			case 0xe:
			case 0xf:
				{
					uint32_t offset = displacementWord;
					ssArgs << "0x";
					ssArgs.flags(std::ios::hex);
					ssArgs << std::setfill('0') << std::setw(8) << offset;
					if (MEMB_mode == 0xf)
						ssArgs << "(" << regname_or_literal(MEMB_abase, 0, 0) << ")";
					ssArgs << "[" << regname_or_literal(MEMB_index, 0, 0) << "*" << scale << "]";
				}
				break;
			default:
				ssArgs << "unimplemented MEMB mode!";
			}
		} else {
			/*  MEMA:  */
			ssArgs << "0x";
			ssArgs.flags(std::ios::hex);
			ssArgs << std::setfill('0') << std::setw(1) << MEMA_offset;

			if (MEMA_md)
				ssArgs << "(" << regname_or_literal(MEMA_abase, 0, 0) << ")";
		}

		if (usesDst && !isStore) {
			ssArgs << "," << regname_or_literal(MEMB_src_dst, 0, 0);
		}
	} else if (iword == 0) {
		ssOpcode << "--";
	} else {
		ssOpcode << "unknown_0x";
		ssOpcode.flags(std::ios::hex);
		ssOpcode << std::setfill('0') << std::setw(2) << (int)opcode;
	}

	result.push_back(ssOpcode.str());
	result.push_back(ssArgs.str());
	string comments = ssComments.str();
	if (comments.length() > 0)
		result.push_back(comments);

	return instrSize;
}


string I960_CPUComponent::GetAttribute(const string& attributeName)
{
	if (attributeName == "stable")
		return "yes";

	if (attributeName == "description")
		return "Intel i960 processor.";

	return Component::GetAttribute(attributeName);
}


/*****************************************************************************/


DYNTRANS_INSTR(I960_CPUComponent,b)
{
	DYNTRANS_INSTR_HEAD(I960_CPUComponent)
	cpu->m_pc = ic->arg[0].u32;
	cpu->DyntransPCtoPointers();
}


DYNTRANS_INSTR(I960_CPUComponent,lda_displacement)
{
	DYNTRANS_INSTR_HEAD(I960_CPUComponent)
	REG32(ic->arg[2]) = ic->arg[0].u32;
	cpu->m_nextIC = ic + 2;
}


DYNTRANS_INSTR(I960_CPUComponent,mov_lit_reg)
{
	REG32(ic->arg[2]) = ic->arg[0].u32;
}


DYNTRANS_INSTR(I960_CPUComponent,sysctl)
{
	DYNTRANS_INSTR_HEAD(I960_CPUComponent)

	uint32_t message = REG32(ic->arg[0]);
	int type = (message >> 8) & 0xff;
	
	if (type == 0x01) {
		// Invalidate cache.
		// Right now in GXemul, this is a NOP.
		UI* ui = cpu->GetUI();
		ui->ShowDebugMessage(cpu, "invalidating cache (no-op for now)");
	} else {
		
		// We didn't actually do anything in this instruction.
		cpu->m_executedCycles --;

		// Point to this instruction...
		DYNTRANS_SYNCH_PC;

		// ... and then abort.
		cpu->m_nextIC = &cpu->m_abortIC;
	
		UI* ui = cpu->GetUI();
		ui->ShowDebugMessage(cpu, "unimplemented sysctl message type");
	}
}


/*****************************************************************************/


void I960_CPUComponent::Translate(uint32_t iword, uint32_t iword2, struct DyntransIC* ic)
{
	UI* ui = GetUI();	// for debug messages

	unsigned int opcode = iword >> 24;

	if (opcode >= 0x08 && opcode <= 0x1f) {
		/*  CTRL:  */
		const int CTRL_disp    = (iword >> 2) & 0x3fffff;
		uint32_t disp = CTRL_disp << 2;
		if (disp & 0x00800000)
			disp |= 0xff000000;

		ic->arg[0].u32 = disp + m_pc;
		
		if (opcode == 0x08) {
			ic->f = instr_b;
		}
	} else if (opcode >= 0x58 && opcode <= 0x7f) {
		/*  REG:  */
		const int REG_src_dst  = (iword >> 19) & 0x1f;
		const int REG_src2     = (iword >> 14) & 0x1f;
		const int REG_m3       = (iword >> 13) & 0x1;
		const int REG_m2       = (iword >> 12) & 0x1;
		const int REG_m1       = (iword >> 11) & 0x1;
		const int REG_opcode2  = (iword >> 7) & 0xf;
		const int REG_s2       = (iword >> 6) & 0x1;
		const int REG_s1       = (iword >> 5) & 0x1;
		const int REG_src1     = (iword >> 0) & 0x1f;

		int op3 = (opcode << 4) + REG_opcode2;

		if (REG_m1)
			ic->arg[0].u32 = REG_src1;
		else {
			if (REG_s1)
				ic->arg[0].p = &m_sfr[REG_src1];
			else
				ic->arg[0].p = &m_r[REG_src1];
		}
		
		if (REG_m2)
			ic->arg[1].u32 = REG_src2;
		else {
			if (REG_s2)
				ic->arg[1].p = &m_sfr[REG_src1];
			else
				ic->arg[1].p = &m_r[REG_src2];
		}
		
		if (REG_m3) {
			// TODO: write to sfr.
			if (ui != NULL)
				ui->ShowDebugMessage(this, "unimplemented write to sfr");
			return;
		} else {
			ic->arg[2].p = &m_r[REG_src_dst];
		}

		void (*f_lit_lit_reg)(CPUDyntransComponent*, struct DyntransIC*) = NULL;
		void (*f_lit_reg_reg)(CPUDyntransComponent*, struct DyntransIC*) = NULL;
		void (*f_reg_lit_reg)(CPUDyntransComponent*, struct DyntransIC*) = NULL;
		void (*f_reg_reg_reg)(CPUDyntransComponent*, struct DyntransIC*) = NULL;

		if (op3 == 0x5cc) {
			// mov		NOTE: mov does not use src2.
			f_lit_lit_reg = instr_mov_lit_reg;
			f_lit_reg_reg = instr_mov_lit_reg;
		} else if (op3 == 0x659) {
			// sysctl
			f_reg_reg_reg = instr_sysctl;
		}

		if (REG_m3 == 0) {
			if (REG_m1 && REG_m2)
				ic->f = f_lit_lit_reg;
			if (REG_m1 && !REG_m2)
				ic->f = f_lit_reg_reg;
			if (!REG_m1 && REG_m2)
				ic->f = f_reg_lit_reg;
			if (!REG_m1 && !REG_m2)
				ic->f = f_reg_reg_reg;
		} else {
			if (ui != NULL)
				ui->ShowDebugMessage(this, "unimplemented write to sfr");
		}
	} else if (opcode >= 0x80 && opcode <= 0xcf) {
		/*  MEM:  */
//		const int MEMA_abase   = (iword >> 14) & 0x1f;
		const int MEMA_md      = (iword >> 13) & 0x1;
		const int MEMA_offset  = (iword >> 0) & 0xfff;
		const int MEMB_src_dst = (iword >> 19) & 0x1f;
//		const int MEMB_abase   = (iword >> 14) & 0x1f;
		const int MEMB_mode    = (iword >> 10) & 0xf;
//		const int MEMB_scale   = (iword >> 7) & 0x7;
//		const int MEMB_index   = (iword >> 0) & 0x1f;

		ic->arg[2].p = &m_r[MEMB_src_dst];

		if (iword & 0x1000) {
			/*  MEMB:  */
			switch (MEMB_mode) {
			case 0xc:
				ic->arg[0].u32 = iword2;			
				ic->f = instr_lda_displacement;
				break;
			default:
				ui->ShowDebugMessage(this, "unimplemented MEMB_mode");
			}
		} else {
			/*  MEMA:  */
			if (MEMA_md)
				ui->ShowDebugMessage(this, "TODO: MEMA");
			else {
				ic->arg[0].u32 = MEMA_offset;
				ic->f = instr_mov_lit_reg;
			}
		}
	}

	if (ic->f == NULL && ui != NULL) {
		stringstream ss;
		ss.flags(std::ios::hex);
		ss << "unimplemented opcode 0x" << opcode;
		ui->ShowDebugMessage(this, ss.str());
	}
}


DYNTRANS_INSTR(I960_CPUComponent,ToBeTranslated)
{
	DYNTRANS_INSTR_HEAD(I960_CPUComponent)

	cpu->DyntransToBeTranslatedBegin(ic);

	uint32_t iword;
	if (cpu->DyntransReadInstruction(iword)) {
		bool readCompleteInstruction = true;
		uint32_t iword2 = 0;
		uint32_t opcode = iword >> 24;
		if (opcode >= 0x80 && opcode <= 0xcf) {
			/*  Only some MEMB instructions have displacement words:  */
			int mode = (iword >> 10) & 0xf;
			if (mode == 0x5 || mode >= 0xc)
				readCompleteInstruction = cpu->DyntransReadInstruction(iword2, 4);
			if (!readCompleteInstruction) {
				UI* ui = cpu->GetUI();
				ui->ShowDebugMessage(cpu, "last part of instruction could not be read: TODO");
			}
		}
		
		if (readCompleteInstruction)
			cpu->Translate(iword, iword2, ic);
	}

	cpu->DyntransToBeTranslatedDone(ic);
}


/*****************************************************************************/


#ifdef WITHUNITTESTS

#include "ComponentFactory.h"

static void Test_I960_CPUComponent_Create()
{
	refcount_ptr<Component> cpu = ComponentFactory::CreateComponent("i960_cpu");
	UnitTest::Assert("component was not created?", !cpu.IsNULL());

	const StateVariable * p = cpu->GetVariable("pfp");
	UnitTest::Assert("cpu has no pfp state variable?", p != NULL);
}

static void Test_I960_CPUComponent_Disassembly_Basic()
{
	refcount_ptr<Component> i960_cpu = ComponentFactory::CreateComponent("i960_cpu");
	CPUComponent* cpu = i960_cpu->AsCPUComponent();

	vector<string> result;
	size_t len;
	unsigned char instruction[sizeof(uint32_t) * 2];

	// This assumes that the default endianness is little endian...
	instruction[0] = 0x00;
	instruction[1] = 0x30;
	instruction[2] = 0x68;
	instruction[3] = 0x8c;

	instruction[4] = 0x01;
	instruction[5] = 0x23;
	instruction[6] = 0x34;
	instruction[7] = 0x45;

	len = cpu->DisassembleInstruction(0x12345678, sizeof(instruction), instruction, result);

	UnitTest::Assert("disassembled instruction was wrong length?", len, 8);
	UnitTest::Assert("disassembly result incomplete?", result.size(), 3);
	UnitTest::Assert("disassembly result[0]", result[0], "8c683000 45342301");
	UnitTest::Assert("disassembly result[1]", result[1], "lda");
	UnitTest::Assert("disassembly result[2]", result[2], "0x45342301,r13");
}

static GXemul SimpleMachine()
{
	GXemul gxemul;
	gxemul.GetCommandInterpreter().RunCommand("add mainbus");
	gxemul.GetCommandInterpreter().RunCommand("add i960_cpu mainbus0");
	gxemul.GetCommandInterpreter().RunCommand("add ram mainbus0");
	gxemul.GetCommandInterpreter().RunCommand("ram0.memoryMappedBase = 0x3fe00000");
	gxemul.GetCommandInterpreter().RunCommand("ram0.memoryMappedSize = 0x1000");
	return gxemul;
}

static void Test_I960_CPUComponent_Execute_mov()
{
	GXemul gxemul = SimpleMachine();
	refcount_ptr<Component> cpu = gxemul.GetRootComponent()->LookupPath("root.mainbus0.cpu0");
	AddressDataBus* bus = cpu->AsAddressDataBus();

	bus->AddressSelect(0x3fe00048);
	bus->WriteData((uint32_t)0x5c201e06, LittleEndian);	// mov   6,r4
	bus->AddressSelect(0x3fe0004c);
	bus->WriteData((uint32_t)0x5c201e06, LittleEndian);	// mov   6,r4

	cpu->SetVariableValue("pc", "0x3fe00048");
	cpu->SetVariableValue("r4", "0x1234");

	gxemul.SetRunState(GXemul::Running);
	gxemul.Execute(1);

	UnitTest::Assert("pc should have increased", cpu->GetVariable("pc")->ToInteger(), 0x3fe0004c);
	UnitTest::Assert("r4 should have been modified", cpu->GetVariable("r4")->ToInteger(), 6);

	cpu->SetVariableValue("r4", "0x12345");

	gxemul.SetRunState(GXemul::SingleStepping);
	gxemul.Execute(1);

	UnitTest::Assert("pc should have increased again", cpu->GetVariable("pc")->ToInteger(), 0x3fe00050);
	UnitTest::Assert("r4 should have been modified again", cpu->GetVariable("r4")->ToInteger(), 6);
}

static void Test_I960_CPUComponent_Execute_b()
{
	GXemul gxemul = SimpleMachine();
	refcount_ptr<Component> cpu = gxemul.GetRootComponent()->LookupPath("root.mainbus0.cpu0");
	AddressDataBus* bus = cpu->AsAddressDataBus();

	bus->AddressSelect(0x3fe00004);
	bus->WriteData((uint32_t)0x080006c0, LittleEndian);	// b 0x3fe006c4

	cpu->SetVariableValue("pc", "0x3fe00004");

	gxemul.SetRunState(GXemul::Running);
	gxemul.Execute(1);

	UnitTest::Assert("pc should have changed", cpu->GetVariable("pc")->ToInteger(), 0x3fe006c4);

	cpu->SetVariableValue("pc", "0x3fe00004");

	gxemul.SetRunState(GXemul::SingleStepping);
	gxemul.Execute(1);

	UnitTest::Assert("pc should have changed again", cpu->GetVariable("pc")->ToInteger(), 0x3fe006c4);
}

static void Test_I960_CPUComponent_Execute_lda_with_offset()
{
	GXemul gxemul = SimpleMachine();
	refcount_ptr<Component> cpu = gxemul.GetRootComponent()->LookupPath("root.mainbus0.cpu0");
	AddressDataBus* bus = cpu->AsAddressDataBus();

	bus->AddressSelect(0x3fe00010);
	bus->WriteData((uint32_t)0x8c180f13, LittleEndian);	// lda r3, 0xf13

	cpu->SetVariableValue("pc", "0x3fe00010");
	gxemul.SetRunState(GXemul::Running);
	gxemul.Execute(1);
	UnitTest::Assert("lda length", cpu->GetVariable("pc")->ToInteger(), 0x3fe00014);
	UnitTest::Assert("lda", cpu->GetVariable("r3")->ToInteger(), 0xf13);
}

static void Test_I960_CPUComponent_Execute_lda_with_displacement()
{
	GXemul gxemul = SimpleMachine();
	refcount_ptr<Component> cpu = gxemul.GetRootComponent()->LookupPath("root.mainbus0.cpu0");
	AddressDataBus* bus = cpu->AsAddressDataBus();

	bus->AddressSelect(0x3fe00010);
	bus->WriteData((uint32_t)0x8cf03000, LittleEndian);	// lda
	bus->AddressSelect(0x3fe00014);
	bus->WriteData((uint32_t)0x3fe0507c, LittleEndian);	//     0x3fe0507c, g14

	cpu->SetVariableValue("pc", "0x3fe00010");
	gxemul.SetRunState(GXemul::Running);
	gxemul.Execute(1);
	UnitTest::Assert("lda length", cpu->GetVariable("pc")->ToInteger(), 0x3fe00018);
	UnitTest::Assert("lda", cpu->GetVariable("g14")->ToInteger(), 0x3fe0507c);
}

UNITTESTS(I960_CPUComponent)
{
	UNITTEST(Test_I960_CPUComponent_Create);
	UNITTEST(Test_I960_CPUComponent_Disassembly_Basic);

	UNITTEST(Test_I960_CPUComponent_Execute_mov);
	UNITTEST(Test_I960_CPUComponent_Execute_b);
	UNITTEST(Test_I960_CPUComponent_Execute_lda_with_offset);
	UNITTEST(Test_I960_CPUComponent_Execute_lda_with_displacement);
}

#endif
