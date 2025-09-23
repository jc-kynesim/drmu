#include <stdio.h>

#include "drmu.h"

int
main(int argc, char *argv[])
{
    unsigned int i, j;
    (void)argc;
    (void)argv;

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
            }
        }
    }
    return 0;
}

