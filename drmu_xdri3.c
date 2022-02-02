#include "drmu.h"
#include "drmu_log.h"

#include <fcntl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>

static int
get_drm_fd(const drmu_log_env_t * const log)
{
   Display *dpy = XOpenDisplay(NULL);
   xcb_connection_t *c = XGetXCBConnection(dpy);
   xcb_window_t root = RootWindow(dpy, DefaultScreen(dpy));
   int fd;

   const xcb_query_extension_reply_t *extension =
      xcb_get_extension_data(c, &xcb_dri3_id);
   if (!(extension && extension->present))
      return -1;

   xcb_dri3_open_cookie_t cookie =
      xcb_dri3_open(c, root, None);

   xcb_dri3_open_reply_t *reply = xcb_dri3_open_reply(c, cookie, NULL);
   if (!reply)
      return -1;

   if (reply->nfd != 1) {
      free(reply);
      return -1;
   }

   fd = xcb_dri3_open_reply_fds(c, reply)[0];
   free(reply);
   fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);

   return fd;
}

drmu_env_t *
drmu_env_new_xdri3(const drmu_log_env_t * const log)
{
    const int fd = get_drm_fd(log);

    if (fd == -1) {
        drmu_err_log(log, "Failed to get xlease");
        return NULL;
    }
    return drmu_env_new_fd(fd, log);
}


