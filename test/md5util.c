#include "md5util.h"

#include "md5.h"

/* Locally written helper code from here on */

const uint8_t *
md5sum_1d(uint8_t digest[16], const void * const data, size_t len)
{
    MD5_CTX ctx;

    MD5Init(&ctx);
    MD5Update(&ctx, data, len);
    MD5Final(digest, &ctx);
    return digest;
}

const uint8_t *
md5sum_2d(uint8_t digest[16], const void * const data, size_t stride, unsigned int width, unsigned int lines)
{
    const uint8_t * p = data;
    MD5_CTX ctx;

    MD5Init(&ctx);
    while (lines--) {
        MD5Update(&ctx, p, width);
        p += stride;
    }
    MD5Final(digest, &ctx);
    return digest;
}

static inline char htoc(unsigned int h)
{
    h &= 0xf;
    return h < 10 ? '0' + h : 'a' - 10 + h;
}

const char *
md5sum_to_str(char buf[33], const uint8_t digest[16])
{
    unsigned int i;
    char * p = buf;

    for (i = 0; i != 16; ++i) {
        *p++ = htoc(digest[i] >> 4);
        *p++ = htoc(digest[i]);
    }
    *p++ = 0;
    return buf;
}



