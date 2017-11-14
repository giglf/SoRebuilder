#include <iostream>
#include <cstdio>
#include <getopt.h>
#include <string>
#include "Log.h"
#include "ELFReader.h"

void usage(){
	std::cout<<"So Rebuilder  --Powered by giglf\n"
			 <<"usage: sb <file.so>\n"
			 <<"       sb <file.so> -o <repaired.so>\n"
			 <<"\n"
			 <<"option: \n"
			 <<"    -o --output <outputfile>   Specify the output file name. Or append \"_repaired\" default.\n"
			 <<"    -c --check                 Check the damage level and print it.\n"
			 <<"    -f --force                 Force to fully rebuild the section.\n"
			 <<"    -v --verbose               Print the verbose repair information\n"
			 <<"    -h --help                  Print this usage.\n"
			 <<"    -d --debug                 Print this program debug log."
			 <<std::endl;
}

/* Using to store the command line argument. */
struct GlobalArgument{
	std::string inFileName;
	std::string outFileName;	// -o option
	bool check;					// -c option
	bool force;
	bool verbose;				// -v option
	bool debug;					// -d option
	bool isValid;				// is the argv Valid
}GlobalArgv;

static const char *optString = "o:cvhd";
static const struct option longOpts[] = {
	{"output", required_argument, NULL, 'o'},
	{"check", no_argument, NULL, 'c'},
	{"force", no_argument, NULL, 'f'},
	{"verbose", no_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},
	{"debug", no_argument, NULL, 'd'}
};

int main(int argc, char *argv[]){
	
	if(argc <= 1){
		usage();
		return 0;
	}
	
	GlobalArgv.check = false;
	GlobalArgv.force = false;
	GlobalArgv.verbose = false;
	GlobalArgv.debug = false;
	GlobalArgv.isValid = true;

	int opt;
	int longIndex;
	// I have try that getopt doesn't work with multiple option.
	// So it must using getopt_long instead.
	while((opt = getopt_long(argc, argv, optString, longOpts, &longIndex)) != -1){
		switch(opt){
			case 'o':
				GlobalArgv.outFileName = optarg;
				break;
			case 'c':
				GlobalArgv.check = true;
				break;
			case 'f':
				GlobalArgv.force = true;
				break;
			case 'v':
				GlobalArgv.verbose = true;
				break;
			case 'h':
				usage();
				return 0;
			case 'd':
				GlobalArgv.debug = true;
				break;
			default:	
				GlobalArgv.isValid = false;
				break;
		}
	}
	GlobalArgv.inFileName = argv[optind];	

	if(!GlobalArgv.isValid) { usage(); return 1; }
	if(GlobalArgv.debug) { DEBUG = true; LOG("=====Debug modol=====");}
	if(GlobalArgv.verbose) { VERBOSE = true; DLOG("verbose set"); }
	if(GlobalArgv.outFileName.empty()){
		GlobalArgv.outFileName = GlobalArgv.inFileName.substr(0, GlobalArgv.inFileName.size()-3) + "_repaired.so";
	}
	DLOG("InputFile: %s", GlobalArgv.inFileName.c_str());
	DLOG("OutputFile: %s", GlobalArgv.outFileName.c_str());


	ELFReader reader(GlobalArgv.inFileName.c_str());
	reader.readSofile();
	if(GlobalArgv.check){
		reader.damagePrint();
		DLOG("Enter check elf file");
	}

	/**
	 * Because judge if a so-file section headers fully damage or 
	 * just missing address and offset is difficult to me. For example, 
	 * I don't know why in some file .got section are align 8 but the 
	 * section record align 4. That lead the wrong result with the check 
	 * function.
	 * Because of my limit ability. I recommand you using -f option to 
	 * force rebuild the section headers.
	 * Hope you can help me with it.
	 */
	//TODO: ELFRebuilder rebuilder(reader, force);

	
	return 0;
}