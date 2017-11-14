#ifndef _SO_REBUILDER_ELFREBUILDER_H_
#define _SO_REBUILDER_ELFREBUILDER_H_


#include "ELFReader.h"

class ELFRebuilder{

public:

	ELFRebuilder(ELFReader &_reader, bool _force);
	~ELFRebuilder();
	bool rebuild();
private:

	bool force;			// using to mark if force to rebuild the section.
	ELFReader &reader;

	uint8_t *rebuid_data = NULL;
	size_t rebuid_size = 0;
	
	bool simpleRebuild();	// just repair the section address and offset.
	bool totalRebuild();	// all rebuild.

};


#endif