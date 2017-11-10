#ifndef _SO_REBUILDER_ELFREADER_H_
#define _SO_REBUILDER_ELFREADER_H_

#include "elf.h"

class ELFReader{

public:
	ELFReader(const char * filename);
	~ELFReader();

	void load();
	void damagePrint();
private:

	const char* filename;
	const FILE* inputFile;
	bool isLoad;
	/** 
	 * This is a parameter define by myself, which target at 
	 * evaluate the damage level of the .so file.
	 * I have made 4 number to represent it. The bigger number 
	 * stand for bigger damage of the .so file.
	 *  -1   ==>   The file haven't evaluated yet.
	 *   0   ==>   The so file is complete.
	 *   1   ==>   Offset and Vaddr in section header have been
	 *             damaged. But still have size information.
	 *   2   ==>   All section information have been damaged.
	 *   3   ==>   The whole file beyond recognition.
	 * The .so file still can be used in damage level 0~2. But
	 * cannot run under the level 3. The ELFRebuilder will give
	 * different programs to repair the file according to the 
	 * damage level.
	 */
	int damageLevel;


	/**
	 * This is standard structure for an elf file. If damageLevel is 1 or 0.
	 * We don't necessary load the content into memory and try to rebuild the 
	 * section.
	 * Otherwise, we need to load the so file and rebuild the section.
	 */
	Elf32_Ehdr elf_header; 		// store elf header

	Elf32_Phdr* phdr_table; 	// store program header table
	Elf32_Half phdr_entrySize;	// program header entry size
	size_t phdr_num;			// the number of program header 

	Elf32_Addr midPart_start;	// start address between program table and section table
	Elf32_Addr midPart_end;		// end address between program table and section table 

	Elf32_Shdr* shdr_table;		// store section header table
	Elf32_Half shdr_entrySize;	// section header entry size
	size_t shdr_num;			// the number of section header

	/* Load information */
	void* load_start;			// First page of reserved address space.
	Elf32_Addr load_size;		// Size in bytes of reserved address space.
	Elf32_Addr load_bias;		// Load bias.

};

#endif