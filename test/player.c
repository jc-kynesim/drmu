#include "player.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include "drmprime_out.h"


#if AV_TIME_BASE != 1000000
#error Code assumes time base is in us
#endif

typedef struct display_wait_s {
    int64_t base_pts;
    int64_t base_now;
    int64_t last_conv;
} display_wait_t;

static uint64_t
us_time()
{
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void
display_wait_init(display_wait_t * const dw)
{
    memset(dw, 0, sizeof(*dw));
}

static int64_t
frame_pts(const AVFrame * const frame)
{
    return frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
}

static void
display_wait(display_wait_t * const dw, const AVFrame * const frame, const AVRational time_base)
{
    int64_t now = us_time();
    int64_t now_delta = now - dw->base_now;
    int64_t pts = frame_pts(frame);
    int64_t pts_delta = pts - dw->base_pts;
    // If we haven't been given any clues then guess 60fps
    int64_t pts_conv = (pts == AV_NOPTS_VALUE || time_base.den == 0 || time_base.num == 0) ?
        dw->last_conv + 1000000 / 60 :
        av_rescale_q(pts_delta, time_base, (AVRational) {1, 1000000});  // frame->timebase seems invalid currently
    int64_t delta = pts_conv - now_delta;

    dw->last_conv = pts_conv;

//    printf("PTS_delta=%" PRId64 ", Now_delta=%" PRId64 ", TB=%d/%d, Delta=%" PRId64 "\n", pts_delta, now_delta, time_base.num, time_base.den, delta);

    if (delta < 0 || delta > 6000000) {
        dw->base_pts = pts;
        dw->base_now = now;
        return;
    }

    if (delta > 0)
        usleep(delta);
}

typedef struct player_env_s {
    drmprime_video_env_t * dve;
    drmprime_out_env_t * dpo;
    enum AVHWDeviceType hwdev_type;
    AVFormatContext * input_ctx;
#if LIBAVFORMAT_VERSION_MAJOR >= 59
    const AVCodec *decoder;
#else
    AVCodec *decoder;
#endif
    AVCodecContext *decoder_ctx;
    enum AVPixelFormat hw_pix_fmt;
    int video_stream;  // in input_ctx->streams[]

    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;

    long frames;
    long pace_input_hz;
    uint64_t input_t0;
    display_wait_t dw;

    FILE *output_file;
    bool wants_modeset;
} player_env_t;

static int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type)
{
    int err = 0;

    ctx->hw_frames_ctx = NULL;
    // ctx->hw_device_ctx gets freed when we call avcodec_free_context
    if ((err = av_hwdevice_ctx_create(&ctx->hw_device_ctx, type,
                                      NULL, NULL, 0)) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return err;
    }

    return err;
}

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    player_env_t * const pe = ctx->opaque;
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == pe->hw_pix_fmt)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

int
player_decode_video_packet(player_env_t * const pe, AVPacket * const packet)
{
    AVCodecContext *const avctx = pe->decoder_ctx;
    drmprime_video_env_t * const dpo = pe->dve;
    AVFrame *frame = NULL, *sw_frame = NULL;
    uint8_t *buffer = NULL;
    int size;
    int ret = 0;

    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }

    for (;;) {
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }

        // push the decoded frame into the filtergraph if it exists
        if (pe->filter_graph != NULL &&
            (ret = av_buffersrc_add_frame_flags(pe->buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF)) < 0) {
            fprintf(stderr, "Error while feeding the filtergraph\n");
            goto fail;
        }

        do {
            AVRational time_base = pe->input_ctx->streams[pe->video_stream]->time_base;
            if (pe->filter_graph != NULL) {
                av_frame_unref(frame);
                ret = av_buffersink_get_frame(pe->buffersink_ctx, frame);
                if (ret == AVERROR(EAGAIN)) {
                    ret = 0;
                    break;
                }
                if (ret < 0) {
                    if (ret != AVERROR_EOF)
                        fprintf(stderr, "Failed to get frame: %s", av_err2str(ret));
                    goto fail;
                }
                if (pe->wants_modeset)
                    drmprime_video_modeset(dpo, av_buffersink_get_w(pe->buffersink_ctx), av_buffersink_get_h(pe->buffersink_ctx), av_buffersink_get_time_base(pe->buffersink_ctx));
                time_base = av_buffersink_get_time_base(pe->buffersink_ctx);
            }
            else if (pe->wants_modeset) {
                drmprime_video_modeset(dpo, avctx->coded_width, avctx->coded_height, avctx->framerate);
            }

            display_wait(&pe->dw, frame, time_base);
            drmprime_video_display(dpo, frame);

            if (pe->output_file != NULL) {
                AVFrame *tmp_frame;

                if (frame->format == pe->hw_pix_fmt) {
                    /* retrieve data from GPU to CPU */
                    if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                        fprintf(stderr, "Error transferring the data to system memory\n");
                        goto fail;
                    }
                    tmp_frame = sw_frame;
                } else
                    tmp_frame = frame;

                size = av_image_get_buffer_size(tmp_frame->format, tmp_frame->width,
                                                tmp_frame->height, 1);
                buffer = av_malloc(size);
                if (!buffer) {
                    fprintf(stderr, "Can not alloc buffer\n");
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                ret = av_image_copy_to_buffer(buffer, size,
                                              (const uint8_t * const *)tmp_frame->data,
                                              (const int *)tmp_frame->linesize, tmp_frame->format,
                                              tmp_frame->width, tmp_frame->height, 1);
                if (ret < 0) {
                    fprintf(stderr, "Can not copy image to buffer\n");
                    goto fail;
                }

                if ((ret = fwrite(buffer, 1, size, pe->output_file)) < 0) {
                    fprintf(stderr, "Failed to dump raw data.\n");
                    goto fail;
                }
            }
        } while (pe->buffersink_ctx != NULL);  // Loop if we have a filter to drain

        if (pe->frames >= 0 && (pe->frames == 0 || --pe->frames == 0))
            ret = -1;

    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);
        if (ret < 0)
            return ret;
    }
    return 0;
}

// read video packet from input & discard any other packets
// Packet must bot contain data that needs freeing
int
player_read_video_packet(player_env_t * const pe, AVPacket * const packet)
{
    int ret;
    uint64_t now;

    for (;;) {
        if ((ret = av_read_frame(pe->input_ctx, packet)) < 0)
            return ret;
        if (pe->video_stream == packet->stream_index)
            break;
        av_packet_unref(packet);
    }

    if (pe->pace_input_hz <= 0)
        return 0;

    now = us_time();
    if (now < pe->input_t0)
        usleep(pe->input_t0 - now);
    else
        pe->input_t0 = now;

    pe->input_t0 += 1000000 / pe->pace_input_hz;
    return 0;
}

int
player_run_one_packet(player_env_t * const pe)
{
    int rv;
    AVPacket packet;

    if ((rv = player_read_video_packet(pe, &packet)) < 0)
        return rv;

    rv = player_decode_video_packet(pe, &packet);
    av_packet_unref(&packet);
    return rv;
}

int
player_run_eos(player_env_t * const pe)
{
    int rv;
    AVPacket packet = {.buf = NULL};

    rv = player_decode_video_packet(pe, &packet);
    av_packet_unref(&packet);
    return rv;
}


void
player_set_write_frame_count(player_env_t * const pe, long frame_count)
{
    pe->frames = frame_count;
}

void
player_set_input_pace_hz(player_env_t * const pe, long hz)
{
    pe->pace_input_hz = hz;
}

void
player_set_modeset(player_env_t * const pe, bool modeset)
{
    pe->wants_modeset = modeset;
}

// File not closed by player
void
player_set_output_file(player_env_t * const pe, FILE * output_file)
{
    pe->output_file = output_file;
}


int
player_filter_add_deinterlace(player_env_t * const pe)
{
    const AVStream * const stream = pe->input_ctx->streams[pe->video_stream];
    const AVCodecContext *const dec_ctx = pe->decoder_ctx;
    const char * const filters_descr = "deinterlace_v4l2m2m";

    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = stream->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_DRM_PRIME, AV_PIX_FMT_NONE };

    pe->filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !pe->filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            time_base.num, time_base.den,
            dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&pe->buffersrc_ctx, buffersrc, "in",
                                       args, NULL, pe->filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&pe->buffersink_ctx, buffersink, "out",
                                       NULL, NULL, pe->filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(pe->buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = pe->buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = pe->buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(pe->filter_graph, filters_descr,
                                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(pe->filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

int
player_seek(player_env_t * const pe, uint64_t seek_pos_us)
{
    // Allow 0.1s before in case of rounding error in position
    return avformat_seek_file(pe->input_ctx, -1,
                              seek_pos_us < AV_TIME_BASE / 10 ? 0 : seek_pos_us - AV_TIME_BASE / 10,
                              seek_pos_us,
                              INT64_MAX, 0);
}

void
player_close_file(player_env_t * const pe)
{
    avfilter_graph_free(&pe->filter_graph);
    avcodec_free_context(&pe->decoder_ctx);
    avformat_close_input(&pe->input_ctx);
}

static int player_get_buffer2(struct AVCodecContext *s, struct AVFrame *frame, int flags)
{
    player_env_t * const pe = s->opaque;
    return drmprime_video_get_buffer2(pe->dve, s, frame, flags);
}

int
player_open_file(player_env_t * const pe, const char * const fname)
{
    bool try_hw = true;
    AVStream *video = NULL;
    int ret;

    display_wait_init(&pe->dw);

    /* open the input file */
    if (avformat_open_input(&pe->input_ctx, fname, NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", fname);
        return -1;
    }

    if (avformat_find_stream_info(pe->input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

retry_hw:
    /* find the video stream information */
    ret = av_find_best_stream(pe->input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &pe->decoder, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    pe->video_stream = ret;

    pe->hw_pix_fmt = AV_PIX_FMT_NONE;
    if (!try_hw) {
        /* Nothing */
    }
    else if (pe->decoder->id == AV_CODEC_ID_H264) {
        if ((pe->decoder = avcodec_find_decoder_by_name("h264_v4l2m2m")) == NULL)
            fprintf(stderr, "Cannot find the h264 v4l2m2m decoder\n");
        else
            pe->hw_pix_fmt = AV_PIX_FMT_DRM_PRIME;
    }
    else {
        unsigned int i;

        for (i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(pe->decoder, i);
            if (!config) {
                fprintf(stderr, "Decoder %s does not support device type %s.\n",
                        pe->decoder->name, av_hwdevice_get_type_name(pe->hwdev_type));
                break;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == pe->hwdev_type) {
                pe->hw_pix_fmt = config->pix_fmt;
                break;
            }
        }
    }

    if (pe->hw_pix_fmt == AV_PIX_FMT_NONE && try_hw) {
        fprintf(stderr, "No h/w format found - trying s/w\n");
        try_hw = false;
    }

    if (!(pe->decoder_ctx = avcodec_alloc_context3(pe->decoder)))
        return AVERROR(ENOMEM);

    video = pe->input_ctx->streams[pe->video_stream];
    if (avcodec_parameters_to_context(pe->decoder_ctx, video->codecpar) < 0)
        return -1;

    pe->decoder_ctx->opaque = pe;
    if (try_hw) {
        pe->decoder_ctx->get_format = get_hw_format;

        if (hw_decoder_init(pe->decoder_ctx, pe->hwdev_type) < 0)
            return -1;

        pe->decoder_ctx->pix_fmt = AV_PIX_FMT_DRM_PRIME;
        pe->decoder_ctx->sw_pix_fmt = AV_PIX_FMT_NONE;
        pe->decoder_ctx->thread_count = 3;
    }
    else {
        pe->decoder_ctx->get_buffer2 = player_get_buffer2;
        pe->decoder_ctx->thread_count = 0; // FFmpeg will pick a default
    }
    pe->decoder_ctx->flags = 0;
    // Pick any threading method
    pe->decoder_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

#if LIBAVCODEC_VERSION_MAJOR < 60
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    pe->decoder_ctx->thread_safe_callbacks = 1;
#pragma GCC diagnostic pop
#endif

    if ((ret = avcodec_open2(pe->decoder_ctx, pe->decoder, NULL)) < 0) {
        if (try_hw) {
            try_hw = false;
            avcodec_free_context(&pe->decoder_ctx);

            printf("H/w init failed - trying s/w\n");
            goto retry_hw;
        }
        fprintf(stderr, "Failed to open codec for stream #%u\n", pe->video_stream);
        return -1;
    }

    printf("Pixfmt after init: %s / %s\n", av_get_pix_fmt_name(pe->decoder_ctx->pix_fmt), av_get_pix_fmt_name(pe->decoder_ctx->sw_pix_fmt));

    return 0;
}

int
player_set_hwdevice_by_name(player_env_t * const pe, const char * const hwdev)
{
    enum AVHWDeviceType type;
    type = av_hwdevice_find_type_by_name(hwdev);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", hwdev);
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }
    pe->hwdev_type = type;
    return 0;
}

void
player_set_window(player_env_t * const pe, unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int z)
{
    drmprime_video_set_window_size(pe->dve, w, h);
    drmprime_video_set_window_pos(pe->dve, x, y);
    drmprime_video_set_window_zpos(pe->dve, z);
}

player_env_t *
player_new(drmprime_out_env_t * const dpo)
{
    player_env_t * pe = calloc(1, sizeof(*pe));

    if (pe == NULL)
        return NULL;

    pe->dpo = dpo;
    if ((pe->dve = drmprime_video_new(dpo)) == NULL) {
        fprintf(stderr, "%s: Failed to create video out", __func__);
        free(pe);
        return NULL;
    }
    pe->frames = -1;

    return pe;
}

void
player_delete(player_env_t ** ppPe)
{
    player_env_t * pe = *ppPe;

    if (pe == NULL)
        return;

    player_close_file(pe);
    drmprime_video_delete(pe->dve);
    free(pe);
}


