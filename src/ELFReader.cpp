#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "ELFReader.h"
#include "Log.h"

ELFReader::ELFReader(const char *filename)
	: filename(filename), inputFile(NULL), damageLevel(-1), isLoad(false),
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

bool ELFReader::readSofile(){
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
	return true;
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
	void *mapMidPart = new uint8_t[midPart_size];
	
	if(!loadFileData(mapMidPart, midPart_size, midPart_start)){
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