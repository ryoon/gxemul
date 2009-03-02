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

#include "IRBackend.h"

#include "IRBackendPortable.h"

#ifdef NATIVE_ABI_AMD64
#include "IRBackendAMD64.h"
#endif

#ifdef NATIVE_ABI_ALPHA
#include "IRBackendAlpha.h"
#endif


refcount_ptr<IRBackend> IRBackend::GetIRBackend(bool useNativeIfAvailable)
{
	if (useNativeIfAvailable) {
#ifndef NATIVE_CODE_GENERATION
		return new IRBackendPortable();
#endif
#ifdef NATIVE_ABI_AMD64
		return new IRBackendAMD64();
#endif
#ifdef NATIVE_ABI_ALPHA
		return new IRBackendAlpha();
#endif
	} else {
		return new IRBackendPortable();
	}
}


void IRBackend::SetAddress(void* address)
{
	m_address = address;
}


void* IRBackend::GetAddress() const
{
	return m_address;
}


/*
 *  Note: This .cc module needs to be compiled without -ansi -pedantic,
 *  since calling generated code like this gives a warning.
 */
void IRBackend::Execute(void *addr, void *cpustruct)
{
	void (*func)(void *) = (void (*)(void *)) addr;
	func(cpustruct);
}


/*****************************************************************************/


#ifdef WITHUNITTESTS

static int variable;

struct something
{
	int	dummy1;
	int	dummy2;
};

static void SmallFunction(void *input)
{
	struct something* s = (struct something*) input;
	variable = s->dummy1 + s->dummy2;
}

static void Test_IRBackend_Execute()
{
	// Tests that it is possible to execute code, given a pointer to
	// a void function.

	variable = 42;
	UnitTest::Assert("variable before", variable, 42);

	struct something testStruct;
	testStruct.dummy1 = 120;
	testStruct.dummy2 = 3;

	IRBackend::Execute((void*)&SmallFunction, &testStruct);

	UnitTest::Assert("variable after", variable, 123);
}

UNITTESTS(IRBackend)
{
	UNITTEST(Test_IRBackend_Execute);
}

#endif
