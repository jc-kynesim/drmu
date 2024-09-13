// Initial skeleton of code to obtain a DRM lease from wayland
// Incomplete as at this point in time (Feb 2023) I can't find a wayland
// implementation that usefully supports (i.e. allows desktop grabbing)
// the required extension

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "drmu.h"
#include "drmu_log.h"

#include <wayland-client-core.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-client-protocol.h>

#include "drm-lease-v1-client-protocol.h"

struct waylease_env_s;

typedef struct waylease_conn_s {
    struct waylease_env_s * we;
    struct wp_drm_lease_connector_v1 * w_conn;
} waylease_conn_t;

typedef struct waylease_env_s {
    struct drmu_log_env_s log;
    struct wl_display *display;
    struct wp_drm_lease_device_v1 * drm_lease_device;
    struct wp_drm_lease_v1 * lease;

    int info_fd;
    int leased_fd;
    unsigned int wcs_size;
    unsigned int wcs_n;
    waylease_conn_t ** wcs;
} waylease_env_t;

#define we_err(_we, ...)      drmu_err_log(&((_we)->log), __VA_ARGS__)
#define we_warn(_we, ...)     drmu_warn_log(&((_we)->log), __VA_ARGS__)
#define we_info(_we, ...)     drmu_info_log(&((_we)->log), __VA_ARGS__)
#define we_debug(_we, ...)    drmu_debug_log(&((_we)->log), __VA_ARGS__)



static void
display_destroy(struct wl_display ** const ppDisplay)
{
    struct wl_display * const display = *ppDisplay;
    if (display == NULL)
        return;
    *ppDisplay = NULL;
    wl_display_disconnect(display);
}

static void
drm_lease_connector_destroy(struct wp_drm_lease_connector_v1 ** const ppW_conn)
{
    struct wp_drm_lease_connector_v1 * const w_conn = *ppW_conn;
    if (w_conn == NULL)
        return;
    *ppW_conn = NULL;
    wp_drm_lease_connector_v1_destroy(w_conn);
}

static void
drm_lease_device_destroy(struct wp_drm_lease_device_v1 ** const ppW_dev)
{
    struct wp_drm_lease_device_v1 * const w_dev = *ppW_dev;
    if (w_dev == NULL)
        return;
    *ppW_dev = NULL;
    wp_drm_lease_device_v1_destroy(w_dev);
}

static void
close_fd(int * const pFd)
{
    const int fd = *pFd;
    if (fd == -1)
        return;
    *pFd = -1;
    close(fd);
}

/**
 * name
 *
 * The compositor sends this event once the connector is created
 * to indicate the name of this connector. This will not change for
 * the duration of the Wayland session, but is not guaranteed to be
 * consistent between sessions.
 *
 * If the compositor supports wl_output version 4 and this
 * connector corresponds to a wl_output, the compositor should use
 * the same name as for the wl_output.
 * @param name connector name
 */
static void
wlc_name_cb(void *data,
         struct wp_drm_lease_connector_v1 *wp_drm_lease_connector_v1,
         const char *name)
{
    waylease_conn_t * const wc = data;
    waylease_env_t * const we = wc->we;
    (void)wp_drm_lease_connector_v1;
    we_debug(we, "%s: name = '%s'", __func__, name);
}

/**
 * description
 *
 * The compositor sends this event once the connector is created
 * to provide a human-readable description for this connector,
 * which may be presented to the user. The compositor may send this
 * event multiple times over the lifetime of this object to reflect
 * changes in the description.
 * @param description connector description
 */
static void
wlc_description_cb(void *data,
            struct wp_drm_lease_connector_v1 *wp_drm_lease_connector_v1,
            const char *description)
{
    waylease_conn_t * const wc = data;
    waylease_env_t * const we = wc->we;
    (void)wp_drm_lease_connector_v1;
    we_debug(we, "%s: description = '%s'", __func__, description);
}

/**
 * connector_id
 *
 * The compositor sends this event once the connector is created
 * to indicate the DRM object ID which represents the underlying
 * connector that is being offered. Note that the final lease may
 * include additional object IDs, such as CRTCs and planes.
 * @param connector_id DRM connector ID
 */
static void
wlc_connector_id_cb(void *data,
             struct wp_drm_lease_connector_v1 *wp_drm_lease_connector_v1,
             uint32_t connector_id)
{
    waylease_conn_t * const wc = data;
    waylease_env_t * const we = wc->we;
    (void)wp_drm_lease_connector_v1;
    we_debug(we, "%s: id = %d", __func__, connector_id);
}

/**
 * all properties have been sent
 *
 * This event is sent after all properties of a connector have
 * been sent. This allows changes to the properties to be seen as
 * atomic even if they happen via multiple events.
 */
static void wlc_done_cb(void *data,
         struct wp_drm_lease_connector_v1 *wp_drm_lease_connector_v1)
{
    waylease_conn_t * const wc = data;
    waylease_env_t * const we = wc->we;
    (void)wp_drm_lease_connector_v1;
    we_debug(we, "%s", __func__);
}

/**
 * lease offer withdrawn
 *
 * Sent to indicate that the compositor will no longer honor
 * requests for DRM leases which include this connector. The client
 * may still issue a lease request including this connector, but
 * the compositor will send wp_drm_lease_v1.finished without
 * issuing a lease fd. Compositors are encouraged to send this
 * event when they lose access to connector, for example when the
 * connector is hot-unplugged, when the connector gets leased to a
 * client or when the compositor loses DRM master.
 */

static void wlc_withdrawn_cb(void *data,
          struct wp_drm_lease_connector_v1 *wp_drm_lease_connector_v1)
{
    waylease_conn_t * const wc = data;
    waylease_env_t * const we = wc->we;
    (void)wp_drm_lease_connector_v1;
    we_debug(we, "%s", __func__);
}

static const struct wp_drm_lease_connector_v1_listener drm_lease_connector_listener = {
    .name = wlc_name_cb,
    .description = wlc_description_cb,
    .connector_id = wlc_connector_id_cb,
    .done = wlc_done_cb,
    .withdrawn = wlc_withdrawn_cb,
};

static void
waylease_conn_destroy(waylease_conn_t ** const ppWc)
{
    waylease_conn_t * const wc = *ppWc;
    if (wc == NULL)
        return;
    *ppWc = NULL;
    drm_lease_connector_destroy(&wc->w_conn);
    free(wc);
}

static waylease_conn_t *
waylease_conn_new(waylease_env_t * const we, struct wp_drm_lease_connector_v1 ** const ppW_conn)
{
    waylease_conn_t * wc = calloc(1, sizeof(*wc));
    if (wc == NULL) {
        drm_lease_connector_destroy(ppW_conn);
        return NULL;
    }
    wc->we = we;
    wc->w_conn = *ppW_conn;
    *ppW_conn = NULL;

    wp_drm_lease_connector_v1_add_listener(wc->w_conn, &drm_lease_connector_listener, wc);
    return wc;
}

static waylease_conn_t *
waylease_env_conn_add(waylease_env_t * const we, struct wp_drm_lease_connector_v1 ** const ppW_conn)
{
    waylease_conn_t * wc;

    if (we->wcs_n >= we->wcs_size) {
        const unsigned int n = we->wcs_size == 0 ? 4 : we->wcs_size * 2;
        waylease_conn_t ** const wcs = realloc(we->wcs, sizeof(*we->wcs) * n);
        if (wcs == NULL) {
            we_err(we, "Failed to allocate wcs array");
            goto fail;
        }
        we->wcs = wcs;
        we->wcs_size = n;
    }

    if ((wc = waylease_conn_new(we, ppW_conn)) == NULL) {
        we_err(we, "Failed to allocate new conn");
        goto fail;
    }

    return (we->wcs[we->wcs_n++] = wc);

fail:
    drm_lease_connector_destroy(ppW_conn);
    return NULL;
}

/**
 * open a non-master fd for this DRM node
 *
 * The compositor will send this event when the
 * wp_drm_lease_device_v1 global is bound, although there are no
 * guarantees as to how long this takes - the compositor might need
 * to wait until regaining DRM master. The included fd is a
 * non-master DRM file descriptor opened for this device and the
 * compositor must not authenticate it. The purpose of this event
 * is to give the client the ability to query DRM and discover
 * information which may help them pick the appropriate DRM device
 * or select the appropriate connectors therein.
 * @param fd DRM file descriptor
 */
static void drm_lease_device_v1_drm_fd(void *data,
                                struct wp_drm_lease_device_v1 *wp_drm_lease_device_v1,
                                int32_t fd)
{
    struct waylease_env_s * const we = data;
    (void)wp_drm_lease_device_v1;

    we->info_fd = fd;
    we_debug(we, "%s: fd=%"PRId32, __func__, fd);
}

/**
 * advertise connectors available for leases
 *
 * The compositor will use this event to advertise connectors
 * available for lease by clients. This object may be passed into a
 * lease request to indicate the client would like to lease that
 * connector, see wp_drm_lease_request_v1.request_connector for
 * details. While the compositor will make a best effort to not
 * send disconnected connectors, no guarantees can be made.
 *
 * The compositor must send the drm_fd event before sending
 * connectors. After the drm_fd event it will send all available
 * connectors but may send additional connectors at any time.
 */
static void drm_lease_device_v1_connector(void *data,
          struct wp_drm_lease_device_v1 *wp_drm_lease_device_v1,
          struct wp_drm_lease_connector_v1 *id)
{
    struct waylease_env_s * const we = data;
    (void)wp_drm_lease_device_v1;

    we_debug(we, "%s", __func__);
    waylease_env_conn_add(we, &id);
}

/**
 * signals grouping of connectors
 *
 * The compositor will send this event to indicate that it has
 * sent all currently available connectors after the client binds
 * to the global or when it updates the connector list, for example
 * on hotplug, drm master change or when a leased connector becomes
 * available again. It will similarly send this event to group
 * wp_drm_lease_connector_v1.withdrawn events of connectors of this
 * device.
 */
static void drm_lease_device_v1_done(void *data,
         struct wp_drm_lease_device_v1 *wp_drm_lease_device_v1)
{
    struct waylease_env_s * const we = data;
    (void)wp_drm_lease_device_v1;

    we_debug(we, "%s", __func__);
}

/**
 * the compositor has finished using the device
 *
 * This event is sent in response to the release request and
 * indicates that the compositor is done sending connector events.
 * The compositor will destroy this object immediately after
 * sending the event and it will become invalid. The client should
 * release any resources associated with this device after
 * receiving this event.
 */
static void drm_lease_device_v1_released(void *data,
         struct wp_drm_lease_device_v1 *wp_drm_lease_device_v1)
{
    struct waylease_env_s * const we = data;
    (void)wp_drm_lease_device_v1;

    we_debug(we, "%s", __func__);
}


static const struct wp_drm_lease_device_v1_listener drm_lease_device_v1_listener = {
	.drm_fd = drm_lease_device_v1_drm_fd,
	.connector = drm_lease_device_v1_connector,
	.done = drm_lease_device_v1_done,
	.released = drm_lease_device_v1_released,
};

static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                        const char *interface, uint32_t version)
{
    struct waylease_env_s * const we = data;
    (void)version;

    we_debug(we, "Got interface '%s'", interface);
    if (strcmp(interface, wp_drm_lease_device_v1_interface.name) == 0) {
        we->drm_lease_device = wl_registry_bind(registry, id, &wp_drm_lease_device_v1_interface, 1);
        wp_drm_lease_device_v1_add_listener(we->drm_lease_device, &drm_lease_device_v1_listener, we);
    }
}

static void global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
    (void)id;
    (void)data;
    (void)registry;
}

static const struct wl_registry_listener listener = {
    global_registry_handler,
    global_registry_remover
};

static void
drm_lease_destroy(struct wp_drm_lease_v1 ** const ppLease)
{
    struct wp_drm_lease_v1 * const lease = *ppLease;
    if (lease == NULL)
        return;
    *ppLease = NULL;
    wp_drm_lease_v1_destroy(lease);
}

static void
wcs_free(waylease_env_t * we)
{
    for (unsigned int i = 0; i != we->wcs_n; ++i)
        waylease_conn_destroy(we->wcs + i);
    free(we->wcs);
    we->wcs = NULL;
    we->wcs_n = 0;
    we->wcs_size = 0;
}

static void
waylease_env_destroy(waylease_env_t ** ppWe)
{
    waylease_env_t * we = *ppWe;
    if (we == NULL)
        return;
    *ppWe = NULL;

    wcs_free(we);
    // Should have already been released but tidy up if not
    drm_lease_device_destroy(&we->drm_lease_device);
    display_destroy(&we->display);
    close_fd(&we->info_fd);
    close_fd(&we->leased_fd);
    free(we);
}

/**
 * shares the DRM file descriptor
 *
 * This event returns a file descriptor suitable for use with
 * DRM-related ioctls. The client should use drmModeGetLease to
 * enumerate the DRM objects which have been leased to them. The
 * compositor guarantees it will not use the leased DRM objects
 * itself until it sends the finished event. If the compositor
 * cannot or will not grant a lease for the requested connectors,
 * it will not send this event, instead sending the finished event.
 *
 * The compositor will send this event at most once during this
 * objects lifetime.
 * @param leased_fd leased DRM file descriptor
 */
static void
we_lease_lease_fd_cb(void *data,
         struct wp_drm_lease_v1 *wp_drm_lease_v1,
         int32_t leased_fd)
{
    waylease_env_t * const we = data;
    (void)wp_drm_lease_v1;
    we->leased_fd = leased_fd;
    we_debug(we, "%s: fd=%d", __func__, leased_fd);
}

/**
 * sent when the lease has been revoked
 *
 * The compositor uses this event to either reject a lease
 * request, or if it previously sent a lease_fd, to notify the
 * client that the lease has been revoked. If the client requires a
 * new lease, they should destroy this object and submit a new
 * lease request. The compositor will send no further events for
 * this object after sending the finish event. Compositors should
 * revoke the lease when any of the leased resources become
 * unavailable, namely when a hot-unplug occurs or when the
 * compositor loses DRM master.
 */
static void 
we_lease_finished_cb(void *data,
         struct wp_drm_lease_v1 *wp_drm_lease_v1)
{
    waylease_env_t * const we = data;
    (void)wp_drm_lease_v1;
    we_debug(we, "%s", __func__);
}

static const struct wp_drm_lease_v1_listener drm_lease_listener = {
    .lease_fd = we_lease_lease_fd_cb,
    .finished = we_lease_finished_cb,
};

static void
we_env_deleted(void * v, int fd)
{
    waylease_env_t * we = v;
    (void)fd;
    we_info(we, "%s", __func__);
    drm_lease_destroy(&we->lease);
    wl_display_roundtrip(we->display);
    wcs_free(we);
    wl_display_roundtrip(we->display);
    wp_drm_lease_device_v1_release(we->drm_lease_device);
    wl_display_roundtrip(we->display);
    close_fd(&we->leased_fd);
    waylease_env_destroy(&we);
}

drmu_env_t * drmu_env_new_waylease(const struct drmu_log_env_s * const log2)
{
    const struct drmu_log_env_s * const log = (log2 == NULL) ? &drmu_log_env_none : log2;
    waylease_env_t * we = calloc(1, sizeof(*we));
    struct wl_registry *registry = NULL;

    if (!we)
        return NULL;
    we->log = *log;
    we->info_fd = -1;
    we->leased_fd = -1;

    we->display = wl_display_connect(NULL);
    if (we->display == NULL) {
        drmu_debug_log(log, "Can't connect to wayland display");
        goto fail;
    }
    drmu_debug_log(log, "Got display");

    registry = wl_display_get_registry(we->display);
    wl_registry_add_listener(registry, &listener, we);

    // This call the attached listener global_registry_handler
    wl_display_roundtrip(we->display);
    wl_registry_destroy(registry);
    wl_display_roundtrip(we->display);

    if (!we->drm_lease_device) {
        drmu_debug_log(log, "Wayland %s not supported", wp_drm_lease_device_v1_interface.name);
        goto fail;
    }

    if (we->wcs_n == 0) {
        drmu_debug_log(log, "No connectors availible");
        goto fail;
    }

    {
        struct wp_drm_lease_request_v1 *lease_request = wp_drm_lease_device_v1_create_lease_request(we->drm_lease_device);
        wp_drm_lease_request_v1_request_connector(lease_request, we->wcs[0]->w_conn);
        we->lease = wp_drm_lease_request_v1_submit(lease_request);
        wp_drm_lease_v1_add_listener(we->lease, &drm_lease_listener, we);
        wl_display_roundtrip(we->display);
    }

    if (we->leased_fd == -1) {
        drmu_debug_log(log, "No lease fd");
        goto fail;
    }

    drmu_debug_log(log, "Has lease fd: %d", we->leased_fd);
    return drmu_env_new_fd2(we->leased_fd, log, we_env_deleted, we);


fail:
    waylease_env_destroy(&we);
    return NULL;
}

