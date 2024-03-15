#include <libavutil/rational.h>

struct drmprime_out_env_s;
typedef struct drmprime_out_env_s drmprime_out_env_t;

struct AVFrame;
struct AVCodecContext;

int drmprime_out_get_buffer2(struct AVCodecContext *s, struct AVFrame *frame, int flags);
int drmprime_out_display(drmprime_out_env_t * dpo, struct AVFrame * frame);
int drmprime_out_modeset(drmprime_out_env_t *dpo, int w, int h, const AVRational rate);
void drmprime_out_delete(drmprime_out_env_t * dpo);
drmprime_out_env_t * drmprime_out_new();

void drmprime_out_runticker_start(drmprime_out_env_t * const dpo, const char * const ticker_text);
void drmprime_out_runticker_stop(drmprime_out_env_t * const dpo);

void drmprime_out_runcube_start(drmprime_out_env_t * const dpo);
void drmprime_out_runcube_stop(drmprime_out_env_t * const dpo);

