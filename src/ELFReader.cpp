#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "ELFReader.h"
#include "Log.h"
#include "exutil.h"

ELFReader::ELFReader(const char *filename)
	: filename(filename), inputFile(NULL), damageLevel(-1), didLoad(false), didRead(false), 
	  phdr_table(NULL), phdr_entrySize(0), phdr_num(0), phdr_size(0), 
	  midPart_start(0), midPart_end(0), midPart_size(0), 
	  shdr_table(NULL), shdr_entrySize(0), shdr_num(0), shdr_size(0), 
	  load_start(NULL), load_size(0), load_bias(0){

	inputFile = fopen(filename, "rb");
	if(inputFile == NULL){
		ELOG("File \"%s\" open error.", filename);
		exit(EXIT_FAILURE);
	}
}

ELFReader::~ELFReader(){
	if(load_start != NULL){	delete [](uint8_t*)load_start; }
	if(phdr_table != NULL){ delete [](uint8_t*)phdr_table; }
	if(midPart != NULL){ delete [](uint8_t*)midPart; }
	if(shdr_table != NULL){ delete [](uint8_t*)shdr_table; }
}

bool ELFReader::read(){
	if(!(readElfHeader()&&verifyElfHeader()&&readProgramHeader())){
		ELOG("so-file invalid.");
		exit(EXIT_FAILURE);
	}

	// try to figure out which plan should use to repair the so-file.
	if(readSectionHeader()){
		// damagelevel should set inside checkSectionHeader()
		checkSectionHeader();
		if(!readOtherPart()){
			ELOG("Read other part data failed.");
			exit(EXIT_FAILURE);
		}
	} else{
		damageLevel = 2;
	}
	didRead = true;
	return true;
}

/**
 * load function should be called after readSofile()
 */ 
bool ELFReader::load(){
	if(!didRead){
		read();
	}
	if(reserveAddressSpace() && loadSegments() && findPhdr()){
		didLoad = true;
	}
	return didLoad;
}

// Reserve a virtual address range big enough to hold all loadable
// segments of a program header table. This is done by creating a
// private anonymous mmap() with PROT_NONE.
bool ELFReader::reserveAddressSpace(){
	Elf32_Addr min_vaddr;
	load_size = phdr_table_get_load_size(phdr_table, phdr_num, &min_vaddr);
    if (load_size == 0) {
        ELOG("\"%s\" has no loadable segments\n", filename);
        return false;
    }

    uint8_t* addr = reinterpret_cast<uint8_t*>(min_vaddr);
    // alloc map data, and load in addr
    void* start = new uint8_t[load_size];

    load_start = start;
    load_bias = reinterpret_cast<uint8_t*>(start) - addr;
    return true;
}

/**
 * Map all loadable segments in process' address space.
 * This assume you already called reserveAddressSpace.
 */ 
bool ELFReader::loadSegments(){
	for(int i=0;i<phdr_num;i++){
		const Elf32_Phdr *phdr = &phdr_table[i];
		if(phdr->p_type != PT_LOAD){
			continue;
		}

		// Segment addresses in memory
		Elf32_Addr seg_start = phdr->p_vaddr;
		Elf32_Addr seg_end = seg_start + phdr->p_memsz;

		Elf32_Addr seg_page_start = PAGE_START(seg_start);
		Elf32_Addr seg_page_end = PAGE_END(seg_end);

		Elf32_Addr seg_file_end = seg_start + phdr->p_filesz;

		// File offsets.
		Elf32_Addr file_start = phdr->p_offset;
		Elf32_Addr file_end = file_start + phdr->p_filesz;

		Elf32_Addr file_page_start = PAGE_START(file_start);
		Elf32_Addr file_length = file_end - file_page_start;

		if(file_length != 0){
			// memory data loading
			void* load_point = (uint8_t*)seg_page_start + load_bias;
			if(!loadFileData(load_point, file_length, file_page_start)){
				ELOG("couldn't map \"%s\" segment %d", filename, i);
				return false;
			}
		}

		// if the segment is writable, and does not end on a page boundary,
		// zero-fill it until the page limit.
		if((phdr->p_flags & PF_W) != 0 && PAGE_OFFSET(seg_file_end) > 0){
			memset((uint8_t*)seg_file_end + load_bias, 0, PAGE_SIZE - PAGE_OFFSET(seg_file_end));
		}
		seg_file_end = PAGE_END(seg_file_end);

		// seg_file_end is now the first page address after the file
		// content. If seg_end is larger, we need to zero anything 
		// between them. This is done by using a private anonymous 
		// map for all extra pages.
		if(seg_page_end > seg_file_end){
			void* load_point = (uint8_t*)load_bias + seg_file_end;
			memset(load_point, 0, seg_page_end - seg_file_end);
		}
	}
	return true;
}

/** 
 * Returns the address of the program header table as it appears in the loaded
 * segments in memory. This is in contrast with 'phdr_table_' which
 * is temporary and will be released before the library is relocated.
 */
bool ELFReader::findPhdr() {
    const Elf32_Phdr *phdr_limit = phdr_table + phdr_num;

	// If there is a PT_PHDR, use it directly
	for(const Elf32_Phdr* phdr = phdr_table;phdr < phdr_limit;phdr++){
		if(phdr->p_type == PT_PHDR){
			return checkPhdr(load_bias + phdr->p_vaddr);
		}
	}

	// Otherwise, check the first loadable segment. If its file offset 
	// is 0, it starts with the ELF header, and we can trivially find the
	// loaded program header from it.
	for(const Elf32_Phdr* phdr = phdr_table; phdr < phdr_limit; phdr++){
		if(phdr->p_type == PT_LOAD){
			if(phdr->p_offset == 0){
				Elf32_Addr elf_addr = load_bias + phdr->p_vaddr;
				const Elf32_Ehdr* ehdr = (const Elf32_Ehdr*)(void *)elf_addr;
				Elf32_Addr offset = ehdr->e_phoff;
				return checkPhdr((Elf32_Addr)ehdr + offset);
			}
			break;
		}
	}
    ELOG("can't find loaded phdr for \"%s\"", filename);
    return false;
}

/**
 * Ensures that out program header is actually within a loadable
 * segment. This should help catch badly-formed ELF files that 
 * would cause the linker to crash later when trying to access it.
 */
bool ELFReader::checkPhdr(Elf32_Addr loaded){
	const Elf32_Phdr* phdr_limit = phdr_table + phdr_num;
	Elf32_Addr loaded_end = loaded + (phdr_num * sizeof(Elf32_Phdr));
	for(Elf32_Phdr* phdr = phdr_table; phdr < phdr_limit; phdr++){
		if(phdr->p_type != PT_LOAD){
			continue;
		}
		Elf32_Addr seg_start = phdr->p_vaddr + load_bias;
		Elf32_Addr seg_end = seg_start + phdr->p_filesz;
		if(seg_start <= loaded && loaded_end <= seg_end){
			loaded_phdr = reinterpret_cast<const Elf32_Phdr*>(loaded);
			return true;
		}
	}
	ELOG("\"%s\" loaded phdr %x not in loadable segment", filename, loaded);
	return false;
}

void ELFReader::damagePrint(){
	switch(damageLevel){
		case -1:
			LOG("Not verify yet."); break;
		case 0:
			LOG("\"%s\" is perfect. Do not need to be repaired.", filename); break;
		case 1:
			LOG("\"%s\" section invalid. But still have sh_size. Can use plan A to repair.", filename); break;
		case 2:
			LOG("\"%s\" section totally damage. Should use plan B to repair.", filename); break;
		case 3:
			LOG("\"%s\" is an invalid elf file. Cannot be run.", filename); break;
	}
}

bool ELFReader::readElfHeader(){
	size_t sz = fread(&elf_header, sizeof(char), sizeof(elf_header), inputFile);
	
	if(sz < 0){
		ELOG("Cannot read file \"%s\"", filename);
		return false;
	}
	if(sz != sizeof(elf_header)){
		ELOG("\"%s\" is too small to be an ELF file.", filename);
		return false;
	}

	DLOG("Read ELF header success");
	return true;
}

/* Assume that elf header have been read successful. */
bool ELFReader::verifyElfHeader(){
	
	if(!elf_header.checkMagic()){ // using the function elf.h support
		ELOG("\"%s\" has bad elf magic number. May not an elf file", filename);
		return false;
	}
	
	if(elf_header.getFileClass() == ELFCLASS64){
		ELOG("Not support 64-bit so repair temporary.");
		return false;
	}
	
	if(elf_header.getFileClass() != ELFCLASS32){
		ELOG("\"%s\" is not a 32-bit file", filename);
		return false;
	}
	VLOG("32-bit file \"%s\" read.", filename);
	
	if(elf_header.getDataEncoding() != ELFDATA2LSB){
		ELOG("\"%s\" not little-endian. Unsupport.", filename);
		return false;
	}
	
	if(elf_header.e_type != ET_DYN){
		ELOG("\"%s\" has unexpected e_type. Not a .so file", filename);
		return false;
	}

	if(elf_header.e_version != EV_CURRENT){
		ELOG("\"%s\" has unexpected e_version", filename);
		return false;
	}

	DLOG("ELF header verify pass.");
	return true;
}


bool ELFReader::readProgramHeader(){
	phdr_num = elf_header.e_phnum;
	phdr_entrySize = elf_header.e_phentsize;

	// phdr table max size is 65536, then we can calculate the max of phdr_num	
	if(phdr_num < 1 || phdr_num > 65536/sizeof(Elf32_Phdr)){
		ELOG("\"%s\" has invalid program header number", filename);
		return false;
	}

	phdr_size = phdr_num * phdr_entrySize;
	void *mapPhdr = new uint8_t[phdr_size];
	if(!loadFileData(mapPhdr, phdr_size, elf_header.e_phoff)){
		ELOG("\"%s\" has not valid program header data.", filename);
		return false;
	}
	phdr_table = reinterpret_cast<Elf32_Phdr*>(mapPhdr);

	DLOG("Read program header success.");
	return true;
}

bool ELFReader::readSectionHeader(){
	if(elf_header.e_shnum < 1){
		// Because program valid is necessary. So we use ELOG print the error message.
		// But section need to be repaired. So we accept it invalid.
		// We use VLOG for that who want verbose information.
		VLOG("\"%s\" don't have valid section num.", filename);
		return false;
	}
	shdr_num = elf_header.e_shnum;
	shdr_entrySize = elf_header.e_shentsize;

	// section header table should behind the program header table
	if(elf_header.e_shoff < elf_header.e_phoff + phdr_entrySize*phdr_num){
		VLOG("\"%s\" don't have valid section offset", filename);
		return false;
	}

	shdr_size = shdr_num * shdr_entrySize;
	void *mapShdr = new uint8_t[shdr_size];
	if(!loadFileData(mapShdr, shdr_size, elf_header.e_shoff)){
		VLOG("\"%s\" don't have valid section data.", filename);
		return false;
	}
	shdr_table = reinterpret_cast<Elf32_Shdr*>(mapShdr);

	DLOG("Read section header success.");
	return true;
}


/**
 * Only call after read section header success.
 * Just for build the new file.
 * So we can assume that program header and section header valid here. 
 */
bool ELFReader::readOtherPart(){
	midPart_start = elf_header.e_phoff + phdr_num*phdr_entrySize;
	midPart_end = elf_header.e_shoff;
	midPart_size = midPart_end - midPart_start;
	midPart = new uint8_t[midPart_size];
	
	if(!loadFileData(midPart, midPart_size, midPart_start)){
		ELOG("\"%s\" don't have valid data.", filename);
		return false;
	}

	DLOG("Read the other part data success.");
	return true;
}

/**
 * Just verify the status of section header.
 * Do not try to repair it.
 */
bool ELFReader::checkSectionHeader(){
	//check SHN_UNDEF section
	Elf32_Shdr temp;
	memset((void *)&temp, 0, sizeof(Elf32_Shdr));
	if(memcmp(shdr_table, &temp, sizeof(Elf32_Shdr))){
		VLOG("Wrong section data in 0 section header.");
		damageLevel = 2;
		return false;
	}

	// Get the two load segment index at phdr_table.
	// Thus wo can use segment load address and offset 
	// to check the section header.
	int loadIndex[2] = {-1, -1};
	for(int i=0, j=0;i<phdr_num;i++){
		if(phdr_table[i].p_type == PT_LOAD){
			loadIndex[j++] = i;
		}
	}

	bool isShdrValid = true;
	size_t firstAddress = sizeof(Elf32_Ehdr) + getPhdrSize();
	//check .interp section or the 1 section
	// because some so file can't find .interp section
	if(shdr_table[1].sh_size == 0){
		VLOG("Error shdr_size at index 1");
		damageLevel = 2;
		return false;
	}

	if(shdr_table[1].sh_addr != firstAddress ||
	shdr_table[1].sh_offset != firstAddress){
		// Because we haven't checked the other section size.
		// We not sure whether the other section size available.
		// Thus we cannot return immediately.
		VLOG("Not valid section address or offset at section index 1.");
		isShdrValid = false;
	}
	
	// check each section address and offset if size not empty.
	// Here to check the first LOAD segment.
	int i;
	DLOG("Check the section mapping at first LOAD segment.");
	for(i=2;i<shdr_num;i++){	//we have already check 2 section. So "i" start at 2.
		if(shdr_table[i].sh_size == 0){
			VLOG("Error shdr_size at index %d", i);
			damageLevel = 2;
			return false;
		}
		if(isShdrValid){
			Elf32_Addr curAddr = shdr_table[i-1].sh_addr + shdr_table[i-1].sh_size;
			Elf32_Off curOffset = shdr_table[i-1].sh_offset + shdr_table[i-1].sh_size;
			// bits align
			while(curAddr & (shdr_table[i].sh_addralign-1)) { curAddr++; }
			while(curOffset & (shdr_table[i].sh_addralign-1)) {curOffset++;}
			if(curOffset >= phdr_table[loadIndex[0]].p_filesz) { break; }

			if(curAddr != shdr_table[i].sh_addr || curOffset != shdr_table[i].sh_offset){
				VLOG("Not valid section address or offset at section index %d", i);
				isShdrValid = false;
			}

		}
	}
	
	// check the second LOAD segment
	// if section and offset invalid. All shdr_size would been check in previous loop
	// This loop will pass
	if(isShdrValid){
		// Because we have already check sh_size in previous loop.
		// We don't need to check again with this section.
		if(shdr_table[i].sh_addr != phdr_table[loadIndex[1]].p_paddr ||
		   shdr_table[i].sh_offset != phdr_table[loadIndex[1]].p_offset){
			VLOG("Not valid section address or offset at section index %d", i);
			isShdrValid = false;
		}
		i++;
	}
	if(i < shdr_num) DLOG("Check the section mapping at second LOAD segment.");
	for(;i<shdr_num;i++){
		if(shdr_table[i].sh_size == 0 && shdr_table[i].sh_type != SHT_NOBITS){
			VLOG("Error shdr_size at index %d", i);
			damageLevel = 2;
			return false;
		}
		if(isShdrValid){
			Elf32_Addr curAddr = shdr_table[i-1].sh_addr + shdr_table[i-1].sh_size;
			Elf32_Off curOffset = shdr_table[i-1].sh_offset + shdr_table[i-1].sh_size;
			//bits align
			//FIXME: It looks like .got section align 8. But record 4 in it section header. No idea.
			while(curAddr & (shdr_table[i].sh_addralign-1)) { curAddr++; }
			while(curOffset & (shdr_table[i].sh_addralign-1)) {curOffset++;}
			// Beside Load segment, break
			if(curOffset >= phdr_table[loadIndex[1]].p_filesz + phdr_table[loadIndex[1]].p_offset) { break; }

			if(curAddr != shdr_table[i].sh_addr || curOffset != shdr_table[i].sh_offset){
				VLOG("Not valid section address or offset at section index %d", i);
				isShdrValid = false;
			}
		}
	}

	//FIXME: then figure with .comment .shstrtab and others don't load
	// it's unimportant for these section. Temporary not handle it.
	if(isShdrValid){
		VLOG("The section headers are valid.");
		damageLevel = 0;
	} else{
		VLOG("Section headers need a bit repair");
		damageLevel = 1;
	}
	DLOG("Finish section check. Not fully damage.");
	return isShdrValid;
}

bool ELFReader::loadFileData(void *addr, size_t len, int offset){
	fseek(inputFile, offset, SEEK_SET);
	size_t sz = fread(addr, sizeof(uint8_t), len, inputFile);

	if(sz < 0){
		ELOG("\"%s\" file read error", filename);
		return false;
	}

	if(sz != len){
		ELOG("\"%s\" has no enough data at %x:%x, not valid file.", filename, offset, len);
		return false;
	}
	return true;

}


/* Returns the size of the extent of all the possibly non-contiguous
 * loadable segments in an ELF program header table. This corresponds
 * to the page-aligned size in bytes that needs to be reserved in the
 * process' address space. If there are no loadable segments, 0 is
 * returned.
 *
 * If out_min_vaddr or out_max_vaddr are non-NULL, they will be
 * set to the minimum and maximum addresses of pages to be reserved,
 * or 0 if there is nothing to load.
 */
size_t phdr_table_get_load_size(const Elf32_Phdr* phdr_table,
                                size_t phdr_count,
                                Elf32_Addr* out_min_vaddr,
                                Elf32_Addr* out_max_vaddr)
{
    Elf32_Addr min_vaddr = 0xFFFFFFFFU;
    Elf32_Addr max_vaddr = 0x00000000U;

    bool found_pt_load = false;
    for (size_t i = 0; i < phdr_count; ++i) {
        const Elf32_Phdr* phdr = &phdr_table[i];

        if (phdr->p_type != PT_LOAD) {
            continue;
        }
        found_pt_load = true;

        if (phdr->p_vaddr < min_vaddr) {
            min_vaddr = phdr->p_vaddr;
        }

        if (phdr->p_vaddr + phdr->p_memsz > max_vaddr) {
            max_vaddr = phdr->p_vaddr + phdr->p_memsz;
        }
    }
    if (!found_pt_load) {
        min_vaddr = 0x00000000U;
    }

    min_vaddr = PAGE_START(min_vaddr);
    max_vaddr = PAGE_END(max_vaddr);

    if (out_min_vaddr != NULL) {
        *out_min_vaddr = min_vaddr;
    }
    if (out_max_vaddr != NULL) {
        *out_max_vaddr = max_vaddr;
    }
    return max_vaddr - min_vaddr;
}

/* Return the address and size of the ELF file's .dynamic section in memory,
 * or NULL if missing.
 *
 * Input:
 *   phdr_table  -> program header table
 *   phdr_count  -> number of entries in tables
 *   load_bias   -> load bias
 * Output:
 *   dynamic       -> address of table in memory (NULL on failure).
 *   dynamic_count -> number of items in table (0 on failure).
 *   dynamic_flags -> protection flags for section (unset on failure)
 * Return:
 *   void
 */
void
phdr_table_get_dynamic_section(const Elf32_Phdr* phdr_table,
                               int               phdr_count,
                               Elf32_Addr        load_bias,
                               Elf32_Dyn**       dynamic,
                               size_t*           dynamic_count,
                               Elf32_Word*       dynamic_flags)
{
    const Elf32_Phdr* phdr = phdr_table;
    const Elf32_Phdr* phdr_limit = phdr + phdr_count;

    for (phdr = phdr_table; phdr < phdr_limit; phdr++) {
        if (phdr->p_type != PT_DYNAMIC) {
            continue;
        }

        *dynamic = reinterpret_cast<Elf32_Dyn*>(load_bias + phdr->p_vaddr);
        if (dynamic_count) {
            *dynamic_count = (unsigned)(phdr->p_memsz / sizeof(Elf32_Dyn));
        }
        if (dynamic_flags) {
            *dynamic_flags = phdr->p_flags;
        }
        return;
    }
    *dynamic = NULL;
    if (dynamic_count) {
        *dynamic_count = 0;
    }
}


/* Return the address and size of the .ARM.exidx section in memory,
 * if present.
 *
 * Input:
 *   phdr_table  -> program header table
 *   phdr_count  -> number of entries in tables
 *   load_bias   -> load bias
 * Output:
 *   arm_exidx       -> address of table in memory (NULL on failure).
 *   arm_exidx_count -> number of items in table (0 on failure).
 * Return:
 *   0 on error, -1 on failure (_no_ error code in errno)
 */
int
phdr_table_get_arm_exidx(const Elf32_Phdr* phdr_table,
                         int               phdr_count,
                         Elf32_Addr        load_bias,
                         Elf32_Addr**      arm_exidx,
                         unsigned*         arm_exidx_count)
{
    const Elf32_Phdr* phdr = phdr_table;
    const Elf32_Phdr* phdr_limit = phdr + phdr_count;

    for (phdr = phdr_table; phdr < phdr_limit; phdr++) {
        if (phdr->p_type != PT_ARM_EXIDX)
            continue;

        *arm_exidx = (Elf32_Addr*)(load_bias + phdr->p_vaddr);
        *arm_exidx_count = (unsigned)(phdr->p_memsz / sizeof(Elf32_Addr));
        return 0;
    }
    *arm_exidx = NULL;
    *arm_exidx_count = 0;
    return -1;
}