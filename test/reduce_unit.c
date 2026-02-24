#include <stdio.h>
#include <limits.h>

#include "drmu_math.h"

static const struct reduce_check_s {
    drmu_ufrac_t t;
    drmu_ufrac_t r;
} reduce_checks[] = {
    {{0, 0}, {0, 0}},
    {{99, 0}, {0, 0}},
    {{0, 99}, {0, 1}},
    {{2, 2}, {1, 1}},
    {{UINT_MAX, UINT_MAX}, {1, 1}},
    {{UINT_MAX-1, UINT_MAX}, {UINT_MAX-1, UINT_MAX}},
    {{UINT_MAX, UINT_MAX-1}, {UINT_MAX, UINT_MAX-1}},
    {{UINT_MAX-1, UINT_MAX-1}, {1,1}},
    {{2*3*5*31, 3*31*7}, {2*5, 7}},
    {{3*31*7, 2*3*5*31}, {7, 2*5}},
    {{3*3*3*3, 3*3*5}, {3*3, 5}},
    {{2, 3}, {2, 3}},
    {{2*3*5*31, 3*31*7*13}, {2*5, 7*13}},
};

int
main(int argc, char *argv[])
{
    unsigned int i;
    unsigned int x = 0;
    (void)argc;
    (void)argv;

    for (i = 0; i != sizeof(reduce_checks)/sizeof(reduce_checks[0]); ++i)
    {
        const drmu_ufrac_t r = drmu_ufrac_reduce(reduce_checks[i].t);
        if (r.num != reduce_checks[i].r.num || r.den != reduce_checks[i].r.den)
        {
            printf("Reduce fail: %u/%u -> %u/%u expected %u/%u\n",
                   reduce_checks[i].t.num, reduce_checks[i].t.den,
                   r.num, r.den,
                   reduce_checks[i].r.num, reduce_checks[i].r.den);
            ++x;
        }
    }

    if (x != 0)
        printf("*** Reduce check failed %d tests\n", x);
    else
        printf("Reduce check OK\n");

    return x == 0 ? 0 : 1;
}
