#ifndef _DRMU_DRMPRIME_OUT_H
#define _DRMU_DRMPRIME_OUT_H

#include <libavutil/rational.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmprime_video_env_s;
typedef struct drmprime_video_env_s drmprime_video_env_t;

struct drmprime_out_env_s;
typedef struct drmprime_out_env_s drmprime_out_env_t;

struct drmu_output_s;
typedef struct drmu_output_s drmu_output_t;

struct AVFrame;
struct AVCodecContext;

int drmprime_video_get_buffer2(drmprime_video_env_t * const dve, struct AVCodecContext *s, struct AVFrame *frame, int flags);
int drmprime_video_display(drmprime_video_env_t * dve, struct AVFrame * frame);
int drmprime_video_modeset(drmprime_video_env_t *dve, int w, int h, const AVRational rate);

void drmprime_video_set_window_pos(drmprime_video_env_t *de, const unsigned int x, const unsigned int y);
void drmprime_video_set_window_size(drmprime_video_env_t *de, const unsigned int w, const unsigned int h);
void drmprime_video_set_window_zpos(drmprime_video_env_t *de, const unsigned int z);

void drmprime_out_size(drmprime_out_env_t * const dpo, unsigned int *pW, unsigned int *pH);

void drmprime_video_delete(drmprime_video_env_t * dve);
drmprime_video_env_t * drmprime_video_new(struct drmprime_out_env_s * dpo);

void drmprime_out_delete(drmprime_out_env_t * dpo);
drmprime_out_env_t * drmprime_out_new();
drmprime_out_env_t * drmprime_out_new_fd(int fd);
drmu_output_t * drmprime_out_drmu_output(drmprime_out_env_t * const dpo);

void drmprime_out_runticker_start(drmprime_out_env_t * const dpo, const char * const ticker_text);
void drmprime_out_runticker_stop(drmprime_out_env_t * const dpo);

void drmprime_out_runcube_start(drmprime_out_env_t * const dpo);
void drmprime_out_runcube_stop(drmprime_out_env_t * const dpo);

#ifdef __cplusplus
}
#endif

#endif
