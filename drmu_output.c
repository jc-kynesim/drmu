#include "drmu_output.h"
#include "drmu_log.h"

#include <errno.h>
#include <string.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>

// Update return value with a new one for cases where we don't stop on error
static inline int rvup(int rv1, int rv2)
{
    return rv2 ? rv2 : rv1;
}

struct drmu_output_s {
    drmu_env_t * du;
    drmu_crtc_t * dc;
    unsigned int conn_n;
    unsigned int conn_size;
    drmu_conn_t ** dns;
    bool max_bpc_allow;
    bool modeset_allow;
    int mode_id;
    drmu_mode_simple_params_t mode_params;

    // These are expected to be static consts so no copy / no free
    const drmu_format_info_t * fmt_info;
    const char * colorspace;
    const char * broadcast_rgb;

    // HDR metadata
    drmu_isset_t hdr_metadata_isset;
    struct hdr_output_metadata hdr_metadata;
};

drmu_plane_t *
drmu_output_plane_ref_primary(drmu_output_t * const dout)
{
    drmu_plane_t * const dp = drmu_plane_new_find_type(dout->dc, DRMU_PLANE_TYPE_PRIMARY);

    if (dp == NULL || drmu_plane_ref_crtc(dp, dout->dc) != 0)
        return NULL;

    return dp;
}

drmu_plane_t *
drmu_output_plane_ref_other(drmu_output_t * const dout)
{
    drmu_plane_t *const dp = drmu_plane_new_find_type(dout->dc, DRMU_PLANE_TYPE_CURSOR | DRMU_PLANE_TYPE_OVERLAY);

    if (dp == NULL || drmu_plane_ref_crtc(dp, dout->dc) != 0)
        return NULL;

    return dp;
}

int
drmu_atomic_add_output_props(drmu_atomic_t * const da, drmu_output_t * const dout)
{
    int rv = 0;
    unsigned int i;

    if (!dout->modeset_allow)
        return 0;

    rv = drmu_atomic_crtc_add_modeinfo(da, dout->dc, drmu_conn_modeinfo(dout->dns[0], dout->mode_id));

    for (i = 0; i != dout->conn_n; ++i) {
        drmu_conn_t * const dn = dout->dns[i];

        if (dout->fmt_info && dout->max_bpc_allow)
            rv = rvup(rv, drmu_atomic_conn_hi_bpc_set(da, dn, (drmu_format_info_bit_depth(dout->fmt_info) > 8)));
        if (dout->colorspace)
            rv = rvup(rv, drmu_atomic_conn_colorspace_set(da, dn, dout->colorspace));
        if (dout->broadcast_rgb)
            rv = rvup(rv, drmu_atomic_conn_broadcast_rgb_set(da, dn, dout->broadcast_rgb));
        if (dout->hdr_metadata_isset != DRMU_ISSET_UNSET)
            rv = rvup(rv, drmu_atomic_conn_hdr_metadata_set(da, dn,
                dout->hdr_metadata_isset == DRMU_ISSET_NULL ? NULL : &dout->hdr_metadata));
    }

    return rv;
}

// Set all the fb info props that might apply to a crtc on the crtc
// (e.g. hdr_metadata, colorspace) but do not set the mode (resolution
// and refresh)
int
drmu_output_fb_info_set(drmu_output_t * const dout, const drmu_fb_t * const fb)
{
    drmu_isset_t hdr_isset = drmu_fb_hdr_metadata_isset(fb);
    const drmu_format_info_t * fmt_info = drmu_fb_format_info_get(fb);
    const char * colorspace             = drmu_fb_colorspace_get(fb);
    const char * broadcast_rgb          = drmu_color_range_to_broadcast_rgb(drmu_fb_color_range_get(fb));

    if (fmt_info)
        dout->fmt_info = fmt_info;
    if (colorspace)
        dout->colorspace = colorspace;
    if (broadcast_rgb)
        dout->broadcast_rgb = broadcast_rgb;

    if (hdr_isset != DRMU_ISSET_UNSET) {
        dout->hdr_metadata_isset = hdr_isset;
        if (hdr_isset == DRMU_ISSET_SET)
            dout->hdr_metadata = *drmu_fb_hdr_metadata_get(fb);
    }

    return 0;
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

int
drmu_mode_pick_simple_cb(void * v, const drmu_mode_simple_params_t * mode)
{
    const drmu_mode_simple_params_t * const p = v;

    const int pref = (mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
    const unsigned int r_m = mode->hz_x_1000;
    const unsigned int r_f = p->hz_x_1000;
    int score = -1;

    // We don't understand interlace
    if ((mode->flags & DRM_MODE_FLAG_INTERLACE) != 0)
        return -1;

    if (p->width == mode->width && p->height == mode->height)
    {
        // If we haven't been given any hz then pick pref or fastest
        // Max out at 300Hz (=300,0000)
        if (r_f == 0)
            score = pref ? 83000000 : 80000000 + (r_m >= 2999999 ? 2999999 : r_m);
        // Prefer a good match to 29.97 / 30 but allow the other
        else if ((r_m + 10 >= r_f && r_m <= r_f + 10))
            score = 100000000;
        else if ((r_m + 100 >= r_f && r_m <= r_f + 100))
            score = 95000000;
        // Double isn't bad
        else if ((r_m + 10 >= r_f * 2 && r_m <= r_f * 2 + 10))
            score = 90000000;
        else if ((r_m + 100 >= r_f * 2 && r_m <= r_f * 2 + 100))
            score = 85000000;
    }
    if (score > 0 && (p->width != mode->width || p->height != mode->height))
        score -= 30000000;

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
    dout->max_bpc_allow = allow;
    return 0;
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

int
drmu_output_add_output(drmu_output_t * const dout, const char * const conn_name)
{
    const size_t nlen = !conn_name ? 0 : strlen(conn_name);
    unsigned int i;
    unsigned int retries = 0;
    drmu_env_t * const du = dout->du;
    drmu_conn_t * dn;
    drmu_conn_t * dn_t;
    drmu_crtc_t * dc_t;
    uint32_t crtc_id;
    int rv;

    // *****
    // This logic fatally flawed for anything other than adding a single
    // conn already attached to a single crtc

retry:
    if (++retries > 16) {
        drmu_err(du, "Retry count exceeded");
        return -EBUSY;
    }
    dn = NULL;
    dc_t = NULL;

    for (i = 0; (dn_t = drmu_env_conn_find_n(du, i)) != NULL; ++i) {
        if (!drmu_conn_is_output(dn_t) || drmu_conn_is_claimed(dn_t))
            continue;
        if (nlen && strncmp(conn_name, drmu_conn_name(dn_t), nlen) != 0)
            continue;
        // This prefers conns that are already attached to crtcs
        if ((crtc_id = drmu_conn_crtc_id_get(dn_t)) == 0 ||
            (dc_t = drmu_env_crtc_find_id(du, crtc_id)) == NULL) {
            dn = dn_t;
            continue;
        }
        if (drmu_crtc_is_claimed(dc_t)) {
            dc_t = NULL;
            continue;
        }
        dn = dn_t;
        break;
    }

    if (!dn)
        return -ENOENT;

    if (!dc_t) {
        drmu_warn(du, "Adding unattached conns NIF");
        return -EINVAL;
    }

    if ((rv = check_conns_size(dout)) != 0)
        return rv;

    if (drmu_crtc_claim_ref(dc_t)) {
        drmu_debug(du, "Crtc already claimed");
        goto retry;
    }
    if (drmu_conn_claim_ref(dn)) {
        drmu_debug(du, "Conn already claimed");
        drmu_crtc_unref(&dc_t);
        goto retry;
    }

    dout->dns[dout->conn_n++] = dn;
    dout->dc = dc_t;

    dout->mode_params = drmu_crtc_mode_simple_params(dout->dc);

    return 0;
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

static int
try_conn_crtc(drmu_env_t * du, drmu_conn_t * dn, drmu_crtc_t * dc)
{
    int rv;

#if 1
    const struct drm_mode_modeinfo test_mode = modeinfo_fake(128,128);
#else
    // A real mode for testing
    static const struct drm_mode_modeinfo test_mode = {
        .clock = 25175,
        .hdisplay = 640,
        .hsync_start = 656,
        .hsync_end = 752,
        .htotal = 800,
        .hskew = 0,
        .vdisplay = 480,
        .vsync_start = 490,
        .vsync_end = 492,
        .vtotal = 525,
        .vscan = 0,
        .vrefresh = 60,
        .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
        .name = {"640x480-60"},
    };
#endif

    drmu_atomic_t * da = drmu_atomic_new(du);

    if (!da)
        return -ENOMEM;

    if ((rv = drmu_atomic_conn_add_crtc(da, dn, dc)) != 0) {
        drmu_warn(du, "Failed to add writeback connector to crtc: %s", strerror(-rv));
        goto fail;
    }
    if ((rv = drmu_atomic_crtc_add_modeinfo(da, dc, &test_mode)) != 0) {
        drmu_warn(du, "Failed to add modeinfo: %s", strerror(-rv));
        goto fail;
    }

    if ((rv = drmu_atomic_crtc_add_active(da, dc, 1)) != 0) {
        drmu_warn(du, "Failed to add active to crtc: %s", strerror(-rv));
        goto fail;
    }

    if ((rv = drmu_atomic_commit(da, DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET)) != 0) {
        drmu_warn(du, "Failed test commit of writeback connector to crtc: %s", strerror(-rv));
        goto fail;
    }

    drmu_atomic_unref(&da);
    return 0;

fail:
    drmu_atomic_unref(&da);
    return rv;
}

int
drmu_atomic_output_add_writeback_fb(drmu_atomic_t * const da_out, drmu_output_t * const dout,
                                    drmu_fb_t * const dfb)
{
    drmu_env_t * const du = dout->du;
    drmu_atomic_t * da = drmu_atomic_new(drmu_atomic_env(da_out));
    int rv = -ENOMEM;
    struct drm_mode_modeinfo mode = modeinfo_fake(drmu_fb_width(dfb), drmu_fb_height(dfb));
    drmu_conn_t * const dn = dout->dns[0];

    if (da == NULL)
        return -ENOMEM;

    if ((rv = drmu_atomic_conn_add_writeback_fb(da, dn, dfb)) != 0) {
        drmu_err(du, "Failed to add FB to conn");
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
drmu_output_add_writeback(drmu_output_t * const dout)
{
    drmu_env_t * const du = dout->du;
    drmu_conn_t * dn = NULL;
    drmu_crtc_t * dc = NULL;
    drmu_conn_t * dn_t;
    int rv;
    uint32_t possible_crtcs;

    if (!dout->modeset_allow) {
        drmu_debug(du, "modeset_allow required for writeback");
        return -EINVAL;
    }

    for (unsigned int i = 0; (dn_t = drmu_env_conn_find_n(du, i)) != NULL; ++i) {
        drmu_info(du, "%d: try %s", i, drmu_conn_name(dn_t));
        if (!drmu_conn_is_writeback(dn_t))
            continue;
        dn = dn_t;
        break;
    }

    if (!dn) {
        drmu_err(du, "no writeback conn found");
        return -ENOENT;
    }

    possible_crtcs = drmu_conn_possible_crtcs(dn);

    for (unsigned int i = 0; possible_crtcs != 0; ++i, possible_crtcs >>= 1) {
        drmu_crtc_t *dc_t;

        if ((possible_crtcs & 1) == 0)
            continue;

        drmu_info(du, "try Crtc %d", i);

        if ((dc_t = drmu_env_crtc_find_n(du, i)) == NULL)
            break;

        if (try_conn_crtc(du, dn, dc_t) == 0) {
            dc = dc_t;
            break;
        }
    }

    if (!dc) {
        drmu_err(du, "No crtc for writeback found");
        return -ENOENT;
    }

    if ((rv = check_conns_size(dout)) != 0)
        return rv;

    dout->dns[dout->conn_n++] = dn;
    dout->dc = dc;
    return 0;
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

static void
output_free(drmu_output_t * const dout)
{
    unsigned int i;
    for (i = 0; i != dout->conn_n; ++i)
        drmu_conn_unref(dout->dns + i);
    drmu_crtc_unref(&dout->dc);
    free(dout);
}

void
drmu_output_unref(drmu_output_t ** const ppdout)
{
    drmu_output_t * const dout = *ppdout;
    if (dout == NULL)
        return;
    *ppdout = NULL;

    output_free(dout);
}

drmu_output_t *
drmu_output_new(drmu_env_t * const du)
{
    drmu_output_t * const dout = calloc(1, sizeof(*dout));

    if (dout == NULL) {
        drmu_err(du, "Failed to alloc memory for drmu_output");
        return NULL;
    }

    dout->du = du;
    dout->mode_id = -1;
    return dout;
}

