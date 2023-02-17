#include "drmu.h"
#include "drmu_log.h"

#include <xcb/xcb.h>
#include <xcb/randr.h>

static int
get_lease_fd(const drmu_log_env_t * const log)
{
    xcb_generic_error_t *xerr;

    int screen = 0;
    xcb_connection_t * const connection = xcb_connect(NULL, &screen);
    if (!connection) {
        drmu_warn_log(log, "Connection to X server failed");
        return -1;
    }

    {
        xcb_randr_query_version_cookie_t rqv_c = xcb_randr_query_version(connection,
                                                                         XCB_RANDR_MAJOR_VERSION,
                                                                         XCB_RANDR_MINOR_VERSION);
        xcb_randr_query_version_reply_t *rqv_r = xcb_randr_query_version_reply(connection, rqv_c, NULL);

        if (!rqv_r) {
            drmu_warn_log(log, "Failed to get XCB RandR version");
            return -1;
        }

        uint32_t major = rqv_r->major_version;
        uint32_t minor = rqv_r->minor_version;
        free(rqv_r);

        if (minor < 6) {
            drmu_warn_log(log, "XCB RandR version %d.%d too low for lease support", major, minor);
            return -1;
        }
    }

    xcb_window_t root;

    {
        xcb_screen_iterator_t s_i = xcb_setup_roots_iterator(xcb_get_setup(connection));
        int i;

        for (i = 0; i != screen && s_i.rem != 0; ++i) {
             xcb_screen_next(&s_i);
        }

        if (s_i.rem == 0) {
            drmu_err_log(log, "Failed to get root for screen %d", screen);
            return -1;
        }

        drmu_debug_log(log, "index %d screen %d rem %d", s_i.index, screen, s_i.rem);
        root = s_i.data->root;
    }

    xcb_randr_output_t output = 0;
    xcb_randr_crtc_t crtc = 0;

    /* Find a connected in-use output */
    {
        xcb_randr_get_screen_resources_cookie_t gsr_c = xcb_randr_get_screen_resources(connection, root);

        xcb_randr_get_screen_resources_reply_t *gsr_r = xcb_randr_get_screen_resources_reply(connection, gsr_c, NULL);
        int o;

        if (!gsr_r) {
            drmu_err_log(log, "get_screen_resources failed");
            return -1;
        }

        xcb_randr_output_t * const ro = xcb_randr_get_screen_resources_outputs(gsr_r);

        for (o = 0; output == 0 && o < gsr_r->num_outputs; o++) {
            xcb_randr_get_output_info_cookie_t goi_c = xcb_randr_get_output_info(connection, ro[o], gsr_r->config_timestamp);

            xcb_randr_get_output_info_reply_t *goi_r = xcb_randr_get_output_info_reply(connection, goi_c, NULL);

            drmu_debug_log(log, "output[%d/%d] %d: conn %d/%d crtc %d", o, gsr_r->num_outputs, ro[o], goi_r->connection, XCB_RANDR_CONNECTION_CONNECTED, goi_r->crtc);

            /* Find the first connected and used output */
            if (goi_r->connection == XCB_RANDR_CONNECTION_CONNECTED &&
                goi_r->crtc != 0) {
                output = ro[o];
                crtc = goi_r->crtc;
            }

            free(goi_r);
        }

        free(gsr_r);

        if (output == 0) {
            drmu_warn_log(log, "Failed to find active output (outputs=%d)", o);
            return -1;
        }
    }

    int fd = -1;

    {
        xcb_randr_lease_t lease = xcb_generate_id(connection);

        xcb_randr_create_lease_cookie_t rcl_c = xcb_randr_create_lease(connection,
                                                                       root,
                                                                       lease,
                                                                       1,
                                                                       1,
                                                                       &crtc,
                                                                       &output);
        xcb_randr_create_lease_reply_t *rcl_r = xcb_randr_create_lease_reply(connection, rcl_c, &xerr);

        if (!rcl_r) {
            drmu_err_log(log, "create_lease failed: Xerror %d", xerr->error_code);
            return -1;
        }

        int *rcl_f = xcb_randr_create_lease_reply_fds(connection, rcl_r);

        fd = rcl_f[0];

        free(rcl_r);
    }

    drmu_debug_log(log, "%s OK: fd=%d", __func__, fd);
    return fd;
}

drmu_env_t *
drmu_env_new_xlease(const drmu_log_env_t * const log2)
{
    const struct drmu_log_env_s * const log = (log2 == NULL) ? &drmu_log_env_none : log2;
    const int fd = get_lease_fd(log);

    if (fd == -1) {
        drmu_err_log(log, "Failed to get xlease");
        return NULL;
    }
    return drmu_env_new_fd(fd, log);
}

