#ifndef __TRACE_H_
#define __TRACE_H_

#include <stdio.h>

#define TRACE_LEVEL 2

#define ERROR_LEVEL 0
#define DEBUG_LEVEL 1
#define INFO_LEVEL  2

#define TIME_TSET 0

#ifdef TRACE_LEVEL

	#if (TRACE_LEVEL >= ERROR_LEVEL)
		#define ERROR(FMT, ...) printf("%s:%d:\t%s\terror: " FMT "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
	#else
		#define ERROR(FMT, ...)
	#endif

	#if (TRACE_LEVEL >= DEBUG_LEVEL)
		#define DEBUG(FMT, ...) printf("%s:%d:\t%s\tdebug: " FMT "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
	#else
		#define DEBUG(FMT, ...)
	#endif

	#if (TRACE_LEVEL >= INFO_LEVEL)
		#define INFO(FMT, ...) printf("%s:%d:\t%s\tinfo: " FMT "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
	#else
		#define INFO(FMT, ...)
	#endif

#else

	#define ERROR(FMT, ...)
	#define DEBUG(FMT, ...)
	#define INFO(FMT, ...)

#endif  //TRACE_LEVEL

#endif  //__TRACE_H_
