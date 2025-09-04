#include "drmu.h"
#include "drmu_log.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>

// Atomic property chain structures - no external visibility
typedef struct aprop_prop_s {
    uint32_t id;
    uint64_t value;
    void * v;
    const drmu_atomic_prop_fns_t * fns;
} aprop_prop_t;

typedef struct aprop_obj_s {
    uint32_t id;
    unsigned int n;
    unsigned int size;
    bool unsorted;
    aprop_prop_t * props;
} aprop_obj_t;

typedef struct aprop_hdr_s {
    unsigned int n;
    unsigned int size;
    bool unsorted;
    aprop_obj_t * objs;
} aprop_hdr_t;

typedef struct atomic_cb_s {
    struct atomic_cb_s * next;
    void * v;
    drmu_atomic_commit_fn * cb;
} atomic_cb_t;

typedef struct drmu_atomic_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init

    struct drmu_env_s * du;

    aprop_hdr_t props;

    atomic_cb_t * commit_cb_q;
    atomic_cb_t ** commit_cb_last_ptr;

    struct drmu_atomic_q_s * q;
} drmu_atomic_t;

static inline unsigned int
max_uint(const unsigned int a, const unsigned int b)
{
    return a < b ? b : a;
}

static atomic_cb_t *
atomic_cb_new(drmu_atomic_commit_fn * cb, void * v)
{
    atomic_cb_t * acb = malloc(sizeof(*acb));
    if (acb == NULL)
        return NULL;

    *acb = (atomic_cb_t){
        .next = NULL,
        .cb = cb,
        .v = v
    };
    return acb;
}

static void
aprop_prop_unref(aprop_prop_t * const pp)
{
    pp->fns->unref(pp->v);
}

static void
aprop_prop_ref(aprop_prop_t * const pp)
{
    pp->fns->ref(pp->v);
}

static void
aprop_prop_committed(aprop_prop_t * const pp)
{
    pp->fns->commit(pp->v, pp->value);
}

static void
aprop_obj_uninit(aprop_obj_t * const po)
{
    unsigned int i;
    for (i = 0; i != po->n; ++i)
        aprop_prop_unref(po->props + i);
    free(po->props);
    memset(po, 0, sizeof(*po));
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

static void
aprop_obj_move(aprop_obj_t * const po_c, aprop_obj_t * const po_a)
{
    *po_c = *po_a;
    memset(po_a, 0, sizeof(*po_a));
}

static int
aprop_prop_qsort_cb(const void * va, const void * vb)
{
    const aprop_prop_t * const a = va;
    const aprop_prop_t * const b = vb;
    return a->id == b->id ? 0 : a->id < b->id ? -1 : 1;
}

static void
aprop_obj_props_sort(aprop_obj_t * const po)
{
    if (!po->unsorted)
        return;
    qsort(po->props, po->n, sizeof(po->props[0]), aprop_prop_qsort_cb);
    po->unsorted = false;
}

// Merge b into a and put the result in c. a & b are uninit on exit
// Could (easily) merge into a but its more convienient for the caller to create new
static int
aprop_obj_merge(aprop_obj_t * const po_c, aprop_obj_t * const po_a, aprop_obj_t * const po_b)
{
    unsigned int i, j, k;
    unsigned int c_size;
    aprop_prop_t * c;
    aprop_prop_t * const a = po_a->props;
    aprop_prop_t * const b = po_b->props;

    // As we should have no identical els we don't care that qsort is unstable
    aprop_obj_props_sort(po_a);
    aprop_obj_props_sort(po_b);

    // Pick a size
    c_size = max_uint(po_a->size, po_b->size);
    if (c_size < po_a->n + po_b->n)
        c_size *= 2;
    if ((c = calloc(c_size, sizeof(*c))) == NULL)
        return -ENOMEM;

    for (i = 0, j = 0, k = 0; i < po_a->n && j < po_b->n; ++k) {
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
        c[k] = a[i];
    for (; j < po_b->n; ++j, ++k)
        c[k] = b[j];

    *po_c = (aprop_obj_t){
        .id = po_a->id,
        .n = k,
        .size = c_size,
        .unsorted = false,
        .props = c
    };

    // We have avoided excess ref / unref by simple copy so just free the props array
    free(a);
    free(b);

    memset(po_a, 0, sizeof(*po_a));
    memset(po_b, 0, sizeof(*po_b));

    return 0;
}

// Remove any props in a that are also in b
// b must be sorted
// Returns count of props remaining in a
static unsigned int
aprop_obj_sub(aprop_obj_t * const po_a, const aprop_obj_t * const po_b)
{
    unsigned int i = 0, j = 0, k;
    aprop_prop_t * const a = po_a->props;
    const aprop_prop_t * const b = po_b->props;

    if (po_a->n == 0 || po_b->n == 0)
        return po_a->n;

    // As we should have no identical els we don't care that qsort is unstable
    aprop_obj_props_sort(po_a);
    assert(!po_b->unsorted);

    // Skip initial non-matches, returning if no match found
    while (a[i].id != b[j].id) {
        if (a[i].id < b[j].id) {
            if (++i == po_a->n)
                return po_a->n;
        }
        else {
            if (++j == po_b->n)
                return po_a->n;
        }
    }
    // We have a match - next loop will do the unref
    k = i;

    do {
        if (a[i].id < b[j].id)
            a[k++] = a[i++];
        else {
            if (a[i].id == b[j].id)
                aprop_prop_unref(a + i++);
            j++;
        }
    } while (i != po_a->n && j != po_b->n);

    for (; i < po_a->n; ++i, ++k)
        a[k] = a[i];
    po_a->n = k;

    return po_a->n;
}


static aprop_prop_t *
aprop_obj_prop_get(aprop_obj_t * const po, const uint32_t id)
{
    unsigned int i;
    aprop_prop_t * pp = po->props;

    static const drmu_atomic_prop_fns_t null_fns = {
        .ref    = drmu_prop_fn_null_ref,
        .unref  = drmu_prop_fn_null_unref,
        .commit = drmu_prop_fn_null_commit
    };

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
    if (!po->unsorted && po->n != 0 && pp[-1].id > id)
        po->unsorted = true;
    ++po->n;

    pp->id = id;
    pp->fns = &null_fns;
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
aprop_obj_dump(drmu_env_t * const du,
               const drmu_log_env_t * const log, const enum drmu_log_level_e lvl,
               const aprop_obj_t * const po)
{
    unsigned int i;
    drmu_log_lvl(log, lvl, "Obj: id %#02x size %d n %d", po->id, po->size, po->n);
    for (i = 0; i != po->n; ++i) {
        struct drm_mode_get_property pattr = {.prop_id = po->props[i].id};
        drmu_ioctl(du, DRM_IOCTL_MODE_GETPROPERTY, &pattr);

        drmu_log_lvl(log, lvl, "Obj %#04x: Prop %#04x (%s) Value %#"PRIx64" v %p",
                     po->id, po->props[i].id, pattr.name, po->props[i].value, po->props[i].v);
    }
}

static void
aprop_obj_committed(const aprop_obj_t * const po)
{
    unsigned int i;
    for (i = 0; i != po->n; ++i)
        aprop_prop_committed(po->props + i);
}

static void
aprop_hdr_dump(drmu_env_t * const du,
               const drmu_log_env_t * const log, const enum drmu_log_level_e lvl,
               const aprop_hdr_t * const ph)
{
    unsigned int i;
    drmu_log_lvl(log, lvl, "Header: size %d n %d", ph->size, ph->n);
    for (i = 0; i != ph->n; ++i)
        aprop_obj_dump(du, log, lvl, ph->objs + i);
}

static void
aprop_hdr_committed(const aprop_hdr_t * const ph)
{
    unsigned int i;
    for (i = 0; i != ph->n; ++i)
        aprop_obj_committed(ph->objs + i);
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
    if (!ph->unsorted && ph->n != 0 && po[-1].id > id)
        ph->unsorted = true;
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
    ph_c->unsorted = ph_a->unsorted;

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

// As we should have no identical els we don't care that qsort is unstable
// Doesn't sort props
static void
aprop_hdr_sort(aprop_hdr_t * const ph)
{
    if (!ph->unsorted)
        return;
    qsort(ph->objs, ph->n, sizeof(ph->objs[0]), aprop_obj_qsort_cb);
    ph->unsorted = false;
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

    aprop_hdr_sort(ph_a);
    aprop_hdr_sort(ph_b);

    // Pick a size
    c_size = max_uint(ph_a->size, ph_b->size);
    if (c_size < ph_a->n + ph_b->n)
        c_size *= 2;
    if ((c = calloc(c_size, sizeof(*c))) == NULL)
        return -ENOMEM;

    for (i = 0, j = 0, k = 0; i < ph_a->n && j < ph_b->n; ++k) {
        if (a[i].id < b[j].id)
            aprop_obj_move(c + k, a + i++);
        else if (a[i].id > b[j].id)
            aprop_obj_move(c + k, b + j++);
        else
            aprop_obj_merge(c + k, a + i++, b + j++);
    }
    for (; i < ph_a->n; ++i, ++k)
        aprop_obj_move(c + k, a + i);
    for (; j < ph_b->n; ++j, ++k)
        aprop_obj_move(c + k, b + j);

    aprop_hdr_uninit(ph_a);
    aprop_hdr_uninit(ph_b);

    ph_a->n = k;
    ph_a->size = c_size;
    ph_a->objs = c;
    // Merge will maintain sort so leave unsorted false

    return 0;
}

// Remove any props in a that are also in b
// b must be sorted
static void
aprop_hdr_sub(aprop_hdr_t * const ph_a, const aprop_hdr_t * const ph_b)
{
    unsigned int i = 0, j = 0, k;
    aprop_obj_t * const a = ph_a->objs;
    const aprop_obj_t * const b = ph_b->objs;

    aprop_hdr_sort(ph_a);
    assert(!ph_b->unsorted);

    // Scan whilst we haven't deleted anything
    for (;;) {
        // If we run out of either array then nothing more needed
        if (i == ph_a->n || j == ph_b->n)
            return;

        if (a[i].id < b[j].id)
            ++i;
        else if (a[i].id > b[j].id)
            ++j;
        else {
            k = i;
            if (aprop_obj_sub(a + i++, b + j++) == 0) {
                aprop_obj_uninit(a + k);
                break;
            }
        }
    }

    // Move & scan
    while (i < ph_a->n && j < ph_b->n) {
        if (a[i].id < b[j].id)
            aprop_obj_move(a + k++, a + i++);
        else if (a[i].id > b[j].id)
            j++;
        else {
            if (aprop_obj_sub(a + i, b + j) == 0)
                aprop_obj_uninit(a + i);
            else
                aprop_obj_move(a + k++, a + i);
            i++;
            j++;
        }
    }

    // Move any remaining entries
    for (; i < ph_a->n; ++i, ++k)
        aprop_obj_move(a + k, a + i);
    ph_a->n = k;

    return;
}

// Sort header objs & obj props
static void
aprop_hdr_props_sort(aprop_hdr_t * const ph)
{
    aprop_hdr_sort(ph);
    for (unsigned int i = 0; i != ph->n; ++i)
        aprop_obj_props_sort(ph->objs + i);
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

void
drmu_prop_fn_null_unref(void * v)
{
    (void)v;
}

void
drmu_prop_fn_null_ref(void * v)
{
    (void)v;
}

void
drmu_prop_fn_null_commit(void * v, uint64_t value)
{
    (void)v;
    (void)value;
}

void
drmu_atomic_queue_set(drmu_atomic_t * const da, struct drmu_atomic_q_s * const q)
{
    da->q = q;
}

struct drmu_atomic_q_s *
drmu_atomic_queue_get(const drmu_atomic_t * const da)
{
    return da->q;
}

int
drmu_atomic_add_commit_callback(drmu_atomic_t * const da, drmu_atomic_commit_fn * const cb, void * const v)
{
    if (cb) {
        atomic_cb_t *acb = atomic_cb_new(cb, v);
        if (acb == NULL)
            return -ENOMEM;

        *da->commit_cb_last_ptr = acb;
        da->commit_cb_last_ptr = &acb->next;
    }

    return 0;
}

void
drmu_atomic_clear_commit_callbacks(drmu_atomic_t * const da)
{
    atomic_cb_t *p = da->commit_cb_q;

    da->commit_cb_q = NULL;
    da->commit_cb_last_ptr = &da->commit_cb_q;

    while (p != NULL) {
        atomic_cb_t * const next = p->next;
        free(p);
        p = next;
    }
}

void
drmu_atomic_run_prop_commit_callbacks(const drmu_atomic_t * const da)
{
    if (da == NULL)
        return;
    aprop_hdr_committed(&da->props);
}

void
drmu_atomic_run_commit_callbacks(const drmu_atomic_t * const da)
{
    if (da == NULL)
        return;

    for (const atomic_cb_t *p = da->commit_cb_q; p != NULL; p = p->next)
        p->cb(p->v);
}

int
drmu_atomic_add_prop_generic(drmu_atomic_t * const da,
                  const uint32_t obj_id, const uint32_t prop_id, const uint64_t value,
                  const drmu_atomic_prop_fns_t * const fns, void * const v)
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
        if (fns) {
            pp->fns = fns;
            pp->v = v;
        }
        aprop_prop_ref(pp);
        return 0;
    }
}

int
drmu_atomic_add_prop_value(drmu_atomic_t * const da, const uint32_t obj_id, const uint32_t prop_id, const uint64_t value)
{
    if (drmu_atomic_add_prop_generic(da, obj_id, prop_id, value, NULL, NULL) < 0)
        drmu_warn(drmu_atomic_env(da), "%s: Failed to set obj_id=%#x, prop_id=%#x, val=%" PRId64, __func__,
                 obj_id, prop_id, value);
    return 0;
}

void
drmu_atomic_dump_lvl(const drmu_atomic_t * const da, const int lvl)
{
    drmu_env_t * const du = da->du;
    const drmu_log_env_t * const log = drmu_env_log(du);

    if (!drmu_log_lvl_test(log, lvl))
        return;

    drmu_log_lvl(log, lvl, "Atomic %p: refs %d", da, atomic_load(&da->ref_count)+1);
    aprop_hdr_dump(du, log, lvl, &da->props);
}

void
drmu_atomic_dump(const drmu_atomic_t * const da)
{
    drmu_atomic_dump_lvl(da, DRMU_LOG_LEVEL_INFO);
}

drmu_env_t *
drmu_atomic_env(const drmu_atomic_t * const da)
{
    return da == NULL ? NULL : da->du;
}

static void
drmu_atomic_free(drmu_atomic_t * const da)
{
    drmu_atomic_clear_commit_callbacks(da);
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
    da->commit_cb_last_ptr = &da->commit_cb_q;

    return da;
}

drmu_atomic_t *
drmu_atomic_copy(drmu_atomic_t * const b)
{
    drmu_atomic_t * a;

    if (b == NULL || (a = drmu_atomic_new(b->du)) == NULL)
        return NULL;

    if (aprop_hdr_copy(&a->props, &b->props) != 0)
        goto fail;
    for (atomic_cb_t * p = b->commit_cb_q; p != NULL; p = p->next)
        if (drmu_atomic_add_commit_callback(a, p->cb, p->v) != 0)
            goto fail;
    a->q = b->q;
    return a;

fail:
    drmu_atomic_free(a);
    return NULL;
}

drmu_atomic_t *
drmu_atomic_move(drmu_atomic_t ** const ppb)
{
    drmu_atomic_t * a;
    drmu_atomic_t * b = *ppb;
    *ppb = NULL;

    if (b == NULL || atomic_load(&b->ref_count) == 0)
        return b;

    a = drmu_atomic_copy(b);
    drmu_atomic_unref(&b);
    return a;
}

// Merge b into a. b is unrefed (inc on error)
// Commit cbs are added
// Non matching, non NULL Qs will return -EINVAL
int
drmu_atomic_merge(drmu_atomic_t * const a, drmu_atomic_t ** const ppb)
{
    drmu_atomic_t * b;
    int rv = -EINVAL;

    if (*ppb == NULL)
        return 0;

    if (a == NULL) {
        drmu_atomic_unref(ppb);
        return -EINVAL;
    }

    if ((b = drmu_atomic_move(ppb)) == NULL)
        return -ENOMEM;

    if (a->q == NULL)
        a->q = b->q;
    else if (b->q != NULL && a->q != b->q)
        return -EINVAL;

    if (b->commit_cb_q != NULL) {
        *a->commit_cb_last_ptr = b->commit_cb_q;
        a->commit_cb_last_ptr = b->commit_cb_last_ptr;
        b->commit_cb_q = NULL;
    }

    rv = aprop_hdr_merge(&a->props, &b->props);
    drmu_atomic_unref(&b);

    if (rv != 0) {
        drmu_err(a->du, "%s: Merge Failed", __func__);
        return rv;
    }

    return 0;
}

void
drmu_atomic_sub(drmu_atomic_t * const a, drmu_atomic_t * const b)
{
    aprop_hdr_props_sort(&b->props);
    aprop_hdr_sub(&a->props, &b->props);
}

static void
atomic_props_crop(struct drm_mode_atomic * const f, const unsigned int n, uint32_t ** const undo_p, uint32_t * const undo_v)
{
    unsigned int i;
    unsigned int t = 0;
    uint32_t * const c = (uint32_t *)(uintptr_t)f->count_props_ptr;

    for (i = 0; i != f->count_objs; ++i) {
        t += c[i];
        if (t >= n) {
            f->count_objs = i + 1;
            *undo_p = c + i;
            *undo_v = c[i];
            c[i] -= t - n;
            break;
        }
    }
}

static void
atomic_props_del(struct drm_mode_atomic * const f, const unsigned int n, const unsigned int cp,
                 uint32_t * const objid, uint32_t * const propid, uint64_t * const val)
{
    unsigned int i;
    unsigned int t = 0;
    uint32_t * const c = (uint32_t *)(uintptr_t)f->count_props_ptr;
    uint32_t * const o = (uint32_t *)(uintptr_t)f->objs_ptr;
    uint32_t * const p = (uint32_t *)(uintptr_t)f->props_ptr;
    uint64_t * const v = (uint64_t *)(uintptr_t)f->prop_values_ptr;

    for (i = 0; i != f->count_objs; ++i) {
        t += c[i];
        if (t > n) {
            // Copy out what we are going to delete
            *objid = o[i];
            *propid = p[n];
            *val = v[n];

            memmove(p + n, p + n + 1, (cp - n - 1) * sizeof(*p));
            memmove(v + n, v + n + 1, (cp - n - 1) * sizeof(*v));

            if (--c[i] == 0) {
                memmove(c + i, c + i + 1, (f->count_objs - i - 1) * sizeof(*c));
                memmove(o + i, o + i + 1, (f->count_objs - i - 1) * sizeof(*o));
                --f->count_objs;
            }
            break;
        }
    }
}

// Returns count of initial good els (i.e. n of 1st bad)
static unsigned int
commit_find_good(drmu_env_t * const du, const struct drm_mode_atomic * const atomic, const unsigned int n_props)
{
    unsigned int a = 0;             // N known good
    unsigned int b = n_props + 1;   // N maybe good + 1

    while (a + 1 < b) {
        struct drm_mode_atomic at = *atomic;
        unsigned int n = (a + b) / 2;
        int rv;
        uint32_t * undo_p = NULL;
        uint32_t undo_v = 0;

        at.flags = DRM_MODE_ATOMIC_TEST_ONLY | (DRM_MODE_ATOMIC_ALLOW_MODESET & atomic->flags);
        atomic_props_crop(&at, n, &undo_p, &undo_v);

        if ((rv = drmu_ioctl(du, DRM_IOCTL_MODE_ATOMIC, &at)) == 0) {
            a = n;
        }
        else {
            b = n;
        }

        *undo_p = undo_v;  // Should always be set
    }

    return a;
}

// da_fail does not keep refs to its values - for info only
int
drmu_atomic_commit_test(const drmu_atomic_t * const da, uint32_t flags, drmu_atomic_t * const da_fail)
{
    drmu_env_t * const du = da->du;
    const unsigned int n_objs = aprop_hdr_objs_count(&da->props);
    unsigned int n_props = aprop_hdr_props_count(&da->props);
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

        rv = drmu_ioctl(du, DRM_IOCTL_MODE_ATOMIC, &atomic);

        drmu_atomic_run_commit_callbacks(da);

        if (rv  == 0 || !da_fail)
            return rv;

        for (;;) {
            unsigned int a = commit_find_good(du, &atomic, n_props);
            uint32_t objid = 0;
            uint32_t propid = 0;
            uint64_t val = 0;

            if (a >= n_props)
                break;

            atomic_props_del(&atomic, a, n_props, &objid, &propid, &val);
            --n_props;

            drmu_atomic_add_prop_value(da_fail, objid, propid, val);
        }
    }

    return rv;
}


int
drmu_atomic_commit(const drmu_atomic_t * const da, uint32_t flags)
{
    return drmu_atomic_commit_test(da, flags, NULL);
}


