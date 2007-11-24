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
 *
 *  $Id: ConfigNode.cc,v 1.1 2007-11-24 02:00:05 debug Exp $
 *
 *  A class describing a Configuration Node.
 */

#include "ConfigNode.h"
#include <iostream>


ConfigNode::ConfigNode(const string& strName)
	: m_strName(strName)
{
}


ConfigNode::~ConfigNode()
{
}


void ConfigNode::AddChild(const ConfigNode& node)
{
	m_childNodes.push_back(node);
}


string ConfigNode::ToString(int indentation) const
{
	string str;
	int i;

	for (i=0; i<indentation; i++)
		str += "    ";

	str += "\"" + m_strName + "\"\n";

	for (i=0; i<indentation; i++)
		str += "    ";

	str += "{\n";

	for (size_t j=0; j<m_childNodes.size(); j++) {
		if (j > 0)
			str += "\n";

		str += m_childNodes[j].ToString(indentation + 1);
	}

	for (i=0; i<indentation; i++)
		str += "    ";

	str += "}\n";

	return str;
}


#ifdef TEST

int main(int argc, char *argv[])
{
	ConfigNode rootNode("emulation");

	ConfigNode machineA("machine");
	ConfigNode machineB("machine");

	rootNode.AddChild(machineA);
	rootNode.AddChild(machineB);

	// std::cout << rootNode.ToString();

	std::cout << "TEST OK: ConfigNode\n";
	return 0;
}

#endif

