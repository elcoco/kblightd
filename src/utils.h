#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>     // va_list
#include <stdio.h>
#include <string.h>
#include <math.h>


void info(char* fmt, ...);
void debug(char* fmt, ...);
void error(char* fmt, ...);
int err_stoi(char *str);

// global that indicates showing debugging messages
extern int do_debug;


#endif // !UTILS_H
