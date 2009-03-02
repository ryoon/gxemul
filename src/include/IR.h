#ifndef IR_H
#define	IR_H

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

#include "misc.h"

#include "IRBlockCache.h"
#include "IRBackend.h"
#include "IRregister.h"
#include "UnitTest.h"


/**
 * \brief A Intermediate Representation for code generation.
 *
 * The code generated can either be native code in a format that is executed
 * directly on the host, or it can be a (much slower) host-independent
 * format.
 */
class IR
	: public UnitTestable
{
public:
	/**
	 * \brief Constructs an %IR instance.
	 *
	 * \param blockCache A block cache used to store translated code
	 *		     blocks.
	 */
	IR(IRBlockCache& blockCache);

	~IR();

	/*  Code generation:  */
	size_t GetCurrentGeneratedCodeSize() const;
	void* Finalize();
	void Flush();
	void Let_64(uint64_t value, IRregisterNr *returnReg);
	void Load_64(size_t relativeStructOffset, IRregisterNr* valueReg);
	void Store_64(IRregisterNr valueReg, size_t relativeStructOffset);


	/********************************************************************/

	static void RunUnitTests(int& nSucceeded, int& nFailures);

private:
	void InitRegisterAllocator();
	void UndirtyRegisterOffset(IRregister* reg);
	void FlushRegister(IRregister* reg);
	IRregister* GetNewRegister(int size);

private:
	IRBlockCache&		m_blockCache;
	void*			m_blockCacheCurrentStart;

	refcount_ptr<IRBackend>	m_codeGenerator;

	// Register allocator:
	//	At the front of the mru list is the most recently used
	//	register. When allocating a new register using GetNewRegisterNr,
	//	the back of the list is consulted.
	vector<IRregister>	m_registers;
	list<IRregister*>	m_mruRegisters;
};


#endif	// IR_H
