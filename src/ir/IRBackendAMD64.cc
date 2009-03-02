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

#include "IRBackendAMD64.h"

#ifdef NATIVE_ABI_AMD64


IRBackendAMD64::IRBackendAMD64()
	: IRBackend()
{
}


void IRBackendAMD64::SetupRegisters(vector<IRregister>& registers)
{
	registers.clear();

	IRregister rax("rax", 0);
	registers.push_back(rax);

	IRregister rcx("rcx", 1);
	registers.push_back(rcx);

	IRregister rdx("rdx", 2);
	registers.push_back(rdx);

	IRregister rbx("rbx", 3);
	rbx.reserved = true;		// callee-save. reserved for now.
	registers.push_back(rbx);

	IRregister rsp("rsp", 4);
	rsp.reserved = true;
	registers.push_back(rsp);

	IRregister rbp("rbp", 5);
	rbp.reserved = true;
	registers.push_back(rbp);

	IRregister rsi("rsi", 6);
	registers.push_back(rsi);

	IRregister rdi("rdi", 7);	// rdi always holds a pointer to the
	rdi.reserved = true;		// CPU struct, or equivalent.
	registers.push_back(rdi);

	IRregister r8("r8", 8);
	registers.push_back(r8);

	IRregister r9("r9", 9);
	registers.push_back(r9);

	IRregister r10("r10", 10);
	registers.push_back(r10);

	IRregister r11("r11", 11);
	registers.push_back(r11);

	IRregister r12("r12", 12);
	r12.reserved = true;		// callee-save. reserved for now.
	registers.push_back(r12);

	IRregister r13("r13", 13);
	r13.reserved = true;		// callee-save. reserved for now.
	registers.push_back(r13);

	IRregister r14("r14", 14);
	r14.reserved = true;		// callee-save. reserved for now.
	registers.push_back(r14);

	IRregister r15("r15", 15);
	r15.reserved = true;		// callee-save. reserved for now.
	registers.push_back(r15);
}


void IRBackendAMD64::SetRegisterToImmediate_64(IRregister* reg, uint64_t value)
{
	int r = reg->implementation_register;
	
	uint8_t* addr = (uint8_t *) GetAddress();

	// Worst case: full 64-bit value:  mov $0x123456789abcdef,%reg
	*addr++ = 0x48 + ((r >> 3) & 1);
	*addr++ = 0xb8 + (r & 7);
	*addr++ = value;
	*addr++ = value >>  8;
	*addr++ = value >> 16;
	*addr++ = value >> 24;
	*addr++ = value >> 32;
	*addr++ = value >> 40;
	*addr++ = value >> 48;
	*addr++ = value >> 56;

	// TODO: more optimized cases (8-bit and 32-bit)
	// (but be aware of signedness!)

	SetAddress(addr);
}


void IRBackendAMD64::RegisterRead(IRregister* reg)
{
	// TODO: Emit code.
}


void IRBackendAMD64::RegisterWriteback(IRregister* reg)
{
	// TODO: Emit code.
}


void IRBackendAMD64::WriteIntro()
{
	// TODO
}


void IRBackendAMD64::WriteOutro()
{
	uint8_t* addr = (uint8_t *) GetAddress();
	*addr++ = 0xc3;	// retq
	SetAddress(addr);
}


#endif	// NATIVE_ABI_AMD64


/*****************************************************************************/


// Note: This class is unit tested, but not from here.
// It is tested from the main IR class.


