// Initial skeleton of code to obtain a DRM lease from wayland
// Incomplete as at this point in time (Feb 2023) I can't find a wayland
// implementation that usefully supports (i.e. allows desktop grabbing)
// the required extension

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "drmu.h"
#include "drmu_log.h"

#include <wayland-client-core.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-client-protocol.h>

#include "drm-lease-v1-client-protocol.h"

struct waylease_env_s {
    const struct drmu_log_env_s * log;
    struct wp_drm_lease_device_v1 * drm_lease_device;
};

#if 0
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
drm_lease_v1_lease_fd(void *data, struct wp_drm_lease_v1 *wp_drm_lease_v1, int32_t leased_fd)
{
    struct waylease_env_s * const we = data;
    drmu_debug_log(we->log, "%s: %"PRId32, __func__, leased_fd);
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
drm_lease_v1_finished(void *data, struct wp_drm_lease_v1 *wp_drm_lease_v1)
{
    struct waylease_env_s * const we = data;
    (void)wp_drm_lease_v1;

    drmu_debug_log(we->log, "%s", __func__);
}

static const struct struct wp_drm_lease_v1_listener drm_lease_v1_listener = {
    .lease_fd = drm_lease_v1_lease_fd,
    .finished = drm_lease_v1_finished,
};
#endif

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

    drmu_debug_log(we->log, "%s: fd=%"PRId32, __func__, fd);
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
    (void)id;

    drmu_debug_log(we->log, "%s", __func__);
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

    drmu_debug_log(we->log, "%s", __func__);
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

    drmu_debug_log(we->log, "%s", __func__);
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

    drmu_debug_log(we->log, "Got interface '%s'", interface);
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

drmu_env_t * drmu_env_new_waylease(const struct drmu_log_env_s * const log2)
{
    const struct drmu_log_env_s * const log = (log2 == NULL) ? &drmu_log_env_none : log2;
    struct waylease_env_s * we = calloc(1, sizeof(*we));
    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;

    if (!we)
        return NULL;
    we->log = log;

    display = wl_display_connect(NULL);
    if (display == NULL) {
        drmu_debug_log(log, "Can't connect to wayland display");
        goto fail;
    }
    drmu_debug_log(log, "Got display");

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, we);

    // This call the attached listener global_registry_handler
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (!we->drm_lease_device) {
        drmu_debug_log(log, "Wayland %s not supported", wp_drm_lease_device_v1_interface.name);
        goto fail;
    }

fail:
    free(we);
    return NULL;
}

