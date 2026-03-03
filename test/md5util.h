#include <stddef.h>
#include <stdint.h>

const uint8_t * md5sum_1d(uint8_t digest[16], const void * const data, size_t len);
const uint8_t * md5sum_2d(uint8_t digest[16], const void * const data, size_t stride, unsigned int width, unsigned int lines);

const char * md5sum_to_str(char buf[33], const uint8_t digest[16]);

#define md5sum_str(digest) md5sum_to_str((char[33]){0}, digest)



