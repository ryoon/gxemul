#ifndef I960_CPUCOMPONENT_H
#define	I960_CPUCOMPONENT_H

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

// COMPONENT(i960_cpu)


#include "CPUDyntransComponent.h"


#define	N_I960_REGS		32
#define	N_I960_SFRS		32


/*
Register conventions according to
https://people.cs.clemson.edu/~mark/subroutines/i960.html
*/

static const char* i960_regnames[N_I960_REGS] = {
	"pfp",		// r0 = previous frame pointer
	"sp",		// r1 = stack pointer
	"rip",		// r2 = return instruction pointer
	"r3", "r4", "r5", "r6", "r7",
	"r8", "r9", "r10", "r11", "r12",
	"r13", "r14", "r15",

	"g0", "g1", "g2", "g3",	// parameters 0-3; return words 0-3
	"g4", "g5", "g6", "g7", // parameters 4-7; temporaries
	"g8", "g9", "g10", "g11", "g12",	// preserved accross call
	"g13",		// structure return pointer
	"g14",		// argument block pointer; leaf return address (HW)
	"fp" 		// g15 = frame pointer (16-byte aligned HW)
};

#define	I960_G0		16	// offset to first parameter register


/***********************************************************************/


/**
 * \brief A Component representing an Intel i960 processor.
 */
class I960_CPUComponent
	: public CPUDyntransComponent
{
public:
	/**
	 * \brief Constructs a I960_CPUComponent.
	 */
	I960_CPUComponent();

	/**
	 * \brief Creates a I960_CPUComponent.
	 */
	static refcount_ptr<Component> Create(const ComponentCreateArgs& args);

	static string GetAttribute(const string& attributeName);

	virtual void ResetState();

	virtual bool PreRunCheckForComponent(GXemul* gxemul);

	virtual size_t DisassembleInstruction(uint64_t vaddr, size_t maxlen,
		unsigned char *instruction, vector<string>& result);


	/********************************************************************/

	static void RunUnitTests(int& nSucceeded, int& nFailures);

protected:
	virtual bool CheckVariableWrite(StateVariable& var, const string& oldValue);

	virtual bool VirtualToPhysical(uint64_t vaddr, uint64_t& paddr, bool& writable);

	virtual string VirtualAddressAsString(uint64_t vaddr)
	{
		stringstream ss;
		ss.flags(std::ios::hex | std::ios::showbase | std::ios::right);
		ss << (uint32_t)vaddr;
		return ss.str();
	}

	virtual uint64_t PCtoInstructionAddress(uint64_t pc);

	virtual int FunctionTraceArgumentCount();
	virtual int64_t FunctionTraceArgument(int n);
	virtual bool FunctionTraceReturnImpl(int64_t& retval);

	virtual int GetDyntransICshift() const;
	virtual void (*GetDyntransToBeTranslated())(CPUDyntransComponent*, DyntransIC*);

	virtual void ShowRegisters(GXemul* gxemul, const vector<string>& arguments) const;

private:
	DECLARE_DYNTRANS_INSTR(b);
	DECLARE_DYNTRANS_INSTR(lda_displacement);
	DECLARE_DYNTRANS_INSTR(mov_lit_reg);
	DECLARE_DYNTRANS_INSTR(sysctl);

	void Translate(uint32_t iword, uint32_t iword2, struct DyntransIC* ic);
	DECLARE_DYNTRANS_INSTR(ToBeTranslated);

private:
	/*
	 * State:
	 */
	string		m_model;

	uint32_t	m_r[N_I960_REGS];	// r and g registers

	// NOTE: The i960 "ip" register is m_pc.

	uint32_t	m_i960_ac;		// Arithmetic control
	uint32_t	m_i960_pc;		// Process control
	uint32_t	m_i960_tc;		// Trace control

	uint32_t	m_nr_of_valid_sfrs;	// depends on model
	uint32_t	m_sfr[N_I960_SFRS];
};


#endif	// I960_CPUCOMPONENT_H
