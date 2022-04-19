#include "drmu.h"
#include "drmu_log.h"

#include <stdatomic.h>
#include <pthread.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <libdrm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define TRACE_PROP_NEW 0

#ifndef DRM_FORMAT_P030
#define DRM_FORMAT_P030 fourcc_code('P', '0', '3', '0')
#endif

enum drmu_bo_type_e {
    BO_TYPE_NONE = 0,
    BO_TYPE_FD,
    BO_TYPE_DUMB
};

// BO handles come in 2 very distinct types: DUMB and FD
// They need very different alloc & free but BO usage is the same for both
// so it is better to have a single type.
typedef struct drmu_bo_s {
    // Arguably could be non-atomic for FD as then it is always protected by mutex
    atomic_int ref_count;
    struct drmu_env_s * du;
    enum drmu_bo_type_e bo_type;
    uint32_t handle;

    // FD only els - FD BOs need to be tracked globally
    struct drmu_bo_s * next;
    struct drmu_bo_s * prev;
} drmu_bo_t;

typedef struct drmu_bo_env_s {
    pthread_mutex_t lock;
    drmu_bo_t * fd_head;
} drmu_bo_env_t;


struct drmu_format_info_s;

typedef struct drmu_fb_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init
    struct drmu_fb_s * prev;
    struct drmu_fb_s * next;

    struct drmu_env_s * du;

    const struct drmu_format_info_s * fmt_info;

    struct drm_mode_fb_cmd2 fb;

    drmu_rect_t active;     // Area that was asked for inside the buffer; pixels
    drmu_rect_t crop;       // Cropping inside that; fractional pels (16.16, 16.16)

    void * map_ptr;
    size_t map_size;
    size_t map_pitch;

    drmu_bo_t * bo_list[4];

    const char * color_encoding; // Assumed to be constant strings that don't need freeing
    const char * color_range;
    const char * pixel_blend_mode;
    const char * colorspace;
    drmu_chroma_siting_t chroma_siting;
    drmu_isset_t hdr_metadata_isset;
    struct hdr_output_metadata hdr_metadata;

    void * pre_delete_v;
    drmu_fb_pre_delete_fn pre_delete_fn;

    void * on_delete_v;
    drmu_fb_on_delete_fn on_delete_fn;

    // We pass a pointer to this to DRM which defines it as s32 so do not use
    // int that might be s64.
    int32_t fence_fd;
} drmu_fb_t;

typedef struct drmu_fb_list_s {
    drmu_fb_t * head;
    drmu_fb_t * tail;
} drmu_fb_list_t;

typedef struct drmu_pool_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init

    struct drmu_env_s * du;

    pthread_mutex_t lock;
    int dead;

    unsigned int seq;  // debug

    unsigned int fb_count;
    unsigned int fb_max;

    drmu_fb_list_t free_fbs;
} drmu_pool_t;


typedef struct drmu_atomic_q_s {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    drmu_atomic_t * next_flip;
    drmu_atomic_t * cur_flip;
    drmu_atomic_t * last_flip;
    unsigned int retry_count;
    struct polltask * retry_task;
} drmu_atomic_q_t;

typedef struct drmu_env_s {
    int fd;
    uint32_t plane_count;
    uint32_t conn_count;
    uint32_t crtc_count;
    drmu_plane_t * planes;
    drmu_conn_t * conns;
    drmu_crtc_t * crtcs;

    drmModeResPtr res;

    drmu_log_env_t log;

    bool modeset_allow;

    // global env for atomic flip
    drmu_atomic_q_t aq;
    // global env for bo tracking
    drmu_bo_env_t boe;
    // global atomic for restore op
    drmu_atomic_t * da_restore;

    struct pollqueue * pq;
    struct polltask * pt;
} drmu_env_t;




