#ifndef _SO_REBUILDER_ELFREADER_H_
#define _SO_REBUILDER_ELFREADER_H_

#include <cstdio>
#include "elf.h"

class ELFReader{

public:
	ELFReader(const char * filename);
	~ELFReader();

	bool load();
	bool read();
	void damagePrint();

private:

	bool readElfHeader();
	bool verifyElfHeader();
	bool readProgramHeader();
	bool readSectionHeader();
	bool readOtherPart();
	bool reserveAddressSpace();
	bool loadSegments();
	bool findPhdr();
	bool checkPhdr(Elf32_Addr loaded);

	bool checkSectionHeader();
	bool loadFileData(void *addr, size_t len, int offset);

	const char* filename;
	FILE* inputFile;

	bool didLoad;
	bool didRead;
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
	size_t phdr_size;			// size of program headers

	void *midPart;				// the load address of the middle part between program table and section table
	Elf32_Addr midPart_start;	// start address between program table and section table
	Elf32_Addr midPart_end;		// end address between program table and section table
	size_t midPart_size;		// size of the Middle part. 

	Elf32_Shdr* shdr_table;		// store section header table
	Elf32_Half shdr_entrySize;	// section header entry size
	size_t shdr_num;			// the number of section header
	size_t shdr_size;			// size of section headers

	/* Load information */
	void* load_start;			// First page of reserved address space.
	Elf32_Addr load_size;		// Size in bytes of reserved address space.
	Elf32_Addr load_bias;		// Load bias.

	const Elf32_Phdr* loaded_phdr;	// Loaded phdr.

public:

	bool isRead() { return didRead; }
	bool isLoad() { return didLoad; }
	int getDamageLevel() {return damageLevel; }

	Elf32_Ehdr getElfHeader() { return elf_header; }
	Elf32_Shdr* getShdrTable() { return shdr_table; }
	void* getMidPart() { return midPart; }
	Elf32_Phdr* getPhdrTable() { return phdr_table; }

	size_t getPhdrSize() { return phdr_size; }
	size_t getMidPartSize() { return midPart_size; }
	size_t getShdrSize() { return shdr_size; }

	int getShdrNum() { return shdr_num; }
	int getPhdrNum() { return phdr_num; }

	const Elf32_Phdr* getLoadedPhdr() { return loaded_phdr; }
	Elf32_Addr getLoadBias() { return load_bias; }

};


//The functions below are refer to android source
size_t phdr_table_get_load_size(const Elf32_Phdr* phdr_table,
								size_t phdr_count,
								Elf32_Addr* out_min_vaddr = NULL,
								Elf32_Addr* out_max_vaddr = NULL);

void phdr_table_get_dynamic_section(const Elf32_Phdr* phdr_table,
							   		int               phdr_count,
							  		Elf32_Addr        load_bias,
							   		Elf32_Dyn**       dynamic,
							   		size_t*           dynamic_count,
							   		Elf32_Word*       dynamic_flags);

int phdr_table_get_arm_exidx(const Elf32_Phdr* phdr_table,
							 int               phdr_count,
						 	 Elf32_Addr        load_bias,
						 	 Elf32_Addr**      arm_exidx,
						 	 unsigned*         arm_exidix_count);

void phdr_table_get_interpt_section(const Elf32_Phdr* phdr_table, 
									int 			  phdr_count,
									Elf32_Addr		  load_bias, 
									Elf32_Addr**	  interp,
									size_t*			  interp_size);


#endif