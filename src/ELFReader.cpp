#include <cstdio>
#include <cstdint>
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
	}
}

ELFReader::~ELFReader(){
	if(load_start != NULL){
		delete [](uint8_t*)load_start;
	}
}