#ifndef MYSNPRINTF_H
#define MYSNPRINTF_H

#include "vaarg-altabi.h"

int mysnprintf(char* out, unsigned long size, char* fmt, ...);
int myvsniprintf(char*restrict out, unsigned long size, unsigned max_args, const char*restrict fmt, VA_LIST ap);
int myulogprintf(char*restrict out, unsigned long size, unsigned arg_count, UINT64 isarg64bitmask, const char*restrict fmt, A2_VA_LIST ap);

#endif
