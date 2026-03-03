#include <errno.h>
#include <stdio.h>
#include <stddef.h>

#include "drmu_util.h"

static const struct parse_rect_check_s {
    const char * s;
    int rv;
    drmu_rect_t r;
    ptrdiff_t eos_off;  /* expected peos offset from s; only checked on success */
} parse_rect_checks[] = {
    /* basic WxH */
    {"1920x1080",              0, {  0,   0, 1920, 1080},  9},
    {"1280x720",               0, {  0,   0, 1280,  720},  8},
    /* with position */
    {"1920x1080@100,200",      0, {100, 200, 1920, 1080}, 17},
    {"1920x1080@-10,-20",      0, {-10, -20, 1920, 1080}, 17},
    {"1920x1080@0,0",          0, {  0,   0, 1920, 1080}, 13},
    /* hex values accepted (strtoul base 0) */
    {"0x780x0x438",            0, {  0,   0, 1920, 1080}, 11},
    /* trailing text: accepted, peos points past parsed region */
    {"1920x1080rest",          0, {  0,   0, 1920, 1080},  9},
    {"1920x1080@100,200rest",  0, {100, 200, 1920, 1080}, 17},
    /* error: empty */
    {"",                -EINVAL, {0, 0, 0, 0}, 0},
    /* error: no digits */
    {"abc",             -EINVAL, {0, 0, 0, 0}, 0},
    /* error: missing height */
    {"1920x",           -EINVAL, {0, 0, 0, 0}, 0},
    /* error: missing width */
    {"x1080",           -EINVAL, {0, 0, 0, 0}, 0},
    /* error: "0x1080" parsed as single hex value 4224, no 'x' separator left */
    {"0x1080",          -EINVAL, {0, 0, 0, 0}, 0},
    /* error: @ present but missing comma */
    {"1920x1080@100",   -EINVAL, {0, 0, 0, 0}, 0},
    /* error: @ present but x is empty */
    {"1920x1080@,200",  -EINVAL, {0, 0, 0, 0}, 0},
    /* error: @ present but y is empty */
    {"1920x1080@100,",  -EINVAL, {0, 0, 0, 0}, 0},
};

int
main(int argc, char *argv[])
{
    unsigned int i;
    unsigned int x = 0;
    (void)argc;
    (void)argv;

    for (i = 0; i != sizeof(parse_rect_checks)/sizeof(parse_rect_checks[0]); ++i)
    {
        const struct parse_rect_check_s * const c = parse_rect_checks + i;
        drmu_rect_t r;
        char * eos = NULL;
        const int rv = drmu_parse_rect(c->s, &eos, &r);

        if (rv != c->rv ||
            r.x != c->r.x || r.y != c->r.y || r.w != c->r.w || r.h != c->r.h ||
            (rv == 0 && eos - c->s != c->eos_off))
        {
            printf("parse_rect fail [%s]: got rv=%d {%d,%d,%u,%u} eos+%td"
                   " expected rv=%d {%d,%d,%u,%u} eos+%td\n",
                   c->s,
                   rv, r.x, r.y, r.w, r.h, eos - c->s,
                   c->rv, c->r.x, c->r.y, c->r.w, c->r.h, c->eos_off);
            ++x;
        }
    }

    /* verify peos=NULL does not crash */
    {
        drmu_rect_t r;
        drmu_parse_rect("1920x1080", NULL, &r);
    }

    if (x != 0)
        printf("*** parse_rect check failed %d tests\n", x);
    else
        printf("parse_rect check OK\n");

    return x == 0 ? 0 : 1;
}
