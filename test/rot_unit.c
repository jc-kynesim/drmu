#include <stdio.h>

#include "drmu.h"

#define ADD(a,b,c) {DRMU_ROTATION_##a, DRMU_ROTATION_##b, DRMU_ROTATION_##c}

static const struct add_check_s {
    unsigned int a, b, r;
} add_checks[] = {
    ADD(0, 0, 0),
    ADD(90, 0, 90),
    ADD(90, 90, 180),
    ADD(90, 180, 270),
    ADD(90, 270, 0),
    ADD(270, 0, 270),
    ADD(270, 90, 0),
    ADD(270, 180, 90),
    ADD(270, 270, 180),
    ADD(90, V_FLIP, TRANSPOSE),
    ADD(V_FLIP, 90, 180_TRANSPOSE),
    ADD(TRANSPOSE, V_FLIP, 90),
    ADD(V_FLIP, TRANSPOSE, 270),
    ADD(V_FLIP, V_FLIP, 0),
    ADD(270, H_FLIP, TRANSPOSE),
    ADD(H_FLIP, 90, TRANSPOSE),
    ADD(TRANSPOSE, H_FLIP, 270),
    ADD(H_FLIP, TRANSPOSE, 90),
    ADD(H_FLIP, H_FLIP, 0),
    ADD(V_FLIP, H_FLIP, 180),
};

int
main(int argc, char *argv[])
{
    unsigned int i, j;
    unsigned int x = 0;
    (void)argc;
    (void)argv;

    for (i = 0; i != sizeof(add_checks)/sizeof(add_checks[0]); ++i)
    {
        unsigned int apb = drmu_rotation_add(add_checks[i].a, add_checks[i].b);
        if (apb != add_checks[i].r)
        {
            printf("%d + %d = %d expects %d\n",
                   add_checks[i].a, add_checks[i].b, apb, add_checks[i].r);
            ++x;
        }
    }

    if (x != 0)
        printf("*** Add check failed %d tests\n", x);
    else
        printf("Add check OK\n");

    x = 0;
    for (i = 0; i != 8; ++i)
    {
        for (j = 0; j != 8; ++j)
        {
            unsigned int apb = drmu_rotation_add(i, j);
            unsigned int amb = drmu_rotation_suba(i, j);
            unsigned int a = drmu_rotation_add(amb, j);

            if (a != i)
            {
                printf("A: %d + %d = %d, %d - %d = %d, %d + %d = %d\n",
                       i, j, apb,
                       i, j, amb,
                       amb, j, a);
                ++x;
            }
        }
    }

    if (x != 0)
        printf("*** SubA check failed %d tests\n", x);
    else
        printf("SubA check OK\n");

    x = 0;
    for (i = 0; i != 8; ++i)
    {
        for (j = 0; j != 8; ++j)
        {
            unsigned int apb = drmu_rotation_add(i, j);
            unsigned int bma = drmu_rotation_subb(j, i);
            unsigned int b = drmu_rotation_add(i, bma);

            if (b != j)
            {
                printf("B: %d + %d = %d, %d - %d = %d, %d + %d = %d\n",
                       i, j, apb,
                       j, i, bma,
                       i, bma, b);
                ++x;
            }
        }
    }

    if (x != 0)
        printf("*** SubB check failed %d tests\n", x);
    else
        printf("SubB check OK\n");

    if (drmu_rotation_add(8, 0) != DRMU_ROTATION_INVALID ||
        drmu_rotation_suba(8, 0) != DRMU_ROTATION_INVALID ||
        drmu_rotation_subb(8, 0) != DRMU_ROTATION_INVALID ||
        drmu_rotation_add(0, 8) != DRMU_ROTATION_INVALID ||
        drmu_rotation_suba(0, 8) != DRMU_ROTATION_INVALID ||
        drmu_rotation_subb(0, 8) != DRMU_ROTATION_INVALID ||
        drmu_rotation_add(8, 8) != DRMU_ROTATION_INVALID ||
        drmu_rotation_suba(8, 8) != DRMU_ROTATION_INVALID ||
        drmu_rotation_subb(8, 8) != DRMU_ROTATION_INVALID)
        printf("*** Invalid check failed\n");
    else
        printf("Invalid check OK\n");

    return 0;
}

