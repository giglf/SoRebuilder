#ifndef _SO_REBUILDER_LOG_H_
#define _SO_REBUILDER_LOG_H_

#include <stdio.h>

static bool VERBOSE = false;
static bool DEBUG = false;


#define VLOG(fmt, ...) if(VERBOSE) printf(fmt "\n", ##__VA_ARGS__)			//verbose log
#define DLOG(fmt, ...) if(DEBUG) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)	//debug log
#define LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)						//normal log
#define ELOG(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)				//error log

#endif