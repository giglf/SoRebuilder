#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "ELFReader.h"
#include "Log.h"

ELFReader::ELFReader(const char *filename)
	: filename(filename), inputFile(NULL), damageLevel(0), isLoad(false),
	  phdr_table(NULL), phdr_entrySize(0), phdr_num(0),
	  midPart_start(0), midPart_end(0), 
	  shdr_table(NULL), shdr_entrySize(0), shdr_num(0),
	  load_start(NULL), load_size(0), load_bias(0){

	inputFile = fopen(filename, "rb");
	if(inputFile == NULL){
		ELOG("File \"%s\" open error.", filename);
		exit(EXIT_FAILURE);
	}
}

ELFReader::~ELFReader(){
	if(load_start != NULL){
		delete [](uint8_t*)load_start;
	}
}

bool ELFReader::readSofile(){
	if(!(readElfHeader()&&verifyElfHeader()&&readProgramHeader())){
		ELOG("so-file invaild.");
		exit(EXIT_FAILURE);
	}

	// try to figure out which plan should use to repair the so-file.
	if(readSectionHeader()){
		//TODO: if(checkSectionHeader()){
		// 	damageLevel = 0;
		// } else{
		// 	damageLevel = 1;
		// }
		if(!readOtherPart()){
			ELOG("Read other part data failed.");
			exit(EXIT_FAILURE);
		}
	} else{
		damageLevel = 2;
	}
	return true;
}

bool ELFReader::readElfHeader(){
	size_t sz = fread(&elf_header, sizeof(elf_header), 1, inputFile);
	DLOG("ReadELFHeader");
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

	size_t phdrSize = phdr_num * phdr_entrySize;
	void *mapPhdr = new uint8_t[phdrSize];
	if(!loadFileData(mapPhdr, phdrSize, elf_header.e_phoff)){
		ELOG("\"%s\" has not valid program header data.", filename);
		return false;
	}
	phdr_table = reinterpret_cast<Elf32_Phdr*>(mapPhdr);
	
	DLOG("Read program header success.");
	return true;
}

bool ELFReader::readSectionHeader(){
	if(elf_header.e_shnum < 1){
		// Because program vaild is necessary. So we use ELOG print the error message.
		// But section need to be repaired. So we accept it invaild.
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

	size_t shdrSize = shdr_num * shdr_entrySize;
	void *mapShdr = new uint8_t[shdrSize];
	if(!loadFileData(mapShdr, shdrSize, elf_header.e_shoff)){
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
 * So we can assume that program header and section header vaild here. 
 */
bool ELFReader::readOtherPart(){
	midPart_start = elf_header.e_phoff + phdr_num*phdr_entrySize;
	midPart_end = elf_header.e_shoff;
	size_t midPartSize = midPart_end - midPart_start;
	void *mapMidPart = new uint8_t[midPartSize];
	
	if(!loadFileData(mapMidPart, midPartSize, midPart_start)){
		ELOG("\"%s\" don't have valid data.", filename);
		return false;
	}

	DLOG("Read the other part data success.");
	return true;
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