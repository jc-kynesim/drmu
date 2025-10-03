#include "drmu_output.h"

#include "drmu_fmts.h"
#include "drmu_log.h"

#include <errno.h>
#include <stdatomic.h>
#include <string.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>

// Update return value with a new one for cases where we don't stop on error
static inline int rvup(int rv1, int rv2)
{
    return rv2 ? rv2 : rv1;
}

struct drmu_output_s {
    atomic_int ref_count;

    drmu_env_t * du;
    drmu_crtc_t * dc;
    unsigned int conn_n;
    unsigned int conn_size;
    drmu_conn_t ** dns;
    bool has_max_bpc;
    bool max_bpc_allow;
    bool modeset_allow;
    int mode_id;
    drmu_mode_simple_params_t mode_params;

    // These are expected to be static consts so no copy / no free
    const drmu_fmt_info_t * fmt_info;
    drmu_colorspace_t colorspace;
    drmu_broadcast_rgb_t broadcast_rgb;

    // HDR metadata
    drmu_isset_t hdr_metadata_isset;
    struct hdr_output_metadata hdr_metadata;
};

drmu_plane_t *
drmu_output_plane_ref_primary(drmu_output_t * const dout)
{
    return drmu_plane_new_find_ref_type(dout->dc, DRMU_PLANE_TYPE_PRIMARY);
}

drmu_plane_t *
drmu_output_plane_ref_other(drmu_output_t * const dout)
{
    return drmu_plane_new_find_ref_type(dout->dc, DRMU_PLANE_TYPE_CURSOR | DRMU_PLANE_TYPE_OVERLAY);
}

struct plane_format_s {
    unsigned int types;
    uint32_t fmt;
    uint64_t mod;
};

static bool plane_find_format_cb(const drmu_plane_t * dp, void * v)
{
    const struct plane_format_s * const f = v;
    return (f->types & drmu_plane_type(dp)) != 0 &&
        drmu_plane_format_check(dp, f->fmt, f->mod);
}

drmu_plane_t *
drmu_output_plane_ref_format(drmu_output_t * const dout, const unsigned int types, const uint32_t format, const uint64_t mod)
{
    struct plane_format_s fm = {
        .types = (types != 0) ? types : (DRMU_PLANE_TYPE_PRIMARY |  DRMU_PLANE_TYPE_CURSOR | DRMU_PLANE_TYPE_OVERLAY),
        .fmt = format,
        .mod = mod
    };

    return drmu_plane_new_find_ref(dout->dc, plane_find_format_cb, &fm);
}


int
drmu_atomic_output_add_connect(drmu_atomic_t * const da, drmu_output_t * const dout)
{
    int rv;

    if ((rv = drmu_atomic_crtc_add_active(da, dout->dc, 1)) != 0)
        return rv;

    for (unsigned int i = 0; i != dout->conn_n; ++i) {
        if ((rv = drmu_atomic_conn_add_crtc(da, dout->dns[i], dout->dc)) != 0)
            return rv;
    }

    return 0;
}

int
drmu_atomic_output_add_props(drmu_atomic_t * const da, drmu_output_t * const dout)
{
    int rv = 0;
    unsigned int i;

    if (!dout->modeset_allow)
        return 0;

    rv = drmu_atomic_crtc_add_modeinfo(da, dout->dc, drmu_conn_modeinfo(dout->dns[0], dout->mode_id));

    for (i = 0; i != dout->conn_n; ++i) {
        drmu_conn_t * const dn = dout->dns[i];

        if (dout->fmt_info && dout->max_bpc_allow)
            rv = rvup(rv, drmu_atomic_conn_add_hi_bpc(da, dn, (drmu_fmt_info_bit_depth(dout->fmt_info) > 8)));
        if (drmu_colorspace_is_set(dout->colorspace))
            rv = rvup(rv, drmu_atomic_conn_add_colorspace(da, dn, dout->colorspace));
        if (drmu_broadcast_rgb_is_set(dout->broadcast_rgb))
            rv = rvup(rv, drmu_atomic_conn_add_broadcast_rgb(da, dn, dout->broadcast_rgb));
        if (dout->hdr_metadata_isset != DRMU_ISSET_UNSET)
            rv = rvup(rv, drmu_atomic_conn_add_hdr_metadata(da, dn,
                dout->hdr_metadata_isset == DRMU_ISSET_NULL ? NULL : &dout->hdr_metadata));
    }

    return rv;
}

// Set all the fb info props that might apply to a crtc on the crtc
// (e.g. hdr_metadata, colorspace) but do not set the mode (resolution
// and refresh)
//
// N.B. Only changes those props that are set in the fb. If unset in the fb
// then their value is unchanged.
int
drmu_output_fb_info_set(drmu_output_t * const dout, const drmu_fb_t * const fb)
{
    const drmu_isset_t hdr_isset = drmu_fb_hdr_metadata_isset(fb);
    const drmu_fmt_info_t * fmt_info = drmu_fb_format_info_get(fb);
    const drmu_colorspace_t colorspace  = drmu_fb_colorspace_get(fb);
    const drmu_broadcast_rgb_t broadcast_rgb = drmu_color_range_to_broadcast_rgb(drmu_fb_color_range_get(fb));

    if (fmt_info)
        dout->fmt_info = fmt_info;
    if (drmu_colorspace_is_set(colorspace))
        dout->colorspace = colorspace;
    if (drmu_broadcast_rgb_is_set(broadcast_rgb))
        dout->broadcast_rgb = broadcast_rgb;

    if (hdr_isset != DRMU_ISSET_UNSET) {
        dout->hdr_metadata_isset = hdr_isset;
        if (hdr_isset == DRMU_ISSET_SET)
            dout->hdr_metadata = *drmu_fb_hdr_metadata_get(fb);
    }

    return 0;
}

void
drmu_output_fb_info_unset(drmu_output_t * const dout)
{
    dout->fmt_info = NULL;
    dout->colorspace = DRMU_COLORSPACE_UNSET;
    dout->broadcast_rgb = DRMU_BROADCAST_RGB_UNSET;
    dout->hdr_metadata_isset = DRMU_ISSET_UNSET;
}


int
drmu_output_mode_id_set(drmu_output_t * const dout, const int mode_id)
{
    drmu_info(dout->du, "%s: mode_id=%d", __func__, mode_id);

    if (mode_id != dout->mode_id) {
        drmu_mode_simple_params_t sp = drmu_conn_mode_simple_params(dout->dns[0], mode_id);
        if (sp.width == 0)
            return -EINVAL;

        dout->mode_id = mode_id;
        dout->mode_params = sp;
    }
    return 0;
}

const drmu_mode_simple_params_t *
drmu_output_mode_simple_params(const drmu_output_t * const dout)
{
    return &dout->mode_params;
}

static int
score_freq(const drmu_mode_simple_params_t * const mode, const drmu_mode_simple_params_t * const p)
{
    const int pref = (mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
    const unsigned int r_m = (mode->flags & DRM_MODE_FLAG_INTERLACE) != 0 ?
        mode->hz_x_1000 * 2: mode->hz_x_1000;
    const unsigned int r_f = (p->flags & DRM_MODE_FLAG_INTERLACE) != 0 ?
        p->hz_x_1000 * 2 : p->hz_x_1000;

    // If we haven't been given any hz then pick pref or fastest
    // Max out at 300Hz (=300,0000)
    if (r_f == 0)
        return pref ? 83000000 : 80000000 + (r_m >= 2999999 ? 2999999 : r_m);
    // Prefer a good match to 29.97 / 30 but allow the other
    else if ((r_m + 10 >= r_f && r_m <= r_f + 10))
        return 100000000;
    else if ((r_m + 100 >= r_f && r_m <= r_f + 100))
        return 95000000;
    // Double isn't bad
    else if ((r_m + 10 >= r_f * 2 && r_m <= r_f * 2 + 10))
        return 90000000;
    else if ((r_m + 100 >= r_f * 2 && r_m <= r_f * 2 + 100))
        return 85000000;
    return -1;
}

// Avoid interlace no matter what our source
int
drmu_mode_pick_simple_cb(void * v, const drmu_mode_simple_params_t * mode)
{
    const drmu_mode_simple_params_t * const p = v;
    const int pref = (mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
    int score = -1;

    if (p->width == mode->width && p->height == mode->height &&
        (mode->flags & DRM_MODE_FLAG_INTERLACE) == 0)
        score = score_freq(mode, p);

    if (score > 0 && (p->width != mode->width || p->height != mode->height))
        score -= 30000000;

    if (score <= 0 && pref)
        score = 10000000;

    return score;
}

// Pick the preferred mode or the 1st one if nothing preferred
int
drmu_mode_pick_simple_preferred_cb(void * v, const drmu_mode_simple_params_t * mode)
{
    (void)v;
    return (mode->type & DRM_MODE_TYPE_PREFERRED) != 0 ? 1 : 0;
}

// Try to match interlace as well as everything else
int
drmu_mode_pick_simple_interlace_cb(void * v, const drmu_mode_simple_params_t * mode)
{
    const drmu_mode_simple_params_t * const p = v;

    const int pref = (mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
    int score = -1;

    if (p->width == mode->width && p->height == mode->height)
        score = score_freq(mode, p);

    if (score > 0 && (p->width != mode->width || p->height != mode->height))
        score -= 30000000;
    if (((mode->flags ^ p->flags) & DRM_MODE_FLAG_INTERLACE) != 0)
        score -= 20000000;

    if (score <= 0 && pref)
        score = 10000000;

    return score;
}


int
drmu_output_mode_pick_simple(drmu_output_t * const dout, drmu_mode_score_fn * const score_fn, void * const score_v)
{
    int best_score = -1;
    int best_mode = -1;
    int i;

    for (i = 0;; ++i) {
        const drmu_mode_simple_params_t sp = drmu_conn_mode_simple_params(dout->dns[0], i);
        int score;

        if (sp.width == 0)
            break;

        score = score_fn(score_v, &sp);
        if (score > best_score) {
            best_score = score;
            best_mode = i;
        }
    }

    return best_mode;
}

int
drmu_output_max_bpc_allow(drmu_output_t * const dout, const bool allow)
{
    dout->max_bpc_allow = allow && dout->has_max_bpc;
    return allow && !dout->has_max_bpc ? -ENOENT : 0;
}

int
drmu_output_modeset_allow(drmu_output_t * const dout, const bool allow)
{
    dout->modeset_allow = allow;
    return 0;
}

static int
check_conns_size(drmu_output_t * const dout)
{
    if (dout->conn_n >= dout->conn_size) {
        unsigned int n = !dout->conn_n ? 4 : dout->conn_n * 2;
        drmu_conn_t ** dns = realloc(dout->dns, sizeof(*dout->dns) * n);
        if (dns == NULL) {
            drmu_err(dout->du, "Failed conn array realloc");
            return -ENOMEM;
        }
        dout->dns = dns;
        dout->conn_size = n;
    }
    return 0;
}

// Experimental, more flexible version of _add_output

static drmu_crtc_t *
output_add_find_crtc(drmu_env_t * const du, drmu_conn_t * const dn)
{
    uint32_t possible_crtcs = drmu_conn_possible_crtcs(dn);
    drmu_crtc_t * dc;

    for (unsigned int i = 0; possible_crtcs != 0; ++i, possible_crtcs >>= 1) {
        if ((possible_crtcs & 1) == 0)
            continue;
        if ((dc = drmu_env_crtc_find_n(du, i)) == NULL)
            continue;
        if (drmu_crtc_is_claimed(dc))
            continue;

        return dc;
    }
    return NULL;
}

int
drmu_output_add_output2(drmu_output_t * const dout, const char * const conn_name, const unsigned int flags)
{
    const size_t nlen = !conn_name ? 0 : strlen(conn_name);
    unsigned int retries = 0;
    drmu_env_t * const du = dout->du;
    drmu_conn_t * dn;
    drmu_conn_t * dn_t;
    drmu_crtc_t * dc;
    int rv;
    const bool wants_writeback = ((flags & DRMU_OUTPUT_FLAG_ADD_WRITEBACK) != 0);
    const bool try_connected = (flags & DRMU_OUTPUT_FLAG_ADD_DISCONNECTED_ONLY) == 0;
    const bool try_disconnected = (flags & DRMU_OUTPUT_FLAG_ADD_DISCONNECTED_ONLY) != 0 ||
        (flags & DRMU_OUTPUT_FLAG_ADD_ANY) != 0 ||
        (flags & DRMU_OUTPUT_FLAG_ADD_DISCONNECTED) != 0;

    if (wants_writeback && !dout->modeset_allow) {
        drmu_debug(du, "modeset_allow required for writeback");
        return -EINVAL;
    }

    retry:
    if (++retries > 16) {
        drmu_err(du, "Retry count exceeded");
        return -EBUSY;
    }
    dn = NULL;
    dc = NULL;

    for (unsigned int i = 0; (dn_t = drmu_env_conn_find_n(du, i)) != NULL; ++i) {
        drmu_crtc_t * dc_t;
        uint32_t crtc_id;

        if ((wants_writeback && !drmu_conn_is_writeback(dn_t)) ||
            (!wants_writeback && !drmu_conn_is_output(dn_t)))
            continue;
        if (drmu_conn_is_claimed(dn_t))
            continue;

        if (nlen && strncmp(conn_name, drmu_conn_name(dn_t), nlen) != 0)
            continue;

        crtc_id = drmu_conn_crtc_id_get(dn_t);
        if (crtc_id != 0 && try_connected) {
            dc_t = drmu_env_crtc_find_id(du, crtc_id);
            if (dc_t == NULL || drmu_crtc_is_claimed(dc_t))
                continue;
            dn = dn_t;
            dc = dc_t;
            break;
        }
        else if (crtc_id == 0 && try_disconnected) {
            dc_t = output_add_find_crtc(du, dn_t);
            if (dc_t == NULL || drmu_crtc_is_claimed(dc_t))
                continue;
            dn = dn_t;
            dc = dc_t;
            if ((flags & DRMU_OUTPUT_FLAG_ADD_ANY) != 0 || !try_connected)
                break;
        }
    }

    if (!dn)
        return -ENOENT;

    if ((rv = check_conns_size(dout)) != 0)
        return rv;

    if (drmu_crtc_claim_ref(dc)) {
        drmu_debug(du, "Crtc already claimed");
        goto retry;
    }
    if (drmu_conn_claim_ref(dn)) {
        drmu_debug(du, "Conn already claimed");
        drmu_crtc_unref(&dc);
        goto retry;
    }

    // Test features
    dout->has_max_bpc = drmu_conn_has_hi_bpc(dn);

    dout->dns[dout->conn_n++] = dn;
    dout->dc = dc;

    dout->mode_params = drmu_crtc_mode_simple_params(dout->dc);

    return 0;
}

int
drmu_output_add_output(drmu_output_t * const dout, const char * const conn_name)
{
    return drmu_output_add_output2(dout, conn_name, 0);
}

int
drmu_output_add_writeback(drmu_output_t * const dout)
{
    return drmu_output_add_output2(dout, NULL, DRMU_OUTPUT_FLAG_ADD_WRITEBACK | DRMU_OUTPUT_FLAG_ADD_ANY);
}

static struct drm_mode_modeinfo
modeinfo_fake(unsigned int w, unsigned int h)
{
    return (struct drm_mode_modeinfo){
        .clock = (h + 30)*(w + 20)*60,
        .hdisplay = w,
        .hsync_start = w + 10,
        .hsync_end = w + 20,
        .htotal = w + 30,
        .hskew = 0,
        .vdisplay = h,
        .vsync_start = h + 10,
        .vsync_end = h + 12,
        .vtotal = h + 20,
        .vscan = 0,
        .vrefresh = 60,
        .type = DRM_MODE_TYPE_USERDEF,
        .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
        .name = {"fake"},
    };
}

int
drmu_atomic_output_add_writeback_fb_callback(drmu_atomic_t * const da_out, drmu_output_t * const dout,
                                    drmu_fb_t * const dfb, const unsigned int rot,
                                    drmu_fb_fence_fd_fn * const fn, void * const v)
{
    drmu_env_t * const du = dout->du;
    drmu_atomic_t * da = drmu_atomic_new(drmu_atomic_env(da_out));
    int rv;
    struct drm_mode_modeinfo mode = !drmu_rotation_is_transposed(rot) ?
        modeinfo_fake(drmu_fb_width(dfb), drmu_fb_height(dfb)) :
        modeinfo_fake(drmu_fb_height(dfb), drmu_fb_width(dfb));
    drmu_conn_t * const dn = dout->dns[0];

    if (da == NULL) {
        if (fn)
            fn(v, -1, NULL);
        return -ENOMEM;
    }

    if ((rv = drmu_atomic_conn_add_writeback_fb(da, dn, dfb, fn, v)) != 0) {
        drmu_err(du, "Failed to add FB to conn");
        goto fail;
    }
    if ((rv = drmu_atomic_conn_add_rotation(da, dn, rot)) != 0) {
        drmu_err(du, "Failed to add rotation to conn");
        goto fail;
    }
    if ((rv = drmu_atomic_crtc_add_modeinfo(da, dout->dc, &mode)) != 0) {
        drmu_err(du, "Failed to add modeinfo to CRTC");
        goto fail;
    }
    if ((rv = drmu_atomic_conn_add_crtc(da, dn, dout->dc)) != 0) {
        drmu_err(du, "Failed to add CRTC to Conn");
        goto fail;
    }
    if ((rv = drmu_atomic_crtc_add_active(da, dout->dc, 1)) != 0) {
        drmu_err(du, "Failed to add Active to Conn");
        goto fail;
    }

    return drmu_atomic_merge(da_out, &da);

fail:
    drmu_atomic_unref(&da);
    return rv;
}

int
drmu_atomic_output_add_writeback_fb_rotate(drmu_atomic_t * const da_out, drmu_output_t * const dout,
                                    drmu_fb_t * const dfb, const unsigned int rot)
{
    return drmu_atomic_output_add_writeback_fb_callback(da_out, dout, dfb, rot, NULL, NULL);
}

int
drmu_atomic_output_add_writeback_fb(drmu_atomic_t * const da_out, drmu_output_t * const dout,
                                    drmu_fb_t * const dfb)
{
    return drmu_atomic_output_add_writeback_fb_rotate(da_out, dout, dfb, DRMU_ROTATION_0);
}

drmu_crtc_t *
drmu_output_crtc(const drmu_output_t * const dout)
{
    return !dout ? NULL : dout->dc;
}

drmu_conn_t *
drmu_output_conn(const drmu_output_t * const dout, const unsigned int n)
{
    return !dout || n >= dout->conn_n ? NULL : dout->dns[n];
}

drmu_env_t *
drmu_output_env(const drmu_output_t * const dout)
{
    return dout->du;
}

static void
output_free(drmu_output_t * const dout)
{
    unsigned int i;
    for (i = 0; i != dout->conn_n; ++i)
        drmu_conn_unref(dout->dns + i);
    free(dout->dns);
    drmu_crtc_unref(&dout->dc);
    drmu_env_unref(&dout->du);
    free(dout);
}

void
drmu_output_unref(drmu_output_t ** const ppdout)
{
    drmu_output_t * const dout = *ppdout;
    if (dout == NULL)
        return;
    *ppdout = NULL;

    if (atomic_fetch_sub(&dout->ref_count, 1) == 0)
        output_free(dout);
}

drmu_output_t *
drmu_output_ref(drmu_output_t * const dout)
{
    if (dout != NULL)
        atomic_fetch_add(&dout->ref_count, 1);
    return dout;
}

drmu_output_t *
drmu_output_new(drmu_env_t * const du)
{
    drmu_output_t * const dout = calloc(1, sizeof(*dout));

    if (dout == NULL) {
        drmu_err(du, "Failed to alloc memory for drmu_output");
        return NULL;
    }

    dout->du = drmu_env_ref(du);
    dout->mode_id = -1;
    return dout;
}

