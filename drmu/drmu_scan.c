#include "drmu_scan.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"

#define CARD_DIR    "/dev/dri"
#define CARD_PREFIX "card"

typedef struct card_list_s {
    unsigned int size;
    unsigned int n;
    unsigned int * nos;
} card_list_t;

struct drmu_scan_s {
    const drmu_log_env_t * dlog;
    card_list_t cl;
    unsigned int cur_idx;
    char pathname[64];
};

static void
card_list_uninit(card_list_t * const cl)
{
    free(cl->nos);
    cl->nos = NULL;
    cl->size = 0;
    cl->n = 0;
}

static int
card_list_add(card_list_t * const cl, const unsigned int cardno)
{
    if (cl->n >= cl->size) {
        const unsigned int newsize = cl->size < 16 ? 16 : cl->size * 2;
        unsigned int * const newnos = realloc(cl->nos, newsize * sizeof(*newnos));

        if (newnos == NULL)
            return -ENOMEM;
        cl->nos = newnos;
        cl->size = newsize;
    }
    cl->nos[cl->n++] = cardno;
    return 0;
}

static int
card_no_qsort_cb(const void * va, const void * vb)
{
    const unsigned int a = *(const unsigned int *)va;
    const unsigned int b = *(const unsigned int *)vb;

    return a < b ? -1 : a > b ? 1 : 0;
}

// Match "card<n>" exactly - returns -1 if no match
static int
card_no_parse(const char * name)
{
    unsigned long n;
    char * endp;

    if (strncmp(name, CARD_PREFIX, sizeof(CARD_PREFIX) - 1) != 0)
        return -1;
    name += sizeof(CARD_PREFIX) - 1;
    if (*name < '0' || *name > '9')
        return -1;

    n = strtoul(name, &endp, 10);
    if (*endp != '\0' || n > INT_MAX)
        return -1;

    return (int)n;
}

// Build a list of card numbers found in the dri dir, sorted ascending
static int
card_list_scan(card_list_t * const cl, const drmu_log_env_t * const dlog)
{
    DIR * dir;
    const struct dirent * de;
    int rv = 0;

    if ((dir = opendir(CARD_DIR)) == NULL) {
        rv = -errno;
        drmu_err_log(dlog, "Failed to open " CARD_DIR ": %s", strerror(-rv));
        return rv;
    }

    while ((de = readdir(dir)) != NULL) {
        const int cardno = card_no_parse(de->d_name);

        if (cardno >= 0 &&
            (rv = card_list_add(cl, cardno)) != 0) {
            drmu_err_log(dlog, "Failed to add card %d to list: %s", cardno, strerror(-rv));
            break;
        }
    }

    closedir(dir);

    if (rv != 0)
        return rv;

    qsort(cl->nos, cl->n, sizeof(*cl->nos), card_no_qsort_cb);
    return 0;
}

static void
scan_free(drmu_scan_t * const dscan)
{
    card_list_uninit(&dscan->cl);
    free(dscan);
}

void
drmu_scan_unref(drmu_scan_t ** const ppdscan)
{
    drmu_scan_t *const dscan = *ppdscan;

    if (dscan == NULL)
        return;
    *ppdscan = NULL;

    scan_free(dscan);
}

static bool
scan_eol(const drmu_scan_t * const dscan)
{
    return dscan == NULL || dscan->cur_idx >= dscan->cl.n;
}

const char *
drmu_scan_path(drmu_scan_t * const dscan)
{
    if (scan_eol(dscan))
        return NULL;

    snprintf(dscan->pathname, sizeof(dscan->pathname), CARD_DIR "/" CARD_PREFIX "%u", dscan->cl.nos[dscan->cur_idx]);
    return dscan->pathname;
}

int
drmu_scan_next(drmu_scan_t * const dscan)
{
    if (scan_eol(dscan))
        return -ENOENT;

    ++dscan->cur_idx;
    return dscan->cur_idx >= dscan->cl.n ? -ENOENT : 0;
}

struct drmu_env_s *
drmu_scan_du(drmu_scan_t * const dscan)
{
    const char * path;

    for (; (path = drmu_scan_path(dscan)) != NULL; drmu_scan_next(dscan)) {
        drmu_env_t * du;
        int fd;

        drmu_debug_log(dscan->dlog, "Try card %s", path);

        while ((fd = open(path, O_RDWR | O_CLOEXEC)) == -1 && errno == EINTR)
            /* Loop */;
        if (fd == -1)
            continue;

        // Have FD
        if ((du = drmu_env_new_fd(fd, dscan->dlog)) != NULL)
            return du;

        drmu_debug_log(dscan->dlog, "Cannot create du");
    }

    return NULL;
}

drmu_scan_t *
drmu_scan_new(const drmu_log_env_t * const dlog)
{
    drmu_scan_t * const dscan = calloc(1, sizeof(*dscan));

    if (dscan == NULL)
        return NULL;

    dscan->dlog = dlog == NULL ? &drmu_log_env_none : dlog;

    if (card_list_scan(&dscan->cl, dscan->dlog) != 0)
        goto fail;

    return dscan;

fail:
    scan_free(dscan);
    return NULL;
}

int
drmu_scan_output(const char * const conn_name, const drmu_log_env_t * const dlog,
                 drmu_env_t ** const pDu, drmu_output_t ** const pDoutput)
{
    drmu_scan_t * dscan = drmu_scan_new(dlog);
    drmu_env_t * du;
    int rv = -ENOENT;

    *pDu = NULL;
    *pDoutput = NULL;

    for (dscan = drmu_scan_new(dlog); (du = drmu_scan_du(dscan)) != NULL; drmu_scan_next(dscan)) {
        drmu_output_t * dout = NULL;

        if ((dout = drmu_output_new(du)) == NULL) {
            drmu_debug_log(dlog, "Cannot create output");
        }
        else if (drmu_output_add_output(dout, conn_name) != 0) {
            drmu_debug_log(dlog, "Could not add output for conn '%s'", conn_name);
        }
        else {
            drmu_debug_log(dlog, "Added output OK");
            *pDu = du;
            *pDoutput = dout;
            rv = 0;
            break;
        }

        drmu_output_unref(&dout);
        drmu_env_unref(&du);
    }

    drmu_scan_unref(&dscan);
    return rv;
}

