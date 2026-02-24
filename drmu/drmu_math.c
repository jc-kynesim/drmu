#include "drmu_math.h"

// Euclidean Algorithm
static unsigned int
gcd(unsigned int a, unsigned int b)
{
    do {
        if ((a %= b) == 0)
            return b;
    } while ((b %= a) != 0);
    return a;
}

drmu_ufrac_t
drmu_ufrac_reduce(drmu_ufrac_t x)
{
    unsigned int g;

    // Deal with specials
    if (x.den == 0)
        return (drmu_ufrac_t){0, 0};

    g = gcd(x.num, x.den);

    return (drmu_ufrac_t){
        .num = x.num / g,
        .den = x.den / g
    };
}
