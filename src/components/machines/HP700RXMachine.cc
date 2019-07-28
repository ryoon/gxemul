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
 *
 *
 *  HP 700/RX X-terminal with an i960CA CPU.
 *
 *  Bogus skeleton so far.
 *
 *  My machine says:
 *
 *  2048 KB Base RAM
 *  8192 KB Expansion RAM
 *  2048 KB Video RAM
 */

#include "components/HP700RXMachine.h"
#include "ComponentFactory.h"
#include "GXemul.h"


refcount_ptr<Component> HP700RXMachine::Create(const ComponentCreateArgs& args)
{
	// Defaults:
	ComponentCreationSettings settings;
	settings["cpu"] = "i960CA";
	settings["ram"] = "0x00200000";

	if (!ComponentFactory::GetCreationArgOverrides(settings, args))
		return NULL;


	refcount_ptr<Component> machine = ComponentFactory::CreateComponent("machine");
	if (machine.IsNULL())
		return NULL;

	machine->SetVariableValue("template", "\"hp700rx\"");


	refcount_ptr<Component> mainbus =
	    ComponentFactory::CreateComponent("mainbus");
	if (mainbus.IsNULL())
		return NULL;

	machine->AddChild(mainbus);


	refcount_ptr<Component> cpu =
	    ComponentFactory::CreateComponent("i960_cpu(model=" + settings["cpu"] + ")");
	if (cpu.IsNULL())
		return NULL;

	mainbus->AddChild(cpu);


	refcount_ptr<Component> ram = ComponentFactory::CreateComponent("ram");
	if (ram.IsNULL())
		return NULL;

	// DRAM (guess)
	ram->SetVariableValue("memoryMappedBase", "0x3fe00000");
	ram->SetVariableValue("memoryMappedSize", settings["ram"]);
	mainbus->AddChild(ram);

	return machine;
}


string HP700RXMachine::GetAttribute(const string& attributeName)
{
	if (attributeName == "template")
		return "yes";

	if (attributeName == "machine")
		return "yes";

	if (attributeName == "stable")
		return "yes";

	if (attributeName == "description")
		return "HP 700/RX X-terminal (i960).";

	if (attributeName == "comments")
		return "For experiments that could eventually lead up to "
			"running custom code on the HP 700/RX.";

	return "";
}

