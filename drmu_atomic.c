#include "drmu_int.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

// Atomic property chain structures - no external visibility
typedef struct aprop_prop_s {
    uint32_t id;
    uint64_t value;
    void * v;
    drmu_prop_ref_fn ref_fn;
    drmu_prop_del_fn del_fn;
} aprop_prop_t;

typedef struct aprop_obj_s {
    uint32_t id;
    unsigned int n;
    unsigned int size;
    aprop_prop_t * props;
} aprop_obj_t;

typedef struct aprop_hdr_s {
    unsigned int n;
    unsigned int size;
    aprop_obj_t * objs;
} aprop_hdr_t;

typedef struct drmu_atomic_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init

    struct drmu_env_s * du;

    aprop_hdr_t props;
} drmu_atomic_t;

static inline unsigned int
max_uint(const unsigned int a, const unsigned int b)
{
    return a < b ? b : a;
}

static void
aprop_prop_unref(aprop_prop_t * const pp)
{
    if (pp->del_fn)
        pp->del_fn(pp->v);
}

static void
aprop_prop_ref(aprop_prop_t * const pp)
{
    if (pp->ref_fn)
        pp->ref_fn(pp->v);
}

static void
aprop_obj_uninit(aprop_obj_t * const po)
{
    unsigned int i;
    for (i = 0; i != po->n; ++i)
        aprop_prop_unref(po->props + i);
    free(po->props);
}

static int
aprop_obj_copy(aprop_obj_t * const po_c, const aprop_obj_t * const po_a)
{
    unsigned int i;
    aprop_prop_t * props;

    aprop_obj_uninit(po_c);
    if (po_a->n == 0)
        return 0;

    if ((props = calloc(po_a->size, sizeof(*props))) == NULL)
        return -ENOMEM;
    memcpy(props, po_a->props, po_a->n * sizeof(*po_a->props));

    *po_c = *po_a;
    po_c->props = props;

    for (i = 0; i != po_a->n; ++i)
        aprop_prop_ref(props + i);
    return 0;
}

static int
aprop_obj_move(aprop_obj_t * const po_c, aprop_obj_t * const po_a)
{
    *po_c = *po_a;
    memset(po_a, 0, sizeof(*po_a));
    return 0;
}

static int
aprop_prop_qsort_cb(const void * va, const void * vb)
{
    const aprop_prop_t * const a = va;
    const aprop_prop_t * const b = vb;
    return a->id == b->id ? 0 : a->id < b->id ? -1 : 1;
}

// Merge b into a. b is zeroed on exit so must be discarded
static int
aprop_obj_merge(aprop_obj_t * const po_c, aprop_obj_t * const po_a, aprop_obj_t * const po_b)
{
    unsigned int i, j, k;
    unsigned int c_size;
    aprop_prop_t * c;
    aprop_prop_t * const a = po_a->props;
    aprop_prop_t * const b = po_b->props;

    // As we should have no identical els we don't care that qsort is unstable
    qsort(a, po_a->n, sizeof(a[0]), aprop_prop_qsort_cb);
    qsort(b, po_b->n, sizeof(b[0]), aprop_prop_qsort_cb);

    // Pick a size
    c_size = max_uint(po_a->size, po_b->size);
    if (c_size < po_a->n + po_b->n)
        c_size *= 2;
    if ((c = calloc(c_size, sizeof(*c))) == NULL)
        return -ENOMEM;

    for (i = 0, j = 0, k = 0; i < po_a->n && j < po_a->n; ++k) {
        if (a[i].id < b[j].id)
            c[k] = a[i++];
        else if (a[i].id > b[j].id)
            c[k] = b[j++];
        else {
            c[k] = b[j++];
            aprop_prop_unref(a + i++);
        }
    }
    for (; i < po_a->n; ++i, ++k)
        c[k] = a[i++];
    for (; j < po_b->n; ++j, ++k)
        c[k] = b[j++];

    *po_c = (aprop_obj_t){
        .id = po_a->id,
        .n = k,
        .size = c_size,
        .props = c
    };

    // We have avoided excess ref / unref by simple copy so just free the objs
    free(po_a->props);
    free(po_b->props);

    memset(po_a, 0, sizeof(*po_a));
    memset(po_b, 0, sizeof(*po_b));

    return 0;
}

static aprop_prop_t *
aprop_obj_prop_get(aprop_obj_t * const po, const uint32_t id)
{
    unsigned int i;
    aprop_prop_t * pp = po->props;

    for (i = 0; i != po->n; ++i, ++pp) {
        if (pp->id == id)
            return pp;
    }

    if (po->n >= po->size) {
        size_t newsize = po->size < 16 ? 16 : po->size * 2;
        if ((pp = realloc(po->props, newsize * sizeof(*pp))) == NULL)
            return NULL;
        memset(pp + po->size, 0, (newsize - po->size) * sizeof(*pp));

        po->props = pp;
        po->size = newsize;
        pp += po->n;
    }
    ++po->n;

    pp->id = id;
    return pp;
}

static void
aprop_obj_atomic_fill(const aprop_obj_t * const po, uint32_t * prop_ids, uint64_t * prop_values)
{
    unsigned int i;
    for (i = 0; i != po->n; ++i) {
        *prop_ids++ = po->props[i].id;
        *prop_values++ = po->props[i].value;
    }
}

static void
aprop_obj_dump(drmu_env_t * const du, const aprop_obj_t * const po)
{
    unsigned int i;
    drmu_info(du, "Obj: %02x: size %d n %d", po->id, po->size, po->n);
    for (i = 0; i != po->n; ++i) {
        drmu_info(du, "Obj %02x: Prop %02x Value %"PRIx64" v %p", po->id, po->props[i].id, po->props[i].value, po->props[i].v);
    }
}

static void
aprop_hdr_dump(drmu_env_t * const du, const aprop_hdr_t * const ph)
{
    unsigned int i;
    drmu_info(du, "Header: size %d n %d", ph->size, ph->n);
    for (i = 0; i != ph->n; ++i)
        aprop_obj_dump(du, ph->objs + i);
}

static aprop_obj_t *
aprop_hdr_obj_get(aprop_hdr_t * const ph, const uint32_t id)
{
    unsigned int i;
    aprop_obj_t * po = ph->objs;

    for (i = 0; i != ph->n; ++i, ++po) {
        if (po->id == id)
            return po;
    }

    if (ph->n >= ph->size) {
        size_t newsize = ph->size < 16 ? 16 : ph->size * 2;
        if ((po = realloc(ph->objs, newsize * sizeof(*po))) == NULL)
            return NULL;
        memset(po + ph->size, 0, (newsize - ph->size) * sizeof(*po));

        ph->objs = po;
        ph->size = newsize;
        po += ph->n;
    }
    ++ph->n;

    po->id = id;
    return po;
}

static void
aprop_hdr_uninit(aprop_hdr_t * const ph)
{
    unsigned int i;
    for (i = 0; i != ph->n; ++i)
        aprop_obj_uninit(ph->objs + i);
    free(ph->objs);
    memset(ph, 0, sizeof(*ph));
}

static int
aprop_hdr_copy(aprop_hdr_t * const ph_c, const aprop_hdr_t * const ph_a)
{
    unsigned int i;

    aprop_hdr_uninit(ph_c);

    if (ph_a->n == 0)
        return 0;

    if ((ph_c->objs = calloc(ph_a->size, sizeof(*ph_c->objs))) == NULL)
        return -ENOMEM;

    ph_c->n = ph_a->n;
    ph_c->size = ph_a->size;

    for (i = 0; i != ph_a->n; ++i)
        aprop_obj_copy(ph_c->objs + i, ph_a->objs + i);
    return 0;
}

// Move b to a. a must be empty
static int
aprop_hdr_move(aprop_hdr_t * const ph_a, aprop_hdr_t * const ph_b)
{
    *ph_a = *ph_b;
    *ph_b = (aprop_hdr_t){0};
    return 0;
}

static int
aprop_obj_qsort_cb(const void * va, const void * vb)
{
    const aprop_obj_t * const a = va;
    const aprop_obj_t * const b = vb;
    return a->id == b->id ? 0 : a->id < b->id ? -1 : 1;
}

// Merge b into a. b will be uninited
static int
aprop_hdr_merge(aprop_hdr_t * const ph_a, aprop_hdr_t * const ph_b)
{
    unsigned int i, j, k;
    unsigned int c_size;
    aprop_obj_t * c;
    aprop_obj_t * const a = ph_a->objs;
    aprop_obj_t * const b = ph_b->objs;

    if (ph_b->n == 0)
        return 0;
    if (ph_a->n == 0)
        return aprop_hdr_move(ph_a, ph_b);

    // As we should have no identical els we don't care that qsort is unstable
    qsort(a, ph_a->n, sizeof(a[0]), aprop_obj_qsort_cb);
    qsort(b, ph_b->n, sizeof(b[0]), aprop_obj_qsort_cb);

    // Pick a size
    c_size = max_uint(ph_a->size, ph_b->size);
    if (c_size < ph_a->n + ph_b->n)
        c_size *= 2;
    if ((c = calloc(c_size, sizeof(*c))) == NULL)
        return -ENOMEM;

    for (i = 0, j = 0, k = 0; i < ph_a->n && j < ph_a->n; ++k) {
        if (a[i].id < b[j].id)
            aprop_obj_move(c + k, a + i++);
        else if (a[i].id > b[j].id)
            aprop_obj_move(c + k, b + j++);
        else
            aprop_obj_merge(c + k, a + i++, b + j++);
    }
    for (; i < ph_a->n; ++i, ++k)
        aprop_obj_move(c + k, a + i++);
    for (; j < ph_b->n; ++j, ++k)
        aprop_obj_move(c + k, b + j++);

    aprop_hdr_uninit(ph_a);
    aprop_hdr_uninit(ph_b);

    ph_a->n = k;
    ph_a->size = c_size;
    ph_a->objs = c;

    return 0;
}

static aprop_prop_t *
aprop_hdr_prop_get(aprop_hdr_t * const ph, const uint32_t obj_id, const uint32_t prop_id)
{
    aprop_obj_t * const po = aprop_hdr_obj_get(ph, obj_id);
    return po == NULL ? NULL : aprop_obj_prop_get(po, prop_id);
}

// Total props
static unsigned int
aprop_hdr_props_count(const aprop_hdr_t * const ph)
{
    unsigned int i;
    unsigned int n = 0;

    for (i = 0; i != ph->n; ++i)
        n += ph->objs[i].n;
    return n;
}

static unsigned int
aprop_hdr_objs_count(const aprop_hdr_t * const ph)
{
    return ph->n;
}

static void
aprop_hdr_atomic_fill(const aprop_hdr_t * const ph,
                     uint32_t * obj_ids,
                     uint32_t * prop_counts,
                     uint32_t * prop_ids,
                     uint64_t * prop_values)
{
    unsigned int i;
    for (i = 0; i != ph->n; ++i) {
        const unsigned int n = ph->objs[i].n;
        *obj_ids++ = ph->objs[i].id;
        *prop_counts++ = n;
        aprop_obj_atomic_fill(ph->objs +i, prop_ids, prop_values);
        prop_ids += n;
        prop_values += n;
    }
}

int
drmu_atomic_add_prop_generic(drmu_atomic_t * const da,
                  const uint32_t obj_id, const uint32_t prop_id, const uint64_t value,
                  const drmu_prop_ref_fn ref_fn, const drmu_prop_del_fn del_fn, void * const v)
{
    aprop_hdr_t * const ph = &da->props;

    if (obj_id == 0 || prop_id == 0)
    {
        return -EINVAL;
    }
    else
    {
        aprop_prop_t *const pp = aprop_hdr_prop_get(ph, obj_id, prop_id);
        if (pp == NULL)
            return -ENOMEM;

        aprop_prop_unref(pp);
        pp->value = value;
        pp->ref_fn = ref_fn;
        pp->del_fn = del_fn;
        pp->v = v;
        aprop_prop_ref(pp);
        return 0;
    }
}

int
drmu_atomic_add_prop_value(drmu_atomic_t * const da, const uint32_t obj_id, const uint32_t prop_id, const uint64_t value)
{
    if (drmu_atomic_add_prop_generic(da, obj_id, prop_id, value, 0, 0, NULL) < 0)
        drmu_warn(drmu_atomic_env(da), "%s: Failed to set obj_id=%#x, prop_id=%#x, val=%" PRId64, __func__,
                 obj_id, prop_id, value);
    return 0;
}

void
drmu_atomic_dump(const drmu_atomic_t * const da)
{
    drmu_info(da->du, "Atomic %p: refs %d", da, atomic_load(&da->ref_count)+1);
    aprop_hdr_dump(da->du, &da->props);
}

drmu_env_t *
drmu_atomic_env(const drmu_atomic_t * const da)
{
    return da == NULL ? NULL : da->du;
}

static void
drmu_atomic_free(drmu_atomic_t * const da)
{
    aprop_hdr_uninit(&da->props);
    free(da);
}

void
drmu_atomic_unref(drmu_atomic_t ** const ppda)
{
    drmu_atomic_t * const da = *ppda;

    if (da == NULL)
        return;
    *ppda = NULL;

    if (atomic_fetch_sub(&da->ref_count, 1) == 0)
        drmu_atomic_free(da);
}

drmu_atomic_t *
drmu_atomic_ref(drmu_atomic_t * const da)
{
    atomic_fetch_add(&da->ref_count, 1);
    return da;
}

drmu_atomic_t *
drmu_atomic_new(drmu_env_t * const du)
{
    drmu_atomic_t * const da = calloc(1, sizeof(*da));

    if (da == NULL) {
        drmu_err(du, "%s: Failed to alloc struct", __func__);
        return NULL;
    }
    da->du = du;

    return da;
}

// Merge b into a. b is unrefed (inc on error)
int
drmu_atomic_merge(drmu_atomic_t * const a, drmu_atomic_t ** const ppb)
{
    drmu_atomic_t * b = *ppb;
    aprop_hdr_t bh = {0};
    int rv = -EINVAL;

    if (b == NULL)
        return 0;
    *ppb = NULL;

    if (a == NULL) {
        drmu_atomic_unref(&b);
        return -EINVAL;
    }
    // We expect this to be the sole ref to a
    assert(atomic_load(&a->ref_count) == 0);

    // If this is the only copy of b then use it directly otherwise
    // copy before (probably) making it unusable
    if (atomic_load(&b->ref_count) == 0)
        rv = aprop_hdr_move(&bh, &b->props);
    else
        rv = aprop_hdr_copy(&bh, &b->props);
    drmu_atomic_unref(&b);

    if (rv != 0) {
        drmu_err(a->du, "%s: Copy Failed", __func__);
        return rv;
    }

    rv = aprop_hdr_merge(&a->props, &bh);
    aprop_hdr_uninit(&bh);

    if (rv != 0) {
        drmu_err(a->du, "%s: Merge Failed", __func__);
        return rv;
    }

    return 0;
}

int
drmu_atomic_commit(const drmu_atomic_t * const da, uint32_t flags)
{
    drmu_env_t * const du = da->du;
    const unsigned int n_objs = aprop_hdr_objs_count(&da->props);
    const unsigned int n_props = aprop_hdr_props_count(&da->props);
    int rv = 0;

    if (n_props != 0) {
        uint32_t obj_ids[n_objs];
        uint32_t prop_counts[n_objs];
        uint32_t prop_ids[n_props];
        uint64_t prop_values[n_props];
        struct drm_mode_atomic atomic = {
            .flags           = flags,
            .count_objs      = n_objs,
            .objs_ptr        = (uintptr_t)obj_ids,
            .count_props_ptr = (uintptr_t)prop_counts,
            .props_ptr       = (uintptr_t)prop_ids,
            .prop_values_ptr = (uintptr_t)prop_values,
            .user_data       = (uintptr_t)da
        };

        aprop_hdr_atomic_fill(&da->props, obj_ids, prop_counts, prop_ids, prop_values);

        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_ATOMIC, &atomic)) == 0)
            return 0;

        if (rv == -EINVAL && du->modeset_allow) {
            drmu_debug(du, "%s: EINVAL try ALLOW_MODESET", __func__);
            atomic.flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
            if (drmIoctl(du->fd, DRM_IOCTL_MODE_ATOMIC, &atomic) == 0)
                return 0;
        }

//        drmu_err(du, "%s: Atomic failed: %s", __func__, strerror(-rv));
    }

    return rv;
}


