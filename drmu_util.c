#include "drmu.h"
#include "drmu_util.h"

#include <ctype.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdrm/drm_mode.h>

char *
drmu_util_parse_mode_simple_params(const char * s, drmu_mode_simple_params_t * const p)
{
    unsigned long w = 0, h = 0, hz = 0;
    bool il = false;

    memset(p , 0, sizeof(*p));

    if (isdigit(*s)) {
        w = strtoul(s, (char **)&s, 10);
        if (*s != 'x')
            return (char *)s;
        h = strtoul(s + 1, (char **)&s, 10);
    }

    if (*s == 'i') {
        il = true;
        ++s;
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

    p->width  = (unsigned int)w;
    p->height = (unsigned int)h;
    p->hz_x_1000 = (unsigned int)hz;
    p->flags = !il ? 0 : DRM_MODE_FLAG_INTERLACE;

    return (char *)s;
}

char *
drmu_util_simple_param_to_mode_str(char * buf, size_t buflen, const drmu_mode_simple_params_t * const p)
{
    snprintf(buf, buflen, "%dx%d%s@%d.%03d",
             p->width, p->height,
             (p->flags & DRM_MODE_FLAG_INTERLACE) ? "i" : "",
             p->hz_x_1000 / 1000, p->hz_x_1000 % 1000);
    return buf;
}

char *
drmu_util_parse_mode(const char * s, unsigned int * pw, unsigned int * ph, unsigned int * phz)
{
    drmu_mode_simple_params_t p;
    char * r = drmu_util_parse_mode_simple_params(s, &p);
    *pw = p.width;
    *ph = p.height;
    *phz = p.hz_x_1000;
    return r;
}


