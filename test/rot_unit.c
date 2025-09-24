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
    ADD(90, X_FLIP, TRANSPOSE),
    ADD(X_FLIP, 90, 180_TRANSPOSE),
    ADD(TRANSPOSE, X_FLIP, 90),
    ADD(X_FLIP, TRANSPOSE, 270),
    ADD(X_FLIP, X_FLIP, 0),
    ADD(270, Y_FLIP, TRANSPOSE),
    ADD(Y_FLIP, 90, TRANSPOSE),
    ADD(TRANSPOSE, Y_FLIP, 270),
    ADD(Y_FLIP, TRANSPOSE, 90),
    ADD(Y_FLIP, Y_FLIP, 0),
    ADD(Y_FLIP, X_FLIP, 180),
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
            unsigned int bma = drmu_rotation_sub(j, i);
            unsigned int b = drmu_rotation_add(i, bma);

            if (b != j)
            {
                printf("%d + %d = %d, %d - %d = %d, %d + %d = %d\n",
                       i, j, apb,
                       j, i, bma,
                       i, bma, b);
                ++x;
            }
        }
    }

    if (x != 0)
        printf("*** Sub check failed %d tests\n", x);
    else
        printf("Sub check OK\n");

    return 0;
}

