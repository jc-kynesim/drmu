#include <errno.h>
#include <stdio.h>
#include <stddef.h>

#include "drmu_util.h"
#include "drmu_chroma.h"

static const struct parse_rgba_check_s {
    const char * s;
    int rv;
    drmu_rgba_t c;
    ptrdiff_t eos_off;  /* expected peos offset from s; only checked on success */
} parse_rgba_checks[] = {
    /* #RRGGBB - alpha defaults to 0xffff */
    {"#ff8800",     0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xffff},  7},
    {"#000000",     0, {.r=0x0000, .g=0x0000, .b=0x0000, .a=0xffff},  7},
    {"#ffffff",     0, {.r=0xffff, .g=0xffff, .b=0xffff, .a=0xffff},  7},
    {"#804020",     0, {.r=0x8080, .g=0x4040, .b=0x2020, .a=0xffff},  7},
    /* #RRGGBB - no leading '#' now fails (# is required for web format) */
    {"ff8800",      -EINVAL, {0, 0, 0, 0},  0},
    /* #RRGGBBAA */
    {"#ff8800cc",   0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xcccc},  9},
    {"#ffffff00",   0, {.r=0xffff, .g=0xffff, .b=0xffff, .a=0x0000},  9},
    {"#00000080",   0, {.r=0x0000, .g=0x0000, .b=0x0000, .a=0x8080},  9},
    /* #RGB - each nibble expanded */
    {"#f80",        0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xffff},  4},
    {"#000",        0, {.r=0x0000, .g=0x0000, .b=0x0000, .a=0xffff},  4},
    {"#fff",        0, {.r=0xffff, .g=0xffff, .b=0xffff, .a=0xffff},  4},
    {"#abc",        0, {.r=0xaaaa, .g=0xbbbb, .b=0xcccc, .a=0xffff},  4},
    /* #RGBA */
    {"#f80c",       0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xcccc},  5},
    {"#fff0",       0, {.r=0xffff, .g=0xffff, .b=0xffff, .a=0x0000},  5},
    /* trailing text: accepted, peos points past parsed region */
    {"#ff8800rest", 0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xffff},  7},
    /* lowercase and uppercase both accepted (strtoul base 16) */
    {"#FF8800",     0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xffff},  7},
    {"#AABBCC",     0, {.r=0xaaaa, .g=0xbbbb, .b=0xcccc, .a=0xffff},  7},
    /* error: empty */
    {"",       -EINVAL, {0, 0, 0, 0}, 0},
    /* error: '#' only */
    {"#",      -EINVAL, {0, 0, 0, 0}, 0},
    /* error: non-hex */
    {"#xyz",   -EINVAL, {0, 0, 0, 0}, 0},
    /* error: wrong digit count (5 digits) */
    {"#12345", -EINVAL, {0, 0, 0, 0}, 0},
    /* error: wrong digit count (7 digits) */
    {"#1234567", -EINVAL, {0, 0, 0, 0}, 0},
    /* colon-separated 16-bit decimal: R:G:B (alpha defaults to 0xffff) */
    {"65535:34952:0",         0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xffff}, 13},
    {"0:0:0",                 0, {.r=0x0000, .g=0x0000, .b=0x0000, .a=0xffff},  5},
    {"65535:65535:65535",     0, {.r=0xffff, .g=0xffff, .b=0xffff, .a=0xffff}, 17},
    {"32896:16448:8224",      0, {.r=0x8080, .g=0x4040, .b=0x2020, .a=0xffff}, 16},
    /* colon-separated 16-bit decimal: R:G:B:A */
    {"65535:34952:0:52428",   0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xcccc}, 19},
    {"65535:65535:65535:0",   0, {.r=0xffff, .g=0xffff, .b=0xffff, .a=0x0000}, 19},
    {"0:0:0:32896",           0, {.r=0x0000, .g=0x0000, .b=0x0000, .a=0x8080}, 11},
    /* colon-separated 16-bit hex (0x prefix): R:G:B */
    {"0xffff:0x8888:0x0000",        0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xffff}, 20},
    {"0x8080:0x4040:0x2020",        0, {.r=0x8080, .g=0x4040, .b=0x2020, .a=0xffff}, 20},
    /* colon-separated 16-bit hex (0x prefix): R:G:B:A */
    {"0xffff:0x8888:0x0000:0xcccc", 0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xcccc}, 27},
    /* colon-separated: trailing text accepted */
    {"65535:34952:0rest",     0, {.r=0xffff, .g=0x8888, .b=0x0000, .a=0xffff}, 13},
    /* colon-separated error: value out of 16-bit range */
    {"65536:0:0",        -EINVAL, {0, 0, 0, 0}, 0},
    {"0:0:65536",        -EINVAL, {0, 0, 0, 0}, 0},
    {"0:0:0:65536",      -EINVAL, {0, 0, 0, 0}, 0},
    /* colon-separated error: too few components */
    {"65535:34952",      -EINVAL, {0, 0, 0, 0}, 0},
    /* colon-separated error: bare hex without 0x prefix is not valid */
    {"ffff:8888:0000",   -EINVAL, {0, 0, 0, 0}, 0},
};

int
main(int argc, char *argv[])
{
    unsigned int i;
    unsigned int x = 0;
    (void)argc;
    (void)argv;

    for (i = 0; i != sizeof(parse_rgba_checks)/sizeof(parse_rgba_checks[0]); ++i)
    {
        const struct parse_rgba_check_s * const tc = parse_rgba_checks + i;
        drmu_rgba_t c;
        char * eos = NULL;
        const int rv = drmu_util_parse_rgba(tc->s, &eos, &c);

        if (rv != tc->rv ||
            c.r != tc->c.r || c.g != tc->c.g || c.b != tc->c.b || c.a != tc->c.a ||
            (rv == 0 && eos - tc->s != tc->eos_off))
        {
            printf("parse_rgba fail [%s]: got rv=%d {%04x,%04x,%04x,%04x} eos+%td"
                   " expected rv=%d {%04x,%04x,%04x,%04x} eos+%td\n",
                   tc->s,
                   rv, c.r, c.g, c.b, c.a, eos - tc->s,
                   tc->rv, tc->c.r, tc->c.g, tc->c.b, tc->c.a, tc->eos_off);
            ++x;
        }
    }

    /* verify peos=NULL does not crash */
    {
        drmu_rgba_t c;
        drmu_util_parse_rgba("#ff8800", NULL, &c);
    }

    if (x != 0)
        printf("*** parse_rgba check failed %d tests\n", x);
    else
        printf("parse_rgba check OK\n");

    return x == 0 ? 0 : 1;
}
