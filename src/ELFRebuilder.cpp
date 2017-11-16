#include "ELFRebuilder.h"
#include "Log.h"

ELFRebuilder::ELFRebuilder(ELFReader &_reader, bool _force)
	: reader(_reader), force(_force){
		
	elf_header = reader.getElfHeader();
	phdr_table = reader.getPhdrTable();

}

ELFRebuilder::~ELFRebuilder(){
	if(rebuild_data != NULL){
		delete [](uint8_t*)rebuild_data;
	}
}


bool ELFRebuilder::rebuild(){
	if(force || reader.getDamageLevel() == 2){
		return totalRebuild();
	} else if(reader.getDamageLevel() == 1){
		return simpleRebuild() && rebuildData();
	}
}


/**
 * Just repair the section headers address and offset.
 * This function is just like the checkSectionHeader in ELFReader.
 * Calling this function. We assume that the so-file have valid 
 * program header, elf header, and valid size of each section. 
 * The all thing that this file need is section offset and address.
 */
bool ELFRebuilder::simpleRebuild(){
	VLOG("Starting repair the section.");
	rebuild_size = sizeof(Elf32_Ehdr) + reader.getPhdrSize() + reader.getMidPartSize() + reader.getShdrSize();
	Elf32_Shdr *shdr_table = reader.getShdrTable();
	Elf32_Phdr *phdr_table = reader.getPhdrTable();
	int shdr_num = reader.getShdrNum();
	int phdr_num = reader.getPhdrNum();

	int loadIndex[2] = {-1, -1};
	for(int i=0, j=0;i<phdr_num;i++){
		if(phdr_table[i].p_type == PT_LOAD){
			loadIndex[j++] = i;
		}
	}

	int firstAddress = sizeof(Elf32_Ehdr) + reader.getPhdrSize();
	// build the first section.
	shdr_table[1].sh_addr = shdr_table[1].sh_offset = firstAddress;
	
	
	int i;
	DLOG("Start repair the section mapping at first LOAD segment.");
	for(i=2;i<shdr_num;i++){	//we have already check 2 section. So "i" start at 2.
		
		Elf32_Addr curAddr = shdr_table[i-1].sh_addr + shdr_table[i-1].sh_size;
		Elf32_Off curOffset = shdr_table[i-1].sh_offset + shdr_table[i-1].sh_size;
		// bits align
		while(curAddr & (shdr_table[i].sh_addralign-1)) { curAddr++; }
		while(curOffset & (shdr_table[i].sh_addralign-1)) {curOffset++;}
		
		if(curOffset == phdr_table[loadIndex[0]].p_filesz) { break; }
		shdr_table[i].sh_addr = curAddr;
		shdr_table[i].sh_offset = curOffset;	
		
	}
	
	// Rebuild the second LOAD segment
	// First get the second LOAD segment address and offset
	DLOG("Start repair the section mapping at second LOAD segment.");
	shdr_table[i].sh_addr = phdr_table[loadIndex[1]].p_vaddr;
	shdr_table[i].sh_offset = phdr_table[loadIndex[1]].p_offset;

	for(i=i+1;i<shdr_num;i++){
		
		Elf32_Addr curAddr = shdr_table[i-1].sh_addr + shdr_table[i-1].sh_size;
		Elf32_Off curOffset = shdr_table[i-1].sh_offset + shdr_table[i-1].sh_size;
		//bits align
		while(curAddr & (shdr_table[i].sh_addralign-1)) { curAddr++; }
		while(curOffset & (shdr_table[i].sh_addralign-1)) {curOffset++;}
		
		shdr_table[i].sh_addr = curAddr;
		shdr_table[i].sh_offset = curOffset;
		// Beside Load segment, break
		if(curOffset >= phdr_table[loadIndex[1]].p_filesz + phdr_table[loadIndex[1]].p_offset) { break; }
	}

	// The remain section won't be load. So the address is 0.
	// And the .bss section type is SHT_NOBITS.
	// So the follow section offset is the same as it.
	DLOG("Repair the not LOAD section.");
	i++;
	shdr_table[i].sh_addr = 0;
	shdr_table[i].sh_offset = shdr_table[i-1].sh_offset;	
	for(i=i+1;i<shdr_num;i++){
		Elf32_Off curOffset = shdr_table[i-1].sh_offset + shdr_table[i-1].sh_size;
		while(curOffset & (shdr_table[i].sh_addralign-1)) {curOffset++;}
		
		shdr_table[i].sh_addr = 0;
		shdr_table[i].sh_offset = curOffset;
	}

	DLOG("Repair finish.");
	return true;
}

bool ELFRebuilder::rebuildData(){
	rebuild_data = new uint8_t[rebuild_size];
	DLOG("Copy elf header data. Elf header size = %d", sizeof(elf_header));
	uint8_t *tmp = rebuild_data;
	memcpy(tmp, &elf_header, sizeof(elf_header));
	tmp += sizeof(elf_header);

	size_t phdr_size = reader.getPhdrSize();
	DLOG("Copy program header data. Program header size = %d", phdr_size);
	memcpy(tmp, phdr_table, phdr_size);
	tmp += phdr_size;

	size_t midPart_size = reader.getMidPartSize();
	uint8_t* midPart = reinterpret_cast<uint8_t*>(reader.getMidPart());
	DLOG("Copy midPart data. MidPart size = %d", midPart_size);
	memcpy(tmp, midPart, midPart_size);
	tmp += midPart_size;
	
	size_t shdr_size = reader.getShdrSize();
	DLOG("Copy section header data. Section header size = %d", shdr_size);
	memcpy(tmp, reader.getShdrTable(), shdr_size);

	DLOG("rebuild_data prepared.");
	return false;
}

bool ELFRebuilder::totalRebuild(){
	VLOG("Using plan B to rebuild the section.");
	return rebuildPhdr() && 
		   readSoInfo() &&
		   rebuildShdr() &&	
		   rebuildRelocs() &&	
		   rebuildFinish();
}


bool ELFRebuilder::rebuildPhdr(){
	Elf32_Phdr* phdr = (Elf32_Phdr *)reader.getLoadedPhdr();
	for(int i=0;i<reader.getPhdrNum();i++){
		phdr[i].p_filesz = phdr[i].p_memsz;
		// p_paddr and p_align is not used in load, ignore it.
		phdr[i].p_paddr = phdr[i].p_vaddr;
		phdr[i].p_offset = phdr[i].p_vaddr;
	}
	DLOG("Program header rebuild finish.");
	return true;
}

bool ELFRebuilder::readSoInfo(){
	
	si.base = si.load_bias = reader.getLoadBias();
	si.phdr = reader.getPhdrTable();
	si.phnum = reader.getPhdrNum();

	Elf32_Addr base = si.base;
	phdr_table_get_load_size(si.phdr, si.phnum, &si.min_load, &si.max_load);

	// get .dynamic table
	phdr_table_get_dynamic_section(si.phdr, si.phnum, si.base, &si.dynamic, &si.dynamic_count, &si.dynamic_flags);

	if(si.dynamic == NULL){
		ELOG("dynamic section unavailable. Cannot rebuild.");
		return false;
	}
	//get .arm_exidx
	phdr_table_get_arm_exidx(si.phdr, si.phnum, si.base, &si.ARM_exidx, &si.ARM_exidx_count);

	// scan the dynamic section and get useful information.
	uint32_t needed_count = 0;
	for(Elf32_Dyn* dyn = si.dynamic;dyn->d_tag != DT_NULL;dyn++){
		switch(dyn->d_tag){
			case DT_HASH:
				si.hash = dyn->d_un.d_ptr + base;
				si.nbucket = ((unsigned *)si.hash)[0];
				si.nchain = ((unsigned *)si.hash)[1];
				si.bucket = (unsigned *)si.hash + 8;
				si.chain = (unsigned *)si.bucket + 4*si.nbucket;
				break;
			case DT_STRTAB:
				si.strtab = (const char*)(dyn->d_un.d_ptr + base);
				VLOG("string table found at %x", dyn->d_un.d_ptr);
				break;
							case DT_SYMTAB:
				si.symtab = (Elf32_Sym *) (dyn->d_un.d_ptr + base);
				VLOG("symbol table found at %x\n", dyn->d_un.d_ptr);
				break;
			case DT_PLTREL:
				if (dyn->d_un.d_val != DT_REL) {
					VLOG("unsupported DT_RELA in \"%s\"\n", si.name);
					return false;
				}
				break;
			case DT_JMPREL:
				si.plt_rel = (Elf32_Rel*) (dyn->d_un.d_ptr + base);
				VLOG("%s plt_rel (DT_JMPREL) found at %x\n", si.name, dyn->d_un.d_ptr);
				break;
			case DT_PLTRELSZ:
				si.plt_rel_count = dyn->d_un.d_val / sizeof(Elf32_Rel);
				VLOG("%s plt_rel_count (DT_PLTRELSZ) %d\n", si.name, si.plt_rel_count);
				break;
			case DT_REL:
				si.rel = (Elf32_Rel*) (dyn->d_un.d_ptr + base);
				VLOG("%s rel (DT_REL) found at %x\n", si.name, dyn->d_un.d_ptr);
				break;
			case DT_RELSZ:
				si.rel_count = dyn->d_un.d_val / sizeof(Elf32_Rel);
				VLOG("%s rel_size (DT_RELSZ) %d\n", si.name, si.rel_count);
				break;
			case DT_PLTGOT:
				/* Save this in case we decide to do lazy binding. We don't yet. */
				si.plt_got = (Elf32_Addr *)(dyn->d_un.d_ptr + base);
				break;
			case DT_DEBUG:
				// Set the DT_DEBUG entry to the address of _r_debug for GDB
				// if the dynamic table is writable
				break;
			case DT_RELA:
				VLOG("unsupported DT_RELA in \"%s\"\n", si.name);
				return false;
			case DT_INIT:
				si.init_func = reinterpret_cast<void*>(dyn->d_un.d_ptr + base);
				VLOG("%s constructors (DT_INIT) found at %x\n", si.name, dyn->d_un.d_ptr);
				break;
			case DT_FINI:
				si.fini_func = reinterpret_cast<void*>(dyn->d_un.d_ptr + base);
				VLOG("%s destructors (DT_FINI) found at %x\n", si.name, dyn->d_un.d_ptr);
				break;
			case DT_INIT_ARRAY:
				si.init_array = reinterpret_cast<void**>(dyn->d_un.d_ptr + base);
				VLOG("%s constructors (DT_INIT_ARRAY) found at %x\n", si.name, dyn->d_un.d_ptr);
				break;
			case DT_INIT_ARRAYSZ:
				si.init_array_count = ((unsigned)dyn->d_un.d_val) / sizeof(Elf32_Addr);
				VLOG("%s constructors (DT_INIT_ARRAYSZ) %d\n", si.name, si.init_array_count);
				break;
			case DT_FINI_ARRAY:
				si.fini_array = reinterpret_cast<void**>(dyn->d_un.d_ptr + base);
				VLOG("%s destructors (DT_FINI_ARRAY) found at %x\n", si.name, dyn->d_un.d_ptr);
				break;
			case DT_FINI_ARRAYSZ:
				si.fini_array_count = ((unsigned)dyn->d_un.d_val) / sizeof(Elf32_Addr);
				VLOG("%s destructors (DT_FINI_ARRAYSZ) %d\n", si.name, si.fini_array_count);
				break;
			case DT_PREINIT_ARRAY:
				si.preinit_array = reinterpret_cast<void**>(dyn->d_un.d_ptr + base);
				VLOG("%s constructors (DT_PREINIT_ARRAY) found at %d\n", si.name, dyn->d_un.d_ptr);
				break;
			case DT_PREINIT_ARRAYSZ:
				si.preinit_array_count = ((unsigned)dyn->d_un.d_val) / sizeof(Elf32_Addr);
				VLOG("%s constructors (DT_PREINIT_ARRAYSZ) %d\n", si.preinit_array_count);
				break;
			case DT_TEXTREL:
				si.has_text_relocations = true;
				break;
			case DT_SYMBOLIC:
				si.has_DT_SYMBOLIC = true;
				break;
			case DT_NEEDED:
				++needed_count;
				break;
			case DT_FLAGS:
				if (dyn->d_un.d_val & DF_TEXTREL) {
					si.has_text_relocations = true;
				}
				if (dyn->d_un.d_val & DF_SYMBOLIC) {
					si.has_DT_SYMBOLIC = true;
				}
				break;
			case DT_STRSZ:
				si.strtabsize = dyn->d_un.d_val;
				break;
			case DT_SYMENT:
			case DT_RELENT:
				break;
			case DT_MIPS_RLD_MAP:
				// Set the DT_MIPS_RLD_MAP entry to the address of _r_debug for GDB.
				break;
			case DT_MIPS_RLD_VERSION:
			case DT_MIPS_FLAGS:
			case DT_MIPS_BASE_ADDRESS:
			case DT_MIPS_UNREFEXTNO:
				break;

			case DT_MIPS_SYMTABNO:
				si.mips_symtabno = dyn->d_un.d_val;
				break;

			case DT_MIPS_LOCAL_GOTNO:
				si.mips_local_gotno = dyn->d_un.d_val;
				break;

			case DT_MIPS_GOTSYM:
				si.mips_gotsym = dyn->d_un.d_val;
				break;
			case DT_SONAME:
				si.name = (const char *) (dyn->d_un.d_ptr + base);
				VLOG("soname %s\n", si.name);
				break;
			default:
				VLOG("Unused DT entry: type 0x%08x arg 0x%08x\n", dyn->d_tag, dyn->d_un.d_val);
				break;
		}
	}
	DLOG("Dynamic read finish.");
	return true;
}

bool ELFRebuilder::rebuildShdr(){
	//TODO:
}

bool ELFRebuilder::rebuildRelocs(){
	//TODO:
}

bool ELFRebuilder::rebuildFinish(){
	//TODO:
}


