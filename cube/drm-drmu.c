#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include "common.h"
#include "drm-common.h"

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include "drmu_scan.h"

static struct drm drm_static = {.fd = -1};

static void
drmu_log_stderr_cb(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl)
{
    char buf[256];
    int n = vsnprintf(buf, 255, fmt, vl);

    (void)v;
    (void)level;

    if (n >= 255)
        n = 255;
    buf[n] = '\n';
    fwrite(buf, n + 1, 1, stderr);
}

static void
commit_cb(void * v)
{
    struct drm * drm = v;
    sem_post(&drm->commit_sem);
}

void cube_run_drmu(struct drm * const drm, const struct gbm * const gbm, const struct egl * const egl)
{
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fbs[drm->buf_no].fb);

    egl->draw(drm->run_no++);

    glFinish();

    /*
     * Here you could also update drm plane layers if you want
     * hw composition
     */

    {
        drmu_atomic_t * da = drmu_atomic_new(drm->du);
        drmu_atomic_plane_add_fb(da, drm->dp, gbm->dfbs[drm->buf_no], drmu_rect_wh(drm->mode->hdisplay / 2, drm->mode->vdisplay / 2));
        drmu_atomic_add_commit_callback(da, commit_cb, drm);
        drmu_atomic_queue(&da);
    }

    drm->buf_no = drm->buf_no + 1 >= NUM_BUFFERS ? 0 : drm->buf_no + 1;

    sem_wait(&drm->commit_sem);
}

static int run_drmu(const struct gbm *gbm, const struct egl *egl)
{
    struct drm * const drm = &drm_static;
    unsigned int i;
	int64_t start_time, report_time, cur_time;

	start_time = report_time = get_time_ns();

	for (i = 0 ; i < drm->count; ++i) {

		/* Start fps measuring on second frame, to remove the time spent
		 * compiling shader, etc, from the fps:
		 */
		if (i == 1) {
			start_time = report_time = get_time_ns();
		}

        cube_run_drmu(drm, gbm, egl);

		cur_time = get_time_ns();
		if (cur_time > (report_time + 2 * NSEC_PER_SEC)) {
			double elapsed_time = cur_time - start_time;
			double secs = elapsed_time / (double)NSEC_PER_SEC;
			unsigned frames = i - 1;  /* first frame ignored */
			printf("Rendered %u frames in %f sec (%f fps)\n",
				frames, secs, (double)frames/secs);
			report_time = cur_time;
		}

	}

	finish_perfcntrs();

	cur_time = get_time_ns();
	double elapsed_time = cur_time - start_time;
	double secs = elapsed_time / (double)NSEC_PER_SEC;
	unsigned frames = i - 1;  /* first frame ignored */
	printf("Rendered %u frames in %f sec (%f fps)\n",
		frames, secs, (double)frames/secs);

	dump_perfcntrs(frames, elapsed_time);

	return 0;
}


struct drm *
init_drmu_dout(drmu_output_t * const dout, unsigned int count, const uint32_t format)
{
    struct drm * drm = &drm_static;
    const drmu_mode_simple_params_t * sparam;

    drm->fd = -1;
    drm->count = count;
    drm->run = run_drmu;
    drm->du = drmu_output_env(dout);
    drm->dout = dout;
    if ((drm->mode = calloc(1, sizeof(drm->mode))) == NULL) {
        fprintf(stderr, "Failed drm mode alloc\n");
        goto fail;
    }

    sparam = drmu_output_mode_simple_params(drm->dout);
    drm->mode->hdisplay = sparam->width;
    drm->mode->vdisplay = sparam->height;

    // This doesn't really want to be the primary
    if ((drm->dp = drmu_output_plane_ref_format(drm->dout, DRMU_PLANE_TYPE_OVERLAY, format, 0)) == NULL)
        goto fail;

    sem_init(&drm->commit_sem, 0, 0);

    return drm;

fail:
    free(drm->mode);
    return NULL;
}


const struct drm *
init_drmu(const char *device, const char *mode_str, unsigned int count, const uint32_t format)
{
    drmu_env_t * du = NULL;
    drmu_output_t * dout = NULL;

    const drmu_log_env_t log = {
        .fn = drmu_log_stderr_cb,
        .v = NULL,
        .max_level = DRMU_LOG_LEVEL_INFO
    };

    (void)mode_str;

    if (drmu_scan_output(device, &log, &du, &dout) != 0) {
        fprintf(stderr, "Failed drmu scan for device %s\n", device);
        return NULL;
    }
    drmu_env_restore_enable(du);

    return init_drmu_dout(dout, count, format);
}

const struct gbm * init_gbm_drmu(drmu_env_t * du, int w, int h, uint32_t format, uint64_t modifier)
{
    struct gbm * gbm = calloc(1, sizeof(*gbm));
    unsigned int i;

    if (!gbm)
        return NULL;

    gbm->format = format;
    gbm->width = w;
    gbm->height = h;

    for (i = 0; i != NUM_BUFFERS; ++i) {
        gbm->dfbs[i] = drmu_fb_new_dumb_mod(du, w, h, format, modifier);
    }

    return gbm;
}

