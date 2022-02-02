#include "drmu_int.h"
#include "pollqueue.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

drmu_ufrac_t
drmu_ufrac_reduce(drmu_ufrac_t x)
{
    static const unsigned int primes[] = {2,3,5,7,11,13,17,19,23,0};
    const unsigned int * p;
    for (p = primes; *p != 0; ++p) {
        while (x.den % *p == 0 && x.num % *p ==0) {
            x.den /= *p;
            x.num /= *p;
        }
    }
    return x;
}

//----------------------------------------------------------------------------
//
// Format properties

typedef struct drmu_format_info_s {
    uint32_t fourcc;
    uint8_t  bpp;  // For dumb BO alloc
    uint8_t  plane_count;
    struct {
        uint8_t wdiv;
        uint8_t hdiv;
    } planes[4];
} drmu_format_info_t;

static const drmu_format_info_t format_info[] = {
    { .fourcc = DRM_FORMAT_XRGB8888, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_XBGR8888, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_RGBX8888, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_BGRX8888, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_ARGB8888, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_ABGR8888, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_RGBA8888, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_BGRA8888, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_XRGB2101010, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_XBGR2101010, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_RGBX1010102, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_BGRX1010102, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_ARGB2101010, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_ABGR2101010, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_RGBA1010102, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_BGRA1010102, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_AYUV, .bpp = 32, .plane_count = 1, .planes = {{1, 1}}},

    { .fourcc = DRM_FORMAT_YUYV, .bpp = 16, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_YVYU, .bpp = 16, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_VYUY, .bpp = 16, .plane_count = 1, .planes = {{1, 1}}},
    { .fourcc = DRM_FORMAT_UYVY, .bpp = 16, .plane_count = 1, .planes = {{1, 1}}},

    { .fourcc = DRM_FORMAT_NV12,   .bpp = 8, .plane_count = 2, .planes = {{.wdiv = 1, .hdiv = 1}, {.wdiv = 1, .hdiv = 2}}},
    { .fourcc = DRM_FORMAT_NV21,   .bpp = 8, .plane_count = 2, .planes = {{.wdiv = 1, .hdiv = 1}, {.wdiv = 1, .hdiv = 2}}},
    { .fourcc = DRM_FORMAT_YUV420, .bpp = 8, .plane_count = 3, .planes = {{.wdiv = 1, .hdiv = 1}, {.wdiv = 2, .hdiv = 2}, {.wdiv = 2, .hdiv = 2}}},

    { .fourcc = 0 }
};

static const drmu_format_info_t *
format_info_find(const uint32_t fourcc)
{
    for (const drmu_format_info_t * p = format_info; p->fourcc; ++p) {
        if (p->fourcc == fourcc)
            return p;
    }
    return NULL;
}

//----------------------------------------------------------------------------
//
// Blob fns

typedef struct drmu_blob_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init
    struct drmu_env_s * du;
    uint32_t blob_id;
} drmu_blob_t;

static void
blob_free(drmu_blob_t * const blob)
{
    drmu_env_t * const du = blob->du;

    if (blob->blob_id != 0) {
        struct drm_mode_destroy_blob dblob = {
            .blob_id = blob->blob_id
        };
        if (drmu_ioctl(du, DRM_IOCTL_MODE_DESTROYPROPBLOB, &dblob) != 0)
            drmu_err(du, "%s: Failed to destroy blob: %s", __func__, strerror(errno));
    }
    free(blob);
}

void
drmu_blob_unref(drmu_blob_t ** const ppBlob)
{
    drmu_blob_t * const blob = *ppBlob;

    if (blob == NULL)
        return;
    *ppBlob = NULL;

    if (atomic_fetch_sub(&blob->ref_count, 1) != 0)
        return;

    blob_free(blob);
}

uint32_t
drmu_blob_id(const drmu_blob_t * const blob)
{
    return blob == NULL ? 0 : blob->blob_id;
}

drmu_blob_t *
drmu_blob_ref(drmu_blob_t * const blob)
{
    if (blob != NULL)
        atomic_fetch_add(&blob->ref_count, 1);
    return blob;
}

drmu_blob_t *
drmu_blob_new(drmu_env_t * const du, const void * const data, const size_t len)
{
    int rv;
    drmu_blob_t * blob = calloc(1, sizeof(*blob));
    struct drm_mode_create_blob cblob = {
        .data = (uintptr_t)data,
        .length = (uint32_t)len,
        .blob_id = 0
    };

    if (blob == NULL) {
        drmu_err(du, "%s: Unable to alloc blob", __func__);
        return NULL;
    }
    blob->du = du;

    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_CREATEPROPBLOB, &cblob)) != 0) {
        drmu_err(du, "%s: Unable to create blob: data=%p, len=%zu: %s", __func__,
                 data, len, strerror(-rv));
        blob_free(blob);
        return NULL;
    }

    atomic_init(&blob->ref_count, 0);
    blob->blob_id = cblob.blob_id;
    return blob;
}

static void
atomic_prop_blob_unref(void * v)
{
    drmu_blob_t * blob = v;
    drmu_blob_unref(&blob);
}

static void
atomic_prop_blob_ref(void * v)
{
    drmu_blob_ref(v);
}

int
drmu_atomic_add_prop_blob(drmu_atomic_t * const da, const uint32_t obj_id, const uint32_t prop_id, drmu_blob_t * const blob)
{
    int rv;

    if (blob == NULL)
        return drmu_atomic_add_prop_value(da, obj_id, prop_id, 0);

    rv = drmu_atomic_add_prop_generic(da, obj_id, prop_id, drmu_blob_id(blob), atomic_prop_blob_ref, atomic_prop_blob_unref, blob);
    if (rv != 0)
        drmu_warn(drmu_atomic_env(da), "%s: Failed to add blob obj_id=%#x, prop_id=%#x: %s", __func__, obj_id, prop_id, strerror(-rv));

    return rv;
}

//----------------------------------------------------------------------------
//
// Enum fns

typedef struct drmu_prop_enum_s {
    uint32_t id;
    uint32_t flags;
    unsigned int n;
    const struct drm_mode_property_enum * enums;
    char name[DRM_PROP_NAME_LEN];
} drmu_prop_enum_t;

static void
prop_enum_free(drmu_prop_enum_t * const pen)
{
    free((void*)pen->enums);  // Cast away const
    free(pen);
}

static int
prop_enum_qsort_cb(const void * va, const void * vb)
{
    const struct drm_mode_property_enum * a = va;
    const struct drm_mode_property_enum * b = vb;
    return strcmp(a->name, b->name);
}

// NULL if not found
const uint64_t *
drmu_prop_enum_value(const drmu_prop_enum_t * const pen, const char * const name)
{
    if (pen != NULL && name != NULL) {
        unsigned int i = pen->n / 2;
        unsigned int a = 0;
        unsigned int b = pen->n;

        if (name == NULL)
            return NULL;

        while (a < b) {
            const int r = strcmp(name, pen->enums[i].name);

            if (r == 0)
                return (const uint64_t *)&pen->enums[i].value;  // __u64 defn != uint64_t defn always :-(

            if (r < 0) {
                b = i;
                i = (i + a) / 2;
            } else {
                a = i + 1;
                i = (i + b) / 2;
            }
        }
    }
    return NULL;
}

uint64_t
drmu_prop_bitmask_value(const drmu_prop_enum_t * const pen, const char * const name)
{
    const uint64_t *const p = drmu_prop_enum_value(pen, name);
    return p == NULL || *p >= 64 || (pen->flags & DRM_MODE_PROP_BITMASK) == 0 ?
        (uint64_t)0 : (uint64_t)1 << *p;
}

uint32_t
drmu_prop_enum_id(const drmu_prop_enum_t * const pen)
{
    return pen == NULL ? 0 : pen->id;
}

void
drmu_prop_enum_delete(drmu_prop_enum_t ** const pppen)
{
    drmu_prop_enum_t * const pen = *pppen;
    if (pen == NULL)
        return;
    *pppen = NULL;

    prop_enum_free(pen);
}

drmu_prop_enum_t *
drmu_prop_enum_new(drmu_env_t * const du, const uint32_t id)
{
    drmu_prop_enum_t * pen;
    struct drm_mode_property_enum * enums = NULL;
    unsigned int retries;
    int rv;

    // If id 0 return without warning for ease of getting props on init
    if (id == 0 || (pen = calloc(1, sizeof(*pen))) == NULL)
        return NULL;
    pen->id = id;

    // Docn says we must loop till stable as there may be hotplug races
    for (retries = 0; retries < 8; ++retries) {
        struct drm_mode_get_property prop = {
            .prop_id = id,
            .count_enum_blobs = pen->n,
            .enum_blob_ptr = (uintptr_t)enums
        };

        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETPROPERTY, &prop)) != 0) {
            drmu_err(du, "%s: get property failed: %s", __func__, strerror(-rv));
            goto fail;
        }

        if (prop.count_enum_blobs == 0 ||
            (prop.flags & (DRM_MODE_PROP_ENUM | DRM_MODE_PROP_BITMASK)) == 0) {
            drmu_err(du, "%s: not an enum: flags=%#x, enums=%d", __func__, prop.flags, prop.count_enum_blobs);
            goto fail;
        }

        if (pen->n >= prop.count_enum_blobs) {
            pen->flags = prop.flags;
            pen->n = prop.count_enum_blobs;
            memcpy(pen->name, prop.name, sizeof(pen->name));
            break;
        }

        free(enums);

        pen->n = prop.count_enum_blobs;
        if ((enums = malloc(pen->n * sizeof(*enums))) == NULL)
            goto fail;
    }
    if (retries >= 8) {
        drmu_err(du, "%s: Too many retries", __func__);
        goto fail;
    }

    qsort(enums, pen->n, sizeof(*enums), prop_enum_qsort_cb);
    pen->enums = enums;

#if TRACE_PROP_NEW
    {
        unsigned int i;
        for (i = 0; i != pen->n; ++i) {
            drmu_info(du, "%32s %2d:%02d: %32s %#"PRIx64, pen->name, pen->id, i, pen->enums[i].name, pen->enums[i].value);
        }
    }
#endif

    return pen;

fail:
    free(enums);
    prop_enum_free(pen);
    return NULL;
}

int
drmu_atomic_add_prop_enum(drmu_atomic_t * const da, const uint32_t obj_id, const drmu_prop_enum_t * const pen, const char * const name)
{
    const uint64_t * const pval = drmu_prop_enum_value(pen, name);
    int rv;

    rv = (pval == NULL) ? -EINVAL :
        drmu_atomic_add_prop_generic(da, obj_id, drmu_prop_enum_id(pen), *pval, 0, 0, NULL);

    if (rv != 0 && name != NULL)
        drmu_warn(drmu_atomic_env(da), "%s: Failed to add enum obj_id=%#x, prop_id=%#x, name='%s': %s", __func__,
                  obj_id, drmu_prop_enum_id(pen), name, strerror(-rv));

    return rv;
}

int
drmu_atomic_add_prop_bitmask(struct drmu_atomic_s * const da, const uint32_t obj_id, const drmu_prop_enum_t * const pen, const uint64_t val)
{
    int rv;

    rv = !pen ? -ENOENT :
        ((pen->flags & DRM_MODE_PROP_BITMASK) == 0) ? -EINVAL :
            drmu_atomic_add_prop_generic(da, obj_id, drmu_prop_enum_id(pen), val, 0, 0, NULL);

    if (rv != 0)
        drmu_warn(drmu_atomic_env(da), "%s: Failed to add bitmask obj_id=%#x, prop_id=%#x, val=%#"PRIx64": %s", __func__,
                  obj_id, drmu_prop_enum_id(pen), val, strerror(-rv));

    return rv;
}

//----------------------------------------------------------------------------
//
// Range

typedef struct drmu_prop_range_s {
    uint32_t id;
    uint32_t flags;
    uint64_t range[2];
    char name[DRM_PROP_NAME_LEN];
} drmu_prop_range_t;

static void
prop_range_free(drmu_prop_range_t * const pra)
{
    free(pra);
}

void
drmu_prop_range_delete(drmu_prop_range_t ** pppra)
{
    drmu_prop_range_t * const pra = *pppra;

    if (pra == NULL)
        return;
    *pppra = NULL;

    prop_range_free(pra);
}

bool
drmu_prop_range_validate(const drmu_prop_range_t * const pra, const uint64_t x)
{
    if (pra == NULL)
        return false;
    if ((pra->flags & DRM_MODE_PROP_EXTENDED_TYPE) == DRM_MODE_PROP_TYPE(DRM_MODE_PROP_SIGNED_RANGE)) {
        return (int64_t)pra->range[0] <= (int64_t)x && (int64_t)pra->range[1] >= (int64_t)x;
    }
    return pra->range[0] <= x && pra->range[1] >= x;
}

uint32_t
drmu_prop_range_id(const drmu_prop_range_t * const pra)
{
    return pra == NULL ? 0 : pra->id;
}

drmu_prop_range_t *
drmu_prop_range_new(drmu_env_t * const du, const uint32_t id)
{
    drmu_prop_range_t * pra;
    int rv;

    // If id 0 return without warning for ease of getting props on init
    if (id == 0 || (pra = calloc(1, sizeof(*pra))) == NULL)
        return NULL;
    pra->id = id;

    // We are expecting exactly 2 values so no need to loop
    {
        struct drm_mode_get_property prop = {
            .prop_id = id,
            .count_values = 2,
            .values_ptr = (uintptr_t)pra->range
        };

        if ((rv = drmIoctl(du->fd, DRM_IOCTL_MODE_GETPROPERTY, &prop)) != 0) {
            drmu_err(du, "%s: get property failed: %s", __func__, strerror(-rv));
            goto fail;
        }

        if ((prop.flags & DRM_MODE_PROP_RANGE) == 0 &&
            (prop.flags & DRM_MODE_PROP_EXTENDED_TYPE) != DRM_MODE_PROP_TYPE(DRM_MODE_PROP_SIGNED_RANGE)) {
            drmu_err(du, "%s: not an enum: flags=%#x", __func__, prop.flags);
            goto fail;
        }
        if ((prop.count_values != 2)) {
            drmu_err(du, "%s: unexpected count values: %d", __func__, prop.count_values);
            goto fail;
        }

        pra->flags = prop.flags;
        memcpy(pra->name, prop.name, sizeof(pra->name));
    }

#if TRACE_PROP_NEW
    drmu_info(du, "%32s %2d: %"PRId64"->%"PRId64, pra->name, pra->id, pra->range[0], pra->range[1]);
#endif

    return pra;

fail:
    prop_range_free(pra);
    return NULL;
}

int
drmu_atomic_add_prop_range(drmu_atomic_t * const da, const uint32_t obj_id, const drmu_prop_range_t * const pra, const uint64_t x)
{
    int rv;

    rv = !drmu_prop_range_validate(pra, x) ? -EINVAL :
        drmu_atomic_add_prop_generic(da, obj_id, drmu_prop_range_id(pra), x, 0, 0, NULL);

    if (rv != 0)
        drmu_warn(drmu_atomic_env(da), "%s: Failed to add range obj_id=%#x, prop_id=%#x, val=%"PRId64": %s", __func__,
                  obj_id, drmu_prop_range_id(pra), x, strerror(-rv));

    return rv;
}

//----------------------------------------------------------------------------
//
// BO fns

static int
bo_close(drmu_env_t * const du, uint32_t * const ph)
{
    struct drm_gem_close gem_close = {.handle = *ph};

    if (gem_close.handle == 0)
        return 0;
    *ph = 0;

    return drmu_ioctl(du, DRM_IOCTL_GEM_CLOSE, &gem_close);
}

// BOE lock expected
static void
bo_free_dumb(drmu_bo_t * const bo)
{
    if (bo->handle != 0) {
        drmu_env_t * const du = bo->du;
        struct drm_mode_destroy_dumb destroy_env = {.handle = bo->handle};

        if (drmu_ioctl(du, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_env) != 0)
            drmu_warn(du, "%s: Failed to destroy dumb handle %d", __func__, bo->handle);
    }
    free(bo);
}

static void
bo_free_fd(drmu_bo_t * const bo)
{
    if (bo->handle != 0) {
        drmu_env_t * const du = bo->du;
        drmu_bo_env_t * const boe = &du->boe;
        const uint32_t h = bo->handle;

        if (bo_close(du, &bo->handle) != 0)
            drmu_warn(du, "%s: Failed to close BO handle %d", __func__, h);
        if (bo->next != NULL)
            bo->next->prev = bo->prev;
        if (bo->prev != NULL)
            bo->prev->next = bo->next;
        else
            boe->fd_head = bo->next;
    }
    free(bo);
}


void
drmu_bo_unref(drmu_bo_t ** const ppbo)
{
    drmu_bo_t * const bo = *ppbo;

    if (bo == NULL)
        return;
    *ppbo = NULL;

    switch (bo->bo_type) {
        case BO_TYPE_FD:
        {
            drmu_bo_env_t * const boe = &bo->du->boe;

            pthread_mutex_lock(&boe->lock);
            if (atomic_fetch_sub(&bo->ref_count, 1) == 0)
                bo_free_fd(bo);
            pthread_mutex_unlock(&boe->lock);
            break;
        }
        case BO_TYPE_DUMB:
            if (atomic_fetch_sub(&bo->ref_count, 1) == 0)
                bo_free_dumb(bo);
            break;
        case BO_TYPE_NONE:
        default:
            free(bo);
            break;
    }
}


drmu_bo_t *
drmu_bo_ref(drmu_bo_t * const bo)
{
    if (bo != NULL)
        atomic_fetch_add(&bo->ref_count, 1);
    return bo;
}

static drmu_bo_t *
bo_alloc(drmu_env_t *const du, enum drmu_bo_type_e bo_type)
{
    drmu_bo_t * const bo = calloc(1, sizeof(*bo));
    if (bo == NULL) {
        drmu_err(du, "Failed to alloc BO");
        return NULL;
    }

    bo->du = du;
    bo->bo_type = bo_type;
    atomic_init(&bo->ref_count, 0);
    return bo;
}

drmu_bo_t *
drmu_bo_new_fd(drmu_env_t *const du, const int fd)
{
    drmu_bo_env_t * const boe = &du->boe;
    drmu_bo_t * bo = NULL;
    uint32_t h = 0;

    pthread_mutex_lock(&boe->lock);

    if (drmPrimeFDToHandle(du->fd, fd, &h) != 0) {
        drmu_err(du, "%s: Failed to convert fd %d to BO: %s", __func__, fd, strerror(errno));
        goto unlock;
    }

    bo = boe->fd_head;
    while (bo != NULL && bo->handle != h)
        bo = bo->next;

    if (bo != NULL) {
        drmu_bo_ref(bo);
    }
    else {
        if ((bo = bo_alloc(du, BO_TYPE_FD)) == NULL) {
            bo_close(du, &h);
        }
        else {
            bo->handle = h;

            if ((bo->next = boe->fd_head) != NULL)
                bo->next->prev = bo;
            boe->fd_head = bo;
        }
    }

unlock:
    pthread_mutex_unlock(&boe->lock);
    return bo;
}

// Updates the passed dumb structure with the results of creation
drmu_bo_t *
drmu_bo_new_dumb(drmu_env_t *const du, struct drm_mode_create_dumb * const d)
{
    drmu_bo_t *bo = bo_alloc(du, BO_TYPE_DUMB);
    int rv;

    if (bo == NULL)
        return NULL;

    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_CREATE_DUMB, d)) != 0)
    {
        drmu_err(du, "%s: Create dumb %dx%dx%d failed: %s", __func__,
                 d->width, d->height, d->bpp, strerror(-rv));
        drmu_bo_unref(&bo);  // After this point aux is bound to dfb and gets freed with it
        return NULL;
    }

    bo->handle = d->handle;
    return bo;
}

void
drmu_bo_env_uninit(drmu_bo_env_t * const boe)
{
    if (boe->fd_head != NULL)
        drmu_warn(boe->fd_head->du, "%s: fd chain not null", __func__);
    boe->fd_head = NULL;
    pthread_mutex_destroy(&boe->lock);
}

void
drmu_bo_env_init(drmu_bo_env_t * boe)
{
    boe->fd_head = NULL;
    pthread_mutex_init(&boe->lock, NULL);
}

//----------------------------------------------------------------------------
//
// FB fns

void
drmu_fb_int_free(drmu_fb_t * const dfb)
{
    drmu_env_t * const du = dfb->du;
    unsigned int i;

    if (dfb->pre_delete_fn && dfb->pre_delete_fn(dfb, dfb->pre_delete_v) != 0)
        return;

    if (dfb->fb.fb_id != 0)
        drmModeRmFB(du->fd, dfb->fb.fb_id);

    if (dfb->map_ptr != NULL && dfb->map_ptr != MAP_FAILED)
        munmap(dfb->map_ptr, dfb->map_size);

    for (i = 0; i != 4; ++i)
        drmu_bo_unref(dfb->bo_list + i);

    // Call on_delete last so we have stopped using anything that might be
    // freed by it
    if (dfb->on_delete_fn)
        dfb->on_delete_fn(dfb, dfb->on_delete_v);

    free(dfb);
}

void
drmu_fb_unref(drmu_fb_t ** const ppdfb)
{
    drmu_fb_t * const dfb = *ppdfb;

    if (dfb == NULL)
        return;
    *ppdfb = NULL;

    if (atomic_fetch_sub(&dfb->ref_count, 1) > 0)
        return;

    drmu_fb_int_free(dfb);
}

drmu_fb_t *
drmu_fb_ref(drmu_fb_t * const dfb)
{
    if (dfb != NULL)
        atomic_fetch_add(&dfb->ref_count, 1);
    return dfb;
}

// Beware: used by pool fns
void
drmu_fb_pre_delete_set(drmu_fb_t *const dfb, drmu_fb_pre_delete_fn fn, void * v)
{
    dfb->pre_delete_fn = fn;
    dfb->pre_delete_v  = v;
}

void
drmu_fb_pre_delete_unset(drmu_fb_t *const dfb)
{
    dfb->pre_delete_fn = (drmu_fb_pre_delete_fn)0;
    dfb->pre_delete_v  = NULL;
}

uint32_t
drmu_fb_pitch(const drmu_fb_t *const dfb, const unsigned int layer)
{
    return layer >= 4 ? 0 : dfb->fb.pitches[layer];
}

void *
drmu_fb_data(const drmu_fb_t *const dfb, const unsigned int layer)
{
    return (layer >= 4 || dfb->map_ptr == NULL) ? NULL : (uint8_t * )dfb->map_ptr + dfb->fb.offsets[layer];
}

uint32_t
drmu_fb_width(const drmu_fb_t *const dfb)
{
    return dfb->fb.width;
}

uint32_t
drmu_fb_height(const drmu_fb_t *const dfb)
{
    return dfb->fb.height;
}

const drmu_rect_t *
drmu_fb_crop(const drmu_fb_t *const dfb)
{
    return &dfb->cropped;
}

void
drmu_fb_int_fmt_size_set(drmu_fb_t *const dfb, uint32_t fmt, uint32_t w, uint32_t h, const drmu_rect_t crop)
{
    dfb->fmt_info = format_info_find(fmt);
    dfb->fb.pixel_format = fmt;
    dfb->fb.width        = w;
    dfb->fb.height       = h;
    dfb->cropped         = crop;
}

void
drmu_fb_int_color_set(drmu_fb_t *const dfb, const char * const enc, const char * const range, const char * const space)
{
    dfb->color_encoding = enc;
    dfb->color_range    = range;
    dfb->colorspace     = space;
}

void
drmu_fb_int_on_delete_set(drmu_fb_t *const dfb, drmu_fb_on_delete_fn fn, void * v)
{
    dfb->on_delete_fn = fn;
    dfb->on_delete_v  = v;
}

void
drmu_fb_int_bo_set(drmu_fb_t *const dfb, unsigned int i, drmu_bo_t * const bo)
{
    dfb->bo_list[i] = bo;
}

void
drmu_fb_int_layer_mod_set(drmu_fb_t *const dfb, unsigned int i, unsigned int obj_idx, uint32_t pitch, uint32_t offset, uint64_t modifier)
{
    dfb->fb.handles[i] = dfb->bo_list[obj_idx]->handle;
    dfb->fb.pitches[i] = pitch;
    dfb->fb.offsets[i] = offset;
    // We should be able to have "invalid" modifiers and not set the flag
    // but that produces EINVAL - so don't do that
    dfb->fb.modifier[i] = (modifier == DRM_FORMAT_MOD_INVALID) ? 0 : modifier;
}

void
drmu_fb_int_layer_set(drmu_fb_t *const dfb, unsigned int i, unsigned int obj_idx, uint32_t pitch, uint32_t offset)
{
    drmu_fb_int_layer_mod_set(dfb, i, obj_idx, pitch, offset, DRM_FORMAT_MOD_INVALID);
}

int
drmu_fb_int_make(drmu_fb_t *const dfb)
{
    drmu_env_t * du = dfb->du;
    int rv;

    dfb->fb.flags = (dfb->fb.modifier[0] == DRM_FORMAT_MOD_INVALID || dfb->fb.modifier[0] == 0) ? 0 : DRM_MODE_FB_MODIFIERS;

    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_ADDFB2, &dfb->fb)) != 0)
        drmu_err(du, "AddFB2 failed: %s", strerror(-rv));
    return rv;
}

void
drmu_fb_int_hdr_metadata_set(drmu_fb_t *const dfb, const struct hdr_output_metadata * meta)
{
    if (meta == NULL) {
        dfb->hdr_metadata_isset = DRMU_ISSET_NULL;
    }
    else {
        dfb->hdr_metadata_isset = DRMU_ISSET_SET;
        dfb->hdr_metadata = *meta;
    }
}

drmu_fb_t *
drmu_fb_int_alloc(drmu_env_t * const du)
{
    drmu_fb_t * const dfb = calloc(1, sizeof(*dfb));
    if (dfb == NULL)
        return NULL;

    dfb->du = du;
    return dfb;
}

// Bits per pixel on plane 0
unsigned int
drmu_fb_pixel_bits(const drmu_fb_t * const dfb)
{
    return dfb->fmt_info->bpp;
}

// For allocation purposes given fb_pixel bits how tall
// does the frame have to be to fit all planes if constant width
static unsigned int
fb_total_height(const drmu_fb_t * const dfb, const unsigned int h)
{
    unsigned int i;
    const drmu_format_info_t *const f = dfb->fmt_info;
    unsigned int t = 0;

    for (i = 0; i != f->plane_count; ++i)
        t += h / (f->planes[i].hdiv * f->planes[i].wdiv);

    return t;
}

static void
fb_pitches_set(drmu_fb_t * const dfb)
{
    const drmu_format_info_t *const f = dfb->fmt_info;
    const uint32_t pitch0 = dfb->map_pitch;
    const uint32_t h = drmu_fb_height(dfb);
    uint32_t t = 0;
    unsigned int i;

    for (i = 0; i != f->plane_count; ++i) {
        drmu_fb_int_layer_set(dfb, i, 0, pitch0 / f->planes[i].wdiv, t);
        t += (pitch0 * h) / (f->planes[i].hdiv * f->planes[i].wdiv);
    }
}

drmu_fb_t *
drmu_fb_new_dumb(drmu_env_t * const du, uint32_t w, uint32_t h, const uint32_t format)
{
    drmu_fb_t * const dfb = drmu_fb_int_alloc(du);
    uint32_t bpp;
    int rv;

    if (dfb == NULL) {
        drmu_err(du, "%s: Alloc failure", __func__);
        return NULL;
    }

    drmu_fb_int_fmt_size_set(dfb, format, (w + 63) & ~63, (h + 63) & ~63, drmu_rect_wh(w, h));

    if ((bpp = drmu_fb_pixel_bits(dfb)) == 0) {
        drmu_err(du, "%s: Unexpected format %#x", __func__, format);
        goto fail;
    }

    {
        struct drm_mode_create_dumb dumb = {
            .height = fb_total_height(dfb, dfb->fb.height),
            .width = dfb->fb.width,
            .bpp = bpp
        };

        drmu_bo_t * bo = drmu_bo_new_dumb(du, &dumb);
        if (bo == NULL)
            goto fail;
        drmu_fb_int_bo_set(dfb, 0, bo);

        dfb->map_pitch = dumb.pitch;
        dfb->map_size = (size_t)dumb.size;
    }

    {
        struct drm_mode_map_dumb map_dumb = {
            .handle = dfb->bo_list[0]->handle
        };
        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb)) != 0)
        {
            drmu_err(du, "%s: map dumb failed: %s", __func__, strerror(-rv));
            goto fail;
        }

        if ((dfb->map_ptr = mmap(NULL, dfb->map_size,
                                 PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                                 drmu_fd(du), (off_t)map_dumb.offset)) == MAP_FAILED) {
            drmu_err(du, "%s: mmap failed (size=%zd, fd=%d, off=%zd): %s", __func__,
                     dfb->map_size, du->fd, (size_t)map_dumb.offset, strerror(errno));
            goto fail;
        }
    }

    fb_pitches_set(dfb);

    if (drmu_fb_int_make(dfb))
        goto fail;

    drmu_debug(du, "Create dumb %p %s %dx%d / %dx%d size: %zd", dfb,
               drmu_log_fourcc(format), dfb->fb.width, dfb->fb.height, dfb->cropped.w, dfb->cropped.h, dfb->map_size);
    return dfb;

fail:
    drmu_fb_int_free(dfb);
    return NULL;
}

static int
fb_try_reuse(drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format)
{
    if (w > dfb->fb.width || h > dfb->fb.height || format != dfb->fb.pixel_format)
        return 0;

    dfb->cropped = drmu_rect_wh(w, h);
    return 1;
}

drmu_fb_t *
drmu_fb_realloc_dumb(drmu_env_t * const du, drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format)
{
    if (dfb == NULL)
        return drmu_fb_new_dumb(du, w, h, format);

    if (fb_try_reuse(dfb, w, h, format))
        return dfb;

    drmu_fb_unref(&dfb);
    return drmu_fb_new_dumb(du, w, h, format);
}

static void
atomic_prop_fb_unref(void * v)
{
    drmu_fb_t * fb = v;
    drmu_fb_unref(&fb);
}

static void
atomic_prop_fb_ref(void * v)
{
    drmu_fb_ref(v);
}

int
drmu_atomic_add_prop_fb(drmu_atomic_t * const da, const uint32_t obj_id, const uint32_t prop_id, drmu_fb_t * const dfb)
{
    int rv;

    if (dfb == NULL)
        return drmu_atomic_add_prop_value(da, obj_id, prop_id, 0);

    rv = drmu_atomic_add_prop_generic(da, obj_id, prop_id, dfb->fb.fb_id, atomic_prop_fb_ref, atomic_prop_fb_unref, dfb);
    if (rv != 0)
        drmu_warn(drmu_atomic_env(da), "%s: Failed to add fb obj_id=%#x, prop_id=%#x: %s", __func__, obj_id, prop_id, strerror(-rv));

    return rv;
}

//----------------------------------------------------------------------------
//
// props fns (internal)

typedef struct drmu_propinfo_s {
    uint64_t val;
    struct drm_mode_get_property prop;
} drmu_propinfo_t;

typedef struct drmu_props_s {
    struct drmu_env_s * du;
    unsigned int n;
    drmu_propinfo_t * info;
    const drmu_propinfo_t ** by_name;
} drmu_props_t;

static void
props_free(drmu_props_t * const props)
{
    free(props->info);  // As yet nothing else is allocated off this
    free(props->by_name);
    free(props);
}

static uint32_t
props_name_to_id(const drmu_props_t * const props, const char * const name)
{
    unsigned int i = props->n / 2;
    unsigned int a = 0;
    unsigned int b = props->n;

    while (a < b) {
        const int r = strcmp(name, props->by_name[i]->prop.name);

        if (r == 0)
            return props->by_name[i]->prop.prop_id;

        if (r < 0) {
            b = i;
            i = (i + a) / 2;
        } else {
            a = i + 1;
            i = (i + b) / 2;
        }
    }
    return 0;
}

#if TRACE_PROP_NEW || 1
static void
props_dump(const drmu_props_t * const props)
{
    if (props != NULL) {
        unsigned int i;
        drmu_env_t * const du = props->du;

        for (i = 0; i != props->n; ++i) {
            const drmu_propinfo_t * const inf = props->info + i;
            const struct drm_mode_get_property * const p = &inf->prop;
            char flagbuf[256];

            flagbuf[0] = 0;
            if (p->flags & DRM_MODE_PROP_PENDING)
                strcat(flagbuf, ":pending");
            if (p->flags & DRM_MODE_PROP_RANGE)
                strcat(flagbuf, ":urange");
            if (p->flags & DRM_MODE_PROP_IMMUTABLE)
                strcat(flagbuf, ":immutable");
            if (p->flags & DRM_MODE_PROP_ENUM)
                strcat(flagbuf, ":enum");
            if (p->flags & DRM_MODE_PROP_BLOB)
                strcat(flagbuf, ":blob");
            if (p->flags & DRM_MODE_PROP_BITMASK)
                strcat(flagbuf, ":bitmask");
            if ((p->flags & DRM_MODE_PROP_EXTENDED_TYPE) == DRM_MODE_PROP_OBJECT)
                strcat(flagbuf, ":object");
            else if ((p->flags & DRM_MODE_PROP_EXTENDED_TYPE) == DRM_MODE_PROP_SIGNED_RANGE)
                strcat(flagbuf, ":srange");
            else if ((p->flags & DRM_MODE_PROP_EXTENDED_TYPE) != 0)
                strcat(flagbuf, ":?xtype?");
            if (p->flags & ~(DRM_MODE_PROP_LEGACY_TYPE |
                             DRM_MODE_PROP_EXTENDED_TYPE |
                             DRM_MODE_PROP_PENDING |
                             DRM_MODE_PROP_IMMUTABLE |
                             DRM_MODE_PROP_ATOMIC))
                strcat(flagbuf, ":?other?");
            if (p->flags & DRM_MODE_PROP_ATOMIC)
                strcat(flagbuf, ":atomic");


            drmu_info(du, "Prop%02d/%02d: %#-4x %-16s val=%#-4"PRIx64" flags=%#x%s, values=%d, blobs=%d",
                      i, props->n, p->prop_id,
                      p->name, inf->val,
                      p->flags, flagbuf,
                      p->count_values,
                      p->count_enum_blobs);
        }
    }
}
#endif

static int
props_qsort_by_name_cb(const void * va, const void * vb)
{
    const drmu_propinfo_t * const a = *(drmu_propinfo_t **)va;
    const drmu_propinfo_t * const b = *(drmu_propinfo_t **)vb;
    return strcmp(a->prop.name, b->prop.name);
}

// At the moment we don't need / want to fill in the values / blob arrays
// we just want the name - will get the extra info if we need it
static int
propinfo_fill(drmu_env_t * const du, drmu_propinfo_t * const inf, uint32_t propid, uint64_t val)
{
    int rv;

    inf->val = val;
    inf->prop.prop_id = propid;
    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETPROPERTY, &inf->prop)) != 0)
        drmu_err(du, "Failed to get property %d: %s", propid, strerror(-rv));
    return rv;
}

static drmu_props_t *
props_new(drmu_env_t * const du, const uint32_t objid, const uint32_t objtype)
{
    drmu_props_t * const props = calloc(1, sizeof(*props));
    int rv;
	struct drm_mode_obj_get_properties obj_props = {
        .obj_id = objid,
        .obj_type = objtype,
    };
    uint64_t * values = NULL;
    uint32_t * propids = NULL;
    unsigned int n = 0;

    if (props == NULL) {
        drmu_err(du, "%s: Failed struct alloc", __func__);
        return NULL;
    }
    props->du = du;

    for (;;) {
        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &obj_props)) != 0) {
            drmu_err(du, "drmModeObjectGetProperties failed: %s", strerror(-rv));
            goto fail;
        }

        if (obj_props.count_props <= n)
            break;

        free(values);
        values = NULL;
        free(propids);
        propids = NULL;
        n = obj_props.count_props;
        if ((values = malloc(n * sizeof(*values))) == NULL ||
            (propids = malloc(n * sizeof(*propids))) == NULL) {
            drmu_err(du, "obj/value array alloc failed");
            goto fail;
        }
        obj_props.prop_values_ptr = (uintptr_t)values;
        obj_props.props_ptr = (uintptr_t)propids;
    }

    if ((props->info = calloc(n, sizeof(*props->info))) == NULL ||
        (props->by_name = malloc(n * sizeof(*props->by_name))) == NULL) {
        drmu_err(du, "info/name array alloc failed");
        goto fail;
    }
    props->n = n;

    for (unsigned int i = 0; i < n; ++i) {
        drmu_propinfo_t * const inf = props->info + i;

        props->by_name[i] = inf;
        if ((rv = propinfo_fill(du, inf, propids[i], values[i])) != 0)
            goto fail;
    }

    // Sort into name order for faster lookup
    qsort(props->by_name, n, sizeof(*props->by_name), props_qsort_by_name_cb);

    return props;

fail:
    props_free(props);
    free(values);
    free(propids);
    return NULL;
}

//----------------------------------------------------------------------------
//
// CRTC fns

typedef struct drmu_crtc_s {
    struct drmu_env_s * du;
    drmModeEncoderPtr enc;
    drmModeConnectorPtr con;
    int crtc_idx;
    bool hi_bpc_ok;
    drmu_ufrac_t sar;
    drmu_ufrac_t par;

    struct drm_mode_crtc crtc;

    struct {
        // crtc
        uint32_t mode_id;
        // connection
        drmu_prop_range_t * max_bpc;
        drmu_prop_enum_t * colorspace;
        uint32_t hdr_output_metadata;
    } pid;

    int cur_mode_id;
    drmu_blob_t * mode_id_blob;
    drmu_blob_t * hdr_metadata_blob;
    struct hdr_output_metadata hdr_metadata;

} drmu_crtc_t;

static void
free_crtc(drmu_crtc_t * const dc)
{
    if (dc->enc != NULL)
        drmModeFreeEncoder(dc->enc);
    if (dc->con != NULL)
        drmModeFreeConnector(dc->con);

    drmu_prop_range_delete(&dc->pid.max_bpc);
    drmu_prop_enum_delete(&dc->pid.colorspace);
    drmu_blob_unref(&dc->hdr_metadata_blob);
    drmu_blob_unref(&dc->mode_id_blob);
    free(dc);
}

// Set misc derived vars from mode
static void
crtc_mode_set_vars(drmu_crtc_t * const dc)
{
    switch (dc->crtc.mode.flags & DRM_MODE_FLAG_PIC_AR_MASK) {
        case DRM_MODE_FLAG_PIC_AR_4_3:
            dc->par = (drmu_ufrac_t){4,3};
            break;
        case DRM_MODE_FLAG_PIC_AR_16_9:
            dc->par = (drmu_ufrac_t){16,9};
            break;
        case DRM_MODE_FLAG_PIC_AR_64_27:
            dc->par = (drmu_ufrac_t){64,27};
            break;
        case DRM_MODE_FLAG_PIC_AR_256_135:
            dc->par = (drmu_ufrac_t){256,135};
            break;
        default:
        case DRM_MODE_FLAG_PIC_AR_NONE:
            dc->par = (drmu_ufrac_t){0,0};
            break;
    }

    if (dc->par.den == 0) {
        // Assume 1:1
        dc->sar = (drmu_ufrac_t){1,1};
    }
    else {
        dc->sar = drmu_ufrac_reduce((drmu_ufrac_t) {dc->par.num * dc->crtc.mode.vdisplay, dc->par.den * dc->crtc.mode.hdisplay});
    }
}

static int
atomic_crtc_bpc_set(drmu_atomic_t * const da, drmu_crtc_t * const dc,
                    const char * const colorspace,
                    const unsigned int max_bpc)
{
    const uint32_t con_id = dc->con->connector_id;
    int rv = 0;

    if (!dc->du->modeset_allow)
        return 0;

    if ((dc->pid.colorspace &&
         (rv = drmu_atomic_add_prop_enum(da, con_id, dc->pid.colorspace, colorspace)) != 0) ||
        (dc->pid.max_bpc &&
         (rv = drmu_atomic_add_prop_range(da, con_id, dc->pid.max_bpc, max_bpc)) != 0))
        return rv;
    return 0;
}

static int
atomic_crtc_hi_bpc_set(drmu_atomic_t * const da, drmu_crtc_t * const dc)
{
    return atomic_crtc_bpc_set(da, dc, "BT2020_YCC", 12);
}

void
drmu_crtc_delete(drmu_crtc_t ** ppdc)
{
    drmu_crtc_t * const dc = * ppdc;

    if (dc == NULL)
        return;
    *ppdc = NULL;

    free_crtc(dc);
}

drmu_env_t *
drmu_crtc_env(const drmu_crtc_t * const dc)
{
    return dc == NULL ? NULL : dc->du;
}

uint32_t
drmu_crtc_id(const drmu_crtc_t * const dc)
{
    return dc->crtc.crtc_id;
}

int
drmu_crtc_idx(const drmu_crtc_t * const dc)
{
    return dc->crtc_idx;
}

uint32_t
drmu_crtc_x(const drmu_crtc_t * const dc)
{
    return dc->crtc.x;
}

uint32_t
drmu_crtc_y(const drmu_crtc_t * const dc)
{
    return dc->crtc.y;
}

uint32_t
drmu_crtc_width(const drmu_crtc_t * const dc)
{
    return dc->crtc.mode.hdisplay;
}

uint32_t
drmu_crtc_height(const drmu_crtc_t * const dc)
{
    return dc->crtc.mode.vdisplay;
}

drmu_ufrac_t
drmu_crtc_sar(const drmu_crtc_t * const dc)
{
    return dc->sar;
}

void
drmu_crtc_max_bpc_allow(drmu_crtc_t * const dc, const bool max_bpc_allowed)
{
    if (!max_bpc_allowed)
        dc->hi_bpc_ok = false;
}

static drmu_crtc_t *
crtc_from_con_id(drmu_env_t * const du, const uint32_t con_id)
{
    drmu_crtc_t * const dc = calloc(1, sizeof(*dc));
    int i;

    if (dc == NULL) {
        drmu_err(du, "%s: Alloc failure", __func__);
        return NULL;
    }
    dc->du = du;
    dc->crtc_idx = -1;

    if ((dc->con = drmModeGetConnector(du->fd, con_id)) == NULL) {
        drmu_err(du, "%s: Failed to find connector %d", __func__, con_id);
        goto fail;
    }

    if (dc->con->encoder_id == 0) {
        drmu_debug(du, "%s: Connector %d has no encoder", __func__, con_id);
        goto fail;
    }

    if ((dc->enc = drmModeGetEncoder(du->fd, dc->con->encoder_id)) == NULL) {
        drmu_err(du, "%s: Failed to find encoder %d", __func__, dc->con->encoder_id);
        goto fail;
    }

    if (dc->enc->crtc_id == 0) {
        drmu_debug(du, "%s: Connector %d has no encoder", __func__, con_id);
        goto fail;
    }

    for (i = 0; i <= du->res->count_crtcs; ++i) {
        if (du->res->crtcs[i] == dc->enc->crtc_id) {
            dc->crtc_idx = i;
            break;
        }
    }
    if (dc->crtc_idx < 0) {
        drmu_err(du, "%s: Crtc id %d not in resource list", __func__, dc->enc->crtc_id);
        goto fail;
    }

    dc->crtc.crtc_id = dc->enc->crtc_id;
    if (drmu_ioctl(du, DRM_IOCTL_MODE_GETCRTC, &dc->crtc) != 0) {
        drmu_err(du, "%s: Failed to find crtc %d", __func__, dc->enc->crtc_id);
        goto fail;
    }

    {
        drmu_props_t * props = props_new(du, dc->crtc.crtc_id, DRM_MODE_OBJECT_CRTC);
        if (props != NULL) {
#if TRACE_PROP_NEW
            drmu_info(du, "Crtc:");
            props_dump(props);
#endif

            dc->pid.mode_id = props_name_to_id(props, "MODE_ID");
            props_free(props);
        }
    }

    {
        drmu_props_t * const props = props_new(du, dc->con->connector_id, DRM_MODE_OBJECT_CONNECTOR);

        if (props != NULL) {
#if TRACE_PROP_NEW
            drmu_info(du, "Connector:");
            props_dump(props);
#endif
            dc->pid.max_bpc             = drmu_prop_range_new(du, props_name_to_id(props, "max bpc"));
            dc->pid.colorspace          = drmu_prop_enum_new(du, props_name_to_id(props, "Colorspace"));
            dc->pid.hdr_output_metadata = props_name_to_id(props, "HDR_OUTPUT_METADATA");
            props_free(props);
        }
    }

    if (dc->pid.colorspace && dc->pid.max_bpc) {
        drmu_atomic_t * da = drmu_atomic_new(du);
        if (da != NULL &&
            atomic_crtc_hi_bpc_set(da, dc) == 0 &&
            drmu_atomic_commit(da, DRM_MODE_ATOMIC_TEST_ONLY) == 0) {
            dc->hi_bpc_ok = true;
        }
        drmu_atomic_unref(&da);
    }
    drmu_debug(du, "Hi BPC %s", dc->hi_bpc_ok ? "OK" : "no");

    crtc_mode_set_vars(dc);
    drmu_debug(du, "Flags: %#x, par=%d/%d sar=%d/%d", dc->crtc.mode.flags, dc->par.num, dc->par.den, dc->sar.num, dc->sar.den);

    return dc;

fail:
    free_crtc(dc);
    return NULL;
}

int
drmu_mode_pick_simple_cb(void * v, const drmModeModeInfo * mode)
{
    const drmu_mode_pick_simple_params_t * const p = v;

    const int pref = (mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
    const unsigned int r_m = (uint32_t)(((uint64_t)mode->clock * 1000000) / (mode->htotal * mode->vtotal));
    const unsigned int r_f = p->hz_x_1000;

    // We don't understand interlace
    if ((mode->flags & DRM_MODE_FLAG_INTERLACE) != 0)
        return -1;

    if (p->width == mode->hdisplay && p->height == mode->vdisplay)
    {
        // If we haven't been given any hz then pick pref or fastest
        // Max out at 300Hz (=300,0000)
        if (r_f == 0)
            return pref ? 83000000 : 80000000 + (r_m >= 2999999 ? 2999999 : r_m);

        // Prefer a good match to 29.97 / 30 but allow the other
        if ((r_m + 10 >= r_f && r_m <= r_f + 10))
            return 100000000;
        if ((r_m + 100 >= r_f && r_m <= r_f + 100))
            return 95000000;
        // Double isn't bad
        if ((r_m + 10 >= r_f * 2 && r_m <= r_f * 2 + 10))
            return 90000000;
        if ((r_m + 100 >= r_f * 2 && r_m <= r_f * 2 + 100))
            return 85000000;
    }

    if (pref)
        return 50000000;

    return -1;
}

int
drmu_crtc_mode_pick(drmu_crtc_t * const dc, drmu_mode_score_fn * const score_fn, void * const score_v)
{
    int best_score = -1;
    int best_mode = -1;
    int i;

    for (i = 0; i < dc->con->count_modes; ++i) {
        int score = score_fn(score_v, dc->con->modes + i);
        if (score > best_score) {
            best_score = score;
            best_mode = i;
        }
    }

    return best_mode;
}

drmu_crtc_t *
drmu_crtc_new_find(drmu_env_t * const du)
{
    int i;
    drmu_crtc_t * dc;

    if (du->res->count_crtcs <= 0) {
        drmu_err(du, "%s: no crts", __func__);
        return NULL;
    }

    i = 0;
    do {
        if (i >= du->res->count_connectors) {
            drmu_err(du, "%s: No suitable crtc found in %d connectors", __func__, du->res->count_connectors);
            break;
        }

        dc = crtc_from_con_id(du, du->res->connectors[i]);

        ++i;
    } while (dc == NULL);

    return dc;
}

int
drmu_atomic_crtc_hdr_metadata_set(drmu_atomic_t * const da, drmu_crtc_t * const dc, const struct hdr_output_metadata * const m)
{
    drmu_env_t * const du = drmu_atomic_env(da);
    int rv;

    if (dc->pid.hdr_output_metadata == 0 || !du->modeset_allow)
        return 0;

    if (m == NULL) {
        if (dc->hdr_metadata_blob != NULL) {
            drmu_debug(du, "Unset hdr metadata");
            drmu_blob_unref(&dc->hdr_metadata_blob);
        }
    }
    else {
        const size_t blob_len = sizeof(*m);
        drmu_blob_t * blob = NULL;

        if (dc->hdr_metadata_blob == NULL || memcmp(&dc->hdr_metadata, m, blob_len) != 0)
        {
            drmu_debug(du, "Set hdr metadata");

            if ((blob = drmu_blob_new(du, m, blob_len)) == NULL)
                return -ENOMEM;

            // memcpy rather than structure copy to ensure keeping all padding 0s
            memcpy(&dc->hdr_metadata, m, blob_len);

            drmu_blob_unref(&dc->hdr_metadata_blob);
            dc->hdr_metadata_blob = blob;
        }
    }

    rv = drmu_atomic_add_prop_blob(da, dc->con->connector_id, dc->pid.hdr_output_metadata, dc->hdr_metadata_blob);
    if (rv != 0)
        drmu_err(du, "Set property fail: %s", strerror(errno));

    return rv;
}

// This sets width/height etc on the CRTC
// Really it should be held with the atomic but so far I haven't worked out
// a plausible API
int
drmu_atomic_crtc_mode_id_set(drmu_atomic_t * const da, drmu_crtc_t * const dc, const int mode_id)
{
    drmu_env_t * const du = dc->du;
    drmu_blob_t * blob = NULL;
    const struct drm_mode_modeinfo * mode;

    // they are the same structure really
    assert(sizeof(*dc->con->modes) == sizeof(*mode));

    if (mode_id < 0 || dc->pid.mode_id == 0 || !du->modeset_allow)
        return 0;

    if (dc->cur_mode_id == mode_id && dc->mode_id_blob != NULL)
        return 0;

    mode = (const struct drm_mode_modeinfo *)(dc->con->modes + mode_id);
    if ((blob = drmu_blob_new(du, mode, sizeof(*mode))) == NULL) {
        return -ENOMEM;
    }

    drmu_blob_unref(&dc->mode_id_blob);
    dc->cur_mode_id = mode_id;
    dc->mode_id_blob = blob;

    dc->crtc.mode = *mode;
    crtc_mode_set_vars(dc);

    return drmu_atomic_add_prop_blob(da, dc->enc->crtc_id, dc->pid.mode_id, dc->mode_id_blob);
}

int
drmu_atomic_crtc_colorspace_set(drmu_atomic_t * const da, drmu_crtc_t * const dc, const char * colorspace, int hi_bpc)
{
    if (!hi_bpc || !dc->hi_bpc_ok || !colorspace || strcmp(colorspace, "BT2020_RGB") != 0) {
        return atomic_crtc_bpc_set(da, dc, colorspace, 8);
    }
    else {
        return atomic_crtc_hi_bpc_set(da, dc);
    }
}

//----------------------------------------------------------------------------
//
// Atomic Q fns (internal)

static void atomic_q_retry(drmu_atomic_q_t * const aq, drmu_env_t * const du);

// Needs locked
static int
atomic_q_attempt_commit_next(drmu_atomic_q_t * const aq)
{
    drmu_env_t * const du = drmu_atomic_env(aq->next_flip);
    int rv;

    if ((rv = drmu_atomic_commit(aq->next_flip, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT)) == 0) {
        if (aq->retry_count != 0)
            drmu_warn(du, "%s: Atomic commit OK", __func__);
        aq->cur_flip = aq->next_flip;
        aq->next_flip = NULL;
        aq->retry_count = 0;
    }
    else if (rv == -EBUSY && ++aq->retry_count < 16) {
        // This really shouldn't happen but we observe that the 1st commit after
        // a modeset often fails with BUSY.  It seems to be fine on a 10ms retry
        // but allow some more in case ww need a bit longer in some cases
        drmu_warn(du, "%s: Atomic commit BUSY", __func__);
        atomic_q_retry(aq, du);
        rv = 0;
    }
    else {
        drmu_err(du, "%s: Atomic commit failed: %s", __func__, strerror(-rv));
        drmu_atomic_dump(aq->next_flip);
        drmu_atomic_unref(&aq->next_flip);
        aq->retry_count = 0;
    }

    return rv;
}

static void
atomic_q_retry_cb(void * v, short revents)
{
    drmu_atomic_q_t * const aq = v;
    (void)revents;

    pthread_mutex_lock(&aq->lock);

    // If we need a retry then next != NULL && cur == NULL
    // if not that then we've fixed ourselves elsewhere

    if (aq->next_flip != NULL && aq->cur_flip == NULL)
        atomic_q_attempt_commit_next(aq);

    pthread_mutex_unlock(&aq->lock);
}

static void
atomic_q_retry(drmu_atomic_q_t * const aq, drmu_env_t * const du)
{
    if (aq->retry_task == NULL)
        aq->retry_task = polltask_new_timer(du->pq, atomic_q_retry_cb, aq);
    pollqueue_add_task(aq->retry_task, 20);
}

// Called after an atomic commit has completed
// not called on every vsync, so if we haven't committed anything this won't be called
static void
drmu_atomic_page_flip_cb(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, unsigned int crtc_id, void *user_data)
{
    drmu_atomic_t * const da = user_data;
    drmu_env_t * const du = drmu_atomic_env(da);
    drmu_atomic_q_t * const aq = &du->aq;

    (void)fd;
    (void)sequence;
    (void)tv_sec;
    (void)tv_usec;
    (void)crtc_id;

    // At this point:
    //  next   The atomic we are about to commit
    //  cur    The last atomic we committed, now in use (must be != NULL)
    //  last   The atomic that has just become obsolete

    pthread_mutex_lock(&aq->lock);

    if (da != aq->cur_flip) {
        drmu_err(du, "%s: User data el (%p) != cur (%p)", __func__, da, aq->cur_flip);
    }

    drmu_atomic_unref(&aq->last_flip);
    aq->last_flip = aq->cur_flip;
    aq->cur_flip = NULL;

    if (aq->next_flip != NULL)
        atomic_q_attempt_commit_next(aq);

    pthread_mutex_unlock(&aq->lock);
}

// 'consumes' da
static int
atomic_q_queue(drmu_atomic_q_t * const aq, drmu_atomic_t * da)
{
    int rv = 0;

    pthread_mutex_lock(&aq->lock);

    if (aq->next_flip != NULL) {
        // We already have something pending or retrying - merge the new with it
        rv = drmu_atomic_merge(aq->next_flip, &da);
    }
    else {
        aq->next_flip = da;

        // No pending commit?
        if (aq->cur_flip == NULL)
            rv = atomic_q_attempt_commit_next(aq);
    }

    pthread_mutex_unlock(&aq->lock);
    return rv;
}

// Consumes the passed atomic structure as it isn't copied
// * arguably should copy & unref if ref count != 0
int
drmu_atomic_queue(drmu_atomic_t ** ppda)
{
    drmu_atomic_t * da = *ppda;

    if (da == NULL)
        return 0;
    *ppda = NULL;

    return atomic_q_queue(&drmu_atomic_env(da)->aq, da);
}

static void
drmu_atomic_q_uninit(drmu_atomic_q_t * const aq)
{
    polltask_delete(&aq->retry_task);
    drmu_atomic_unref(&aq->next_flip);
    drmu_atomic_unref(&aq->cur_flip);
    drmu_atomic_unref(&aq->last_flip);
    pthread_mutex_destroy(&aq->lock);
}

static void
drmu_atomic_q_init(drmu_atomic_q_t * const aq)
{
    aq->next_flip = NULL;
    pthread_mutex_init(&aq->lock, NULL);
}

//----------------------------------------------------------------------------
//
// Pool fns

static void
fb_list_add_tail(drmu_fb_list_t * const fbl, drmu_fb_t * const dfb)
{
    assert(dfb->prev == NULL && dfb->next == NULL);

    if (fbl->tail == NULL)
        fbl->head = dfb;
    else
        fbl->tail->next = dfb;
    dfb->prev = fbl->tail;
    fbl->tail = dfb;
}

static drmu_fb_t *
fb_list_extract(drmu_fb_list_t * const fbl, drmu_fb_t * const dfb)
{
    if (dfb == NULL)
        return NULL;

    if (dfb->prev == NULL)
        fbl->head = dfb->next;
    else
        dfb->prev->next = dfb->next;

    if (dfb->next == NULL)
        fbl->tail = dfb->prev;
    else
        dfb->next->prev = dfb->prev;

    dfb->next = NULL;
    dfb->prev = NULL;
    return dfb;
}

static drmu_fb_t *
fb_list_extract_head(drmu_fb_list_t * const fbl)
{
    return fb_list_extract(fbl, fbl->head);
}

static drmu_fb_t *
fb_list_peek_head(drmu_fb_list_t * const fbl)
{
    return fbl->head;
}

static bool
fb_list_is_empty(drmu_fb_list_t * const fbl)
{
    return fbl->head == NULL;
}

static void
pool_free_pool(drmu_pool_t * const pool)
{
    drmu_fb_t * dfb;
    while ((dfb = fb_list_extract_head(&pool->free_fbs)) != NULL)
        drmu_fb_unref(&dfb);
}

static void
pool_free(drmu_pool_t * const pool)
{
    pool_free_pool(pool);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

void
drmu_pool_unref(drmu_pool_t ** const pppool)
{
    drmu_pool_t * const pool = *pppool;

    if (pool == NULL)
        return;
    *pppool = NULL;

    if (atomic_fetch_sub(&pool->ref_count, 1) != 0)
        return;

    pool_free(pool);
}

drmu_pool_t *
drmu_pool_ref(drmu_pool_t * const pool)
{
    atomic_fetch_add(&pool->ref_count, 1);
    return pool;
}

drmu_pool_t *
drmu_pool_new(drmu_env_t * const du, unsigned int total_fbs_max)
{
    drmu_pool_t * const pool = calloc(1, sizeof(*pool));

    if (pool == NULL) {
        drmu_err(du, "Failed pool env alloc");
        return NULL;
    }

    pool->du = du;
    pool->fb_max = total_fbs_max;
    pthread_mutex_init(&pool->lock, NULL);

    return pool;
}

static int
pool_fb_pre_delete_cb(drmu_fb_t * dfb, void * v)
{
    drmu_pool_t * pool = v;

    // Ensure we cannot end up in a delete loop
    drmu_fb_pre_delete_unset(dfb);

    // If dead set then might as well delete now
    // It should all work without this shortcut but this reclaims
    // storage quicker
    if (pool->dead) {
        drmu_pool_unref(&pool);
        return 0;
    }

    drmu_fb_ref(dfb);  // Restore ref

    pthread_mutex_lock(&pool->lock);
    fb_list_add_tail(&pool->free_fbs, dfb);
    pthread_mutex_unlock(&pool->lock);

    // May cause suicide & recursion on fb delete, but that should be OK as
    // the 1 we return here should cause simple exit of fb delete
    drmu_pool_unref(&pool);
    return 1;  // Stop delete
}

drmu_fb_t *
drmu_pool_fb_new_dumb(drmu_pool_t * const pool, uint32_t w, uint32_t h, const uint32_t format)
{
    drmu_env_t * const du = pool->du;
    drmu_fb_t * dfb;

    pthread_mutex_lock(&pool->lock);

    dfb = fb_list_peek_head(&pool->free_fbs);
    while (dfb != NULL) {
        if (fb_try_reuse(dfb, w, h, format)) {
            fb_list_extract(&pool->free_fbs, dfb);
            break;
        }
        dfb = dfb->next;
    }

    if (dfb == NULL) {
        if (pool->fb_count >= pool->fb_max && !fb_list_is_empty(&pool->free_fbs)) {
            --pool->fb_count;
            dfb = fb_list_extract_head(&pool->free_fbs);
        }
        ++pool->fb_count;
        pthread_mutex_unlock(&pool->lock);

        drmu_fb_unref(&dfb);  // Will free the dfb as pre-delete CB will be unset
        if ((dfb = drmu_fb_realloc_dumb(du, NULL, w, h, format)) == NULL) {
            --pool->fb_count;  // ??? lock
            return NULL;
        }
    }
    else {
        pthread_mutex_unlock(&pool->lock);
    }

    drmu_fb_pre_delete_set(dfb, pool_fb_pre_delete_cb, pool);
    drmu_pool_ref(pool);
    return dfb;
}

// Mark pool as dead (i.e. no new allocs) and unref it
// Simple unref will also work but this reclaims storage faster
// Actual pool structure will persist until all referencing fbs are deleted too
void
drmu_pool_delete(drmu_pool_t ** const pppool)
{
    drmu_pool_t * pool = *pppool;

    if (pool == NULL)
        return;
    *pppool = NULL;

    pool->dead = 1;
    pool_free_pool(pool);

    drmu_pool_unref(&pool);
}

//----------------------------------------------------------------------------
//
// Plane fns

static int
plane_set_atomic(drmu_atomic_t * const da,
                 drmu_plane_t * const dp,
                 drmu_fb_t * const dfb,
                int32_t crtc_x, int32_t crtc_y,
                uint32_t crtc_w, uint32_t crtc_h,
                uint32_t src_x, uint32_t src_y,
                uint32_t src_w, uint32_t src_h)
{
    const uint32_t plid = dp->plane->plane_id;
    drmu_atomic_add_prop_value(da, plid, dp->pid.crtc_id, dfb == NULL ? 0 : drmu_crtc_id(dp->dc));
    drmu_atomic_add_prop_fb(da, plid, dp->pid.fb_id, dfb);
    drmu_atomic_add_prop_value(da, plid, dp->pid.crtc_x, crtc_x);
    drmu_atomic_add_prop_value(da, plid, dp->pid.crtc_y, crtc_y);
    drmu_atomic_add_prop_value(da, plid, dp->pid.crtc_w, crtc_w);
    drmu_atomic_add_prop_value(da, plid, dp->pid.crtc_h, crtc_h);
    drmu_atomic_add_prop_value(da, plid, dp->pid.src_x,  src_x);
    drmu_atomic_add_prop_value(da, plid, dp->pid.src_y,  src_y);
    drmu_atomic_add_prop_value(da, plid, dp->pid.src_w,  src_w);
    drmu_atomic_add_prop_value(da, plid, dp->pid.src_h,  src_h);
    return 0;
}

int
drmu_atomic_plane_set(drmu_atomic_t * const da, drmu_plane_t * const dp,
    drmu_fb_t * const dfb, const drmu_rect_t pos)
{
    int rv;
    const uint32_t plid = dp->plane->plane_id;

    if (dfb == NULL) {
        rv = plane_set_atomic(da, dp, NULL,
                              0, 0, 0, 0,
                              0, 0, 0, 0);
    }
    else {
        rv = plane_set_atomic(da, dp, dfb,
                              pos.x, pos.y, pos.w, pos.h,
                              dfb->cropped.x << 16, dfb->cropped.y << 16, dfb->cropped.w << 16, dfb->cropped.h << 16);
    }
    if (rv != 0 || dfb == NULL)
        return rv;

    drmu_atomic_add_prop_enum(da, plid, dp->pid.color_encoding, dfb->color_encoding);
    drmu_atomic_add_prop_enum(da, plid, dp->pid.color_range,    dfb->color_range);

    // *** Need to rethink this
    if (dp->dc != NULL) {
        drmu_crtc_t * const dc = dp->dc;

        if (dfb->colorspace != NULL) {
            drmu_atomic_crtc_colorspace_set(da, dc, dfb->colorspace, 1);
        }
        if (dfb->hdr_metadata_isset == DRMU_ISSET_NULL)
            drmu_atomic_crtc_hdr_metadata_set(da, dc, NULL);
        else if (dfb->hdr_metadata_isset == DRMU_ISSET_SET)
            drmu_atomic_crtc_hdr_metadata_set(da, dc, &dfb->hdr_metadata);
    }

    return rv != 0 ? -errno : 0;
}

uint32_t
drmu_plane_id(const drmu_plane_t * const dp)
{
    return dp->plane->plane_id;
}

const uint32_t *
drmu_plane_formats(const drmu_plane_t * const dp, unsigned int * const pCount)
{
    *pCount = dp->plane->count_formats;
    return dp->plane->formats;
}

void
drmu_plane_delete(drmu_plane_t ** const ppdp)
{
    drmu_plane_t * const dp = *ppdp;

    if (dp == NULL)
        return;
    *ppdp = NULL;

    drmu_prop_enum_delete(&dp->pid.color_encoding);
    drmu_prop_enum_delete(&dp->pid.color_range);
    dp->dc = NULL;
}

drmu_plane_t *
drmu_plane_new_find(drmu_crtc_t * const dc, const uint32_t fmt)
{
    uint32_t i;
    drmu_env_t * const du = drmu_crtc_env(dc);
    drmu_plane_t * dp = NULL;
    const uint32_t crtc_mask = (uint32_t)1 << drmu_crtc_idx(dc);

    for (i = 0; i != du->plane_count && dp == NULL; ++i) {
        uint32_t j;
        const drmModePlane * const p = du->planes[i].plane;

        // In use?
        if (du->planes[i].dc != NULL)
            continue;

        // Availible for this crtc?
        if ((p->possible_crtcs & crtc_mask) == 0)
            continue;

        // Has correct format?
        for (j = 0; j != p->count_formats; ++j) {
            if (p->formats[j] == fmt) {
                dp = du->planes + i;
                break;
            }
        }
    }
    if (dp == NULL) {
        drmu_err(du, "%s: No plane (count=%d) found for fmt %#x", __func__, du->plane_count, fmt);
        return NULL;
    }

    dp->dc = dc;
    return dp;
}

static void
free_planes(drmu_env_t * const du)
{
    uint32_t i;
    for (i = 0; i != du->plane_count; ++i)
        drmModeFreePlane((drmModePlane*)du->planes[i].plane);
    free(du->planes);
    du->plane_count = 0;
    du->planes = NULL;
}

//----------------------------------------------------------------------------
//
// Env fns

int
drmu_ioctl(const drmu_env_t * const du, unsigned long req, void * arg)
{
    while (ioctl(du->fd, req, arg)) {
        const int err = errno;
        // DRM docn suggests we should try again on EAGAIN as well as EINTR
        // and drm userspace does this.
        if (err != EINTR && err != EAGAIN)
            return -err;
    }
    return 0;
}

static int
drmu_env_planes_populate(drmu_env_t * const du)
{
    int err = EINVAL;
    drmModePlaneResPtr res;
    uint32_t i;

    if ((res = drmModeGetPlaneResources(du->fd)) == NULL) {
        err = errno;
        drmu_err(du, "%s: drmModeGetPlaneResources failed: %s", __func__, strerror(err));
        goto fail0;
    }

    if ((du->planes = calloc(res->count_planes, sizeof(*du->planes))) == NULL) {
        err = ENOMEM;
        drmu_err(du, "%s: drmModeGetPlaneResources failed: %s", __func__, strerror(err));
        goto fail1;
    }

    for (i = 0; i != res->count_planes; ++i) {
        drmu_plane_t * const dp = du->planes + i;
        drmu_props_t *props;

        dp->du = du;

        if ((dp->plane = drmModeGetPlane(du->fd, res->planes[i])) == NULL) {
            err = errno;
            drmu_err(du, "%s: drmModeGetPlane failed: %s", __func__, strerror(err));
            goto fail2;
        }

        if ((props = props_new(du, dp->plane->plane_id, DRM_MODE_OBJECT_PLANE)) == NULL) {
            err = errno;
            drmu_err(du, "%s: drmModeObjectGetProperties failed: %s", __func__, strerror(err));
            goto fail2;
        }

        if ((dp->pid.crtc_id = props_name_to_id(props, "CRTC_ID")) == 0 ||
            (dp->pid.fb_id  = props_name_to_id(props, "FB_ID")) == 0 ||
            (dp->pid.crtc_h = props_name_to_id(props, "CRTC_H")) == 0 ||
            (dp->pid.crtc_w = props_name_to_id(props, "CRTC_W")) == 0 ||
            (dp->pid.crtc_x = props_name_to_id(props, "CRTC_X")) == 0 ||
            (dp->pid.crtc_y = props_name_to_id(props, "CRTC_Y")) == 0 ||
            (dp->pid.src_h  = props_name_to_id(props, "SRC_H")) == 0 ||
            (dp->pid.src_w  = props_name_to_id(props, "SRC_W")) == 0 ||
            (dp->pid.src_x  = props_name_to_id(props, "SRC_X")) == 0 ||
            (dp->pid.src_y  = props_name_to_id(props, "SRC_Y")) == 0)
        {
            drmu_err(du, "%s: failed to find required id", __func__);
            props_free(props);
            goto fail2;
        }

        dp->pid.color_encoding = drmu_prop_enum_new(du, props_name_to_id(props, "COLOR_ENCODING"));
        dp->pid.color_range    = drmu_prop_enum_new(du, props_name_to_id(props, "COLOR_RANGE"));

        props_free(props);
        du->plane_count = i + 1;
    }

    return 0;

fail2:
    free_planes(du);
fail1:
    drmModeFreePlaneResources(res);
fail0:
    return -err;
}

int
drmu_fd(const drmu_env_t * const du)
{
    return du->fd;
}

const struct drmu_log_env_s *
drmu_env_log(const drmu_env_t * const du)
{
    return &du->log;
}

void
drmu_env_delete(drmu_env_t ** const ppdu)
{
    drmu_env_t * const du = *ppdu;

    if (!du)
        return;
    *ppdu = NULL;

    pollqueue_unref(&du->pq);
    polltask_delete(&du->pt);

    if (du->res != NULL)
        drmModeFreeResources(du->res);
    free_planes(du);
    drmu_atomic_q_uninit(&du->aq);
    drmu_bo_env_uninit(&du->boe);

    close(du->fd);
    free(du);
}

// Default is yes
void
drmu_env_modeset_allow(drmu_env_t * const du, const bool modeset_allowed)
{
    du->modeset_allow = modeset_allowed;
}

static void
drmu_env_polltask_cb(void * v, short revents)
{
    drmu_env_t * const du = v;
    drmEventContext ctx = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler2 = drmu_atomic_page_flip_cb,
    };

    if (revents == 0) {
        drmu_warn(du, "%s: Timeout", __func__);
    }
    else {
        drmHandleEvent(du->fd, &ctx);
    }

    pollqueue_add_task(du->pt, 1000);
}

// Closes fd on failure
drmu_env_t *
drmu_env_new_fd(const int fd, const struct drmu_log_env_s * const log)
{
    drmu_env_t * du = calloc(1, sizeof(*du));
    if (!du) {
        drmu_err_log(log, "Failed to create du: No memory");
        close(fd);
        return NULL;
    }

    du->log = *log;
    du->fd = fd;
    du->modeset_allow = true;

    drmu_bo_env_init(&du->boe);
    drmu_atomic_q_init(&du->aq);

    if ((du->pq = pollqueue_new()) == NULL) {
        drmu_err(du, "Failed to create pollqueue");
        goto fail1;
    }
    if ((du->pt = polltask_new(du->pq, du->fd, POLLIN | POLLPRI, drmu_env_polltask_cb, du)) == NULL) {
        drmu_err(du, "Failed to create polltask");
        goto fail1;
    }

    // We want the primary plane for video
    drmSetClientCap(du->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(du->fd, DRM_CLIENT_CAP_ATOMIC, 1);

    if (drmu_env_planes_populate(du) != 0)
        goto fail1;

    if ((du->res = drmModeGetResources(du->fd)) == NULL) {
        drmu_err(du, "%s: Failed to get resources", __func__);
        goto fail1;
    }

    pollqueue_add_task(du->pt, 1000);

    return du;

fail1:
    drmu_env_delete(&du);
    return NULL;
}

drmu_env_t *
drmu_env_new_open(const char * name, const struct drmu_log_env_s * const log)
{
    int fd = drmOpen(name, NULL);
    if (fd == -1) {
        drmu_err_log(log, "Failed to open %s", name);
        return NULL;
    }
    return drmu_env_new_fd(fd, log);
}

//----------------------------------------------------------------------------
//
// Logging

static void
log_none_cb(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl)
{
    (void)v;
    (void)level;
    (void)fmt;
    (void)vl;
}

const struct drmu_log_env_s drmu_log_env_none = {
    .fn = log_none_cb,
    .v = NULL,
    .max_level = DRMU_LOG_LEVEL_NONE
};

void
drmu_log_generic(const struct drmu_log_env_s * const log, const enum drmu_log_level_e level,
                 const char * const fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    log->fn(log->v, level, fmt, vl);
    va_end(vl);
}


