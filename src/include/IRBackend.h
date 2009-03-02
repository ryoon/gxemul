#ifndef IRBACKEND_H
#define	IRBACKEND_H

/*
 *  Copyright (C) 2008-2009  Anders Gavare.  All rights reserved.
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

#include "IRregister.h"
#include "UnitTest.h"


/**
 * \brief A helper/baseclass for code generator backends.
 *
 * TODO
 */
class IRBackend
	: public UnitTestable
	, public ReferenceCountable
{
protected:
	/**
	 * \brief Constructs an %IRBackend instance.
	 */
	IRBackend()
	{
	}

public:
	virtual ~IRBackend()
	{
	}

	/**
	 * \brief Gets a code generator backend for the host architecture.
	 *
	 * \param useNativeIfAvailable True if a native JIT should be tried
	 *	first, before falling back to the portable (slow)
	 *	implementation.
	 * \return A reference to a code generator backend.
	 */
	static refcount_ptr<IRBackend> GetIRBackend(
	    bool useNativeIfAvailable = true);

	/**
	 * \brief Setup native registers.
	 *
	 * This function adds registers that the IR register allocator then
	 * uses.
	 */
	virtual void SetupRegisters(vector<IRregister>& registers) = 0;

	/**
	 * \brief Executes code on the host, from a specified address in
	 * host memory.
	 *
	 * \param addr The address of the (generated) code.
	 */
	static void Execute(void *addr);


	/********************************************************************/

	static void RunUnitTests(int& nSucceeded, int& nFailures);
};


#endif	// IRBACKEND_H
