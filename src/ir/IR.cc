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

	int dummyGeneration;
	m_blockCacheCurrentStart = m_blockCache.Allocate(0,dummyGeneration);
	m_codeGenerator->SetAddress(m_blockCacheCurrentStart);
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
		// ... but make sure first that there are no name or
		// register number collisions.
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

	m_codeGenerator->RegisterWriteback(reg);
}


IRregister* IR::GetNewRegister(int size)
{
	// Take the register at the back of the list:
	IRregister* reg = m_mruRegisters.back();
	m_mruRegisters.pop_back();

	FlushRegister(reg);

	m_mruRegisters.push_front(reg);
	reg->inUse = false;
	reg->size = size;

	return reg;
}


// TODO: WriteIntro!


size_t IR::GetCurrentGeneratedCodeSize() const
{
	void* curEnd = m_codeGenerator->GetAddress();
	void* curStart = m_blockCacheCurrentStart;
	return (char *)curEnd - (char *)curStart;
}


void* IR::Finalize()
{
	// Emit function return code:
	m_codeGenerator->WriteOutro();

	size_t length = GetCurrentGeneratedCodeSize();

	// Allocate _after_ we've outputted the actual code :-)
	int generation;
	void* curStart = m_blockCache.Allocate(length, generation);

	// Now, allocate 0 bytes to get a new nicely aligned start address:
	m_blockCacheCurrentStart = m_blockCache.Allocate(0, generation);
	m_codeGenerator->SetAddress(m_blockCacheCurrentStart);

	return curStart;
}


void IR::Flush()
{
	// Flush all registers:
	for (size_t i=0; i<m_registers.size(); i++)
		FlushRegister(&m_registers[i]);
}


void IR::Let_64(uint64_t value, IRregisterNr *returnReg)
{
	IRregister* reg = GetNewRegister(sizeof(uint64_t));
	*returnReg = reg->implementation_register;

	m_codeGenerator->SetRegisterToImmediate_64(reg, value);
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
	IRregister* reg = GetNewRegister(sizeof(uint64_t));
	*valueReg = reg->implementation_register;

	// Mark the register as inUse with the specified address:
	reg->inUse = true;
	reg->address = relativeStructOffset;

	// Emit instruction to load into the register:
	m_codeGenerator->RegisterRead(reg);
}


void IR::Store_64(IRregisterNr valueReg, size_t relativeStructOffset)
{
	// Mark the register as dirty, but don't store it right away.
	list<IRregister*>::iterator it = m_mruRegisters.begin();
	for (; it != m_mruRegisters.end(); ++it) {
		if ((*it)->implementation_register == valueReg) {
			IRregister* reg = *it;

			if (reg->size != sizeof(uint64_t)) {
				std::cerr << "TODO: Store a different size"
				    " than the register's size?\n";
				throw std::exception();
			}
			
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

	size_t s1 = ir.GetCurrentGeneratedCodeSize();

	ir.Store_64(r_1, (size_t)&cpu.variable - (size_t)&cpu);

	// This store above should not have outputted anything.
	size_t s2 = ir.GetCurrentGeneratedCodeSize();
	UnitTest::Assert("store should not have outputted anything yet", s1, s2);

	// Flushing should output the actual store.
	ir.Flush();
	size_t s3 = ir.GetCurrentGeneratedCodeSize();
	UnitTest::Assert("store should now have been flushed", s2 != s3);
	
	// Flushing again should not output anything new.
	ir.Flush();
	size_t s4 = ir.GetCurrentGeneratedCodeSize();
	UnitTest::Assert("nothing more should have been outputted", s3, s4);

	// Finally, generate the code...
	void* generatedCode = ir.Finalize();

	UnitTest::Assert("no generated code?", generatedCode != NULL);
	UnitTest::Assert("before IR execution", cpu.variable, 42);

	{
		uint8_t* p = (uint8_t *) generatedCode;
		printf("Debug dump of generated code:\n");
		for (int i=0; i<4; i++) {
			for (int j=0; j<16; j++) {
				printf(" %02x", p[i*16+j]);
			}
			printf("\n");
		}
	}

	// ... and execute it:
	IRBackend::Execute(generatedCode, &cpu);

	UnitTest::Assert("after IR execution", cpu.variable, 123);
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
