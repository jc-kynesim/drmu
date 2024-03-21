#include "drmu_util.h"

#include "drmu.h"

#include <ctype.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdrm/drm_mode.h>

static unsigned long
h_to_w(const unsigned long h)
{
    switch (h) {
        case 480:
        case 576:
            return 720;
        case 720:
            return 1280;
        case 1080:
            return 1920;
        case 2160:
            return 3840;
        default:
            break;
    }
    return 0;
}

char *
drmu_util_parse_mode_simple_params(const char * s, drmu_mode_simple_params_t * const p)
{
    unsigned long w = 0, h = 0, hz = 0;
    bool il = false;
    bool drmhz = false;

    memset(p , 0, sizeof(*p));

    if (isdigit(*s)) {
        h = strtoul(s, (char **)&s, 10);

        if (*s == 'p' || *s == 'i') {
            w = h_to_w(h);
        }
        else if (*s == 'x') {
            w = h;
            h = strtoul(s + 1, (char **)&s, 10);
        }
        else {
            return (char *)s;
        }
    }

    // Consume 'i' or 'p'
    // Can still have (now) optional hz separator after
    if (*s == 'p') {
        ++s;
    }
    else if (*s == 'i') {
        il = true;
        ++s;
    }

    // I've used '@' in the past to separate size from hz
    // DRM uses '-' in modetest so accept that
    if (*s == '@') {
        ++s;
    }
    else if (*s == '-') {
        drmhz = true;
        ++s;
    }

    if (isdigit(*s)) {
        hz = strtoul(s, (char **)&s, 10) * 1000;

        if (*s == '.') {
            unsigned int m = 100;
            while (isdigit(*++s)) {
                hz += (*s - '0') * m;
                m /= 10;
            }
        }
    }

    // DRM thinks in frame rate, rest of the world specifies as field rate
    if (il && !drmhz)
        hz /= 2;

    p->width  = (unsigned int)w;
    p->height = (unsigned int)h;
    p->hz_x_1000 = (unsigned int)hz;
    p->flags = !il ? 0 : DRM_MODE_FLAG_INTERLACE;

    return (char *)s;
}

char *
drmu_util_simple_param_to_mode_str(char * buf, size_t buflen, const drmu_mode_simple_params_t * const p)
{
    int hz = p->hz_x_1000;

    if ((p->flags & DRM_MODE_FLAG_INTERLACE))
        hz *= 2;

    snprintf(buf, buflen, "%dx%d%c%d.%03d",
             p->width, p->height,
             (p->flags & DRM_MODE_FLAG_INTERLACE) ? 'i' : 'p',
             hz / 1000, hz % 1000);
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

drmu_ufrac_t
drmu_util_guess_par(const unsigned int w, const unsigned int h)
{
    if (((w == 720 || w == 704) && (h == 480 || h == 576)) ||
        ((w == 360 || w == 352) && (h == 240 || h == 288)))
    {
        return (drmu_ufrac_t){.num = 4, .den = 3};
    }
    return drmu_ufrac_reduce((drmu_ufrac_t){.num = w, .den = h});
}

drmu_ufrac_t
drmu_util_guess_simple_mode_par(const drmu_mode_simple_params_t * const p)
{
    if (p->par.den != 0 && p->par.num != 0)
        return p->par;
    return drmu_util_guess_par(p->width, p->height);
}

