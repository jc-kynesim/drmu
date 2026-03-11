#ifndef _DRMU_DRMU_FOURCC_H
#define _DRMU_DRMU_FOURCC_H

// DRM fourccs, with fixups for things that might not exist depending
// on version

#include <libdrm/drm_fourcc.h>

#ifndef DRM_FORMAT_P030
#define DRM_FORMAT_P030 fourcc_code('P', '0', '3', '0')
#endif
#ifndef DRM_FORMAT_S010
#define DRM_FORMAT_S010	fourcc_code('S', '0', '1', '0') /* 2x2 subsampled Cb (1) and Cr (2) planes 10 bits per channel */
#endif
#ifndef DRM_FORMAT_S210
#define DRM_FORMAT_S210	fourcc_code('S', '2', '1', '0') /* 2x1 subsampled Cb (1) and Cr (2) planes 10 bits per channel */
#endif
#ifndef DRM_FORMAT_S410
#define DRM_FORMAT_S410	fourcc_code('S', '4', '1', '0') /* non-subsampled Cb (1) and Cr (2) planes 10 bits per channel */
#endif
#ifndef DRM_FORMAT_S012
#define DRM_FORMAT_S012	fourcc_code('S', '0', '1', '2') /* 2x2 subsampled Cb (1) and Cr (2) planes 12 bits per channel */
#endif
#ifndef DRM_FORMAT_S212
#define DRM_FORMAT_S212	fourcc_code('S', '2', '1', '2') /* 2x1 subsampled Cb (1) and Cr (2) planes 12 bits per channel */
#endif
#ifndef DRM_FORMAT_S412
#define DRM_FORMAT_S412	fourcc_code('S', '4', '1', '2') /* non-subsampled Cb (1) and Cr (2) planes 12 bits per channel */
#endif
#ifndef DRM_FORMAT_S016
#define DRM_FORMAT_S016	fourcc_code('S', '0', '1', '6') /* 2x2 subsampled Cb (1) and Cr (2) planes 16 bits per channel */
#endif
#ifndef DRM_FORMAT_S216
#define DRM_FORMAT_S216	fourcc_code('S', '2', '1', '6') /* 2x1 subsampled Cb (1) and Cr (2) planes 16 bits per channel */
#endif
#ifndef DRM_FORMAT_S416
#define DRM_FORMAT_S416	fourcc_code('S', '4', '1', '6') /* non-subsampled Cb (1) and Cr (2) planes 16 bits per channel */
#endif
#ifndef DRM_FORMAT_P210
#define DRM_FORMAT_P210 fourcc_code('P', '2', '1', '0')
#endif
#ifndef DRM_FORMAT_P212
#define DRM_FORMAT_P212 fourcc_code('P', '2', '1', '2')
#endif
#ifndef DRM_FORMAT_P410
#define DRM_FORMAT_P410 fourcc_code('P', '4', '1', '0')
#endif
#ifndef DRM_FORMAT_P412
#define DRM_FORMAT_P412 fourcc_code('P', '4', '1', '2')
#endif

#ifndef fourcc_mod_get_vendor
#define fourcc_mod_get_vendor(modifier)\
    (((modifier) >> 56) & 0xff)
#endif

#ifndef fourcc_mod_is_vendor
#define fourcc_mod_is_vendor(modifier, vendor) \
    (fourcc_mod_get_vendor(modifier) == DRM_FORMAT_MOD_VENDOR_## vendor)
#endif

#endif
