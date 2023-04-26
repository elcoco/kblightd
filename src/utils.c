#include "utils.h"

int do_debug;

void debug(char* fmt, ...)
{
    if (do_debug) {
        va_list ptr;
        va_start(ptr, fmt);
        vfprintf(stdout, fmt, ptr);
        va_end(ptr);
    }
}

void info(char* fmt, ...)
{
    va_list ptr;
    va_start(ptr, fmt);
    vfprintf(stdout, fmt, ptr);
    va_end(ptr);
}

void error(char* fmt, ...)
{
    va_list ptr;
    va_start(ptr, fmt);
    vfprintf(stderr, fmt, ptr);
    va_end(ptr);
}

int err_stoi(char *str)
{
    /* String to unsigned integer, return -1 on error */
    int ret = 0;
    char *c = str;
    for (int i=strlen(str)-1 ; i>=0 ; i--, c++) {
        if (*c >='0' && *c <= '9')
            ret += (*c-48) * pow(10, i);
        else
            return -1;
    }
    return ret;
}
