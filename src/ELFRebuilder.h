#ifndef _SO_REBUILDER_ELFREBUILDER_H_
#define _SO_REBUILDER_ELFREBUILDER_H_

#include <vector>
#include <string>
#include "exutil.h"
#include "ELFReader.h"

/**
 * This structure are modified from android source.
 */ 
struct soinfo {
public:
	const char* name = "name";
	const Elf_Phdr* phdr = nullptr;
	size_t phnum = 0;
	Elf_Addr entry = 0;
	Elf_Addr base = 0;
	unsigned size = 0;

	Elf_Addr min_load;
	Elf_Addr max_load;

	uint32_t unused1 = 0;  // DO NOT USE, maintained for compatibility.

	Elf_Dyn* dynamic = nullptr;
	size_t dynamic_count = 0;
	Elf_Word dynamic_flags = 0;

	uint32_t unused2 = 0; // DO NOT USE, maintained for compatibility
	uint32_t unused3 = 0; // DO NOT USE, maintained for compatibility

	unsigned flags = 0;

	const char* strtab = nullptr;
	Elf_Sym* symtab = nullptr;

	Elf_Addr hash = 0;
	size_t strtabsize = 0;
	size_t nbucket = 0;
	size_t nchain = 0;
	unsigned* bucket = nullptr;
	unsigned* chain = nullptr;

	Elf_Addr * plt_got = nullptr;

	Elf_Rel* plt_rel = nullptr;
	size_t plt_rel_count = 0;

	Elf_Rel* rel = nullptr;
	size_t rel_count = 0;

	void* preinit_array = nullptr;
	size_t preinit_array_count = 0;

	void** init_array = nullptr;
	size_t init_array_count = 0;
	void** fini_array = nullptr;
	size_t fini_array_count = 0;

	void* init_func = nullptr;
	void* fini_func = nullptr;

	// ARM EABI section used for stack unwinding.
	Elf_Addr * ARM_exidx = nullptr;
	unsigned ARM_exidx_count = 0;
	unsigned mips_symtabno = 0;
	unsigned mips_local_gotno = 0;
	unsigned mips_gotsym = 0;

	// When you read a virtual address from the ELF file, add this
	// value to get the corresponding address in the process' address space.
	Elf_Addr load_bias = 0;

	bool has_text_relocations = false;
	bool has_DT_SYMBOLIC = false;

	//Add by myself
	size_t dynsym_size = 0;
	Elf_Addr* interp = nullptr;
	size_t interp_size = 0;
	Elf_Addr loadSegEnd = 0;
};

class ELFRebuilder{

public:

	ELFRebuilder(ELFReader &_reader, bool _force);
	~ELFRebuilder();
	bool rebuild();
	uint8_t* getRebuildData() { return rebuild_data; }
	size_t getRebuildDataSize() { return rebuild_size; }
private:

	bool force;			// using to mark if force to rebuild the section.
	ELFReader &reader;

	Elf_Ehdr elf_header;
	Elf_Phdr *phdr_table;

	uint8_t *rebuild_data = NULL;
	size_t rebuild_size = 0;
	
	// Plan A
	bool simpleRebuild();	// just repair the section address and offset.
	bool rebuildData();		// restore data to rebuild_data.

private:
	// Plan B
	bool totalRebuild();	// all rebuild.
	bool rebuildPhdr();
	bool readSoInfo();
	bool rebuildShdr();
	bool rebuildRelocs();
	bool rebuildFinish();
	
	soinfo si;
	Elf_Word sINTERP = 0;
	Elf_Word sDYNSYM = 0;
	Elf_Word sDYNSTR = 0;
	Elf_Word sHASH = 0;
	Elf_Word sRELDYN = 0;
	Elf_Word sRELPLT = 0;
	Elf_Word sPLT = 0;
	Elf_Word sTEXTTAB = 0;
	Elf_Word sARMEXIDX = 0;
	Elf_Word sFINIARRAY = 0;
	Elf_Word sINITARRAY = 0;
	Elf_Word sDYNAMIC = 0;
	Elf_Word sGOT = 0;
	Elf_Word sDATA = 0;
	Elf_Word sBSS = 0;
	Elf_Word sSHSTRTAB = 0;

	std::vector<Elf_Shdr> shdrs;
	std::string shstrtab;
};



#endif