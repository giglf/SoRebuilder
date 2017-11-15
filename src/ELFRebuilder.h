#ifndef _SO_REBUILDER_ELFREBUILDER_H_
#define _SO_REBUILDER_ELFREBUILDER_H_


#include "ELFReader.h"

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

	Elf32_Ehdr elf_header;
	Elf32_Phdr *phdr_table;

	uint8_t *rebuild_data = NULL;
	size_t rebuild_size = 0;
	
	bool simpleRebuild();	// just repair the section address and offset.
	bool totalRebuild();	// all rebuild.
	bool rebuildData();		// restore data to rebuild_data.
};


#endif