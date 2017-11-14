#include "ELFRebuilder.h"
#include "Log.h"

ELFRebuilder::ELFRebuilder(ELFReader &_reader, bool _force)
	: reader(_reader), force(_force){}

ELFRebuilder::~ELFRebuilder(){}


bool ELFRebuilder::rebuild(){
	if(force || reader.getDamageLevel() == 2){
		return totalRebuild();
	} else if(reader.getDamageLevel() == 1){
		return simpleRebuild();
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
	rebuid_size = sizeof(Elf32_Ehdr) + reader.getPhdrSize() + reader.getOtherSize() + reader.getShdrSize();
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

	bool firstAddress = sizeof(Elf32_Ehdr) + reader.getPhdrSize();
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
		
		shdr_table[i].sh_addr = curAddr;
		shdr_table[i].sh_offset = curOffset;	
		
		if(curOffset == phdr_table[loadIndex[0]].p_filesz) { break; }
	}
	
	// Rebuild the second LOAD segment
	// First get the second LOAD segment address and offset
	DLOG("Start repair the section mapping at second LOAD segment.");
	i++;
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


bool ELFRebuilder::totalRebuild(){
	//TODO:
}