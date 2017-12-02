#ifndef _SO_REBUILDER_LOG_H_
#define _SO_REBUILDER_LOG_H_

#include <stdio.h>

extern bool VERBOSE;
extern bool DEBUG;


#define NONE 	"\e[0m"   				// end of the ansi control
#define BLUE 	"\e[0;34m"				// Blue color, normal
#define B_BLUE 	"\e[1;34m"				// Blue color, bold
#define RED 	"\e[0;31m"				// Red color, normal
#define B_RED	"\e[1;31m"				// Red color, bold
#define YELLOW	"\e[0;33m"				// Yellow color


#define VLOG(fmt, ...) if(VERBOSE) printf(YELLOW fmt "\n" NONE, ##__VA_ARGS__)			//verbose log
#define DLOG(fmt, ...) if(DEBUG) printf(B_BLUE "[DEBUG] " BLUE fmt "\n" NONE, ##__VA_ARGS__)	//debug log
#define LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)						//normal log
#define ELOG(fmt, ...) printf(B_RED "[ERROR] " RED fmt "\n" NONE, ##__VA_ARGS__)				//error log

#endif