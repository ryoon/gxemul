#ifndef IRREGISTER_H
#define	IRREGISTER_H

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

typedef int IRregisterNr;


/**
 * \brief A class describing a (native) register.
 */
class IRregister
{
public:
	IRregister(const string& native_register_name, int native_register)
		: reserved(false)
		, inUse(false)
		, dirty(false)
		, dirty_offset(0)
		, address(0)
		, implementation_register(native_register)
		, implementation_register_name(native_register_name)
	{
	}

	/**
	 * \brief True if this register is reserved (e.g. for a cpu struct
	 * pointer, or for ABI reasons), false if it is available for use
	 * by the register allocator.
	 */
	bool	reserved;

	/**
	 * \brief True if the register corresponds to a value in the CPU
	 * struct, described by the address field.
	 */
	bool	inUse;

	/**
	 * \brief True if the register needs to be written back to memory.
	 *
	 * Only valid if inUse is also true.
	 */
	bool	dirty;

	/**
	 * \brief A constant offset to be added to a register before reading
	 * from it, or writing it back to memory.
	 */
	int	dirty_offset;

	/**
	 * \brief Offset into the cpu struct that this register corresponds
	 * to.
	 *
	 * Only valid if inUse is true.
	 */
	size_t	address;

	/**
	 * \brief Backend register number.
	 */
	IRregisterNr	implementation_register;

	/**
	 * \brief Register name, for debugging.
	 */
	string	implementation_register_name;
};


#endif	// IRREGISTER_H
