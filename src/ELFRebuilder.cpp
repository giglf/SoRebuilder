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
	//TODO:
}