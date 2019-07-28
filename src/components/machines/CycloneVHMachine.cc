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
 *  For experiments with uClinux/i960.
 *
 *  A binary (vmlinux) can be found on this page:
 *  https://web.archive.org/web/20010417034914/http://www.cse.ogi.edu/~kma/uClinux.html
 *
 *  NOTE!!! The binary at http://www.uclinux.org/pub/uClinux/ports/i960/ is corrupt;
 *          it seems to have been uploaded/encoded with the wrong character encoding.
 *          (At least it is broken as of 2016-04-18.)
 *
 *
 *  See the following link for details about the Cyclone VH board:
 *  
 *  http://www.nj7p.org/Manuals/PDFs/Intel/273194-003.PDF
 *  "EVAL80960VH Evaluation Platform Board Manual, December 1998"
 *
 *  and for the CPU:
 *
 *  http://www.nj7p.info/Manuals/PDFs/Intel/273173-001.PDF
 *  "i960 VH Processor Developer's Manual, October 1998"
 */

#include "components/CycloneVHMachine.h"
#include "ComponentFactory.h"
#include "GXemul.h"


refcount_ptr<Component> CycloneVHMachine::Create(const ComponentCreateArgs& args)
{
	// Defaults:
	ComponentCreationSettings settings;
	settings["cpu"] = "i960CA";	// TODO: uClinux is compiled for i960Jx
	settings["ram"] = "0x00400000";

	if (!ComponentFactory::GetCreationArgOverrides(settings, args))
		return NULL;


	refcount_ptr<Component> machine = ComponentFactory::CreateComponent("machine");
	if (machine.IsNULL())
		return NULL;

	machine->SetVariableValue("template", "\"cyclonevh\"");


	refcount_ptr<Component> mainbus =
	    ComponentFactory::CreateComponent("mainbus");
	if (mainbus.IsNULL())
		return NULL;

	machine->AddChild(mainbus);

	// TODO: CPU frequency is perhaps 100 MHz?
	refcount_ptr<Component> cpu =
	    ComponentFactory::CreateComponent("i960_cpu(model=" + settings["cpu"] + ")");
	if (cpu.IsNULL())
		return NULL;

	mainbus->AddChild(cpu);


	refcount_ptr<Component> ram = ComponentFactory::CreateComponent("ram");
	if (ram.IsNULL())
		return NULL;

	// DRAM is at a3c0 0000 - a3ff ffff according to
	// https://groups.google.com/forum/#!topic/intel.microprocessors.i960/tgpjDcW5Dxc
	ram->SetVariableValue("memoryMappedBase", "0xa3c00000");
	ram->SetVariableValue("memoryMappedSize", settings["ram"]);
	mainbus->AddChild(ram);

	// There is supposed to be a LED at e0040000 too. TODO.
	
	return machine;
}


string CycloneVHMachine::GetAttribute(const string& attributeName)
{
	if (attributeName == "template")
		return "yes";

	if (attributeName == "machine")
		return "yes";

	if (attributeName == "stable")
		return "yes";

	if (attributeName == "description")
		return "Cyclone/VH i960 evaluation board machine.";

	if (attributeName == "comments")
		return "For experiments with uClinux/i960.";

	return "";
}

