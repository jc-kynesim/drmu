#ifndef _DRMU_TEST_FB_DUMP_BMP_H
#define _DRMU_TEST_FB_DUMP_BMP_H

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_fb_s;

// Write the active area of fb to fname as a 24-bit (BGR) bottom-up .BMP
// Any alpha in the source is discarded
// Has the current limitation that it requires 0,0 xy active area offset
// Has a plane16 intermediate buffer so neither fast nor memory efficient
//
// Returns:
//   0        OK
//   -EINVAL  FB format cannot be represented as a BMP (YUV, subsampled,
//            multi-plane or non-linear)
//   -ve      errno from file operations
int fb_dump_to_bmp(const char * fname, struct drmu_fb_s * const fb);

#ifdef __cplusplus
}
#endif

#endif
