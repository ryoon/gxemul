#ifndef DEBUGCONSOLE_H
#define	DEBUGCONSOLE_H

/*
 *  Copyright (C) 2007  Anders Gavare.  All rights reserved.
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
 *  $Id: DebugConsole.h,v 1.3 2007-11-16 08:40:52 debug Exp $
 *
 *  A DebugConsole is a place where debug message are outputted during
 *  runtime, and a means to input key presses when execution is paused
 *  (to enter commands in the interactive debugger).
 *
 *  Currently implemented subclasses:
 *
 *	TTYDebugConsole
 */

#include <string>

class DebugConsole
{
public:
	DebugConsole()
	    : m_bQuiet(false)
	    , m_indentationSteps(0)
	{
	}

	virtual ~DebugConsole()
	{
	}

	virtual void Print(const std::string& str) = 0;
	virtual int GetChar() = 0;

	bool GetQuiet() const
	{
		return m_bQuiet;
	}

	void SetQuiet(bool bQuiet)
	{
		m_bQuiet = bQuiet;
	}

	void AddIndentation(int nSteps)
	{
		m_indentationSteps += nSteps;
	}

	int GetIndentation() const
	{
		return m_indentationSteps;
	}

private:
	bool	m_bQuiet;
	int	m_indentationSteps;
};


// DebugIndentation
//	A helper class for temporary debug message indentation.
class DebugIndentation
{
public:
	DebugIndentation(DebugConsole& debugConsole, int steps = 1)
	    : m_steps(steps)
	    , m_debugConsole(debugConsole)
	{
		m_debugConsole.AddIndentation(m_steps);
	}
	
	~DebugIndentation()
	{
		m_debugConsole.AddIndentation(-m_steps);
	}

private:
	int		m_steps;
	DebugConsole&	m_debugConsole;
};

#endif	// DEBUGCONSOLE_H

