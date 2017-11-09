#ifndef _SO_REBUILDER_LOG
#define _SO_REBUILDER_LOG

#include <stdio.h>

static bool VERBOSE = false;
static bool DEBUG = false;


#define VLOG(fmt, ...) if(VERBOSE) printf(fmt"\n", ##__VA_ARGS__)
#define DLOG(fmt, ...) if(DEBUG) printf("[DEBUG] "fmt"\n", ##__VA_ARGS__)
#define LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)


#endif