#include "drmu_util.h"
#include <ctype.h>
#include <error.h>
#include <stdlib.h>

char *
drmu_util_parse_mode(const char * s, unsigned int * pw, unsigned int * ph, unsigned int * phz)
{
    unsigned long w = 0, h = 0, hz = 0;

    if (isdigit(*s)) {
        w = strtoul(s, (char **)&s, 10);
        if (*s != 'x')
            return (char *)s;
        h = strtoul(s + 1, (char **)&s, 10);
    }

    if (*s == '@') {
        hz = strtoul(s + 1, (char **)&s, 10) * 1000;

        if (*s == '.') {
            unsigned int m = 100;
            while (isdigit(*++s)) {
                hz += (*s - '0') * m;
                m /= 10;
            }
        }
    }

    *pw = (unsigned int)w;
    *ph = (unsigned int)h;
    *phz = (unsigned int)hz;
    return (char *)s;
}

