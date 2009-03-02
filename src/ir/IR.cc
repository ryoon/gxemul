/*
 *  Copyright (C) 2009  Anders Gavare.  All rights reserved.
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

#include "IR.h"


IR::IR(IRBlockCache& blockCache)
	: m_blockCache(blockCache)
	, m_codeGenerator(IRBackend::GetIRBackend())
{
	InitRegisterAllocator();
}


IR::~IR()
{
}


void IR::InitRegisterAllocator()
{
	// Ask the code generator backend for definitions of all its registers:
	m_registers.clear();
	m_codeGenerator->SetupRegisters(m_registers);

	// Add all non-reserved registers to a linked list. This is the list
	// that registers are allocated from.
	m_mruRegisters.clear();
	for (size_t i=0; i<m_registers.size(); i++) {
#ifndef NDEBUG
		for (size_t j=0; j<i; j++) {
			if (m_registers[i].implementation_register ==
			    m_registers[j].implementation_register) {
				std::cerr << "Duplicate implementation_register"
				    " values! Aborting.\n";
				throw std::exception();
			}

			if (m_registers[i].implementation_register_name ==
			    m_registers[j].implementation_register_name) {
				std::cerr << "Duplicate register name"
				    " values! Aborting.\n";
				throw std::exception();
			}
		}
#endif

		if (!m_registers[i].reserved)
			m_mruRegisters.push_back(&m_registers[i]);
	}
}


void IR::UndirtyRegisterOffset(IRregister* reg)
{
	if (reg->dirty_offset != 0) {
		// TODO: add or sub value from register...
		std::cerr << "todo: add not yet implemented\n";
		throw std::exception();
	}

	reg->dirty_offset = 0;
}


void IR::FlushRegister(IRregister* reg)
{
	if (!reg->dirty)
		return;

	if (!reg->inUse) {
		std::cerr << "Huh? Register dirty, but not in use?\n";
		throw std::exception();
	}

	UndirtyRegisterOffset(reg);

	// TODO: write back to memory
	
	std::cerr << "todo: writeback not yet implemented\n";
	throw std::exception();
}


IRregisterNr IR::GetNewRegisterNr()
{
	// Take the register at the back of the list:
	IRregister* reg = m_mruRegisters.back();
	m_mruRegisters.pop_back();

	FlushRegister(reg);

	m_mruRegisters.push_front(reg);
	reg->inUse = false;

	return reg->implementation_register;
}


void IR::Flush()
{
	// Flush all registers:
	for (size_t i=0; i<m_registers.size(); i++)
		FlushRegister(&m_registers[i]);
}


void IR::Let_64(uint64_t value, IRregisterNr *returnReg)
{
	*returnReg = GetNewRegisterNr();

	// TODO: emit instruction "let" into the specific register.
}


void IR::Load_64(size_t relativeStructOffset, IRregisterNr* valueReg)
{
	// If a register is already holding the value for this struct
	// offset, then move that register to the front of the list
	// and return it.
	list<IRregister*>::iterator it = m_mruRegisters.begin();
	for (; it != m_mruRegisters.end(); ++it) {
		if ((*it)->inUse &&
		    (*it)->address == relativeStructOffset) {
			IRregister* reg = *it;
			*valueReg = reg->implementation_register;
			m_mruRegisters.erase(it);
			m_mruRegisters.push_front(reg);
			UndirtyRegisterOffset(reg);
			return;
		}
	}

	// Otherwise, perform an actual load:
	*valueReg = GetNewRegisterNr();

	// TODO: emit instruction to load into the register

	// Mark the register as inUse with the specified address:
	// (It should be at the front of the mru list by now.)
	IRregister* reg = m_mruRegisters.front();
	if (reg->implementation_register != *valueReg) {
		std::cerr << "Internal error: Load\n";
		throw std::exception();
	}
	reg->inUse = true;
	reg->address = relativeStructOffset;
}


void IR::Store_64(IRregisterNr valueReg, size_t relativeStructOffset)
{
	// Mark the register as dirty, but don't store it right away.
	list<IRregister*>::iterator it = m_mruRegisters.begin();
	for (; it != m_mruRegisters.end(); ++it) {
		if ((*it)->implementation_register == valueReg) {
			IRregister* reg = *it;

			// Already dirty, with some other address?
			if (reg->dirty && reg->address != relativeStructOffset)
				FlushRegister(reg);

			reg->inUse = true;
			reg->dirty = true;
			reg->address = relativeStructOffset;

			m_mruRegisters.erase(it);
			m_mruRegisters.push_front(reg);
			return;
		}
	}

	std::cerr << "Store of non-existing register?\n";
	throw std::exception();
}



/*****************************************************************************/


#ifdef WITHUNITTESTS

// A small CPU-like structure:
struct cpu {
	int		dummy;
	uint64_t	variable;
	int		other_dummy;
};

static void Test_IR_RegisterAllocation()
{
	IRBlockCache blockCache(1048576);
	IR ir(blockCache);

	IRregisterNr r_1 = 12345;
	ir.Let_64(123, &r_1);
	UnitTest::Assert("should have received a register number",
	    r_1 != 12345);

	IRregisterNr r_2 = r_1;
	ir.Let_64(123, &r_2);
	UnitTest::Assert("should have received another register number",
	    r_1 != r_2);
}

static void Test_IR_RegisterReuse()
{
	IRBlockCache blockCache(1048576);
	IR ir(blockCache);

	IRregisterNr r_1 = 12345;
	ir.Load_64(0x100, &r_1);
	UnitTest::Assert("should have received a register number",
	    r_1 != 12345);

	IRregisterNr r_2 = r_1;
	ir.Load_64(0x108, &r_2);
	UnitTest::Assert("should have received another register number",
	    r_1 != r_2);

	IRregisterNr r_3 = 10000;
	ir.Load_64(0x100, &r_3);
	UnitTest::Assert("should have reused the first register number",
	    r_3, r_1);

	IRregisterNr r_4 = 42;
	ir.Load_64(0x108, &r_4);
	UnitTest::Assert("should have reused the second register number",
	    r_4, r_2);
}

static void Test_IR_DelayedStore()
{
	IRBlockCache blockCache(1048576);
	IR ir(blockCache);

	struct cpu cpu;
	cpu.variable = 42;

	IRregisterNr r_1;
	ir.Let_64(123, &r_1);

	ir.Store_64(r_1, (size_t)&cpu.variable - (size_t)&cpu);
	// TODO: This store above should not have outputted anything.
	
	// TODO: Flushing should output the actual store.
	//ir.Flush();
	
	// TODO: Flushing again should not output anything new.
	//ir.Flush();

	// TODO: generate code

	UnitTest::Assert("before IR execution", cpu.variable, 42);

	// Execute the code.
	// TODO

	// UnitTest::Assert("after IR execution", cpu.variable, 123);
}

static void Test_IR_LoadAfterStore()
{
	IRBlockCache blockCache(1048576);
	IR ir(blockCache);

	struct cpu cpu;
	cpu.variable = 42;

	IRregisterNr r_1;
	ir.Let_64(123, &r_1);

	ir.Store_64(r_1, (size_t)&cpu.variable - (size_t)&cpu);

	IRregisterNr r_2 = 1000;
	ir.Load_64((size_t)&cpu.other_dummy - (size_t)&cpu, &r_2);
	UnitTest::Assert("should have gotten a new register for the dummy",
	    r_2 != r_1);

	IRregisterNr r_3 = 2000;
	ir.Load_64((size_t)&cpu.variable - (size_t)&cpu, &r_3);
	UnitTest::Assert("should have reused the first register number",
	    r_3, r_1);
}

UNITTESTS(IR)
{
	UNITTEST(Test_IR_RegisterAllocation);
	UNITTEST(Test_IR_RegisterReuse);
	UNITTEST(Test_IR_DelayedStore);
	UNITTEST(Test_IR_LoadAfterStore);
}

#endif
