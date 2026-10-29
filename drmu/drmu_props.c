#include "drmu_props.h"

#include "drmu.h"
#include "drmu_log.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <libdrm/drm.h>

#ifndef OPT_IO_CALLOC
#define OPT_IO_CALLOC 0
#endif

// See drmu.c for commentary
#if OPT_IO_CALLOC
#define io_alloc(p, n) (uintptr_t)((p) = calloc((n), sizeof(*(p))))
#else
#define io_alloc(p, n) (uintptr_t)((p) = malloc((n) * sizeof(*(p))))
#endif

struct drmu_propinfo_s {
    uint64_t val;
    struct drm_mode_get_property prop;
};

struct drmu_props_s {
    struct drmu_env_s * du;
    unsigned int n;
    drmu_propinfo_t * info;
    const drmu_propinfo_t ** by_name;
};

uint64_t
drmu_propinfo_val(const drmu_propinfo_t * const pi)
{
    return pi == NULL ? 0 : pi->val;
}

uint32_t
drmu_propinfo_prop_id(const drmu_propinfo_t * const pi)
{
    return pi == NULL ? 0 : pi->prop.prop_id;
}

void
drmu_props_free(drmu_props_t * const props)
{
    free(props->info);  // As yet nothing else is allocated off this
    free(props->by_name);
    free(props);
}

const drmu_propinfo_t *
drmu_props_name_to_propinfo(const drmu_props_t * const props, const char * const name)
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

uint32_t
drmu_props_name_to_id(const drmu_props_t * const props, const char * const name)
{
    return drmu_propinfo_prop_id(drmu_props_name_to_propinfo(props, name));
}

// Data must be freed later
int
drmu_props_name_get_blob(const drmu_props_t * const props, const char * const name, void ** const ppdata, size_t * const plen)
{
    const drmu_propinfo_t * const pinfo = drmu_props_name_to_propinfo(props, name);

    *ppdata = 0;
    *plen = 0;

    if (!pinfo)
        return -ENOENT;
    if ((pinfo->prop.flags & DRM_MODE_PROP_BLOB) == 0)
        return -EINVAL;

    return drmu_blob_data_read(props->du, (uint32_t)pinfo->val, ppdata, plen);
}

void
drmu_props_dump(const drmu_props_t * const props)
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

int
drmu_props_get_properties(drmu_env_t * const du, const uint32_t objid, const uint32_t objtype,
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

drmu_props_t *
drmu_props_new(drmu_env_t * const du, const uint32_t objid, const uint32_t objtype)
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

    if ((rv = drmu_props_get_properties(du, objid, objtype, &propids, &values)) < 0)
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
    drmu_props_free(props);
    free(values);
    free(propids);
    return NULL;
}

int
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
