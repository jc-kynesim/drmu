#include <libavutil/rational.h>

struct drmprime_video_env_s;
typedef struct drmprime_video_env_s drmprime_video_env_t;

struct drmprime_out_env_s;
typedef struct drmprime_out_env_s drmprime_out_env_t;

struct AVFrame;
struct AVCodecContext;

int drmprime_video_get_buffer2(drmprime_video_env_t * const dve, struct AVCodecContext *s, struct AVFrame *frame, int flags);
int drmprime_video_display(drmprime_video_env_t * dve, struct AVFrame * frame);
int drmprime_video_modeset(drmprime_video_env_t *dve, int w, int h, const AVRational rate);

void drmprime_video_set_window_pos(drmprime_video_env_t *de, const unsigned int x, const unsigned int y);
void drmprime_video_set_window_size(drmprime_video_env_t *de, const unsigned int w, const unsigned int h);
void drmprime_video_set_window_zpos(drmprime_video_env_t *de, const unsigned int z);

void drmprime_video_delete(drmprime_video_env_t * dve);
drmprime_video_env_t * drmprime_video_new(struct drmprime_out_env_s * dpo);

void drmprime_out_delete(drmprime_out_env_t * dpo);
drmprime_out_env_t * drmprime_out_new();

void drmprime_out_runticker_start(drmprime_out_env_t * const dpo, const char * const ticker_text);
void drmprime_out_runticker_stop(drmprime_out_env_t * const dpo);

void drmprime_out_runcube_start(drmprime_out_env_t * const dpo);
void drmprime_out_runcube_stop(drmprime_out_env_t * const dpo);

