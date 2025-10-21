#ifndef _DRMU_DRMU_H
#define _DRMU_DRMU_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "drmu_chroma.h"
#include "drmu_math.h"

// Maybe this shoudl not be included?
#include "drmu_poll.h"

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_blob_s;
typedef struct drmu_blob_s drmu_blob_t;

struct drmu_prop_enum_s;
typedef struct drmu_prop_enum_s drmu_prop_enum_t;

struct drmu_prop_range_s;
typedef struct drmu_prop_range_s drmu_prop_range_t;

struct drmu_bo_s;
typedef struct drmu_bo_s drmu_bo_t;
struct drmu_bo_env_s;
typedef struct drmu_bo_env_s drmu_bo_env_t;

struct drmu_fb_s;
typedef struct drmu_fb_s drmu_fb_t;

struct drmu_prop_object_s;
typedef struct drmu_prop_object_s drmu_prop_object_t;

struct drmu_crtc_s;
typedef struct drmu_crtc_s drmu_crtc_t;

struct drmu_conn_s;
typedef struct drmu_conn_s drmu_conn_t;

struct drmu_plane_s;
typedef struct drmu_plane_s drmu_plane_t;

struct drmu_atomic_s;

struct drmu_env_s;
typedef struct drmu_env_s drmu_env_t;

struct drm_log_env_s;
typedef struct drmu_log_env_s drmu_log_env_t;

// HDR enums is copied from linux include/linux/hdmi.h (strangely not part of uapi)
enum hdmi_metadata_type
{
    HDMI_STATIC_METADATA_TYPE1 = 0,
};
enum hdmi_eotf
{
    HDMI_EOTF_TRADITIONAL_GAMMA_SDR,
    HDMI_EOTF_TRADITIONAL_GAMMA_HDR,
    HDMI_EOTF_SMPTE_ST2084,
    HDMI_EOTF_BT_2100_HLG,
};

typedef enum drmu_isset_e {
    DRMU_ISSET_UNSET = 0,  // Thing unset
    DRMU_ISSET_NULL,       // Thing is empty
    DRMU_ISSET_SET,        // Thing has valid data
} drmu_isset_t;

// Blob

void drmu_blob_unref(drmu_blob_t ** const ppBlob);
uint32_t drmu_blob_id(const drmu_blob_t * const blob);
// blob data & length
const void * drmu_blob_data(const drmu_blob_t * const blob);
size_t drmu_blob_len(const drmu_blob_t * const blob);

drmu_blob_t * drmu_blob_ref(drmu_blob_t * const blob);
// Make a new blob - keeps a copy of the data
drmu_blob_t * drmu_blob_new(drmu_env_t * const du, const void * const data, const size_t len);
// Update a blob with new data
// Creates if it didn't exist before, unrefs if data NULL
int drmu_blob_update(drmu_env_t * const du, drmu_blob_t ** const ppblob, const void * const data, const size_t len);
// Create a new blob from an existing blob_id
drmu_blob_t * drmu_blob_copy_id(drmu_env_t * const du, uint32_t blob_id);
int drmu_atomic_add_prop_blob(struct drmu_atomic_s * const da, const uint32_t obj_id, const uint32_t prop_id, drmu_blob_t * const blob);

// Enum & bitmask
// These are very close to the same thing so we use the same struct
typedef drmu_prop_enum_t drmu_prop_bitmask_t;

// Ptr to value of the named enum/bit, NULL if not found or pen == NULL. If bitmask then bit number
const uint64_t * drmu_prop_enum_value(const drmu_prop_enum_t * const pen, const char * const name);
// Bitmask only - value as a (single-bit) bitmask - 0 if not found or not bitmask or pen == NULL
uint64_t drmu_prop_bitmask_value(const drmu_prop_enum_t * const pen, const char * const name);

uint32_t drmu_prop_enum_id(const drmu_prop_enum_t * const pen);
#define drmu_prop_bitmask_id drmu_prop_enum_id
void drmu_prop_enum_delete(drmu_prop_enum_t ** const pppen);
#define drmu_prop_bitmask_delete drmu_prop_enum_delete
drmu_prop_enum_t * drmu_prop_enum_new(drmu_env_t * const du, const uint32_t id);
#define drmu_prop_bitmask_new drmu_prop_enum_new
int drmu_atomic_add_prop_enum(struct drmu_atomic_s * const da, const uint32_t obj_id, const drmu_prop_enum_t * const pen, const char * const name);
int drmu_atomic_add_prop_bitmask(struct drmu_atomic_s * const da, const uint32_t obj_id, const drmu_prop_enum_t * const pen, const uint64_t value);

// Range

void drmu_prop_range_delete(drmu_prop_range_t ** pppra);
bool drmu_prop_range_validate(const drmu_prop_range_t * const pra, const uint64_t x);
bool drmu_prop_range_immutable(const drmu_prop_range_t * const pra);
uint64_t drmu_prop_range_max(const drmu_prop_range_t * const pra);
uint64_t drmu_prop_range_min(const drmu_prop_range_t * const pra);
uint32_t drmu_prop_range_id(const drmu_prop_range_t * const pra);
const char * drmu_prop_range_name(const drmu_prop_range_t * const pra);
drmu_prop_range_t * drmu_prop_range_new(drmu_env_t * const du, const uint32_t id);
int drmu_atomic_add_prop_range(struct drmu_atomic_s * const da, const uint32_t obj_id, const drmu_prop_range_t * const pra, const uint64_t x);

// BO

struct drm_mode_create_dumb;

// Create an fd from a bo
// fd not tracked by the bo so it is the callers reponsibility to free it
// if flags are 0 then RDWR | CLOEXEC will be used
int drmu_bo_export_fd(drmu_bo_t * bo, uint32_t flags);

// Map a BO.
// Size isn't saved in the BO so must be given here
// Returns NULL on failure unlike system mmap
// Mapping isn't held by the BO, must be umapped by user
void * drmu_bo_mmap(const drmu_bo_t * const bo, const size_t length, const int prot, const int flags);

// Get BO handle
uint32_t drmu_bo_handle(const drmu_bo_t * const bo);

void drmu_bo_unref(drmu_bo_t ** const ppbo);
drmu_bo_t * drmu_bo_ref(drmu_bo_t * const bo);
drmu_bo_t * drmu_bo_new_fd(drmu_env_t *const du, const int fd);
drmu_bo_t * drmu_bo_new_dumb(drmu_env_t *const du, struct drm_mode_create_dumb * const d);
drmu_bo_t * drmu_bo_new_external(drmu_env_t *const du, const uint32_t bo_handle);
void drmu_bo_env_uninit(drmu_bo_env_t * const boe);
void drmu_bo_env_init(drmu_bo_env_t * boe);

// fb
struct hdr_output_metadata;
struct drmu_fmt_info_s;

// Called pre delete.
// Zero returned means continue delete.
// Non-zero means stop delete - fb will have zero refs so will probably want a new ref
//   before next use
typedef int (* drmu_fb_pre_delete_fn)(struct drmu_fb_s * dfb, void * v);
// Called after an fb has been deleted and therefore has ceased using any
// user resources
typedef void (* drmu_fb_on_delete_fn)(void * v);

void drmu_fb_pre_delete_set(drmu_fb_t *const dfb, drmu_fb_pre_delete_fn fn, void * v);
void drmu_fb_pre_delete_unset(drmu_fb_t *const dfb);
unsigned int drmu_fb_pixel_bits(const drmu_fb_t * const dfb);
uint32_t drmu_fb_pixel_format(const drmu_fb_t * const dfb);
uint64_t drmu_fb_modifier(const drmu_fb_t * const dfb, const unsigned int plane);
drmu_fb_t * drmu_fb_new_dumb(drmu_env_t * const du, uint32_t w, uint32_t h, const uint32_t format);
drmu_fb_t * drmu_fb_new_dumb_mod(drmu_env_t * const du, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod);
drmu_fb_t * drmu_fb_new_dumb_multi(drmu_env_t * const du, uint32_t w, uint32_t h,
                     const uint32_t format, const uint64_t mod, const bool multi);
drmu_fb_t * drmu_fb_realloc_dumb(drmu_env_t * const du, drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format);
drmu_fb_t * drmu_fb_realloc_dumb_mod(drmu_env_t * const du, drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod);
// Try to reset geometry to these values
// True if done, false if not
bool drmu_fb_try_reuse(drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod);
void drmu_fb_unref(drmu_fb_t ** const ppdfb);
drmu_fb_t * drmu_fb_ref(drmu_fb_t * const dfb);

#define DRMU_FB_PIXEL_BLEND_UNSET               NULL
#define DRMU_FB_PIXEL_BLEND_PRE_MULTIPLIED      "Pre-multiplied"  // Default
#define DRMU_FB_PIXEL_BLEND_COVERAGE            "Coverage"        // Not premultipled
#define DRMU_FB_PIXEL_BLEND_NONE                "None"            // Ignore pixel alpha (opaque)
int drmu_fb_pixel_blend_mode_set(drmu_fb_t *const dfb, const char * const mode);

uint32_t drmu_fb_pitch(const drmu_fb_t *const dfb, const unsigned int layer);
// Pitch2 is only a sand thing
uint32_t drmu_fb_pitch2(const drmu_fb_t *const dfb, const unsigned int layer);
void * drmu_fb_data(const drmu_fb_t *const dfb, const unsigned int layer);
drmu_bo_t * drmu_fb_bo(const drmu_fb_t * const dfb, const unsigned int layer);
// Allocated width height - may be rounded up from requested w/h
uint32_t drmu_fb_width(const drmu_fb_t *const dfb);
uint32_t drmu_fb_height(const drmu_fb_t *const dfb);
// Set cropping (fractional) - x, y, relative to active x, y (and must be +ve)
int drmu_fb_crop_frac_set(drmu_fb_t *const dfb, drmu_rect_t crop_frac);
// get cropping (fractional 16.16) x, y relative to active area
drmu_rect_t drmu_fb_crop_frac(const drmu_fb_t *const dfb);
// get active area (all valid pixels - buffer can/will contain padding outside this)
// rect in pixels (not fractional)
drmu_rect_t drmu_fb_active(const drmu_fb_t *const dfb);

int drmu_atomic_add_prop_fb(struct drmu_atomic_s * const da, const uint32_t obj_id, const uint32_t prop_id, drmu_fb_t * const dfb);

// FB creation helpers - only use for creatino of new FBs
drmu_fb_t * drmu_fb_int_alloc(drmu_env_t * const du);
void drmu_fb_int_free(drmu_fb_t * const dfb);
// Set size
// w, h are buffer sizes, active is the valid pixel area inside that
// crop will be set to the whole active area
void drmu_fb_int_fmt_size_set(drmu_fb_t *const dfb, uint32_t fmt, uint32_t w, uint32_t h, const drmu_rect_t active);
// All assumed to be const strings that do not need freed
typedef const char * drmu_color_encoding_t;
#define DRMU_COLOR_ENCODING_UNSET               NULL
#define DRMU_COLOR_ENCODING_BT2020              "ITU-R BT.2020 YCbCr"
#define DRMU_COLOR_ENCODING_BT709               "ITU-R BT.709 YCbCr"
#define DRMU_COLOR_ENCODING_BT601               "ITU-R BT.601 YCbCr"
static inline bool drmu_color_encoding_is_set(const drmu_color_encoding_t x) {return x != NULL;}
// Note: Color range only applies to YCbCr planes - ignored for RGB
typedef const char * drmu_color_range_t;
#define DRMU_COLOR_RANGE_UNSET                  NULL
#define DRMU_COLOR_RANGE_YCBCR_FULL_RANGE       "YCbCr full range"
#define DRMU_COLOR_RANGE_YCBCR_LIMITED_RANGE    "YCbCr limited range"
static inline bool drmu_color_range_is_set(const drmu_color_range_t x) {return x != NULL;}
typedef const char * drmu_colorspace_t;
#define DRMU_COLORSPACE_UNSET                   NULL
#define DRMU_COLORSPACE_DEFAULT                 "Default"
#define DRMU_COLORSPACE_BT2020_CYCC             "BT2020_CYCC"
#define DRMU_COLORSPACE_BT2020_RGB              "BT2020_RGB"
#define DRMU_COLORSPACE_BT2020_YCC              "BT2020_YCC"
#define DRMU_COLORSPACE_BT709_YCC               "BT709_YCC"
#define DRMU_COLORSPACE_DCI_P3_RGB_D65          "DCI-P3_RGB_D65"
#define DRMU_COLORSPACE_DCI_P3_RGB_THEATER      "DCI-P3_RGB_Theater"
#define DRMU_COLORSPACE_SMPTE_170M_YCC          "SMPTE_170M_YCC"
#define DRMU_COLORSPACE_SYCC_601                "SYCC_601"
#define DRMU_COLORSPACE_XVYCC_601               "XVYCC_601"
#define DRMU_COLORSPACE_XVYCC_709               "XVYCC_709"
static inline bool drmu_colorspace_is_set(const drmu_colorspace_t x) {return x != NULL;}
typedef const char * drmu_broadcast_rgb_t;
#define DRMU_BROADCAST_RGB_UNSET                NULL
#define DRMU_BROADCAST_RGB_AUTOMATIC            "Automatic"
#define DRMU_BROADCAST_RGB_FULL                 "Full"
#define DRMU_BROADCAST_RGB_LIMITED_16_235       "Limited 16:235"
static inline bool drmu_broadcast_rgb_is_set(const drmu_broadcast_rgb_t x) {return x != NULL;}
void drmu_fb_color_set(drmu_fb_t *const dfb, const drmu_color_encoding_t enc, const drmu_color_range_t range, const drmu_colorspace_t space);
void drmu_fb_chroma_siting_set(drmu_fb_t *const dfb, const drmu_chroma_siting_t siting);
void drmu_fb_int_on_delete_set(drmu_fb_t *const dfb, drmu_fb_on_delete_fn fn, void * v);
void drmu_fb_int_bo_set(drmu_fb_t *const dfb, const unsigned int obj_idx, drmu_bo_t * const bo);
void drmu_fb_int_layer_set(drmu_fb_t *const dfb, unsigned int i, unsigned int obj_idx, uint32_t pitch, uint32_t offset);
void drmu_fb_int_layer_mod_set(drmu_fb_t *const dfb, unsigned int i, unsigned int obj_idx, uint32_t pitch, uint32_t offset, uint64_t modifier);
void drmu_fb_int_fd_set(drmu_fb_t *const dfb, const unsigned int obj_idx, const int fd);
void drmu_fb_int_mmap_set(drmu_fb_t *const dfb, const unsigned int obj_idx, void * const buf, const size_t size, const size_t pitch);
drmu_isset_t drmu_fb_hdr_metadata_isset(const drmu_fb_t *const dfb);
const struct hdr_output_metadata * drmu_fb_hdr_metadata_get(const drmu_fb_t *const dfb);
drmu_broadcast_rgb_t drmu_color_range_to_broadcast_rgb(const drmu_color_range_t range);
drmu_colorspace_t drmu_fb_colorspace_get(const drmu_fb_t * const dfb);
drmu_color_range_t drmu_fb_color_range_get(const drmu_fb_t * const dfb);
const struct drmu_fmt_info_s * drmu_fb_format_info_get(const drmu_fb_t * const dfb);
void drmu_fb_hdr_metadata_set(drmu_fb_t *const dfb, const struct hdr_output_metadata * meta);
int drmu_fb_int_make(drmu_fb_t *const dfb);

// Cached fb sync ops
int drmu_fb_write_start(drmu_fb_t * const dfb);
int drmu_fb_write_end(drmu_fb_t * const dfb);
int drmu_fb_read_start(drmu_fb_t * const dfb);
int drmu_fb_read_end(drmu_fb_t * const dfb);

// Wait for data to become ready when fb used as destination of writeback
// Returns:
//  -ve   error
//  0     timeout
//  1     ready
int drmu_fb_out_fence_wait(drmu_fb_t * const fb, const int timeout_ms);

// Object Id

struct drmu_propinfo_s;

uint32_t drmu_prop_object_value(const drmu_prop_object_t * const obj);
void drmu_prop_object_unref(drmu_prop_object_t ** ppobj);
drmu_prop_object_t * drmu_prop_object_new_propinfo(drmu_env_t * const du, const uint32_t obj_id, const struct drmu_propinfo_s * const pi);
int drmu_atomic_add_prop_object(struct drmu_atomic_s * const da, drmu_prop_object_t * obj, uint32_t val);

// Props

// Grab all the props of an object and add to an atomic
// * Does not add references to any properties (BO or FB) currently, it maybe
//   should but if so we need to avoid accidentally closing BOs that we inherit
//   from outside when we delete the atomic.
int drmu_atomic_obj_add_snapshot(struct drmu_atomic_s * const da, const uint32_t objid, const uint32_t objtype);

// CRTC

struct _drmModeModeInfo;
struct hdr_output_metadata;

void drmu_crtc_delete(drmu_crtc_t ** ppdc);
drmu_env_t * drmu_crtc_env(const drmu_crtc_t * const dc);
uint32_t drmu_crtc_id(const drmu_crtc_t * const dc);
int drmu_crtc_idx(const drmu_crtc_t * const dc);

drmu_crtc_t * drmu_env_crtc_find_id(drmu_env_t * const du, const uint32_t crtc_id);
drmu_crtc_t * drmu_env_crtc_find_n(drmu_env_t * const du, const unsigned int n);

typedef struct drmu_mode_simple_params_s {
    unsigned int width;
    unsigned int height;
    unsigned int hz_x_1000;  // Frame rate * 1000 i.e. 50Hz = 50000 (N.B. frame not field rate if interlaced)
    drmu_ufrac_t par;  // Picture Aspect Ratio (0:0 if unknown)
    drmu_ufrac_t sar;  // Sample Aspect Ratio
    uint32_t type;
    uint32_t flags;
} drmu_mode_simple_params_t;

const struct drm_mode_modeinfo * drmu_crtc_modeinfo(const drmu_crtc_t * const dc);
// Get simple properties of initial crtc mode
drmu_mode_simple_params_t drmu_crtc_mode_simple_params(const drmu_crtc_t * const dc);

int drmu_atomic_crtc_add_modeinfo(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, const struct drm_mode_modeinfo * const modeinfo);
int drmu_atomic_crtc_add_active(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, unsigned int val);

bool drmu_crtc_is_claimed(const drmu_crtc_t * const dc);
void drmu_crtc_unref(drmu_crtc_t ** const ppdc);
drmu_crtc_t * drmu_crtc_ref(drmu_crtc_t * const dc);
// A Conn should be claimed before any op that might change its state
int drmu_crtc_claim_ref(drmu_crtc_t * const dc);

// Connector

// Set none if m=NULL
int drmu_atomic_conn_add_hdr_metadata(struct drmu_atomic_s * const da, drmu_conn_t * const dn, const struct hdr_output_metadata * const m);

// Does this connector have > 8 bit support?
bool drmu_conn_has_hi_bpc(const drmu_conn_t * const dn);
// False set max_bpc to 8, true max value
int drmu_atomic_conn_add_hi_bpc(struct drmu_atomic_s * const da, drmu_conn_t * const dn, bool hi_bpc);

int drmu_atomic_conn_add_colorspace(struct drmu_atomic_s * const da, drmu_conn_t * const dn, const drmu_colorspace_t colorspace);
int drmu_atomic_conn_add_broadcast_rgb(struct drmu_atomic_s * const da, drmu_conn_t * const dn, const drmu_broadcast_rgb_t bcrgb);

// Add crtc id
int drmu_atomic_conn_add_crtc(struct drmu_atomic_s * const da, drmu_conn_t * const dn, drmu_crtc_t * const dc);

// Add writeback fb & fence
// Neither makes sense without the other so do together
int drmu_atomic_conn_add_writeback_fb(struct drmu_atomic_s * const da, drmu_conn_t * const dn, drmu_fb_t * const dfb);

// Connector might support some rotations - true if given rotation supported
bool drmu_conn_has_rotation(const drmu_conn_t * const dn, const unsigned int rotation);

// Get mask of rotations supported by this conn
// Will return a mask with only _ROTATION_0 set if the property isn't supported
unsigned int drmu_conn_rotation_mask(const drmu_conn_t * const dn);

// Add rotation to connector
int drmu_atomic_conn_add_rotation(struct drmu_atomic_s * const da, drmu_conn_t * const dn, const unsigned int rotation);

const struct drm_mode_modeinfo * drmu_conn_modeinfo(const drmu_conn_t * const dn, const int mode_id);
drmu_mode_simple_params_t drmu_conn_mode_simple_params(const drmu_conn_t * const dn, const int mode_id);

// Beware: this refects initial value or the last thing set, but currently
// has no way of guessing if the atomic from the set was ever committed
// successfully
uint32_t drmu_conn_crtc_id_get(const drmu_conn_t * const dn);

// Bitmask of CRTCs that might be able to use this Conn
uint32_t drmu_conn_possible_crtcs(const drmu_conn_t * const dn);

bool drmu_conn_is_output(const drmu_conn_t * const dn);
bool drmu_conn_is_writeback(const drmu_conn_t * const dn);
const char * drmu_conn_name(const drmu_conn_t * const dn);
unsigned int drmu_conn_idx_get(const drmu_conn_t * const dn);

// Retrieve the the n-th conn. Use for iteration. Returns NULL when none left
drmu_conn_t * drmu_env_conn_find_n(drmu_env_t * const du, const unsigned int n);

bool drmu_conn_is_claimed(const drmu_conn_t * const dn);
void drmu_conn_unref(drmu_conn_t ** const ppdn);
drmu_conn_t * drmu_conn_ref(drmu_conn_t * const dn);
// A Conn should be claimed before any op that might change its state
int drmu_conn_claim_ref(drmu_conn_t * const dn);


// Plane

uint32_t drmu_plane_id(const drmu_plane_t * const dp);

#define DRMU_PLANE_TYPE_CURSOR  4
#define DRMU_PLANE_TYPE_PRIMARY 2
#define DRMU_PLANE_TYPE_OVERLAY 1
#define DRMU_PLANE_TYPE_UNKNOWN 0
unsigned int drmu_plane_type(const drmu_plane_t * const dp);

const uint32_t * drmu_plane_formats(const drmu_plane_t * const dp, unsigned int * const pCount);
bool drmu_plane_format_check(const drmu_plane_t * const dp, const uint32_t format, const uint64_t modifier);

// Get mask of rotations supported by this plane
// Will return a mask with only _ROTATION_0 set if the property isn't supported
unsigned int drmu_plane_rotation_mask(const drmu_plane_t * const dp);

// Alpha: -1 = no not set, 0 = transparent, 0xffff = opaque
#define DRMU_PLANE_ALPHA_UNSET                  (-1)
#define DRMU_PLANE_ALPHA_TRANSPARENT            0
#define DRMU_PLANE_ALPHA_OPAQUE                 0xffff
int drmu_atomic_plane_add_alpha(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const int alpha);

int drmu_atomic_plane_add_zpos(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const int zpos);

// X, Y & TRANSPOSE can be ORed to get all others
#define DRMU_ROTATION_0                   0
#define DRMU_ROTATION_X_FLIP              1
#define DRMU_ROTATION_Y_FLIP              2
#define DRMU_ROTATION_180                 3
// *** These don't exist on Pi - no inherent transpose
#define DRMU_ROTATION_TRANSPOSE           4
#define DRMU_ROTATION_90                  5  // Rotate 90 clockwise
#define DRMU_ROTATION_270                 6  // Rotate 90 anti-cockwise
#define DRMU_ROTATION_180_TRANSPOSE       7  // Rotate 180 & transpose

#define DRMU_ROTATION_INVALID             ~0U

static inline bool drmu_rotation_is_transposed(const unsigned int r)
{
    return (r & 4) != 0;
}

// Transpose r if c is transposed.
// Probably not a useful user fn but used in +/-
static inline unsigned int
drmu_rotation_ctranspose(const unsigned int r, const unsigned int c)
{
    const unsigned int s = (c & 4) >> 2;
    return (r & 4) | ((r & 2) >> s) | ((r & 1) << s);
}

// a then b
// Beware a + b != b + a
static inline unsigned int
drmu_rotation_add(const unsigned int a, const unsigned int b)
{
    return ((a | b) & ~7) != 0 ? DRMU_ROTATION_INVALID : drmu_rotation_ctranspose(a, b) ^ b;
}

// Returns value that if b is added to gets a
// i.e. suba(a, b) + b = a
static inline unsigned int
drmu_rotation_suba(const unsigned int a, const unsigned int b)
{
    return ((a | b) & ~7) != 0 ? DRMU_ROTATION_INVALID : drmu_rotation_ctranspose(a ^ b, b);
}

// Returns value that would need to be added to a to get b
// i.e. a + subb(b, a) = b
static inline unsigned int
drmu_rotation_subb(const unsigned int b, const unsigned int a)
{
    return ((a | b) & ~7) != 0 ? DRMU_ROTATION_INVALID : drmu_rotation_ctranspose(a, a ^ b) ^ b;
}

// Find a rotation that exists in mask_a which when combined with a rotation
// in mask_b gives req_rot. If req_rot exists in mask_a then the return value
// will be req_rot. If no such value exists _INVALID will be returned
// Use _subb(return_value, req_rot) to get rotation required in b
unsigned int drmu_rotation_find(const unsigned int req_rot, const unsigned int mask_a, const unsigned int mask_b);

int drmu_atomic_plane_add_rotation(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const int rot);

int drmu_atomic_plane_add_chroma_siting(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const drmu_chroma_siting_t siting);

// Set FB to 0 (i.e. clear the plane)
int drmu_atomic_plane_clear_add(struct drmu_atomic_s * const da, drmu_plane_t * const dp);

// Adds the fb to the plane along with all fb properties that apply to a plane
// If fb == NULL is equivalent to _plane_clear_add
// pos is dest rect on the plane in full pixels (not frac)
int drmu_atomic_plane_add_fb(struct drmu_atomic_s * const da, drmu_plane_t * const dp, drmu_fb_t * const dfb, const drmu_rect_t pos);

// Is this plane reffed?
bool drmu_plane_is_claimed(drmu_plane_t * const dp);

// Unref a plane
void drmu_plane_unref(drmu_plane_t ** const ppdp);

// Ref a plane - expects it is already associated
drmu_plane_t * drmu_plane_ref(drmu_plane_t * const dp);

// Associate a plane with a crtc and ref it
// Returns -EBUSY if plane already associated
int drmu_plane_ref_crtc(drmu_plane_t * const dp, drmu_crtc_t * const dc);

typedef bool (*drmu_plane_new_find_ok_fn)(const drmu_plane_t * dp, void * v);

// Find a "free" plane that satisfies (returns true) the ok callback
// Binds to the crtc & takes a reference
drmu_plane_t * drmu_plane_new_find_ref(drmu_crtc_t * const dc, const drmu_plane_new_find_ok_fn cb, void * const v);
// Find a "free" plane of the given type. Types can be ORed
// Binds to the crtc & takes a reference
drmu_plane_t * drmu_plane_new_find_ref_type(drmu_crtc_t * const dc, const unsigned int req_type);

// Find plane n. Does not ref.
drmu_plane_t * drmu_env_plane_find_n(drmu_env_t * const du, const unsigned int n);


// Env
struct drmu_log_env_s;

// Poll environment maintenance functions used by drmu_poll.c
// Could be use to set up custom polling functions. struct drmu_poll_env_s is
// opaque to drmu.c
struct drmu_poll_env_s;
typedef struct drmu_poll_env_s * (* drmu_poll_new_fn)(drmu_env_t * du);
typedef void (* drmu_poll_destroy_fn)(struct drmu_poll_env_s ** ppPoll_env, drmu_env_t * du);
// Get/set poll environment. Value returned in *ppPe
// If du killed then *ppPe = NULL and rv = -EBUSY
// If already set then value returned and rv == 0
// If unset then new_fn called and its value stored. If null then rv == -ENOMEM
// destroy_fn called when du killed
int drmu_env_int_poll_set(drmu_env_t * const du,
                  const drmu_poll_new_fn new_fn, const drmu_poll_destroy_fn destroy_fn,
                  struct drmu_poll_env_s ** const ppPe);
// Return poll env. NULL if unset
struct drmu_poll_env_s * drmu_env_int_poll_get(drmu_env_t * const du);

// Do ioctl - returns -errno on error, 0 on success
// deals with recalling the ioctl when required
int drmu_ioctl(const drmu_env_t * const du, unsigned long req, void * arg);
int drmu_fd(const drmu_env_t * const du);
const struct drmu_log_env_s * drmu_env_log(const drmu_env_t * const du);
void drmu_env_unref(drmu_env_t ** const ppdu);
drmu_env_t * drmu_env_ref(drmu_env_t * const du);
// Disable queue, restore saved state and unref
// Doesn't guarantee that the env will be freed by exit as there may still be
// buffers that hold a ref for logging or DRM fd but it should resolve circular
// reference problems where buffers on the screen hold refs to the env.
void drmu_env_kill(drmu_env_t ** const ppdu);
// Restore state on env close
int drmu_env_restore_enable(drmu_env_t * const du);
bool drmu_env_restore_is_enabled(const drmu_env_t * const du);
// Add an object snapshot to the restore state
// Tests for commitability and removes any props that won't commit
int drmu_atomic_env_restore_add_snapshot(struct drmu_atomic_s ** const ppda);
// Do the restore - semi-internal function - only use externally as part of
// a poll shutdown function. Leaves restore disabled.
void drmu_env_int_restore(drmu_env_t * const du);

// Open a drmu environment with the drm fd
// Takes a logging structure so early errors can be reported. The logging
// environment is copied so does not have to be valid for greater than the
// duration of the call.
// If log = NULL logging is disabled (set to drmu_log_env_none).
// post_delete_fn is called after the env is deleted - this includes failures
// in _new_fd2 itself
typedef void (*drmu_env_post_delete_fn)(void * v, int fd);
drmu_env_t * drmu_env_new_fd2(const int fd, const struct drmu_log_env_s * const log,
                              drmu_env_post_delete_fn post_delete_fn, void * v);
// Same as _new_fd2 but post_delete_fn is set to simply close the fd
drmu_env_t * drmu_env_new_fd(const int fd, const struct drmu_log_env_s * const log);
// open with device name
drmu_env_t * drmu_env_new_open(const char * name, const struct drmu_log_env_s * const log);

// Logging

extern const struct drmu_log_env_s drmu_log_env_none;   // pre-built do-nothing log structure

// drmu_atomic

struct drmu_atomic_s;
typedef struct drmu_atomic_s drmu_atomic_t;

void drmu_atomic_dump_lvl(const drmu_atomic_t * const da, const int lvl);
void drmu_atomic_dump(const drmu_atomic_t * const da);
drmu_env_t * drmu_atomic_env(const drmu_atomic_t * const da);
void drmu_atomic_unref(drmu_atomic_t ** const ppda);
drmu_atomic_t * drmu_atomic_ref(drmu_atomic_t * const da);
drmu_atomic_t * drmu_atomic_new(drmu_env_t * const du);

// Copy (rather than just ref) b
drmu_atomic_t * drmu_atomic_copy(drmu_atomic_t * const b);

// 'Move' b to the return value
// If b has a single ref then rv is simply b otherwise it is a copy of b
drmu_atomic_t * drmu_atomic_move(drmu_atomic_t ** const ppb);

// Merge b into a
// This reference to b is unrefed (inc. on error); if this was the only
// reference to b this allows the code to simply move properites from b
// to a rather than having to copy. If there is >1 ref then the merge
// will copy safely without breaking the other refs to b.
int drmu_atomic_merge(drmu_atomic_t * const a, drmu_atomic_t ** const ppb);

// Merge b into a, b is unrefed; if a == NULL then simply move
// Move and copy work as their descriptions above
static inline int drmu_atomic_move_merge(drmu_atomic_t ** const ppa, drmu_atomic_t ** const ppb)
{
    if (*ppa)
        return drmu_atomic_merge(*ppa, ppb);
    *ppa = drmu_atomic_move(ppb);
    return 0;
}

// Remove all els in a that are also in b
// b may be sorted (if not already) but is otherwise unchanged
void drmu_atomic_sub(drmu_atomic_t * const a, drmu_atomic_t * const b);

// Is da NULL or has no properties set?
bool drmu_atomic_is_empty(const drmu_atomic_t * const da);

// flags are DRM_MODE_ATOMIC_xxx (e.g. DRM_MODE_ATOMIC_TEST_ONLY) and DRM_MODE_PAGE_FLIP_xxx
int drmu_atomic_commit(const drmu_atomic_t * const da, uint32_t flags);
// Attempt commit - if it fails add failing members to da_fail
// This does NOT remove failing props from da.  If da_fail == NULL then same as _commit
int drmu_atomic_commit_test(const drmu_atomic_t * const da, uint32_t flags, drmu_atomic_t * const da_fail);

// Add a callback that occurs when the atomic has been committed
// This will occur on flip if atomic queued via _atomic_queue - if multiple
// atomics are queued before flip then all fill occur on the same flip
// If cb is 0 then NOP
typedef void drmu_atomic_commit_fn(void * v);
int drmu_atomic_add_commit_callback(drmu_atomic_t * const da, drmu_atomic_commit_fn * const cb, void * const v);
// Clear all commit callbacks from this atomic
void drmu_atomic_clear_commit_callbacks(drmu_atomic_t * const da);
// Run all commit callbacks on this atomic. Callbacks are not cleared.
void drmu_atomic_run_commit_callbacks(const drmu_atomic_t * const da);

typedef void drmu_prop_unref_fn(void * v);
typedef void drmu_prop_ref_fn(void * v);
typedef void drmu_prop_commit_fn(void * v, uint64_t value);

typedef struct drmu_atomic_prop_fns_s {
    drmu_prop_ref_fn * ref;
    drmu_prop_unref_fn * unref;
    drmu_prop_commit_fn * commit;
} drmu_atomic_prop_fns_t;

drmu_prop_ref_fn drmu_prop_fn_null_unref;
drmu_prop_unref_fn drmu_prop_fn_null_ref;
drmu_prop_commit_fn drmu_prop_fn_null_commit;

int drmu_atomic_add_prop_generic(drmu_atomic_t * const da,
        const uint32_t obj_id, const uint32_t prop_id, const uint64_t value,
        const drmu_atomic_prop_fns_t * const fns, void * const v);
int drmu_atomic_add_prop_value(drmu_atomic_t * const da, const uint32_t obj_id, const uint32_t prop_id, const uint64_t value);

// drmu_xlease

drmu_env_t * drmu_env_new_xlease(const struct drmu_log_env_s * const log);

// drmu_xdri3

drmu_env_t * drmu_env_new_xdri3(const drmu_log_env_t * const log);

// drmu_waylease

drmu_env_t * drmu_env_new_waylease(const struct drmu_log_env_s * const log);

#ifdef __cplusplus
}
#endif

#endif

