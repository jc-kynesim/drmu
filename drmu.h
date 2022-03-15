#ifndef _DRMU_DRMU_H
#define _DRMU_DRMU_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

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

struct drmu_pool_s;
typedef struct drmu_pool_s drmu_pool_t;

struct drmu_crtc_s;
typedef struct drmu_crtc_s drmu_crtc_t;

struct drmu_plane_s;
typedef struct drmu_plane_s drmu_plane_t;

struct drmu_atomic_s;

struct drmu_env_s;
typedef struct drmu_env_s drmu_env_t;

typedef struct drmu_rect_s {
    int32_t x, y;
    uint32_t w, h;
} drmu_rect_t;

typedef struct drmu_chroma_siting_s {
    int32_t x, y;
} drmu_chroma_siting_t;

typedef struct drmu_ufrac_s {
    unsigned int num;
    unsigned int den;
} drmu_ufrac_t;

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

drmu_ufrac_t drmu_ufrac_reduce(drmu_ufrac_t x);

static inline int
drmu_rect_rescale_1(int x, int mul, int div)
{
    return div == 0 ? x * mul : (x * mul + div/2) / div;
}

static inline drmu_rect_t
drmu_rect_rescale(const drmu_rect_t s, const drmu_rect_t mul, const drmu_rect_t div)
{
    return (drmu_rect_t){
        .x = drmu_rect_rescale_1(s.x - div.x, mul.w, div.w) + mul.x,
        .y = drmu_rect_rescale_1(s.y - div.y, mul.h, div.h) + mul.y,
        .w = drmu_rect_rescale_1(s.w,         mul.w, div.w),
        .h = drmu_rect_rescale_1(s.h,         mul.h, div.h)
    };
}

static inline drmu_rect_t
drmu_rect_add_xy(const drmu_rect_t a, const drmu_rect_t b)
{
    return (drmu_rect_t){
        .x = a.x + b.x,
        .y = a.y + b.y,
        .w = a.w,
        .h = a.h
    };
}

static inline drmu_rect_t
drmu_rect_wh(const unsigned int w, const unsigned int h)
{
    return (drmu_rect_t){
        .w = w,
        .h = h
    };
}

static inline bool
drmu_chroma_siting_eq(const drmu_chroma_siting_t a, const drmu_chroma_siting_t b)
{
    return a.x == b.x && a.y == b.y;
}

// Blob

void drmu_blob_unref(drmu_blob_t ** const ppBlob);
uint32_t drmu_blob_id(const drmu_blob_t * const blob);
drmu_blob_t * drmu_blob_ref(drmu_blob_t * const blob);
drmu_blob_t * drmu_blob_new(drmu_env_t * const du, const void * const data, const size_t len);
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
uint64_t drmu_prop_range_max(const drmu_prop_range_t * const pra);
uint64_t drmu_prop_range_min(const drmu_prop_range_t * const pra);
uint32_t drmu_prop_range_id(const drmu_prop_range_t * const pra);
drmu_prop_range_t * drmu_prop_range_new(drmu_env_t * const du, const uint32_t id);
int drmu_atomic_add_prop_range(struct drmu_atomic_s * const da, const uint32_t obj_id, const drmu_prop_range_t * const pra, const uint64_t x);

// BO

struct drm_mode_create_dumb;

void drmu_bo_unref(drmu_bo_t ** const ppbo);
drmu_bo_t * drmu_bo_ref(drmu_bo_t * const bo);
drmu_bo_t * drmu_bo_new_fd(drmu_env_t *const du, const int fd);
drmu_bo_t * drmu_bo_new_dumb(drmu_env_t *const du, struct drm_mode_create_dumb * const d);
void drmu_bo_env_uninit(drmu_bo_env_t * const boe);
void drmu_bo_env_init(drmu_bo_env_t * boe);

// fb
struct hdr_output_metadata;
struct drmu_format_info_s;

// Called pre delete.
// Zero returned means continue delete.
// Non-zero means stop delete - fb will have zero refs so will probably want a new ref
//   before next use
typedef int (* drmu_fb_pre_delete_fn)(struct drmu_fb_s * dfb, void * v);
typedef void (* drmu_fb_on_delete_fn)(struct drmu_fb_s * dfb, void * v);

void drmu_fb_pre_delete_set(drmu_fb_t *const dfb, drmu_fb_pre_delete_fn fn, void * v);
void drmu_fb_pre_delete_unset(drmu_fb_t *const dfb);
unsigned int drmu_fb_pixel_bits(const drmu_fb_t * const dfb);
drmu_fb_t * drmu_fb_new_dumb(drmu_env_t * const du, uint32_t w, uint32_t h, const uint32_t format);
drmu_fb_t * drmu_fb_new_dumb_mod(drmu_env_t * const du, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod);
drmu_fb_t * drmu_fb_realloc_dumb(drmu_env_t * const du, drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format);
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
uint32_t drmu_fb_width(const drmu_fb_t *const dfb);
uint32_t drmu_fb_height(const drmu_fb_t *const dfb);
//const drmu_rect_t * drmu_fb_crop(const drmu_fb_t *const dfb);

int drmu_atomic_add_prop_fb(struct drmu_atomic_s * const da, const uint32_t obj_id, const uint32_t prop_id, drmu_fb_t * const dfb);

// FB creation helpers - only use for creatino of new FBs
drmu_fb_t * drmu_fb_int_alloc(drmu_env_t * const du);
void drmu_fb_int_free(drmu_fb_t * const dfb);
void drmu_fb_int_fmt_size_set(drmu_fb_t *const dfb, uint32_t fmt, uint32_t w, uint32_t h, const drmu_rect_t crop);
// All assumed to be const strings that do not need freed
void drmu_fb_int_color_set(drmu_fb_t *const dfb, const char * const enc, const char * const range, const char * const space);
void drmu_fb_int_chroma_siting_set(drmu_fb_t *const dfb, const drmu_chroma_siting_t siting);
void drmu_fb_int_on_delete_set(drmu_fb_t *const dfb, drmu_fb_on_delete_fn fn, void * v);
void drmu_fb_int_bo_set(drmu_fb_t *const dfb, unsigned int i, drmu_bo_t * const bo);
void drmu_fb_int_layer_set(drmu_fb_t *const dfb, unsigned int i, unsigned int obj_idx, uint32_t pitch, uint32_t offset);
void drmu_fb_int_layer_mod_set(drmu_fb_t *const dfb, unsigned int i, unsigned int obj_idx, uint32_t pitch, uint32_t offset, uint64_t modifier);
bool drmu_fb_hdr_metadata_isset(const drmu_fb_t *const dfb);
const struct hdr_output_metadata * drmu_fb_hdr_metadata_get(const drmu_fb_t *const dfb);
const char * drmu_color_range_to_broadcast_rgb(const char * const range);
const char * drmu_fb_colorspace_get(const drmu_fb_t * const dfb);
const char * drmu_fb_color_range_get(const drmu_fb_t * const dfb);
const struct drmu_format_info_s * drmu_fb_format_info_get(const drmu_fb_t * const dfb);
void drmu_fb_hdr_metadata_set(drmu_fb_t *const dfb, const struct hdr_output_metadata * meta);
int drmu_fb_int_make(drmu_fb_t *const dfb);

// fb pool

void drmu_pool_unref(drmu_pool_t ** const pppool);
drmu_pool_t * drmu_pool_ref(drmu_pool_t * const pool);
drmu_pool_t * drmu_pool_new(drmu_env_t * const du, unsigned int total_fbs_max);
drmu_fb_t * drmu_pool_fb_new_dumb(drmu_pool_t * const pool, uint32_t w, uint32_t h, const uint32_t format);
void drmu_pool_delete(drmu_pool_t ** const pppool);

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
uint32_t drmu_crtc_x(const drmu_crtc_t * const dc);
uint32_t drmu_crtc_y(const drmu_crtc_t * const dc);
uint32_t drmu_crtc_width(const drmu_crtc_t * const dc);
uint32_t drmu_crtc_height(const drmu_crtc_t * const dc);
drmu_ufrac_t drmu_crtc_sar(const drmu_crtc_t * const dc);
void drmu_crtc_max_bpc_allow(drmu_crtc_t * const dc, const bool max_bpc_allowed);

typedef int drmu_mode_score_fn(void * v, const struct _drmModeModeInfo * mode);
int drmu_crtc_mode_pick(drmu_crtc_t * const dc, drmu_mode_score_fn * const score_fn, void * const score_v);

// Simple mode picker cb - looks for width / height and then refresh
// If nothing "plausible" defaults to EDID preferred mode
typedef struct drmu_mode_pick_simple_params_s {
    unsigned int width;
    unsigned int height;
    unsigned int hz_x_1000;  // Refresh rate * 1000 i.e. 50Hz = 50000
    uint32_t flags;          // Nothing currently - but things like interlace could turn up here
} drmu_mode_pick_simple_params_t;
drmu_mode_score_fn drmu_mode_pick_simple_cb;

// Get simple properties of a mode_id
// If mode_id == -1 retrieves params for current mode
drmu_mode_pick_simple_params_t drmu_crtc_mode_simple_params(const drmu_crtc_t * const dc, const int mode_id);

drmu_crtc_t * drmu_crtc_new_find(drmu_env_t * const du);

// False set max_bpc to 8, true max value
int drmu_atomic_crtc_hi_bpc_set(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, bool hi_bpc);

#define DRMU_CRTC_COLORSPACE_DEFAULT            "Default"
int drmu_atomic_crtc_colorspace_set(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, const char * colorspace);
#define DRMU_CRTC_BROADCAST_RGB_AUTOMATIC       "Automatic"
#define DRMU_CRTC_BROADCAST_RGB_FULL            "Full"
#define DRMU_CRTC_BROADCAST_RGB_LIMITED_16_235  "Limited 16:235"
int drmu_atomic_crtc_broadcast_rgb_set(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, const char * bcrgb);
int drmu_atomic_crtc_mode_id_set(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, const int mode_id);

// Set none if m=NULL
int drmu_atomic_crtc_hdr_metadata_set(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, const struct hdr_output_metadata * const m);

// Set all the fb info props that might apply to a crtc on the crtc
// (e.g. hdr_metadata, colorspace) but do not set the mode (resolution
// and refresh)
int drmu_atomic_crtc_fb_info_set(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, const drmu_fb_t * const fb);

// Plane

uint32_t drmu_plane_id(const drmu_plane_t * const dp);
const uint32_t * drmu_plane_formats(const drmu_plane_t * const dp, unsigned int * const pCount);
void drmu_plane_delete(drmu_plane_t ** const ppdp);
drmu_plane_t * drmu_plane_new_find(drmu_crtc_t * const dc, const uint32_t fmt);

// Alpha: -1 = no not set, 0 = transparent, 0xffff = opaque
#define DRMU_PLANE_ALPHA_UNSET                  (-1)
#define DRMU_PLANE_ALPHA_TRANSPARENT            0
#define DRMU_PLANE_ALPHA_OPAQUE                 0xffff
int drmu_atomic_add_plane_alpha(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const int alpha);

// X, Y & TRANSPOSE can be ORed to get all others
#define DRMU_PLANE_ROTATION_0                   0
#define DRMU_PLANE_ROTATION_X_FLIP              1
#define DRMU_PLANE_ROTATION_Y_FLIP              2
#define DRMU_PLANE_ROTATION_180                 3
// *** These don't exist on Pi - no inherent transpose
#define DRMU_PLANE_ROTATION_TRANSPOSE           4
#define DRMU_PLANE_ROTATION_90                  5  // Rotate 90 clockwise
#define DRMU_PLANE_ROTATION_270                 6  // Rotate 90 anti-cockwise
#define DRMU_PLANE_ROTATION_180_TRANSPOSE       7  // Rotate 180 & transpose
int drmu_atomic_add_plane_rotation(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const int rot);

// Init constants - C winges if the struct is specified in a const init (which seems like a silly error)
#define drmu_chroma_siting_float_i(_x, _y) {.x = (int32_t)((double)(_x) * 65536 + .5), .y = (int32_t)((double)(_y) * 65536 + .5)}
#define DRMU_CHROMA_SITING_BOTTOM_I             drmu_chroma_siting_float_i(0.5, 1.0)
#define DRMU_CHROMA_SITING_BOTTOM_LEFT_I        drmu_chroma_siting_float_i(0.0, 1.0)
#define DRMU_CHROMA_SITING_CENTER_I             drmu_chroma_siting_float_i(0.5, 0.5)
#define DRMU_CHROMA_SITING_LEFT_I               drmu_chroma_siting_float_i(0.0, 0.5)
#define DRMU_CHROMA_SITING_TOP_I                drmu_chroma_siting_float_i(0.5, 0.0)
#define DRMU_CHROMA_SITING_TOP_LEFT_I           drmu_chroma_siting_float_i(0.0, 0.0)
#define DRMU_CHROMA_SITING_UNSPECIFIED_I        {INT32_MIN, INT32_MIN}
// Inline constants
#define drmu_chroma_siting_float(_x, _y) (drmu_chroma_siting_t){.x = (int32_t)((double)(_x) * 65536 + .5), .y = (int32_t)((double)(_y) * 65536 + .5)}
#define DRMU_CHROMA_SITING_BOTTOM               drmu_chroma_siting_float(0.5, 1.0)
#define DRMU_CHROMA_SITING_BOTTOM_LEFT          drmu_chroma_siting_float(0.0, 1.0)
#define DRMU_CHROMA_SITING_CENTER               drmu_chroma_siting_float(0.5, 0.5)
#define DRMU_CHROMA_SITING_LEFT                 drmu_chroma_siting_float(0.0, 0.5)
#define DRMU_CHROMA_SITING_TOP                  drmu_chroma_siting_float(0.5, 0.0)
#define DRMU_CHROMA_SITING_TOP_LEFT             drmu_chroma_siting_float(0.0, 0.0)
#define DRMU_CHROMA_SITING_UNSPECIFIED          (drmu_chroma_siting_t){INT32_MIN, INT32_MIN}
int drmu_atomic_plane_add_chroma_siting(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const drmu_chroma_siting_t siting);

#define DRMU_PLANE_RANGE_FULL                   "YCbCr full range"
#define DRMU_PLANE_RANGE_LIMITED                "YCbCr limited range"
int drmu_atomic_plane_fb_set(struct drmu_atomic_s * const da, drmu_plane_t * const dp, drmu_fb_t * const dfb, const drmu_rect_t pos);

// Env
struct drmu_log_env_s;

// Q the atomic on its associated env
// If there is a pending commit this atomic wiill be merged with it
int drmu_atomic_queue(struct drmu_atomic_s ** ppda);
// Wait for there to be no pending commit (there may be a commit in
// progress)
int drmu_env_queue_wait(drmu_env_t * const du);

// Do ioctl - returns -errno on error, 0 on success
// deals with recalling the ioctl when required
int drmu_ioctl(const drmu_env_t * const du, unsigned long req, void * arg);
int drmu_fd(const drmu_env_t * const du);
const struct drmu_log_env_s * drmu_env_log(const drmu_env_t * const du);
void drmu_env_delete(drmu_env_t ** const ppdu);
void drmu_env_modeset_allow(drmu_env_t * const du, const bool modeset_allowed);
// Restore state on env close
int drmu_env_restore_enable(drmu_env_t * const du);
bool drmu_env_restore_is_enabled(const drmu_env_t * const du);
// Add an object snapshot to the restore state
// Tests for commitability and removes any props that won't commit
int drmu_atomic_env_restore_add_snapshot(struct drmu_atomic_s ** const ppda);


drmu_env_t * drmu_env_new_fd(const int fd, const struct drmu_log_env_s * const log);
drmu_env_t * drmu_env_new_open(const char * name, const struct drmu_log_env_s * const log);

// Logging

enum drmu_log_level_e {
        DRMU_LOG_LEVEL_NONE = -1,     // Max level specifier for nothing (not a real level)
        DRMU_LOG_LEVEL_MESSAGE = 0,   // (Nearly) always printed info
        DRMU_LOG_LEVEL_ERROR,         // Error
        DRMU_LOG_LEVEL_WARNING,
        DRMU_LOG_LEVEL_INFO,          // Interesting but not critical info
        DRMU_LOG_LEVEL_DEBUG,         // Info only useful for debug
        DRMU_LOG_LEVEL_ALL,           // Max level specifier for everything (not a real level)
};

typedef void drmu_log_fn(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl);

typedef struct drmu_log_env_s {
        drmu_log_fn * fn;
        void * v;
        enum drmu_log_level_e max_level;
} drmu_log_env_t;

extern const struct drmu_log_env_s drmu_log_env_none;   // pre-built do-nothing log structure

// drmu_atomic

struct drmu_atomic_s;
typedef struct drmu_atomic_s drmu_atomic_t;

void drmu_atomic_dump(const drmu_atomic_t * const da);
drmu_env_t * drmu_atomic_env(const drmu_atomic_t * const da);
void drmu_atomic_unref(drmu_atomic_t ** const ppda);
drmu_atomic_t * drmu_atomic_ref(drmu_atomic_t * const da);
drmu_atomic_t * drmu_atomic_new(drmu_env_t * const du);
int drmu_atomic_merge(drmu_atomic_t * const a, drmu_atomic_t ** const ppb);

// Remove all els in a that are also in b
// b may be sorted (if not already) but is otherwise unchanged
void drmu_atomic_sub(drmu_atomic_t * const a, drmu_atomic_t * const b);

// flags are DRM_MODE_ATOMIC_xxx (e.g. DRM_MODE_ATOMIC_TEST_ONLY) and DRM_MODE_PAGE_FLIP_xxx
int drmu_atomic_commit(const drmu_atomic_t * const da, uint32_t flags);
// Attempt commit - if it fails add failing members to da_fail
// This does NOT remove failing props from da.  If da_fail == NULL then same as _commit
int drmu_atomic_commit_test(const drmu_atomic_t * const da, uint32_t flags, drmu_atomic_t * const da_fail);

typedef void (* drmu_prop_del_fn)(void * v);
typedef void (* drmu_prop_ref_fn)(void * v);

int drmu_atomic_add_prop_generic(drmu_atomic_t * const da,
        const uint32_t obj_id, const uint32_t prop_id, const uint64_t value,
        const drmu_prop_ref_fn ref_fn, const drmu_prop_del_fn del_fn, void * const v);
int drmu_atomic_add_prop_value(drmu_atomic_t * const da, const uint32_t obj_id, const uint32_t prop_id, const uint64_t value);

// drmu_xlease

drmu_env_t * drmu_env_new_xlease(const struct drmu_log_env_s * const log);

// drmu_xdri3

drmu_env_t * drmu_env_new_xdri3(const drmu_log_env_t * const log);

#ifdef __cplusplus
}
#endif

#endif

