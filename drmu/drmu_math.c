#include "drmu_math.h"

#include <limits.h>

drmu_ufrac_t
drmu_ufrac_reduce(drmu_ufrac_t x)
{
    static const unsigned int primes[] = {2,3,5,7,11,13,17,19,23,29,31,UINT_MAX};
    const unsigned int * p;

    // Deal with specials
    if (x.den == 0) {
        x.num = 0;
        return x;
    }
    if (x.num == 0) {
        x.den = 1;
        return x;
    }

    // Shortcut the 1:1 common case - also ensures the default loop terminates
    if (x.num == x.den) {
        x.num = 1;
        x.den = 1;
        return x;
    }

    // As num != den, (num/UINT_MAX == 0 || den/UINT_MAX == 0) must be true
    // so loop will terminate
    for (p = primes;; ++p) {
        const unsigned int n = *p;
        for (;;) {
            const unsigned int xd = x.den / n;
            const unsigned int xn = x.num / n;
            if (xn == 0 || xd == 0)
                return x;
            if (xn * n != x.num || xd * n != x.den)
                break;
            x.num = xn;
            x.den = xd;
        }
    }
}
