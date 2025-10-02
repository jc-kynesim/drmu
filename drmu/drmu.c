// Needed to ensure we get a 64-bit offset to mmap when mapping BOs
#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64

#include "drmu.h"
#include "drmu_fmts.h"
#include "drmu_log.h"

#include <pthread.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <libdrm/drm_fourcc.h>
#include <xf86drm.h>

#include <linux/dma-buf.h>

#define TRACE_PROP_NEW 0

#ifndef OPT_IO_CALLOC
#define OPT_IO_CALLOC 0
#endif

#ifndef DRM_FORMAT_P030
#define DRM_FORMAT_P030 fourcc_code('P', '0', '3', '0')
#endif

struct drmu_bo_env_s;
static struct drmu_bo_env_s * env_boe(drmu_env_t * const du);
static int env_object_state_save(drmu_env_t * const du, const uint32_t obj_id, const uint32_t obj_type);

// Update return value with a new one for cases where we don't stop on error
static inline int rvup(int rv1, int rv2)
{
    return rv2 ? rv2 : rv1;
}

// Use io_alloc when allocating arrays to pass into ioctls.
//
// When debugging with valgrind use calloc rather than malloc otherwise arrays
// set by ioctls that valgrind doesn't know about (e.g. all drm ioctls) will
// still be full of 'undefined'.
// For normal use malloc should be fine
#if OPT_IO_CALLOC
#define io_alloc(p, n) (uintptr_t)((p) = calloc((n), sizeof(*(p))))
#else
#define io_alloc(p, n) (uintptr_t)((p) = malloc((n) * sizeof(*(p))))
#endif

// Alloc retry helper
static inline int
retry_alloc_u32(uint32_t ** const pp, uint32_t * const palloc_count, uint32_t const new_count)
{
    if (new_count <= *palloc_count)
        return 0;
    free(*pp);
    *palloc_count = 0;
    if (io_alloc(*pp, new_count) == 0)
        return -ENOMEM;
    *palloc_count = new_count;
    return 1;
}

//----------------------------------------------------------------------------
//
// propinfo

typedef struct drmu_propinfo_s {
    uint64_t val;
    struct drm_mode_get_property prop;
} drmu_propinfo_t;

static uint64_t
propinfo_val(const drmu_propinfo_t * const pi)
{
    return pi == NULL ? 0 : pi->val;
}

static uint32_t
propinfo_prop_id(const drmu_propinfo_t * const pi)
{
    return pi == NULL ? 0 : pi->prop.prop_id;
}


//----------------------------------------------------------------------------
//
// Blob fns

typedef struct drmu_blob_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init
    struct drmu_env_s * du;
    uint32_t blob_id;
    // Copy of blob data as we nearly always want to keep a copy to compare
    size_t len;
    void * data;
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
    free(blob->data);
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

const void *
drmu_blob_data(const drmu_blob_t * const blob)
{
    return blob->data;
}

size_t
drmu_blob_len(const drmu_blob_t * const blob)
{
    return blob->len;
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

    if ((blob->data = malloc(len)) == NULL) {
        drmu_err(du, "%s: Unable to alloc blob data", __func__);
        goto fail;
    }
    blob->len = len;
    memcpy(blob->data, data, len);

    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_CREATEPROPBLOB, &cblob)) != 0) {
        drmu_err(du, "%s: Unable to create blob: data=%p, len=%zu: %s", __func__,
                 data, len, strerror(-rv));
        goto fail;
    }

    atomic_init(&blob->ref_count, 0);
    blob->blob_id = cblob.blob_id;
    return blob;

fail:
    blob_free(blob);
    return NULL;
}

int
drmu_blob_update(drmu_env_t * const du, drmu_blob_t ** const ppblob, const void * const data, const size_t len)
{
    drmu_blob_t * blob = *ppblob;

    if (data == NULL || len == 0) {
        drmu_blob_unref(ppblob);
        return 0;
    }

    if (blob && len == blob->len && memcmp(data, blob->data, len) == 0)
        return 0;

    if ((blob = drmu_blob_new(du, data, len)) == NULL)
        return -ENOMEM;
    drmu_blob_unref(ppblob);
    *ppblob = blob;
    return 0;
}

// Data alloced here needs freeing later
static int
blob_data_read(drmu_env_t * const du, uint32_t blob_id, void ** const ppdata, size_t * plen)
{
    uint8_t * data;
    struct drm_mode_get_blob gblob = {.blob_id = blob_id};
    int rv;

    *ppdata = NULL;
    *plen = 0;

    if (blob_id == 0)
        return 0;

    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETPROPBLOB, &gblob)) != 0)
        return rv;

    if (gblob.length == 0)
        return 0;

    if ((gblob.data = io_alloc(data, gblob.length)) == 0)
        return -ENOMEM;

    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETPROPBLOB, &gblob)) != 0) {
        free(data);
        return rv;
    }

    *ppdata = data;
    *plen = gblob.length;
    return 0;
}

// Copy existing blob into a new one
// Useful when saving preexisiting values
drmu_blob_t *
drmu_blob_copy_id(drmu_env_t * const du, uint32_t blob_id)
{
    void * data;
    size_t len;
    drmu_blob_t * blob = NULL;

    if (blob_data_read(du, blob_id, &data, &len) == 0)
        blob = drmu_blob_new(du, data, len);  // * This copies data - could just get it to take the malloc

    free(data);
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
    static const drmu_atomic_prop_fns_t fns = {
        .ref = atomic_prop_blob_ref,
        .unref = atomic_prop_blob_unref,
        .commit = drmu_prop_fn_null_commit
    };

    if (blob == NULL)
        return drmu_atomic_add_prop_value(da, obj_id, prop_id, 0);

    rv = drmu_atomic_add_prop_generic(da, obj_id, prop_id, drmu_blob_id(blob), &fns, blob);
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
        if (io_alloc(enums, pen->n) == 0)
            goto fail;
    }
    if (retries >= 8) {
        drmu_err(du, "%s: Too many retries", __func__);
        goto fail;
    }

    qsort(enums, pen->n, sizeof(*enums), prop_enum_qsort_cb);
    pen->enums = enums;

#if TRACE_PROP_NEW
    if (!pen->n) {
        drmu_info(du, "%32s %2d: no properties");
    }
    else {
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
        drmu_atomic_add_prop_generic(da, obj_id, drmu_prop_enum_id(pen), *pval, NULL, NULL);

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
            drmu_atomic_add_prop_generic(da, obj_id, drmu_prop_enum_id(pen), val, NULL, NULL);

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
    if ((pra->flags & DRM_MODE_PROP_EXTENDED_TYPE) == DRM_MODE_PROP_SIGNED_RANGE) {
        return (int64_t)pra->range[0] <= (int64_t)x && (int64_t)pra->range[1] >= (int64_t)x;
    }
    return pra->range[0] <= x && pra->range[1] >= x;
}

bool
drmu_prop_range_immutable(const drmu_prop_range_t * const pra)
{
    return !pra || (pra->flags & DRM_MODE_PROP_IMMUTABLE) != 0;
}

uint64_t
drmu_prop_range_max(const drmu_prop_range_t * const pra)
{
    return pra == NULL ? 0 : pra->range[1];
}

uint64_t
drmu_prop_range_min(const drmu_prop_range_t * const pra)
{
    return pra == NULL ? 0 : pra->range[0];
}

uint32_t
drmu_prop_range_id(const drmu_prop_range_t * const pra)
{
    return pra == NULL ? 0 : pra->id;
}

const char *
drmu_prop_range_name(const drmu_prop_range_t * const pra)
{
    return pra == NULL ? "{norange}" : pra->name;
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

        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETPROPERTY, &prop)) != 0) {
            drmu_err(du, "%s: get property failed: %s", __func__, strerror(-rv));
            goto fail;
        }

        if ((prop.flags & DRM_MODE_PROP_RANGE) == 0 &&
            (prop.flags & DRM_MODE_PROP_EXTENDED_TYPE) != DRM_MODE_PROP_SIGNED_RANGE) {
            drmu_err(du, "%s: not an signed range: flags=%#x", __func__, prop.flags);
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

    rv = !pra ? -ENOENT :
        !drmu_prop_range_validate(pra, x) ? -EINVAL :
        drmu_prop_range_immutable(pra) ? -EPERM :
        drmu_atomic_add_prop_generic(da, obj_id, drmu_prop_range_id(pra), x, NULL, NULL);

    if (rv != 0) {
        if (rv == -EPERM && x == drmu_prop_range_min(pra) && x == drmu_prop_range_max(pra))
            return 0;
        drmu_warn(drmu_atomic_env(da),
                  "%s: Failed to add range %s obj_id=%#x, prop_id=%#x, val=%"PRId64", range=%"PRId64"->%"PRId64": %s",
                  __func__, drmu_prop_range_name(pra),
                  obj_id, drmu_prop_range_id(pra), x, drmu_prop_range_min(pra), drmu_prop_range_max(pra), strerror(-rv));
    }

    return rv;
}

//----------------------------------------------------------------------------
//
// Object ID (tracked)

typedef struct drmu_prop_object_s {
    atomic_int ref_count;
    uint32_t obj_id;
    uint32_t prop_id;
    uint32_t value;
} drmu_prop_object_t;

uint32_t
drmu_prop_object_value(const drmu_prop_object_t * const obj)
{
    return !obj ? 0 : obj->value;
}

void
drmu_prop_object_unref(drmu_prop_object_t ** ppobj)
{
    drmu_prop_object_t * const obj = *ppobj;

    if (!obj)
        return;
    *ppobj = NULL;

    if (atomic_fetch_sub(&obj->ref_count, 1) != 0)
        return;

    free(obj);
}

drmu_prop_object_t *
drmu_prop_object_new_propinfo(drmu_env_t * const du, const uint32_t obj_id, const drmu_propinfo_t * const pi)
{
    const uint64_t val = propinfo_val(pi);
    const uint32_t prop_id = propinfo_prop_id(pi);

    if (obj_id == 0 || prop_id == 0)
        return NULL;

    if ((val >> 32) != 0) {  // We expect 32-bit values
        drmu_err(du, "Bad object id value: %#"PRIx64, val);
        return NULL;
    }
    else {
        drmu_prop_object_t *const obj = calloc(1, sizeof(*obj));

        if (obj == NULL)
            return obj;

        obj->obj_id = obj_id;
        obj->prop_id = prop_id;
        obj->value = (uint32_t)val;
        return obj;
    }
}

static void
atomic_prop_object_unref(void * v)
{
    drmu_prop_object_t * obj = v;
    drmu_prop_object_unref(&obj);
}
static void
atomic_prop_object_ref(void * v)
{
    drmu_prop_object_t * obj = v;
    atomic_fetch_add(&obj->ref_count, 1);
}
static void
atomic_prop_object_commit(void * v, uint64_t val)
{
    drmu_prop_object_t * obj = v;
    obj->value = (uint32_t)val;
}

int
drmu_atomic_add_prop_object(drmu_atomic_t * const da, drmu_prop_object_t * obj, uint32_t val)
{
    static const drmu_atomic_prop_fns_t fns = {
        .ref = atomic_prop_object_ref,
        .unref = atomic_prop_object_unref,
        .commit = atomic_prop_object_commit,
    };

    return drmu_atomic_add_prop_generic(da, obj->obj_id, obj->prop_id, val, &fns, obj);
}

//----------------------------------------------------------------------------
//
// BO fns
//
// Beware that when importing from FD we need to check that we don't already
// have the BO as multiple FDs can map to the same BO and a single close will
// close it irrespective of how many imports have occured.

enum drmu_bo_type_e {
    BO_TYPE_NONE = 0,
    BO_TYPE_FD,         // Created from FD import
    BO_TYPE_DUMB,       // Locally allocated
    BO_TYPE_EXTERNAL,   // Externally allocated and closed
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
        drmu_bo_env_t *const boe = env_boe(du);
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

void *
drmu_bo_mmap(const drmu_bo_t * const bo, const size_t length, const int prot, const int flags)
{
    void * map_ptr = NULL;

    if (bo != NULL) {
        struct drm_mode_map_dumb map_dumb = { .handle = bo->handle };
        drmu_env_t * const du = bo->du;
        int rv;

        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb)) != 0)
        {
            drmu_err(du, "%s: map dumb failed: %s", __func__, strerror(-rv));
            return NULL;
        }

        // Avoid having to test for MAP_FAILED when testing for mapped/unmapped
        if ((map_ptr = mmap(NULL, length, prot, flags,
                            drmu_fd(du), map_dumb.offset)) == MAP_FAILED) {
            drmu_err(du, "%s: mmap failed (size=%#zx, fd=%d, off=%#"PRIx64"): %s", __func__,
                     length, drmu_fd(du), map_dumb.offset, strerror(errno));
            return NULL;
        }
    }

    return map_ptr;
}

uint32_t
drmu_bo_handle(const drmu_bo_t * const bo)
{
    return bo == NULL ? 0 : bo->handle;
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
            drmu_bo_env_t * const boe = env_boe(bo->du);

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
        case BO_TYPE_EXTERNAL:
            // Simple imported BO - close dealt with elsewhere
            if (atomic_fetch_sub(&bo->ref_count, 1) == 0)
                free(bo);
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

int
drmu_bo_export_fd(drmu_bo_t * bo, uint32_t flags)
{
    struct drm_prime_handle prime_handle = {
        .handle = bo->handle,
        .flags = flags == 0 ? DRM_RDWR | DRM_CLOEXEC : flags,
        .fd = 0
    };

   if (drmu_ioctl(bo->du, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_handle) != 0)
       return -1;

   return prime_handle.fd;
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
drmu_bo_new_external(drmu_env_t *const du, const uint32_t bo_handle)
{
    drmu_bo_t *const bo = bo_alloc(du, BO_TYPE_EXTERNAL);

    if (bo == NULL) {
        drmu_err(du, "%s: Failed to alloc BO", __func__);
        return NULL;
    }

    bo->handle = bo_handle;
    return bo;
}

drmu_bo_t *
drmu_bo_new_fd(drmu_env_t *const du, const int fd)
{
    drmu_bo_env_t * const boe = env_boe(du);
    drmu_bo_t * bo = NULL;
    struct drm_prime_handle ph = { .fd = fd };
    int rv;

    pthread_mutex_lock(&boe->lock);

    if ((rv = drmu_ioctl(du, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph)) != 0) {
        drmu_err(du, "Failed to convert fd %d to BO: %s", __func__, fd, strerror(-rv));
        goto unlock;
    }

    bo = boe->fd_head;
    while (bo != NULL && bo->handle != ph.handle)
        bo = bo->next;

    if (bo != NULL) {
        drmu_bo_ref(bo);
    }
    else {
        if ((bo = bo_alloc(du, BO_TYPE_FD)) == NULL) {
            bo_close(du, &ph.handle);
        }
        else {
            bo->handle = ph.handle;

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

typedef struct drmu_fb_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init

    struct drmu_env_s * du;

    const struct drmu_fmt_info_s * fmt_info;

    struct drm_mode_fb_cmd2 fb;

    drmu_rect_t active;     // Area that was asked for inside the buffer; pixels
    drmu_rect_t crop;       // Cropping inside that; fractional pels (16.16, 16.16)

    struct {
        int fd;

        drmu_bo_t * bo;

        void * map_ptr;
        size_t map_size;
        size_t map_pitch;
    } objects[4];

    int8_t layer_obj[4];

    drmu_color_encoding_t color_encoding; // Assumed to be constant strings that don't need freeing
    drmu_color_range_t    color_range;
    drmu_colorspace_t     colorspace;
    const char * pixel_blend_mode;
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

int
drmu_fb_out_fence_wait(drmu_fb_t * const fb, const int timeout_ms)
{
    struct pollfd pf;
    int rv;

    if (fb->fence_fd == -1)
        return -EINVAL;

    do {
        pf.fd = fb->fence_fd;
        pf.events = POLLIN;
        pf.revents = 0;

        rv = poll(&pf, 1, timeout_ms);
        if (rv >= 0)
            break;

        rv = -errno;
    } while (rv == -EINTR);

    if (rv == 0)
        return 0;

    // Both on error & success close the fd
    close(fb->fence_fd);
    fb->fence_fd = -1;
    return rv;
}

void
drmu_fb_int_free(drmu_fb_t * const dfb)
{
    drmu_env_t * const du = dfb->du;
    unsigned int i;

    if (dfb->pre_delete_fn && dfb->pre_delete_fn(dfb, dfb->pre_delete_v) != 0)
        return;

    // * If we implement callbacks this logic will want revision
    if (dfb->fence_fd != -1) {
        drmu_warn(du, "Out fence still set on FB on delete");
        if (drmu_fb_out_fence_wait(dfb, 500) == 0) {
            drmu_err(du, "Out fence stuck in FB free");
            close(dfb->fence_fd);
        }
    }

    if (dfb->fb.fb_id != 0)
        drmu_ioctl(du, DRM_IOCTL_MODE_RMFB, &dfb->fb.fb_id);

    for (i = 0; i != 4; ++i) {
        if (dfb->objects[i].map_ptr != NULL)
            munmap(dfb->objects[i].map_ptr, dfb->objects[i].map_size);
        drmu_bo_unref(&dfb->objects[i].bo);
        if (dfb->objects[i].fd != -1)
            close(dfb->objects[i].fd);
    }


    // Call on_delete last so we have stopped using anything that might be
    // freed by it
    {
        void * const v = dfb->on_delete_v;
        const drmu_fb_on_delete_fn fn = dfb->on_delete_fn;

        free(dfb);

        if (fn)
            fn(v);
    }
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

static void
atomic_prop_fb_unref_cb(void * v)
{
    drmu_fb_t * dfb = v;
    drmu_fb_unref(&dfb);
}

drmu_fb_t *
drmu_fb_ref(drmu_fb_t * const dfb)
{
    if (dfb != NULL)
        atomic_fetch_add(&dfb->ref_count, 1);
    return dfb;
}

static void
atomic_prop_fb_ref_cb(void * v)
{
    drmu_fb_ref(v);
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

int
drmu_fb_pixel_blend_mode_set(drmu_fb_t *const dfb, const char * const mode)
{
    dfb->pixel_blend_mode = mode;
    return 0;
}

uint32_t
drmu_fb_pitch(const drmu_fb_t *const dfb, const unsigned int layer)
{
    return layer >= 4 ? 0 : dfb->fb.pitches[layer];
}

uint32_t
drmu_fb_pitch2(const drmu_fb_t *const dfb, const unsigned int layer)
{
    if (layer < 4){
        const uint64_t m = dfb->fb.modifier[layer];
        const uint64_t s2 = fourcc_mod_broadcom_param(m);

        if (m == DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(0))
            return layer == 0 ? dfb->fb.height : dfb->fb.height / 2;

        // No good masks to check modifier so check if we convert back it matches
        if (m != 0 && m != DRM_FORMAT_MOD_INVALID &&
            DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(s2) == m)
            return (uint32_t)s2;
    }
    return 0;
}

void *
drmu_fb_data(const drmu_fb_t *const dfb, const unsigned int layer)
{
    int obj_idx;
    if (layer >= 4)
        return NULL;
    obj_idx = dfb->layer_obj[layer];
    if (obj_idx < 0)
        return NULL;
    return (dfb->objects[obj_idx].map_ptr == NULL) ? NULL :
        (uint8_t * )dfb->objects[obj_idx].map_ptr + dfb->fb.offsets[layer];
}

drmu_bo_t *
drmu_fb_bo(const drmu_fb_t * const dfb, const unsigned int layer)
{
    int obj_idx;
    if (layer >= 4)
        return NULL;
    obj_idx = dfb->layer_obj[layer];
    if (obj_idx < 0)
        return NULL;
    return dfb->objects[obj_idx].bo;
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

// Set cropping (fractional) - x, y, relative to active x, y (and must be +ve)
int
drmu_fb_crop_frac_set(drmu_fb_t *const dfb, drmu_rect_t crop_frac)
{
    // Sanity check
    if (crop_frac.x + crop_frac.w > (dfb->active.w << 16) ||
        crop_frac.y + crop_frac.h > (dfb->active.h << 16))
        return -EINVAL;

    dfb->crop = (drmu_rect_t){
        .x = crop_frac.x,
        .y = crop_frac.y,
        .w = crop_frac.w,
        .h = crop_frac.h
    };
    return 0;
}

drmu_rect_t
drmu_fb_crop_frac(const drmu_fb_t *const dfb)
{
    return dfb->crop;
}

drmu_rect_t
drmu_fb_active(const drmu_fb_t *const dfb)
{
    return dfb->active;
}


// active is in pixels
void
drmu_fb_int_fmt_size_set(drmu_fb_t *const dfb, uint32_t fmt, uint32_t w, uint32_t h, const drmu_rect_t active)
{
    dfb->fmt_info        = drmu_fmt_info_find_fmt(fmt);
    dfb->fb.pixel_format = fmt;
    dfb->fb.width        = w;
    dfb->fb.height       = h;
    dfb->active          = active;
    dfb->crop            = drmu_rect_shl16(active);
    dfb->chroma_siting   = drmu_fmt_info_chroma_siting(dfb->fmt_info);
}

void
drmu_fb_color_set(drmu_fb_t *const dfb, const drmu_color_encoding_t enc, const drmu_color_range_t range, const drmu_colorspace_t space)
{
    dfb->color_encoding = enc;
    dfb->color_range    = range;
    dfb->colorspace     = space;
}

void
drmu_fb_chroma_siting_set(drmu_fb_t *const dfb, const drmu_chroma_siting_t siting)
{
    dfb->chroma_siting   = siting;
}

void
drmu_fb_int_on_delete_set(drmu_fb_t *const dfb, drmu_fb_on_delete_fn fn, void * v)
{
    dfb->on_delete_fn = fn;
    dfb->on_delete_v  = v;
}

void
drmu_fb_int_bo_set(drmu_fb_t *const dfb, const unsigned int obj_idx, drmu_bo_t * const bo)
{
    dfb->objects[obj_idx].bo = bo;
}

void
drmu_fb_int_fd_set(drmu_fb_t *const dfb, const unsigned int obj_idx, const int fd)
{
    dfb->objects[obj_idx].fd = fd;
}

void
drmu_fb_int_mmap_set(drmu_fb_t *const dfb, const unsigned int obj_idx, void * const buf, const size_t size, const size_t pitch)
{
    dfb->objects[obj_idx].map_ptr = buf;
    dfb->objects[obj_idx].map_size = size;
    dfb->objects[obj_idx].map_pitch = pitch;
}

void
drmu_fb_int_layer_mod_set(drmu_fb_t *const dfb, unsigned int i, unsigned int obj_idx, uint32_t pitch, uint32_t offset, uint64_t modifier)
{
    dfb->layer_obj[i] = obj_idx;
    dfb->fb.handles[i] = drmu_bo_handle(dfb->objects[obj_idx].bo);
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

    dfb->fb.flags = (dfb->fb.modifier[0] == DRM_FORMAT_MOD_INVALID ||
                     dfb->fb.modifier[0] == DRM_FORMAT_MOD_LINEAR) ? 0 : DRM_MODE_FB_MODIFIERS;

    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_ADDFB2, &dfb->fb)) != 0)
        drmu_err(du, "AddFB2 failed: %s", strerror(-rv));
    return rv;
}

void
drmu_fb_hdr_metadata_set(drmu_fb_t *const dfb, const struct hdr_output_metadata * meta)
{
    if (meta == NULL) {
        dfb->hdr_metadata_isset = DRMU_ISSET_NULL;
    }
    else {
        dfb->hdr_metadata_isset = DRMU_ISSET_SET;
        dfb->hdr_metadata = *meta;
    }
}

drmu_isset_t
drmu_fb_hdr_metadata_isset(const drmu_fb_t *const dfb)
{
    return dfb->hdr_metadata_isset;
}

const struct hdr_output_metadata *
drmu_fb_hdr_metadata_get(const drmu_fb_t *const dfb)
{
    return dfb->hdr_metadata_isset == DRMU_ISSET_SET ? &dfb->hdr_metadata : NULL;
}

drmu_colorspace_t
drmu_fb_colorspace_get(const drmu_fb_t * const dfb)
{
    return dfb->colorspace;
}

const char *
drmu_color_range_to_broadcast_rgb(const drmu_color_range_t range)
{
    if (!drmu_color_range_is_set(range))
        return DRMU_BROADCAST_RGB_UNSET;
    else if (strcmp(range, DRMU_COLOR_RANGE_YCBCR_FULL_RANGE) == 0)
        return DRMU_BROADCAST_RGB_FULL;
    else if (strcmp(range, DRMU_COLOR_RANGE_YCBCR_LIMITED_RANGE) == 0)
        return DRMU_BROADCAST_RGB_LIMITED_16_235;
    return NULL;
}

drmu_color_range_t
drmu_fb_color_range_get(const drmu_fb_t * const dfb)
{
    return dfb->color_range;
}

const struct drmu_fmt_info_s *
drmu_fb_format_info_get(const drmu_fb_t * const dfb)
{
    return dfb->fmt_info;
}

drmu_fb_t *
drmu_fb_int_alloc(drmu_env_t * const du)
{
    drmu_fb_t * const dfb = calloc(1, sizeof(*dfb));
    if (dfb == NULL)
        return NULL;

    dfb->du = du;
    dfb->chroma_siting = DRMU_CHROMA_SITING_UNSPECIFIED;
    for (unsigned int i = 0; i != 4; ++i)
        dfb->objects[i].fd = -1;
    for (unsigned int i = 0; i != 4; ++i)
        dfb->layer_obj[i] = -1;
    dfb->fence_fd = -1;
    return dfb;
}

// Bits per pixel on plane 0
unsigned int
drmu_fb_pixel_bits(const drmu_fb_t * const dfb)
{
    return drmu_fmt_info_pixel_bits(dfb->fmt_info);
}

uint32_t
drmu_fb_pixel_format(const drmu_fb_t * const dfb)
{
    return dfb->fb.pixel_format;
}

uint64_t
drmu_fb_modifier(const drmu_fb_t * const dfb, const unsigned int plane)
{
    return plane >= 4 ? DRM_FORMAT_MOD_INVALID : dfb->fb.modifier[plane];
}

static int
fb_sync(drmu_fb_t * const dfb, unsigned int flags)
{
    unsigned int i;
    for (i = 0; i != 4; ++i) {
        if (dfb->objects[i].fd != -1 && dfb->objects[i].map_ptr != NULL) {
            struct dma_buf_sync sync = {
                .flags = flags
            };
            while (ioctl(dfb->objects[i].fd, DMA_BUF_IOCTL_SYNC, &sync) == -1) {
                const int err = errno;
                if (errno == EINTR)
                    continue;
                drmu_debug(dfb->du, "%s: ioctl failed: flags=%#x\n", __func__, flags);
                return -err;
            }
        }
    }
    return 0;
}

int drmu_fb_write_start(drmu_fb_t * const dfb)
{
    return fb_sync(dfb, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
}

int drmu_fb_write_end(drmu_fb_t * const dfb)
{
    return fb_sync(dfb, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
}

int drmu_fb_read_start(drmu_fb_t * const dfb)
{
    return fb_sync(dfb, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
}

int drmu_fb_read_end(drmu_fb_t * const dfb)
{
    return fb_sync(dfb, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
}

// Writeback fence
// Must be unset before set again
// (This is as a handy hint that you must wait for the previous fence
// to go ready before you set a new one)
static int
atomic_fb_add_out_fence(drmu_atomic_t * const da, const uint32_t obj_id, const uint32_t prop_id, drmu_fb_t * const dfb)
{
    static const drmu_atomic_prop_fns_t fns = {
        .ref    = atomic_prop_fb_ref_cb,
        .unref  = atomic_prop_fb_unref_cb,
        .commit = drmu_prop_fn_null_commit,
    };

    if (!dfb)
        return -EINVAL;
    if (dfb->fence_fd != -1)
        return -EBUSY;

    return drmu_atomic_add_prop_generic(da, obj_id, prop_id, (uintptr_t)&dfb->fence_fd, &fns, dfb);
}

// For allocation purposes given fb_pixel bits how tall
// does the frame have to be to fit all planes if constant width
static unsigned int
fb_total_height(const drmu_fb_t * const dfb, const unsigned int h)
{
    unsigned int i;
    const drmu_fmt_info_t *const f = dfb->fmt_info;
    unsigned int t = 0;
    unsigned int h0 = h * drmu_fmt_info_wdiv(f, 0);
    const unsigned int c = drmu_fmt_info_plane_count(f);

    for (i = 0; i != c; ++i)
        t += h0 / (drmu_fmt_info_hdiv(f, i) * drmu_fmt_info_wdiv(f, i));

    return t;
}

drmu_fb_t *
drmu_fb_new_dumb_multi(drmu_env_t * const du, uint32_t w, uint32_t h,
                     const uint32_t format, const uint64_t mod, const bool multi)
{
    drmu_fb_t * const dfb = drmu_fb_int_alloc(du);
    uint32_t bpp;
    uint32_t w2;
    const uint32_t s30_cw = 128 / 4 * 3;
    unsigned int plane_count;
    const drmu_fmt_info_t * f;

    if (dfb == NULL) {
        drmu_err(du, "%s: Alloc failure", __func__);
        return NULL;
    }

    if (mod != DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(0))
        w2 = w;
    else if (format == DRM_FORMAT_NV12)
        w2 = (w + 127) & ~127;
    else if (format == DRM_FORMAT_P030)
        w2 = ((w + s30_cw - 1) / s30_cw) * s30_cw;
    else {
        // Unknown form of sand128
        drmu_err(du, "Sand modifier on unexpected format");
        goto fail;
    }

    drmu_fb_int_fmt_size_set(dfb, format, w2, h, drmu_rect_wh(w, h));

    if ((bpp = drmu_fb_pixel_bits(dfb)) == 0) {
        drmu_err(du, "%s: Unexpected format %#x", __func__, format);
        goto fail;
    }

    f = drmu_fb_format_info_get(dfb);
    plane_count = !multi ? 1 : drmu_fmt_info_plane_count(f);

    for (unsigned int i = 0; i != plane_count; ++i) {
        const unsigned int wdiv = drmu_fmt_info_wdiv(f, i);
        const unsigned int hdiv = drmu_fmt_info_hdiv(f, i);
        drmu_bo_t * bo;
        void * map_ptr;

        struct drm_mode_create_dumb dumb = {
            .bpp = bpp,
        };

        if (!multi) {
            dumb.height = fb_total_height(dfb, (h + 1) & ~1);
            dumb.width = ((w2 + 31) & ~31) / wdiv;
        }
        else {
            dumb.height = (h + hdiv - 1) / hdiv;
            dumb.width = (w + wdiv - 1) / wdiv;
        }

        if ((bo = drmu_bo_new_dumb(du, &dumb)) == NULL)
            goto fail;
        drmu_fb_int_bo_set(dfb, i, bo);

        if ((map_ptr = drmu_bo_mmap(bo, (size_t)dumb.size,
                               PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE)) == NULL)
            goto fail;
        drmu_fb_int_mmap_set(dfb, i, map_ptr, (size_t)dumb.size, dumb.pitch);

        if (multi) {
            drmu_fb_int_layer_mod_set(dfb, i, i, dumb.pitch, 0, mod);
        }
        else if (mod == DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(0)) {
            // Cope with the joy that is legacy sand
            const uint64_t sand1_mod = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(h * 3/2);
            drmu_fb_int_layer_mod_set(dfb, 0, 0, dumb.pitch, 0, sand1_mod);
            drmu_fb_int_layer_mod_set(dfb, 1, 0, dumb.pitch, h * 128, sand1_mod);
        }
        else {
            const uint32_t pitch0 = dumb.pitch * wdiv;
            const unsigned int c = drmu_fmt_info_plane_count(f);
            uint32_t t = 0;

            // This should be true for anything we've allocated
            for (unsigned int layer = 0; layer != c; ++layer) {
                const unsigned int wdiv2 = drmu_fmt_info_wdiv(f, layer);
                drmu_fb_int_layer_mod_set(dfb, layer, 0, pitch0 / wdiv2, t, mod);
                t += (pitch0 * h) / (drmu_fmt_info_hdiv(f, layer) * wdiv2);
            }
        }
    }

    if (drmu_fb_int_make(dfb))
        goto fail;

//    drmu_debug(du, "Create dumb %p %s %dx%d / %dx%d size: %zd", dfb,
//               drmu_log_fourcc(format), dfb->fb.width, dfb->fb.height, dfb->active.w, dfb->active.h, dfb->map_size);
    return dfb;

fail:
    drmu_fb_int_free(dfb);
    return NULL;
}

drmu_fb_t *
drmu_fb_new_dumb_mod(drmu_env_t * const du, uint32_t w, uint32_t h,
                     const uint32_t format, const uint64_t mod)
{
    return drmu_fb_new_dumb_multi(du, w, h, format, mod, false);
}

drmu_fb_t *
drmu_fb_new_dumb(drmu_env_t * const du, uint32_t w, uint32_t h, const uint32_t format)
{
    return drmu_fb_new_dumb_multi(du, w, h, format, DRM_FORMAT_MOD_LINEAR, false);
}

bool
drmu_fb_try_reuse(drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod)
{
    if (w > dfb->fb.width || h > dfb->fb.height || format != dfb->fb.pixel_format || mod != dfb->fb.modifier[0])
        return false;

    dfb->active = drmu_rect_wh(w, h);
    dfb->crop   = drmu_rect_shl16(dfb->active);
    return true;
}

drmu_fb_t *
drmu_fb_realloc_dumb_mod(drmu_env_t * const du, drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod)
{
    if (dfb == NULL)
        return drmu_fb_new_dumb_mod(du, w, h, format, mod);

    if (drmu_fb_try_reuse(dfb, w, h, format, mod))
        return dfb;

    drmu_fb_unref(&dfb);
    return drmu_fb_new_dumb_mod(du, w, h, format, mod);
}

drmu_fb_t *
drmu_fb_realloc_dumb(drmu_env_t * const du, drmu_fb_t * dfb, uint32_t w, uint32_t h, const uint32_t format)
{
    return drmu_fb_realloc_dumb_mod(du, dfb, w, h, format, DRM_FORMAT_MOD_LINEAR);
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
    static const drmu_atomic_prop_fns_t fns = {
        .ref    = atomic_prop_fb_ref,
        .unref  = atomic_prop_fb_unref,
        .commit = drmu_prop_fn_null_commit,
    };

    if (dfb == NULL)
        return drmu_atomic_add_prop_value(da, obj_id, prop_id, 0);

    rv = drmu_atomic_add_prop_generic(da, obj_id, prop_id, dfb->fb.fb_id, &fns, dfb);
    if (rv != 0)
        drmu_warn(drmu_atomic_env(da), "%s: Failed to add fb obj_id=%#x, prop_id=%#x: %s", __func__, obj_id, prop_id, strerror(-rv));

    return rv;
}

//----------------------------------------------------------------------------
//
// props fns (internal)

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

static const drmu_propinfo_t *
props_name_to_propinfo(const drmu_props_t * const props, const char * const name)
{
    unsigned int i = props->n / 2;
    unsigned int a = 0;
    unsigned int b = props->n;

    while (a < b) {
        const int r = strcmp(name, props->by_name[i]->prop.name);

        if (r == 0)
            return props->by_name[i];

        if (r < 0) {
            b = i;
            i = (i + a) / 2;
        } else {
            a = i + 1;
            i = (i + b) / 2;
        }
    }
    return NULL;
}

static uint32_t
props_name_to_id(const drmu_props_t * const props, const char * const name)
{
    return propinfo_prop_id(props_name_to_propinfo(props, name));
}

// Data must be freed later
static int
props_name_get_blob(const drmu_props_t * const props, const char * const name, void ** const ppdata, size_t * const plen)
{
    const drmu_propinfo_t * const pinfo = props_name_to_propinfo(props, name);

    *ppdata = 0;
    *plen = 0;

    if (!pinfo)
        return -ENOENT;
    if ((pinfo->prop.flags & DRM_MODE_PROP_BLOB) == 0)
        return -EINVAL;

    return blob_data_read(props->du, (uint32_t)pinfo->val, ppdata, plen);
}

#if TRACE_PROP_NEW
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

static int
props_get_properties(drmu_env_t * const du, const uint32_t objid, const uint32_t objtype,
                     uint32_t ** const ppPropids, uint64_t ** const ppValues)
{
    struct drm_mode_obj_get_properties obj_props = {
        .obj_id = objid,
        .obj_type = objtype,
    };
    uint64_t * values = NULL;
    uint32_t * propids = NULL;
    unsigned int n = 0;
    int rv;

    for (;;) {
        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &obj_props)) < 0) {
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
        if ((obj_props.prop_values_ptr = io_alloc(values, n)) == 0 ||
            (obj_props.props_ptr =       io_alloc(propids, n)) == 0) {
            drmu_err(du, "obj/value array alloc failed");
            rv = -ENOMEM;
            goto fail;
        }
    }

    *ppValues = values;
    *ppPropids = propids;
    return (int)n;

fail:
    free(values);
    free(propids);
    *ppPropids = NULL;
    *ppValues = NULL;
    return rv;
}

static drmu_props_t *
props_new(drmu_env_t * const du, const uint32_t objid, const uint32_t objtype)
{
    drmu_props_t * const props = calloc(1, sizeof(*props));
    int rv;
    uint64_t * values;
    uint32_t * propids;

    if (props == NULL) {
        drmu_err(du, "%s: Failed struct alloc", __func__);
        return NULL;
    }
    props->du = du;

    if ((rv = props_get_properties(du, objid, objtype, &propids, &values)) < 0)
        goto fail;

    props->n = rv;
    if ((props->info = calloc(props->n, sizeof(*props->info))) == NULL ||
        (props->by_name = malloc(props->n * sizeof(*props->by_name))) == NULL) {
        drmu_err(du, "info/name array alloc failed");
        goto fail;
    }

    for (unsigned int i = 0; i < props->n; ++i) {
        drmu_propinfo_t * const inf = props->info + i;

        props->by_name[i] = inf;
        if (propinfo_fill(du, inf, propids[i], values[i]) != 0)
            goto fail;
    }

    // Sort into name order for faster lookup
    qsort(props->by_name, props->n, sizeof(*props->by_name), props_qsort_by_name_cb);

    free(values);
    free(propids);
    return props;

fail:
    props_free(props);
    free(values);
    free(propids);
    return NULL;
}

int
drmu_atomic_obj_add_snapshot(drmu_atomic_t * const da, const uint32_t objid, const uint32_t objtype)
{
#if 0
    drmu_env_t * const du = drmu_atomic_env(da);
    drmu_props_t * props = NULL;
    unsigned int i;
    int rv;

    if (!du)
        return -EINVAL;

    if ((props = props_new(du, objid, objtype)) == NULL)
        return -ENOENT;

    for (i = 0; i != props->n; ++i) {
        if ((props->info[i].prop.flags & (DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_ATOMIC)) != 0 || props->info[i].prop.prop_id == 2)
            continue;
        if ((rv = drmu_atomic_add_prop_generic(da, objid, props->info[i].prop.prop_id, props->info[i].val, 0, 0, NULL)) != 0)
            goto fail;
    }
    rv = 0;

fail:
    props_free(props);
    return rv;
#else
    uint64_t * values;
    uint32_t * propids;
    int rv;
    unsigned int i, n;
    drmu_env_t * const du = drmu_atomic_env(da);

    if (!du)
        return -EINVAL;

    if ((rv = props_get_properties(du, objid, objtype, &propids, &values)) < 0)
        goto fail;
    n = rv;

    for (i = 0; i != n; ++i) {
        if ((rv = drmu_atomic_add_prop_value(da, objid, propids[i], values[i])) != 0)
            goto fail;
    }

fail:
    free(values);
    free(propids);
    return rv;
#endif
}

static int
drmu_atomic_props_add_save(drmu_atomic_t * const da, const uint32_t objid, const drmu_props_t * props)
{
    unsigned int i;
    int rv;
    drmu_env_t * const du = drmu_atomic_env(da);

    if (props == NULL)
        return 0;
    if (du == NULL)
        return -EINVAL;

    for (i = 0; i != props->n; ++i) {
        if ((props->info[i].prop.flags & DRM_MODE_PROP_IMMUTABLE) != 0)
            continue;

        // Blobs, if set, are prone to running out of refs and vanishing, so we
        // must copy. If we fail to copy the blob for any reason drop through
        // to the generic add and hope that that will do
        if ((props->info[i].prop.flags & DRM_MODE_PROP_BLOB) != 0 && props->info[i].val != 0) {
            drmu_blob_t * b = drmu_blob_copy_id(du, (uint32_t)props->info[i].val);
            if (b != NULL) {
                rv = drmu_atomic_add_prop_blob(da, objid, props->info[i].prop.prop_id, b);
                drmu_blob_unref(&b);
                if (rv == 0)
                    continue;
            }
        }

        // Generic value
        if ((rv = drmu_atomic_add_prop_value(da, objid, props->info[i].prop.prop_id, props->info[i].val)) != 0)
            return rv;
    }
    return 0;
}

//----------------------------------------------------------------------------
//
// Rotation util

static void
rotation_make_array(drmu_prop_bitmask_t * const pid, uint64_t values[8])
{
    uint64_t r0;

    memset(values, 0, sizeof(values[0]) * 8);
    if (pid == NULL)
        return;

    r0 = drmu_prop_bitmask_value(pid, "rotate-0");
    if (r0 != 0) {
        values[DRMU_ROTATION_0] = r0;
        // Flips MUST be combined with a rotate
        if ((values[DRMU_ROTATION_X_FLIP] = drmu_prop_bitmask_value(pid, "reflect-x")) != 0)
            values[DRMU_ROTATION_X_FLIP] |= r0;
        if ((values[DRMU_ROTATION_Y_FLIP] = drmu_prop_bitmask_value(pid, "reflect-y")) != 0)
            values[DRMU_ROTATION_Y_FLIP] |= r0;
        // Transpose counts as a Flip
        if ((values[DRMU_ROTATION_TRANSPOSE] = drmu_prop_bitmask_value(pid, "transpose")) != 0)
            values[DRMU_ROTATION_TRANSPOSE] |= r0;
    }
    values[DRMU_ROTATION_180] = drmu_prop_bitmask_value(pid, "rotate-180");
    if (!values[DRMU_ROTATION_180] && values[DRMU_ROTATION_X_FLIP] && values[DRMU_ROTATION_Y_FLIP])
        values[DRMU_ROTATION_180] = values[DRMU_ROTATION_X_FLIP] | values[DRMU_ROTATION_Y_FLIP];
    values[DRMU_ROTATION_90] = drmu_prop_bitmask_value(pid, "rotate-90");
    values[DRMU_ROTATION_270] = drmu_prop_bitmask_value(pid, "rotate-270");
}

//----------------------------------------------------------------------------
//
// CRTC fns

typedef struct drmu_crtc_s {
    struct drmu_env_s * du;
    int crtc_idx;

    atomic_int ref_count;
    bool saved;

    struct drm_mode_crtc crtc;

    struct {
        drmu_prop_range_t * active;
        uint32_t mode_id;
    } pid;

    drmu_blob_t * mode_id_blob;

} drmu_crtc_t;

static void
crtc_uninit(drmu_crtc_t * const dc)
{
    drmu_prop_range_delete(&dc->pid.active);
    drmu_blob_unref(&dc->mode_id_blob);
}

static void
crtc_free(drmu_crtc_t * const dc)
{
    crtc_uninit(dc);
    free(dc);
}


#if 0
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
#endif

void
drmu_crtc_delete(drmu_crtc_t ** ppdc)
{
    drmu_crtc_t * const dc = * ppdc;

    if (dc == NULL)
        return;
    *ppdc = NULL;

    crtc_free(dc);
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

static int
crtc_init(drmu_env_t * const du, drmu_crtc_t * const dc, const unsigned int idx, const uint32_t crtc_id)
{
    int rv;
    drmu_props_t * props;

    memset(dc, 0, sizeof(*dc));
    dc->du = du;
    dc->crtc_idx = idx;
    dc->crtc.crtc_id = crtc_id;

    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETCRTC, &dc->crtc)) != 0) {
        drmu_err(du, "Failed to get crtc id %d: %s", crtc_id, strerror(-rv));
        return rv;
    }

    props = props_new(du, dc->crtc.crtc_id, DRM_MODE_OBJECT_CRTC);

    if (props != NULL) {
#if TRACE_PROP_NEW
        drmu_info(du, "CRTC id=%#x, idx=%d:", dc->crtc.crtc_id, dc->crtc_idx);
        props_dump(props);
#endif
        dc->pid.mode_id = props_name_to_id(props, "MODE_ID");
        dc->pid.active = drmu_prop_range_new(du, props_name_to_id(props, "ACTIVE"));

        props_free(props);
    }

    return 0;
}

static drmu_ufrac_t
modeinfo_par(const struct drm_mode_modeinfo * const mode)
{
    switch (mode->flags & DRM_MODE_FLAG_PIC_AR_MASK) {
        case DRM_MODE_FLAG_PIC_AR_4_3:
            return (drmu_ufrac_t){4,3};
        case DRM_MODE_FLAG_PIC_AR_16_9:
            return (drmu_ufrac_t){16,9};
        case DRM_MODE_FLAG_PIC_AR_64_27:
            return (drmu_ufrac_t){64,27};
        case DRM_MODE_FLAG_PIC_AR_256_135:
            return (drmu_ufrac_t){256,135};
        default:
        case DRM_MODE_FLAG_PIC_AR_NONE:
            break;
    }
    return (drmu_ufrac_t){0,0};
}

static drmu_mode_simple_params_t
modeinfo_simple_params(const struct drm_mode_modeinfo * const mode)
{
    if (!mode)
        return (drmu_mode_simple_params_t){ 0 };
    else {
        drmu_mode_simple_params_t rv = {
            .width = mode->hdisplay,
            .height = mode->vdisplay,
            .hz_x_1000 = (uint32_t)(((uint64_t)mode->clock * 1000000) / (mode->htotal * mode->vtotal)),
            .par = modeinfo_par(mode),
            .sar = {1, 1},
            .type = mode->type,
            .flags = mode->flags,
        };

        if (rv.par.den != 0)
            rv.sar = drmu_ufrac_reduce((drmu_ufrac_t) {rv.par.num * rv.height, rv.par.den * rv.width});

        return rv;
    }
}

drmu_crtc_t *
drmu_env_crtc_find_id(drmu_env_t * const du, const uint32_t crtc_id)
{
    unsigned int i;
    drmu_crtc_t * dc;

    for (i = 0; (dc = drmu_env_crtc_find_n(du, i)) != NULL; ++i) {
        if (dc->crtc.crtc_id == crtc_id)
            return dc;
    }
    return NULL;
}

const struct drm_mode_modeinfo *
drmu_crtc_modeinfo(const drmu_crtc_t * const dc)
{
    if (!dc || !dc->crtc.mode_valid)
        return NULL;
    return &dc->crtc.mode;
}

drmu_mode_simple_params_t
drmu_crtc_mode_simple_params(const drmu_crtc_t * const dc)
{
    return modeinfo_simple_params(drmu_crtc_modeinfo(dc));
}

int
drmu_atomic_crtc_add_modeinfo(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, const struct drm_mode_modeinfo * const modeinfo)
{
    drmu_env_t * const du = drmu_atomic_env(da);
    int rv;

    if (!modeinfo || dc->pid.mode_id == 0)
        return 0;

    if ((rv = drmu_blob_update(du, &dc->mode_id_blob, modeinfo, sizeof(*modeinfo))) != 0)
        return rv;

    return drmu_atomic_add_prop_blob(da, dc->crtc.crtc_id, dc->pid.mode_id, dc->mode_id_blob);
}

int
drmu_atomic_crtc_add_active(struct drmu_atomic_s * const da, drmu_crtc_t * const dc, unsigned int val)
{
    return drmu_atomic_add_prop_range(da, dc->crtc.crtc_id, dc->pid.active, val);
}

// Use the same claim logic as we do for planes
// As it stands we don't do anything much on final unref so the logic
// isn't really needed but it doesn't cost us much so do this way against
// future need

bool
drmu_crtc_is_claimed(const drmu_crtc_t * const dc)
{
    return atomic_load(&dc->ref_count) != 0;
}

void
drmu_crtc_unref(drmu_crtc_t ** const ppdc)
{
    drmu_crtc_t * const dc = *ppdc;

    if (dc == NULL)
        return;
    *ppdc = NULL;

    if (atomic_fetch_sub(&dc->ref_count, 1) != 2)
        return;
    atomic_store(&dc->ref_count, 0);
}

drmu_crtc_t *
drmu_crtc_ref(drmu_crtc_t * const dc)
{
    if (!dc)
        return NULL;
    atomic_fetch_add(&dc->ref_count, 1);
    return dc;
}

static int
crtc_state_save(drmu_env_t * const du, drmu_crtc_t * const dc)
{
    int rv = 0;
    // 1st time through save state
    if (!dc->saved &&
        (rv = env_object_state_save(du, dc->crtc.crtc_id, DRM_MODE_OBJECT_CRTC)) == 0)
        dc->saved = true;
    return rv;
}

// A Conn should be claimed before any op that might change its state
int
drmu_crtc_claim_ref(drmu_crtc_t * const dc)
{
    drmu_env_t * const du = dc->du;
    int ref0 = 0;
    if (!atomic_compare_exchange_strong(&dc->ref_count, &ref0, 2))
        return -EBUSY;

    // 1st time through save state
    crtc_state_save(du, dc);

    return 0;
}

//----------------------------------------------------------------------------
//
// CONN functions

static const char * conn_type_names[32] = {
    [DRM_MODE_CONNECTOR_Unknown]     = "Unknown",
    [DRM_MODE_CONNECTOR_VGA]         = "VGA",
    [DRM_MODE_CONNECTOR_DVII]        = "DVI-I",
    [DRM_MODE_CONNECTOR_DVID]        = "DVI-D",
    [DRM_MODE_CONNECTOR_DVIA]        = "DVI-A",
    [DRM_MODE_CONNECTOR_Composite]   = "Composite",
    [DRM_MODE_CONNECTOR_SVIDEO]      = "SVIDEO",
    [DRM_MODE_CONNECTOR_LVDS]        = "LVDS",
    [DRM_MODE_CONNECTOR_Component]   = "Component",
    [DRM_MODE_CONNECTOR_9PinDIN]     = "9PinDIN",
    [DRM_MODE_CONNECTOR_DisplayPort] = "DisplayPort",
    [DRM_MODE_CONNECTOR_HDMIA]       = "HDMI-A",
    [DRM_MODE_CONNECTOR_HDMIB]       = "HDMI-B",
    [DRM_MODE_CONNECTOR_TV]          = "TV",
    [DRM_MODE_CONNECTOR_eDP]         = "eDP",
    [DRM_MODE_CONNECTOR_VIRTUAL]     = "VIRTUAL",
    [DRM_MODE_CONNECTOR_DSI]         = "DSI",
    [DRM_MODE_CONNECTOR_DPI]         = "DPI",
    [DRM_MODE_CONNECTOR_WRITEBACK]   = "WRITEBACK",
    [DRM_MODE_CONNECTOR_SPI]         = "SPI",
#ifdef DRM_MODE_CONNECTOR_USB
    [DRM_MODE_CONNECTOR_USB]         = "USB",
#endif
};

struct drmu_conn_s {
    drmu_env_t * du;
    unsigned int conn_idx;

    atomic_int ref_count;
    bool saved;

    struct drm_mode_get_connector conn;
    bool probed;
    unsigned int modes_size;
    unsigned int enc_ids_size;
    struct drm_mode_modeinfo * modes;
    uint32_t * enc_ids;

    uint32_t avail_crtc_mask;

    struct {
        drmu_prop_object_t * crtc_id;
        drmu_prop_range_t * max_bpc;
        drmu_prop_enum_t * colorspace;
        drmu_prop_enum_t * broadcast_rgb;
        drmu_prop_bitmask_t * rotation;
        uint32_t hdr_output_metadata;
        uint32_t writeback_out_fence_ptr;
        uint32_t writeback_fb_id;
        uint32_t writeback_pixel_formats;
    } pid;

    uint64_t rot_vals[8];
    drmu_blob_t * hdr_metadata_blob;

    char name[32];
};


int
drmu_atomic_conn_add_hdr_metadata(drmu_atomic_t * const da, drmu_conn_t * const dn, const struct hdr_output_metadata * const m)
{
    drmu_env_t * const du = drmu_atomic_env(da);
    int rv;

    if (!du || !dn)  // du will be null if da is null
        return -ENOENT;

    if (dn->pid.hdr_output_metadata == 0)
        return 0;

    if ((rv = drmu_blob_update(du, &dn->hdr_metadata_blob, m, sizeof(*m))) != 0)
        return rv;

    rv = drmu_atomic_add_prop_blob(da, dn->conn.connector_id, dn->pid.hdr_output_metadata, dn->hdr_metadata_blob);
    if (rv != 0)
        drmu_err(du, "Set property fail: %s", strerror(errno));

    return rv;
}

bool
drmu_conn_has_hi_bpc(const drmu_conn_t * const dn)
{
    return drmu_prop_range_max(dn->pid.max_bpc) > 8;
}

int
drmu_atomic_conn_add_hi_bpc(drmu_atomic_t * const da, drmu_conn_t * const dn, bool hi_bpc)
{
    return !hi_bpc && dn->pid.max_bpc == NULL ? 0 :
        drmu_atomic_add_prop_range(da, dn->conn.connector_id, dn->pid.max_bpc, !hi_bpc ? 8 :
                                      drmu_prop_range_max(dn->pid.max_bpc));
}

int
drmu_atomic_conn_add_colorspace(drmu_atomic_t * const da, drmu_conn_t * const dn, const drmu_colorspace_t colorspace)
{
    if (!dn->pid.colorspace)
        return 0;

    return drmu_atomic_add_prop_enum(da, dn->conn.connector_id, dn->pid.colorspace, colorspace);
}

int
drmu_atomic_conn_add_broadcast_rgb(drmu_atomic_t * const da, drmu_conn_t * const dn, const drmu_broadcast_rgb_t bcrgb)
{
    if (!dn->pid.broadcast_rgb)
        return 0;

    return drmu_atomic_add_prop_enum(da, dn->conn.connector_id, dn->pid.broadcast_rgb, bcrgb);
}

int
drmu_atomic_conn_add_crtc(drmu_atomic_t * const da, drmu_conn_t * const dn, drmu_crtc_t * const dc)
{
    return drmu_atomic_add_prop_object(da, dn->pid.crtc_id, drmu_crtc_id(dc));
}

bool
drmu_conn_has_rotation(drmu_conn_t * const dn, const unsigned int rotation)
{
    return rotation < 8 && dn != NULL &&
        (dn->rot_vals[rotation] != 0 ||
            (!dn->pid.rotation && rotation == DRMU_ROTATION_0));
}

int
drmu_atomic_conn_add_rotation(drmu_atomic_t * const da, drmu_conn_t * const dn, const unsigned int rotation)
{
    return !drmu_conn_has_rotation(dn, rotation) ? -EINVAL :
        !dn->pid.rotation ? 0 : // Must be rotation_0 here
            drmu_atomic_add_prop_bitmask(da, dn->conn.connector_id, dn->pid.rotation, dn->rot_vals[rotation]);
}

int
drmu_atomic_conn_add_writeback_fb(drmu_atomic_t * const da_out, drmu_conn_t * const dn,
                                  drmu_fb_t * const dfb)
{
    // Add both or neither, so build a temp atomic to store the intermediate result
    drmu_atomic_t * da = drmu_atomic_new(drmu_atomic_env(da_out));
    int rv;

    if (!da)
        return -ENOMEM;

    if ((rv = atomic_fb_add_out_fence(da, dn->conn.connector_id, dn->pid.writeback_out_fence_ptr, dfb)) != 0)
        goto fail;

    if ((rv = drmu_atomic_add_prop_fb(da, dn->conn.connector_id, dn->pid.writeback_fb_id, dfb)) != 0)
        goto fail;

    return drmu_atomic_merge(da_out, &da);

fail:
    drmu_atomic_unref(&da);
    return rv;
}

const struct drm_mode_modeinfo *
drmu_conn_modeinfo(const drmu_conn_t * const dn, const int mode_id)
{
    return !dn || mode_id < 0 || (unsigned int)mode_id >= dn->conn.count_modes ? NULL :
        dn->modes + mode_id;
}

drmu_mode_simple_params_t
drmu_conn_mode_simple_params(const drmu_conn_t * const dn, const int mode_id)
{
    return modeinfo_simple_params(drmu_conn_modeinfo(dn, mode_id));
}

bool
drmu_conn_is_output(const drmu_conn_t * const dn)
{
    return dn->conn.connector_type != DRM_MODE_CONNECTOR_WRITEBACK;
}

bool
drmu_conn_is_writeback(const drmu_conn_t * const dn)
{
    return dn->conn.connector_type == DRM_MODE_CONNECTOR_WRITEBACK;
}

const char *
drmu_conn_name(const drmu_conn_t * const dn)
{
    return dn->name;
}

uint32_t
drmu_conn_crtc_id_get(const drmu_conn_t * const dn)
{
    return drmu_prop_object_value(dn->pid.crtc_id);
}

uint32_t
drmu_conn_possible_crtcs(const drmu_conn_t * const dn)
{
    return dn->avail_crtc_mask;
}

unsigned int
drmu_conn_idx_get(const drmu_conn_t * const dn)
{
    return dn->conn_idx;
}

static void
conn_uninit(drmu_conn_t * const dn)
{
    drmu_prop_object_unref(&dn->pid.crtc_id);
    drmu_prop_range_delete(&dn->pid.max_bpc);
    drmu_prop_enum_delete(&dn->pid.colorspace);
    drmu_prop_enum_delete(&dn->pid.broadcast_rgb);

    drmu_blob_unref(&dn->hdr_metadata_blob);

    free(dn->modes);
    free(dn->enc_ids);
    dn->modes = NULL;
    dn->enc_ids = NULL;
    dn->modes_size = 0;
    dn->enc_ids_size = 0;
}

// Assumes zeroed before entry
static int
conn_init(drmu_env_t * const du, drmu_conn_t * const dn, unsigned int conn_idx, const uint32_t conn_id)
{
    int rv;
    drmu_props_t * props;
    uint32_t modes_req = 0;
    uint32_t encs_req = 0;

    dn->du = du;
    dn->conn_idx = conn_idx;
    // * As count_modes == 0 this probes - do we really want this?

    do {
        memset(&dn->conn, 0, sizeof(dn->conn));
        dn->conn.connector_id = conn_id;

        if (modes_req > dn->modes_size) {
            free(dn->modes);
            if (io_alloc(dn->modes, modes_req) == 0) {
                drmu_err(du, "Failed to alloc modes array");
                goto fail;
            }
            dn->modes_size = modes_req;
        }
        dn->conn.modes_ptr = (uintptr_t)dn->modes;
        dn->conn.count_modes = modes_req;

        if (encs_req > dn->enc_ids_size) {
            free(dn->enc_ids);
            if (io_alloc(dn->enc_ids, encs_req) == 0) {
                drmu_err(du, "Failed to alloc encs array");
                goto fail;
            }
            dn->enc_ids_size = encs_req;
        }
        dn->conn.encoders_ptr = (uintptr_t)dn->enc_ids;
        dn->conn.count_encoders = encs_req;

        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETCONNECTOR, &dn->conn)) != 0) {
            drmu_err(du, "Get connector id %d failed: %s", dn->conn.connector_id, strerror(-rv));
            goto fail;
        }
        modes_req = dn->conn.count_modes;
        encs_req = dn->conn.count_encoders;

    } while (dn->modes_size < modes_req || dn->enc_ids_size < encs_req);

    dn->probed = true;

    if (dn->conn.connector_type >= sizeof(conn_type_names) / sizeof(conn_type_names[0]))
        snprintf(dn->name, sizeof(dn->name)-1, "CT%"PRIu32"-%"PRIu32,
                 dn->conn.connector_type, dn->conn.connector_type_id);
    else
        snprintf(dn->name, sizeof(dn->name)-1, "%s-%"PRIu32,
                 conn_type_names[dn->conn.connector_type], dn->conn.connector_type_id);

    props = props_new(du, dn->conn.connector_id, DRM_MODE_OBJECT_CONNECTOR);

    // Spin over encoders to create a crtc mask
    dn->avail_crtc_mask = 0;
    for (unsigned int i = 0; i != dn->conn.count_encoders; ++i) {
        struct drm_mode_get_encoder enc = {.encoder_id = dn->enc_ids[i]};
        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETENCODER, &enc)) != 0) {
            drmu_warn(du, "Failed to get encoder: id: %#x", enc.encoder_id);
            continue;
        }
        dn->avail_crtc_mask |= enc.possible_crtcs;
    }

    if (props != NULL) {
#if TRACE_PROP_NEW
        drmu_info(du, "Connector id=%d, type=%d.%d (%s), crtc_mask=%#x:",
                  dn->conn.connector_id, dn->conn.connector_type, dn->conn.connector_type_id, drmu_conn_name(dn),
                  dn->avail_crtc_mask);
        props_dump(props);
#endif
        dn->pid.crtc_id             = drmu_prop_object_new_propinfo(du, dn->conn.connector_id, props_name_to_propinfo(props, "CRTC_ID"));
        dn->pid.max_bpc             = drmu_prop_range_new(du, props_name_to_id(props, "max bpc"));
        dn->pid.colorspace          = drmu_prop_enum_new(du, props_name_to_id(props, "Colorspace"));
        dn->pid.broadcast_rgb       = drmu_prop_enum_new(du, props_name_to_id(props, "Broadcast RGB"));
        dn->pid.rotation            = drmu_prop_bitmask_new(du, props_name_to_id(props, "rotation"));
        dn->pid.hdr_output_metadata = props_name_to_id(props, "HDR_OUTPUT_METADATA");
        dn->pid.writeback_fb_id     = props_name_to_id(props, "WRITEBACK_FB_ID");
        dn->pid.writeback_out_fence_ptr = props_name_to_id(props, "WRITEBACK_OUT_FENCE_PTR");
        dn->pid.writeback_pixel_formats = props_name_to_id(props, "WRITEBACK_PIXEL_FORMATS");  // Blob of fourccs (no modifier info)
        props_free(props);

        rotation_make_array(dn->pid.rotation, dn->rot_vals);
    }

    return 0;

fail:
    conn_uninit(dn);
    return rv;
}

// Use the same claim logic as we do for planes
// As it stands we don't do anything much on final unref so the logic
// isn't really needed but it doesn't cost us much so do this way against
// future need

bool
drmu_conn_is_claimed(const drmu_conn_t * const dn)
{
    return atomic_load(&dn->ref_count) != 0;
}

void
drmu_conn_unref(drmu_conn_t ** const ppdn)
{
    drmu_conn_t * const dn = *ppdn;

    if (dn == NULL)
        return;
    *ppdn = NULL;

    if (atomic_fetch_sub(&dn->ref_count, 1) != 2)
        return;
    atomic_store(&dn->ref_count, 0);
}

drmu_conn_t *
drmu_conn_ref(drmu_conn_t * const dn)
{
    if (!dn)
        return NULL;
    atomic_fetch_add(&dn->ref_count, 1);
    return dn;
}

static int
conn_state_save(drmu_env_t * const du, drmu_conn_t * const dn)
{
    int rv = 0;
    // 1st time through save state
    if (!dn->saved &&
        (rv = env_object_state_save(du, dn->conn.connector_id, DRM_MODE_OBJECT_CONNECTOR)) == 0)
        dn->saved = true;
    return rv;
}

// A Conn should be claimed before any op that might change its state
int
drmu_conn_claim_ref(drmu_conn_t * const dn)
{
    drmu_env_t * const du = dn->du;
    int ref0 = 0;
    if (!atomic_compare_exchange_strong(&dn->ref_count, &ref0, 2))
        return -EBUSY;

    // 1st time through save state
    conn_state_save(du, dn);

    return 0;
}

//----------------------------------------------------------------------------
//
// Plane fns

typedef struct drmu_plane_s {
    struct drmu_env_s * du;

    // Unlike most ref counts in drmu this is 0 for unrefed, 2 for single ref
    // and 1 for whilst unref cleanup is in progress. Guards dc
    atomic_int ref_count;
    struct drmu_crtc_s * dc;    // NULL if not in use
    bool saved;

    int plane_type;
    struct drm_mode_get_plane plane;

    void * formats_in;
    size_t formats_in_len;
    const struct drm_format_modifier_blob * fmts_hdr;

    struct {
        uint32_t crtc_id;
        uint32_t fb_id;
        drmu_prop_range_t * crtc_h;
        drmu_prop_range_t * crtc_w;
        uint32_t crtc_x;
        uint32_t crtc_y;
        drmu_prop_range_t * src_h;
        drmu_prop_range_t * src_w;
        uint32_t src_x;
        uint32_t src_y;
        drmu_prop_range_t * alpha;
        drmu_prop_enum_t * color_encoding;
        drmu_prop_enum_t * color_range;
        drmu_prop_enum_t * pixel_blend_mode;
        drmu_prop_bitmask_t * rotation;
        drmu_prop_range_t * chroma_siting_h;
        drmu_prop_range_t * chroma_siting_v;
        drmu_prop_range_t * zpos;
    } pid;
    uint64_t rot_vals[8];

} drmu_plane_t;

static int
plane_set_atomic(drmu_atomic_t * const da,
                 drmu_plane_t * const dp,
                 drmu_fb_t * const dfb,
                int32_t crtc_x, int32_t crtc_y,
                uint32_t crtc_w, uint32_t crtc_h,
                uint32_t src_x, uint32_t src_y,
                uint32_t src_w, uint32_t src_h)
{
    const uint32_t plid = dp->plane.plane_id;
    drmu_atomic_add_prop_value(da, plid, dp->pid.crtc_id, dfb == NULL ? 0 : drmu_crtc_id(dp->dc));
    drmu_atomic_add_prop_fb(da, plid, dp->pid.fb_id, dfb);
    drmu_atomic_add_prop_value(da, plid, dp->pid.crtc_x, crtc_x);
    drmu_atomic_add_prop_value(da, plid, dp->pid.crtc_y, crtc_y);
    drmu_atomic_add_prop_range(da, plid, dp->pid.crtc_w, crtc_w);
    drmu_atomic_add_prop_range(da, plid, dp->pid.crtc_h, crtc_h);
    drmu_atomic_add_prop_value(da, plid, dp->pid.src_x,  src_x);
    drmu_atomic_add_prop_value(da, plid, dp->pid.src_y,  src_y);
    drmu_atomic_add_prop_range(da, plid, dp->pid.src_w,  src_w);
    drmu_atomic_add_prop_range(da, plid, dp->pid.src_h,  src_h);
    return 0;
}

int
drmu_atomic_plane_add_alpha(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const int alpha)
{
    if (alpha == DRMU_PLANE_ALPHA_UNSET)
        return 0;
    return drmu_atomic_add_prop_range(da, dp->plane.plane_id, dp->pid.alpha, alpha);
}

int
drmu_atomic_plane_add_zpos(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const int zpos)
{
    return drmu_atomic_add_prop_range(da, dp->plane.plane_id, dp->pid.zpos, zpos);
}

int
drmu_atomic_plane_add_rotation(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const int rot)
{
    if (!dp->pid.rotation)
        return rot == DRMU_ROTATION_0 ? 0 : -EINVAL;
    if (rot < 0 || rot >= 8 || !dp->rot_vals[rot])
        return -EINVAL;
    return drmu_atomic_add_prop_bitmask(da, dp->plane.plane_id, dp->pid.rotation, dp->rot_vals[rot]);
}

int
drmu_atomic_plane_add_chroma_siting(struct drmu_atomic_s * const da, const drmu_plane_t * const dp, const drmu_chroma_siting_t siting)
{
    int rv = 0;

    if (!dp->pid.chroma_siting_h || !dp->pid.chroma_siting_v)
        return -ENOENT;

    if (!drmu_chroma_siting_eq(siting, DRMU_CHROMA_SITING_UNSPECIFIED)) {
        const uint32_t plid = dp->plane.plane_id;
        rv = drmu_atomic_add_prop_range(da, plid, dp->pid.chroma_siting_h, siting.x);
        rv = rvup(rv, drmu_atomic_add_prop_range(da, plid, dp->pid.chroma_siting_v, siting.y));
    }
    return rv;
}

int
drmu_atomic_plane_clear_add(drmu_atomic_t * const da, drmu_plane_t * const dp)
{
    return plane_set_atomic(da, dp, NULL,
                            0, 0, 0, 0,
                            0, 0, 0, 0);
}

int
drmu_atomic_plane_add_fb(drmu_atomic_t * const da, drmu_plane_t * const dp,
    drmu_fb_t * const dfb, const drmu_rect_t pos)
{
    int rv;
    const uint32_t plid = dp->plane.plane_id;

    if (dfb == NULL)
        return drmu_atomic_plane_clear_add(da, dp);

    if ((rv = plane_set_atomic(da, dp, dfb,
                              pos.x, pos.y,
                              pos.w, pos.h,
                              dfb->crop.x + (dfb->active.x << 16), dfb->crop.y + (dfb->active.y << 16),
                              dfb->crop.w, dfb->crop.h)) != 0)
        return rv;

    drmu_atomic_add_prop_enum(da, plid, dp->pid.pixel_blend_mode, dfb->pixel_blend_mode);
    drmu_atomic_add_prop_enum(da, plid, dp->pid.color_encoding,   dfb->color_encoding);
    drmu_atomic_add_prop_enum(da, plid, dp->pid.color_range,      dfb->color_range);
    drmu_atomic_plane_add_chroma_siting(da, dp, dfb->chroma_siting);
    return 0;
}

uint32_t
drmu_plane_id(const drmu_plane_t * const dp)
{
    return dp->plane.plane_id;
}

unsigned int
drmu_plane_type(const drmu_plane_t * const dp)
{
    return dp->plane_type;
}

const uint32_t *
drmu_plane_formats(const drmu_plane_t * const dp, unsigned int * const pCount)
{
    *pCount = dp->fmts_hdr->count_formats;
    return (const uint32_t *)((const uint8_t *)dp->formats_in + dp->fmts_hdr->formats_offset);
}

bool
drmu_plane_format_check(const drmu_plane_t * const dp, const uint32_t format, const uint64_t modifier)
{
    const struct drm_format_modifier * const mods = (const struct drm_format_modifier *)((const uint8_t *)dp->formats_in + dp->fmts_hdr->modifiers_offset);
    const uint32_t * const fmts = (const uint32_t *)((const uint8_t *)dp->formats_in + dp->fmts_hdr->formats_offset);
    uint64_t modbase = modifier;
    unsigned int i;

    if (!format)
        return false;

    // If broadcom then remove parameters before checking
    if ((modbase >> 56) == DRM_FORMAT_MOD_VENDOR_BROADCOM)
        modbase = fourcc_mod_broadcom_mod(modbase);

    // * Simplistic lookup; Could be made much faster

    for (i = 0; i != dp->fmts_hdr->count_modifiers; ++i) {
        const struct drm_format_modifier * const mod = mods + i;
        uint64_t fbits;
        unsigned int j;

        if (mod->modifier != modbase)
            continue;

        for (fbits = mod->formats, j = mod->offset; fbits; fbits >>= 1, ++j) {
            if ((fbits & 1) != 0 && fmts[j] == format)
                return true;
        }
    }
    return false;
}

bool
drmu_plane_is_claimed(drmu_plane_t * const dp)
{
    return atomic_load(&dp->ref_count) != 0;
}

void
drmu_plane_unref(drmu_plane_t ** const ppdp)
{
    drmu_plane_t * const dp = *ppdp;

    if (dp == NULL)
        return;
    *ppdp = NULL;

    if (atomic_fetch_sub(&dp->ref_count, 1) != 2)
        return;
    dp->dc = NULL;
    atomic_store(&dp->ref_count, 0);
}

drmu_plane_t *
drmu_plane_ref(drmu_plane_t * const dp)
{
    if (dp)
        atomic_fetch_add(&dp->ref_count, 1);
    return dp;
}

static int
plane_state_save(drmu_env_t * const du, drmu_plane_t * const dp)
{
    int rv = 0;

    // 1st time through save state
    if (!dp->saved &&
        (rv = env_object_state_save(du, drmu_plane_id(dp), DRM_MODE_OBJECT_PLANE)) == 0)
        dp->saved = true;
    return rv;
}

// Associate a plane with a crtc and ref it
// Returns -EBUSY if plane already associated
int
drmu_plane_ref_crtc(drmu_plane_t * const dp, drmu_crtc_t * const dc)
{
    drmu_env_t * const du = dp->du;

    int ref0 = 0;
    if (!atomic_compare_exchange_strong(&dp->ref_count, &ref0, 2))
        return -EBUSY;
    dp->dc = dc;

    // 1st time through save state if required - ignore fail
    plane_state_save(du, dp);

    return 0;
}

drmu_plane_t *
drmu_plane_new_find_ref(drmu_crtc_t * const dc, const drmu_plane_new_find_ok_fn cb, void * const v)
{
    uint32_t i;
    drmu_env_t * const du = drmu_crtc_env(dc);
    drmu_plane_t * dp = NULL;
    drmu_plane_t * dp_t;
    const uint32_t crtc_mask = (uint32_t)1 << drmu_crtc_idx(dc);

    for (i = 0; (dp_t = drmu_env_plane_find_n(du, i)) != NULL; ++i) {
        // Is unused?
        // Availible for this crtc?
        if (dp_t->dc != NULL ||
            (dp_t->plane.possible_crtcs & crtc_mask) == 0)
            continue;

        if (cb(dp_t, v) && drmu_plane_ref_crtc(dp_t, dc) == 0) {
            dp = dp_t;
            break;
        }
    }
    return dp;
}

static bool plane_find_type_cb(const drmu_plane_t * dp, void * v)
{
    const unsigned int * const pReq = v;
    return (*pReq & drmu_plane_type(dp)) != 0;
}

drmu_plane_t *
drmu_plane_new_find_ref_type(drmu_crtc_t * const dc, const unsigned int req_type)
{
    drmu_env_t * const du = drmu_crtc_env(dc);
    drmu_plane_t * const dp = drmu_plane_new_find_ref(dc, plane_find_type_cb, (void*)&req_type);
    if (dp == NULL) {
        drmu_err(du, "%s: No plane found for types %#x", __func__, req_type);
        return NULL;
    }
    return dp;
}

static void
plane_uninit(drmu_plane_t * const dp)
{
    drmu_prop_range_delete(&dp->pid.crtc_h);
    drmu_prop_range_delete(&dp->pid.crtc_w);
    drmu_prop_range_delete(&dp->pid.src_h);
    drmu_prop_range_delete(&dp->pid.src_w);
    drmu_prop_range_delete(&dp->pid.alpha);
    drmu_prop_range_delete(&dp->pid.chroma_siting_h);
    drmu_prop_range_delete(&dp->pid.chroma_siting_v);
    drmu_prop_enum_delete(&dp->pid.color_encoding);
    drmu_prop_enum_delete(&dp->pid.color_range);
    drmu_prop_enum_delete(&dp->pid.pixel_blend_mode);
    drmu_prop_enum_delete(&dp->pid.rotation);
    drmu_prop_range_delete(&dp->pid.zpos);
    free(dp->formats_in);
    dp->formats_in = NULL;
}


static int
plane_init(drmu_env_t * const du, drmu_plane_t * const dp, const uint32_t plane_id)
{
    drmu_props_t *props;
    int rv;

    memset(dp, 0, sizeof(*dp));
    dp->du = du;

    dp->plane.plane_id = plane_id;
    if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETPLANE, &dp->plane)) != 0) {
        drmu_err(du, "%s: drmModeGetPlane failed: %s", __func__, strerror(-rv));
        return rv;
    }

    if ((props = props_new(du, dp->plane.plane_id, DRM_MODE_OBJECT_PLANE)) == NULL)
        return -EINVAL;

#if TRACE_PROP_NEW
    drmu_info(du, "Plane id %d:", plane_id);
    props_dump(props);
#endif

    if ((dp->pid.crtc_id = props_name_to_id(props, "CRTC_ID")) == 0 ||
        (dp->pid.fb_id  = props_name_to_id(props, "FB_ID")) == 0 ||
        (dp->pid.crtc_h = drmu_prop_range_new(du, props_name_to_id(props, "CRTC_H"))) == NULL ||
        (dp->pid.crtc_w = drmu_prop_range_new(du, props_name_to_id(props, "CRTC_W"))) == NULL ||
        (dp->pid.crtc_x = props_name_to_id(props, "CRTC_X")) == 0 ||
        (dp->pid.crtc_y = props_name_to_id(props, "CRTC_Y")) == 0 ||
        (dp->pid.src_h  = drmu_prop_range_new(du, props_name_to_id(props, "SRC_H"))) == NULL ||
        (dp->pid.src_w  = drmu_prop_range_new(du, props_name_to_id(props, "SRC_W"))) == NULL ||
        (dp->pid.src_x  = props_name_to_id(props, "SRC_X")) == 0 ||
        (dp->pid.src_y  = props_name_to_id(props, "SRC_Y")) == 0 ||
        props_name_get_blob(props, "IN_FORMATS", &dp->formats_in, &dp->formats_in_len) != 0)
    {
        drmu_err(du, "%s: failed to find required id", __func__);
        props_free(props);
        return -EINVAL;
    }
    dp->fmts_hdr = dp->formats_in;

    dp->pid.alpha            = drmu_prop_range_new(du, props_name_to_id(props, "alpha"));
    dp->pid.color_encoding   = drmu_prop_enum_new(du, props_name_to_id(props, "COLOR_ENCODING"));
    dp->pid.color_range      = drmu_prop_enum_new(du, props_name_to_id(props, "COLOR_RANGE"));
    dp->pid.pixel_blend_mode = drmu_prop_enum_new(du, props_name_to_id(props, "pixel blend mode"));
    dp->pid.rotation         = drmu_prop_enum_new(du, props_name_to_id(props, "rotation"));
    dp->pid.chroma_siting_h  = drmu_prop_range_new(du, props_name_to_id(props, "CHROMA_SITING_H"));
    dp->pid.chroma_siting_v  = drmu_prop_range_new(du, props_name_to_id(props, "CHROMA_SITING_V"));
    dp->pid.zpos             = drmu_prop_range_new(du, props_name_to_id(props, "zpos"));

    rotation_make_array(dp->pid.rotation, dp->rot_vals);

    {
        const drmu_propinfo_t * const pinfo = props_name_to_propinfo(props, "type");
        drmu_prop_enum_t * etype = drmu_prop_enum_new(du, props_name_to_id(props, "type"));
        const uint64_t * p;

        if ((p = drmu_prop_enum_value(etype, "Primary")) && *p == pinfo->val)
            dp->plane_type = DRMU_PLANE_TYPE_PRIMARY;
        else if ((p = drmu_prop_enum_value(etype, "Cursor")) && *p == pinfo->val)
            dp->plane_type = DRMU_PLANE_TYPE_CURSOR;
        else if ((p = drmu_prop_enum_value(etype, "Overlay")) && *p == pinfo->val)
            dp->plane_type = DRMU_PLANE_TYPE_OVERLAY;
        else {
            drmu_debug(du, "Unexpected plane type: %"PRId64, pinfo->val);
            dp->plane_type = DRMU_PLANE_TYPE_UNKNOWN;
        }
        drmu_prop_enum_delete(&etype);
    }

    props_free(props);
    return 0;
}

//----------------------------------------------------------------------------
//
// Env fns

typedef struct drmu_env_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init
    bool kill;
    int fd;
    uint32_t plane_count;
    uint32_t conn_count;
    uint32_t crtc_count;
    drmu_plane_t * planes;
    drmu_conn_t * conns;
    drmu_crtc_t * crtcs;

    drmu_log_env_t log;

    pthread_mutex_t lock;

    // global env for bo tracking
    drmu_bo_env_t boe;
    // global atomic for restore op
    drmu_atomic_t * da_restore;

    struct drmu_poll_env_s * poll_env;
    drmu_poll_destroy_fn poll_destroy;

    drmu_env_post_delete_fn post_delete_fn;
    void * post_delete_v;
} drmu_env_t;

// Retrieve the the n-th conn
// Use for iteration
// Returns NULL when none left
drmu_crtc_t *
drmu_env_crtc_find_n(drmu_env_t * const du, const unsigned int n)
{
    return n >= du->crtc_count ? NULL : du->crtcs + n;
}

// Retrieve the the n-th conn
// Use for iteration
// Returns NULL when none left
drmu_conn_t *
drmu_env_conn_find_n(drmu_env_t * const du, const unsigned int n)
{
    return n >= du->conn_count ? NULL : du->conns + n;
}

drmu_plane_t *
drmu_env_plane_find_n(drmu_env_t * const du, const unsigned int n)
{
    return n >= du->plane_count ? NULL : du->planes + n;
}

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

static void
env_free_planes(drmu_env_t * const du)
{
    uint32_t i;
    for (i = 0; i != du->plane_count; ++i)
        plane_uninit(du->planes + i);
    free(du->planes);
    du->plane_count = 0;
    du->planes = NULL;
}

static void
env_free_conns(drmu_env_t * const du)
{
    uint32_t i;
    for (i = 0; i != du->conn_count; ++i)
        conn_uninit(du->conns + i);
    free(du->conns);
    du->conn_count = 0;
    du->conns = NULL;
}

static void
env_free_crtcs(drmu_env_t * const du)
{
    uint32_t i;
    for (i = 0; i != du->crtc_count; ++i)
        crtc_uninit(du->crtcs + i);
    free(du->crtcs);
    du->crtc_count = 0;
    du->crtcs = NULL;
}


static int
env_planes_populate(drmu_env_t * const du, unsigned int n, const uint32_t * const ids)
{
    uint32_t i;
    int rv;

    if ((du->planes = calloc(n, sizeof(*du->planes))) == NULL) {
        drmu_err(du, "Plane array alloc failed");
        return -ENOMEM;
    }

    for (i = 0; i != n; ++i) {
        if ((rv = plane_init(du, du->planes + i, ids[i])) != 0)
            goto fail2;
        du->plane_count = i + 1;
    }
    return 0;

fail2:
    env_free_planes(du);
    return rv;
}

// Doesn't clean up on error - assumes that env construction will abort and
// that will tidy up for us
static int
env_conn_populate(drmu_env_t * const du, unsigned int n, const uint32_t * const ids)
{
    unsigned int i;
    int rv;

    if (n == 0) {
        drmu_err(du, "No connectors");
        return -EINVAL;
    }

    if ((du->conns = calloc(n, sizeof(*du->conns))) == NULL) {
        drmu_err(du, "Failed to malloc conns");
        return -ENOMEM;
    }

    for (i = 0; i != n; ++i) {
        if ((rv = conn_init(du, du->conns + i, i, ids[i])) != 0)
            return rv;
        du->conn_count = i + 1;
    }

    return 0;
}

static int
env_crtc_populate(drmu_env_t * const du, unsigned int n, const uint32_t * const ids)
{
    unsigned int i;
    int rv;

    if (n == 0) {
        drmu_err(du, "No crtcs");
        return -EINVAL;
    }

    if ((du->crtcs = malloc(n * sizeof(*du->crtcs))) == NULL) {
        drmu_err(du, "Failed to malloc conns");
        return -ENOMEM;
    }

    for (i = 0; i != n; ++i) {
        if ((rv = crtc_init(du, du->crtcs + i, i, ids[i])) != 0)
            return rv;
        du->crtc_count = i + 1;
    }

    return 0;
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

static struct drmu_bo_env_s *
env_boe(drmu_env_t * const du)
{
    return &du->boe;
}

static void
env_restore(drmu_env_t * const du)
{
    int rv;
    drmu_atomic_t * bad = drmu_atomic_new(du);
    if (drmu_atomic_commit_test(du->da_restore, DRM_MODE_ATOMIC_ALLOW_MODESET, bad) != 0) {
        drmu_atomic_sub(du->da_restore, bad);
        if ((rv = drmu_atomic_commit(du->da_restore, DRM_MODE_ATOMIC_ALLOW_MODESET)) != 0)
            drmu_err(du, "Failed to restore old mode on exit: %s", strerror(-rv));
        else
            drmu_err(du, "Failed to completely restore old mode on exit");

        if (drmu_env_log(du)->max_level >= DRMU_LOG_LEVEL_DEBUG)
            drmu_atomic_dump(bad);
    }
    drmu_atomic_unref(&bad);
    drmu_atomic_unref(&du->da_restore);
}

void
drmu_env_int_restore(drmu_env_t * const du)
{
    if (du->da_restore != NULL)
        env_restore(du);
}

static void
env_free(drmu_env_t * const du)
{
    if (!du)
        return;

    if (du->poll_env)
        du->poll_destroy(&du->poll_env, du);
    drmu_env_int_restore(du);

    env_free_planes(du);
    env_free_conns(du);
    env_free_crtcs(du);
    drmu_bo_env_uninit(&du->boe);
    pthread_mutex_destroy(&du->lock);

    {
        void * const post_delete_v = du->post_delete_v;
        const drmu_env_post_delete_fn post_delete_fn = du->post_delete_fn;
        const int fd = du->fd;
        free(du);
        post_delete_fn(post_delete_v, fd);
    }
}

void
drmu_env_unref(drmu_env_t ** const ppdu)
{
    drmu_env_t * const du = *ppdu;
    int n;

    if (!du)
        return;
    *ppdu = NULL;

    n = atomic_fetch_sub(&du->ref_count, 1);
    assert(n >= 0);
    if (n == 0)
        env_free(du);
}

// Kill the Q
void
drmu_env_kill(drmu_env_t ** const ppdu)
{
    drmu_env_t * du = *ppdu;

    if (!du)
        return;
    *ppdu = NULL;

    pthread_mutex_lock(&du->lock);
    du->kill = true;
    if (du->poll_env)
        du->poll_destroy(&du->poll_env, du);
    pthread_mutex_unlock(&du->lock);

    // If we had a poll env this should have already been done, if it has
    // already been done this is a noop
    drmu_env_int_restore(du);

    drmu_env_unref(&du);
}

drmu_env_t *
drmu_env_ref(drmu_env_t * const du)
{
    if (du)
        atomic_fetch_add(&du->ref_count, 1);
    return du;
}

static int
env_object_state_save(drmu_env_t * const du, const uint32_t obj_id, const uint32_t obj_type)
{
    drmu_props_t * props;
    drmu_atomic_t * da;
    int rv;

    if (!du->da_restore)
        return -EINVAL;

    if ((props = props_new(du, obj_id, obj_type)) == NULL)
        return -ENOENT;

    if ((da = drmu_atomic_new(du)) == NULL) {
        rv = -ENOMEM;
        goto fail;
    }

    if ((rv = drmu_atomic_props_add_save(da, obj_id, props)) != 0)
        goto fail;

    props_free(props);
    return drmu_atomic_env_restore_add_snapshot(&da);

fail:
    if (props)
        props_free(props);
    return rv;
}

int
drmu_env_restore_enable(drmu_env_t * const du)
{
    uint32_t i;

    if (du->da_restore)
        return 0;
    if ((du->da_restore = drmu_atomic_new(du)) == NULL)
        return -ENOMEM;

    // Save state of anything already claimed
    // Cannot rewind time but this allows us to be a bit lax with the
    // precise ordering of calls on setup (which is handy for scan)
    for (i = 0; i != du->conn_count; ++i)
        if (drmu_conn_is_claimed(du->conns + i))
            conn_state_save(du, du->conns + i);
    for (i = 0; i != du->crtc_count; ++i)
        if (drmu_crtc_is_claimed(du->crtcs + i))
            crtc_state_save(du, du->crtcs + i);
    for (i = 0; i != du->plane_count; ++i)
        if (drmu_plane_is_claimed(du->planes + i))
            plane_state_save(du, du->planes + i);

    return 0;
}

bool
drmu_env_restore_is_enabled(const drmu_env_t * const du)
{
    return du->da_restore != NULL;
}

int
drmu_atomic_env_restore_add_snapshot(drmu_atomic_t ** const ppda)
{
    drmu_atomic_t * da = *ppda;
    drmu_atomic_t * fails = NULL;
    drmu_env_t * const du = drmu_atomic_env(da);

    *ppda = NULL;

    if (!du || !du->da_restore) {
        drmu_atomic_unref(&da);
        return 0;
    }

    // Lose anything we can't restore
    fails = drmu_atomic_new(du);
    if (drmu_atomic_commit_test(da, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_TEST_ONLY, fails) != 0)
        drmu_atomic_sub(da, fails);
    drmu_atomic_unref(&fails);

    return drmu_atomic_merge(du->da_restore, &da);
}

static int
env_set_client_cap(drmu_env_t * const du, uint64_t cap_id, uint64_t cap_val)
{
    struct drm_set_client_cap cap = {
        .capability = cap_id,
        .value = cap_val
    };
    return drmu_ioctl(du, DRM_IOCTL_SET_CLIENT_CAP, &cap);
}

int
drmu_env_int_poll_set(drmu_env_t * const du,
                  const drmu_poll_new_fn new_fn, const drmu_poll_destroy_fn destroy_fn,
                  struct drmu_poll_env_s ** const ppPe)
{
    int rv = 0;

    if (du == NULL)
        return -EINVAL;

    pthread_mutex_lock(&du->lock);
    if (du->kill) {
        rv = -EBUSY;
    }
    else if (du->poll_env == NULL) {
        du->poll_destroy = destroy_fn;
        if ((du->poll_env = new_fn(du)) == NULL)
            rv = -ENOMEM;
    }
    *ppPe = du->poll_env;
    pthread_mutex_unlock(&du->lock);

    if (rv == -ENOMEM)
        drmu_err(du, "Failed to create poll env");

    return rv;
}

struct drmu_poll_env_s *
drmu_env_int_poll_get(drmu_env_t * const du)
{
    struct drmu_poll_env_s * pe;
    pthread_mutex_lock(&du->lock);
    pe = du->poll_env;
    pthread_mutex_unlock(&du->lock);
    return pe;
}

// Closes fd on failure
drmu_env_t *
drmu_env_new_fd2(const int fd, const struct drmu_log_env_s * const log,
                 drmu_env_post_delete_fn post_delete_fn, void * post_delete_v)
{
    drmu_env_t * const du = calloc(1, sizeof(*du));
    int rv;
    uint32_t * conn_ids = NULL;
    uint32_t * crtc_ids = NULL;
    uint32_t * plane_ids = NULL;

    if (!du) {
        drmu_err_log(log, "Failed to create du: No memory");
        post_delete_fn(post_delete_v, fd);
        return NULL;
    }

    du->log = (log == NULL) ? drmu_log_env_none : *log;
    du->fd = fd;
    du->post_delete_fn = post_delete_fn;
    du->post_delete_v = post_delete_v;

    pthread_mutex_init(&du->lock, NULL);
    drmu_bo_env_init(&du->boe);

    // We need atomic for almost everything we do
    if (env_set_client_cap(du, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        drmu_err(du, "Failed to set atomic cap");
        goto fail1;
    }
    // We want the primary plane for video
    if (env_set_client_cap(du, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
        drmu_debug(du, "Failed to set universal planes cap");
    // We can understand AR info
    if (env_set_client_cap(du, DRM_CLIENT_CAP_ASPECT_RATIO, 1) != 0)
        drmu_debug(du, "Failed to set AR cap");
    // We would like to see writeback connectors
    if (env_set_client_cap(du, DRM_CLIENT_CAP_WRITEBACK_CONNECTORS, 1) != 0)
        drmu_debug(du, "Failed to set writeback cap");

    {
        struct drm_mode_get_plane_res res;
        uint32_t req_planes = 0;

        do {
            memset(&res, 0, sizeof(res));
            res.plane_id_ptr     = (uintptr_t)plane_ids;
            res.count_planes     = req_planes;

            if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETPLANERESOURCES, &res)) != 0) {
                drmu_err(du, "Failed to get resources: %s", strerror(-rv));
                goto fail1;
            }
        } while ((rv = retry_alloc_u32(&plane_ids, &req_planes, res.count_planes)) == 1);
        if (rv < 0)
            goto fail1;

        if (env_planes_populate(du, res.count_planes, plane_ids) != 0)
            goto fail1;

        free(plane_ids);
        plane_ids = NULL;
    }

    {
        struct drm_mode_card_res res;
        uint32_t req_conns = 0;
        uint32_t req_crtcs = 0;

        for (;;) {
            memset(&res, 0, sizeof(res));
            res.crtc_id_ptr      = (uintptr_t)crtc_ids;
            res.connector_id_ptr = (uintptr_t)conn_ids;
            res.count_crtcs      = req_crtcs;
            res.count_connectors = req_conns;

            if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_GETRESOURCES, &res)) != 0) {
                drmu_err(du, "Failed to get resources: %s", strerror(-rv));
                goto fail1;
            }

            if (res.count_crtcs <= req_crtcs && res.count_connectors <= req_conns)
                break;

            if (retry_alloc_u32(&conn_ids, &req_conns, res.count_connectors) < 0 ||
                retry_alloc_u32(&crtc_ids, &req_crtcs, res.count_crtcs) < 0)
                goto fail1;
        }

        if (env_conn_populate(du, res.count_connectors, conn_ids) != 0)
            goto fail1;
        if (env_crtc_populate(du, res.count_crtcs,      crtc_ids) != 0)
            goto fail1;

        free(conn_ids);
        free(crtc_ids);
        conn_ids = NULL;
        crtc_ids = NULL;
    }

    free(plane_ids);
    return du;

fail1:
    env_free(du);
    free(conn_ids);
    free(crtc_ids);
    free(plane_ids);
    return NULL;
}

static void
env_post_delete_close_cb(void * v, int fd)
{
    (void)v;
    close(fd);
}

drmu_env_t *
drmu_env_new_fd(const int fd, const struct drmu_log_env_s * const log)
{
    return drmu_env_new_fd2(fd, log, env_post_delete_close_cb, NULL);
}

// * As the only remaining libdrm code dependency this should maybe be evicted
// * to its own file
drmu_env_t *
drmu_env_new_open(const char * name, const struct drmu_log_env_s * const log2)
{
    const struct drmu_log_env_s * const log = (log2 == NULL) ? &drmu_log_env_none : log2;
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


