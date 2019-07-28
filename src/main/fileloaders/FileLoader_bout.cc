/*
 *  Copyright (C) 2009-2018  Anders Gavare.  All rights reserved.
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

#include <assert.h>
#include <string.h>
#include <fstream>
#include <iomanip>

using std::setw;
using std::setfill;
using std::ifstream;

#include "AddressDataBus.h"
#include "components/CPUComponent.h"
#include "FileLoader_bout.h"

#include "thirdparty/exec_bout.h"


FileLoader_bout::FileLoader_bout(const string& filename)
	: FileLoaderImpl(filename)
{
}


string FileLoader_bout::DetectFileType(unsigned char *buf, size_t buflen, float& matchness) const
{
	matchness = 0.9;

	if (buflen >= 0x2c && buf[0] == 0x0d && buf[1] == 0x01 && buf[2] == 0x00 && buf[3] == 0x00) {
		return "b.out_i960_little";
	}

	if (buflen >= 0x2c && buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01 && buf[3] == 0x0d) {
		return "b.out_i960_big";
	}

	matchness = 0.0;
	return "";
}


static uint32_t unencode32(uint8_t *p, Endianness endianness)
{
	uint32_t res;
	
	if (endianness == BigEndian)
		res = p[3] + (p[2] << 8) + (p[1] << 16) + (p[0] << 24);
	else
		res = p[0] + (p[1] << 8) + (p[2] << 16) + (p[3] << 24);

	return res;
}


bool FileLoader_bout::LoadIntoComponent(refcount_ptr<Component> component, ostream& messages) const
{
	AddressDataBus* bus = component->AsAddressDataBus();
	if (bus == NULL) {
		messages << "Target is not an AddressDataBus.\n";
		return false;
	}

	ifstream file(Filename().c_str());
	if (!file.is_open()) {
		messages << "Unable to read file.\n";
		return false;
	}

	unsigned char buf[65536];

	memset(buf, 0, sizeof(buf));
	file.seekg(0, std::ios_base::end);
	uint64_t totalSize = file.tellg();
	file.seekg(0, std::ios_base::beg);
	file.read((char *)buf, totalSize < sizeof(buf)? totalSize : sizeof(buf));
	size_t amountRead = file.gcount();

	float matchness;
	string format = DetectFileType(buf, amountRead, matchness);

	if (format == "") {
		messages << "Unknown b.out format.\n";
		return false;
	}

	file.seekg(0, std::ios_base::beg);

	StateVariable* var = component->GetVariable("bigendian");
	if (var == NULL) {
		messages << "Target does not have the 'bigendian' variable,"
		    " which is needed to\n"
		    "load b.out files.\n";
		return false;
	}

	Endianness endianness = LittleEndian;
	if (format == "b.out_i960_big")
		endianness = BigEndian;

	struct bout_exec header;
	uint32_t entry;

	file.read((char *)&header, sizeof(header));
	if (file.gcount() != sizeof(header)) {
		messages << "The file is too small to be a b.out.\n";
		return false;
	}

	entry = unencode32((unsigned char*)&header.a_entry, endianness);

	uint32_t textaddr = unencode32((unsigned char*)&header.a_tload, endianness);
	uint32_t textsize = unencode32((unsigned char*)&header.a_text, endianness);

	uint32_t dataaddr = unencode32((unsigned char*)&header.a_dload, endianness);
	uint32_t datasize = unencode32((unsigned char*)&header.a_data, endianness);

	int32_t symbsize = unencode32((unsigned char*)&header.a_syms, endianness);

	messages.flags(std::ios::hex);
	messages << "b.out: entry point 0x";
	messages << setw(8) << setfill('0') << (uint32_t) entry << "\n";

	messages.flags(std::ios::dec);
	messages << "text + data = " << textsize << " + " << datasize << " bytes\n";

	// Load text:
	uint32_t vaddr = textaddr;
	while (textsize != 0) {
		int len = textsize > sizeof(buf) ? sizeof(buf) : textsize;
		file.read((char *)buf, len);
		len = file.gcount();
		if (len < 1)
			break;

		// Write to the bus, one byte at a time.
		for (int k=0; k<len; ++k) {
			bus->AddressSelect(vaddr);
			if (!bus->WriteData(buf[k])) {
				messages.flags(std::ios::hex);
				messages << "Failed to write data to virtual "
				    "address 0x" << vaddr << "\n";
				return false;
			}
			
			++ vaddr;
		}

		textsize -= len;
	}

	if (textsize != 0) {
		messages << "Failed to read the entire file.\n";
		return false;
	}

	// Load data:
	vaddr = dataaddr;
	while (datasize != 0) {
		int len = datasize > sizeof(buf) ? sizeof(buf) : datasize;
		file.read((char *)buf, len);
		len = file.gcount();
		if (len < 1)
			break;

		// Write to the bus, one byte at a time.
		for (int k=0; k<len; ++k) {
			bus->AddressSelect(vaddr);
			if (!bus->WriteData(buf[k])) {
				messages.flags(std::ios::hex);
				messages << "Failed to write data to virtual "
				    "address 0x" << vaddr << "\n";
				return false;
			}
			
			++ vaddr;
		}

		datasize -= len;
	}

	if (datasize != 0) {
		messages << "Failed to read the entire file.\n";
		return false;
	}

#if 0
	// TODO: Symbols. Similar to a.out?

	SymbolRegistry* symbolRegistry = NULL;
	CPUComponent* cpu = component->AsCPUComponent();
	if (cpu != NULL)
		symbolRegistry = &cpu->GetSymbolRegistry();

	// Symbols:
	if (symbolRegistry != NULL && symbsize > 0) {
		messages.flags(std::ios::dec);
		messages << "symbols: " << symbsize << " bytes at 0x";
		messages.flags(std::ios::hex);
		messages << file.tellg() << "\n";

		vector<char> symbolData;
		symbolData.resize(symbsize);
		file.read(&symbolData[0], symbsize);
		if (file.gcount() != symbsize) {
			messages << "Failed to read all symbols.\n";
			return false;
		}

		off_t oldpos = file.tellg();
		file.seekg(0, std::ios_base::end);
		size_t strings_len = (off_t)file.tellg() - oldpos;
		file.seekg(oldpos, std::ios_base::beg);

		messages.flags(std::ios::dec);
		messages << "strings: " << strings_len << " bytes at 0x";
		messages.flags(std::ios::hex);
		messages << file.tellg() << "\n";

		vector<char> symbolStrings;
		// Note: len + 1 for a nul terminator, for safety.
		symbolStrings.resize(strings_len + 1);
		file.read(&symbolStrings[0], strings_len);
		if (file.gcount() != strings_len) {
			messages << "Failed to read all strings.\n";
			return false;
		}

		assert(sizeof(struct aout_symbol) == 12);

		int nsymbols = 0;

		struct aout_symbol* aout_symbol_ptr = (struct aout_symbol *) (void*) &symbolData[0];
		int n_symbols = symbsize / sizeof(struct aout_symbol);
		for (int i = 0; i < n_symbols; i++) {
			uint32_t index = unencode32((unsigned char*)&aout_symbol_ptr[i].strindex, endianness);
			uint32_t type  = unencode32((unsigned char*)&aout_symbol_ptr[i].type, endianness);
			uint32_t addr  = unencode32((unsigned char*)&aout_symbol_ptr[i].addr, endianness);

			// TODO: These bits probably mean different things for
			// different b.out formats. For OpenBSD/m88k at least,
			// this bit (0x01000000) seems to mean "a normal symbol".
			if (!(type & 0x01000000))
				continue;

			// ... and the rectangle drawing demo says
			// "_my_memset" at addr 1020, type 5020000
			// "rectangles_m88k_O2.o" at addr 1020, type 1f000000
			if ((type & 0x1f000000) == 0x1f000000)
				continue;

			if (index >= (uint32_t)strings_len) {
				messages << "symbol " << i << " has invalid string index\n";
				continue;
			}

			string symbol = ((char*) &symbolStrings[0]) + index;
			if (symbol == "")
				continue;

			// messages << "\"" << symbol << "\" at addr " << addr
			//   << ", type " << type << "\n";

			// Add this symbol to the symbol registry:
			symbolRegistry->AddSymbol(symbol, addr);
			++ nsymbols;
		}

		messages.flags(std::ios::dec);
		messages << nsymbols << " symbols read\n";
	}
#endif

	// Set the CPU's entry point.
	stringstream ss;
	ss << (int64_t)(int32_t)entry;
	component->SetVariableValue("pc", ss.str());

	return true;
}


/*****************************************************************************/


#ifdef WITHUNITTESTS

#include "ComponentFactory.h"

static void Test_FileLoader_bout_Constructor()
{
	FileLoader_bout boutLoader("test/FileLoader_B.OUT_i960");
}

UNITTESTS(FileLoader_bout)
{
	UNITTEST(Test_FileLoader_bout_Constructor);

	// TODO
}

#endif
