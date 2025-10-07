/*
 * Copyright (c) 2017 Jun Zhao
 * Copyright (c) 2017 Kaixuan Liu
 *
 * HW Acceleration API (video decoding) decode sample
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * HW-Accelerated decoding example.
 *
 * @example hw_decode.c
 * This example shows how to do HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */

#include <pthread.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "drmprime_out.h"
#include "drmu_util.h"
#include "player.h"

#include <libavcodec/packet.h>

// ---------------------------------------------------------------------------

typedef struct playlist_s {
    // Cmd line vars
    int zpos;
    int x, y;
    unsigned int w, h;
    const char * out_name;

    unsigned int in_count;
    const char * const * in_filelist;

    uint64_t seek_start;
    long loop_count;
    long frame_count;
    long pace_input_hz;
    player_output_pace_mode_t pace_output_mode;
    bool wants_deinterlace;
    bool wants_modeset;
    const char * hwdev;
    unsigned int rotation;

    // run vars
    FILE *output_file;
    player_env_t * pe;
    pthread_t thread_id;

} playlist_t;

typedef struct playlist_env_s {
    playlist_t ** pla;
    size_t n;
    size_t alloc;
} playlist_env_t;

static playlist_t *
playlist_new(playlist_env_t * ple)
{
    playlist_t * pl;

    if (ple->n >= ple->alloc) {
        size_t n = ple->alloc == 0 ? 4 : ple->alloc * 2;
        playlist_t **pla = realloc(ple->pla, sizeof(*ple->pla) * n);
        if (pla == NULL)
            return NULL;
        ple->alloc = n;
        ple->pla = pla;
    }
    if ((pl = calloc(1, sizeof(*pl))) == NULL)
        return NULL;

    pl->loop_count = 1;
    pl->frame_count = -1;
    pl->hwdev = "drm";
    pl->zpos = ple->n;
    pl->pace_output_mode = PLAYER_PACE_PTS;
    pl->rotation = 0;

    ple->pla[ple->n++] = pl;
    return pl;
}

static void
playlist_env_init(playlist_env_t * ple)
{
    memset(ple, 0, sizeof(*ple));
}

static void
playlist_env_uninit(playlist_env_t * ple)
{
    size_t i;
    for (i = 0; i != ple->n; ++i)
        free(ple->pla[i]);
    free(ple->pla);
    playlist_env_init(ple);
}

static int
playlist_set_win(playlist_t * const pl, const char * arg)
{
    char * p = (char *)arg;

    pl->w = strtoul(p, &p, 0);
    if (*p++ != 'x' || pl->w == 0)
        return -1;
    pl->h = strtoul(p, &p, 0);
    if (*p++ != '@' || pl->h == 0)
        return -1;
    pl->x = strtoul(p, &p, 0);
    if (*p++ != ',')
        return -1;
    pl->y = strtoul(p, &p, 0);
    if (*p++ != '\0')
        return -1;
    return 0;
}

static int
playlist_set_rot(playlist_t * const pl, const char * arg)
{
    const char * p = arg;
    unsigned int n = drmu_util_str_to_rotation(arg, (char**)&p);

    if (p == arg || *p != '\0')
        return -1;

    pl->rotation = n;
    return 0;
}

static void *
playlist_thread(void *v)
{
    playlist_t * const pl = v;
    player_env_t * const pe = pl->pe;
    unsigned int in_n = 0;
    const char * in_file;
    bool play0;

loopy:
    in_file = pl->in_filelist[in_n];
    if (++in_n >= pl->in_count)
        in_n = 0;

    player_open_file(pe, in_file);

    if (pl->wants_deinterlace) {
        if (player_filter_add_deinterlace(pe)) {
            fprintf(stderr, "Failed to init deinterlace\n");
            return NULL;
        }
    }

    play0 = true;
reseek:
    if (!play0 || pl->seek_start != 0) {
        if (player_seek(pe, pl->seek_start) != 0)
            fprintf(stderr, "Seek failed to %d.%06d\n", (int)(pl->seek_start / 1000000), (int)(pl->seek_start % 1000000));
    }
    play0 = false;

    /* actual decoding and dump the raw data */
    player_set_write_frame_count(pe, pl->frame_count);
    player_set_input_pace_hz(pe, pl->pace_input_hz);

    while (player_run_one_packet(pe) >= 0)
        /* loop */;

    // Do not close & reopen if looping within a single file
    if (pl->in_count == 1 &&
        (pl->loop_count == -1 ||
         (pl->loop_count != 0 && --pl->loop_count > 0)))
        goto reseek;

    player_run_eos(pe);

    player_close_file(pe);

    if (pl->loop_count == -1 ||
        (pl->loop_count != 0 && --pl->loop_count > 0))
        goto loopy;

    return NULL;
}

static int
playlist_run(playlist_t * const pl)
{
    if (pthread_create(&pl->thread_id, NULL, playlist_thread, pl) != 0) {
        int rv = -errno;
        fprintf(stderr, "%s: Failed to create player thread", __func__);
        return rv;
    }
    return 0;
}

static int
playlist_wait(playlist_t * const pl)
{
    pthread_join(pl->thread_id, NULL);
    return 0;
}

static int get_time_arg(const char * const arg, uint64_t * pTime)
{
    char * p = (char*)arg;
    uint64_t t = strtoul(p, &p, 0) * (uint64_t)1000000;
    if (*p == '.')
        t += strtoul(p + 1, &p, 0);
    if (*p != '\0')
        return -1;
    *pTime = t;
    return 0;
}

void usage()
{
    fprintf(stderr,
"Usage: hello_drmprime [--ticker <text>]\n"
"                      [--cube]\n"
"                      [--tile]\n"
"                      <playlist0> [: <playlist1> [: ...]]\n"
" <playlist> = [--win <w>x<h>@<x>,<y>]\n"
"              [--rot 0|90|180|270|T|180T|X|Y]\n"
"              [-l <loop_count>] [-f <frames>] [-o yuv_output_file]\n"
"              [--deinterlace] [--pace-input <hz>] [--modeset]\n"
"              <input file> [<input_file> ...]\n"
"\n"
"The --tile option will tile the video windows, if unset then playlist1 and\n"
"later must have the --win option\n"
"If loop count is set then the playlist will be repeated that many times, a\n"
"loop count of -1 means forever\n"
"N.B. frame counts and similar options are currently global to a playlist\n"
"so generally do not work well with multiple input files in a playlist.\n"
            );
    exit(1);
}

int main(int argc, char *argv[])
{
    drmprime_out_env_t * dpo;
    bool wants_cube = false;
    bool tile_video = false;
    const char * ticker_text = NULL;
    unsigned int screen_width, screen_height;
    unsigned int tiles_w = 1, tiles_h = 1;

    playlist_env_t ple;
    playlist_t * pl = NULL;

    playlist_env_init(&ple);

    {
        const char * const * a = (const char * const *)argv + 1;
        int n = argc - 1;
        bool is_file = false;

        while (n-- > 0) {
            const char *arg = *a++;
            char *e;

            if (pl == NULL &&
                (pl = playlist_new(&ple)) == NULL) {
                fprintf(stderr, "Unable to allocate playlist %zd\n", ple.n);
                return -1;
            }

            if (arg[0] != '-')
                is_file = true;

            if (is_file) {
                if (strcmp(arg, ":") == 0) {
                    is_file = false;
                    pl = NULL;
                }
                else {
                    if (pl->in_filelist == NULL)
                        pl->in_filelist = a - 1;
                    ++pl->in_count;
                }
                continue;
            }

            if (strcmp(arg, "--win") == 0) {
                if (n == 0)
                    usage();
                if (playlist_set_win(pl, *a) != 0) {
                    fprintf(stderr, "Bad window <w>x<h>@<x>,<y>: '%s'", *a);
                    return -1;
                }
                --n;
                ++a;
            }
            else if (strcmp(arg, "--rot") == 0) {
                if (n == 0)
                    usage();
                if (playlist_set_rot(pl, *a) != 0) {
                    fprintf(stderr, "Bad rotation: '%s'", *a);
                    return -1;
                }
                --n;
                ++a;
            }
            else if (strcmp(arg, "--seek") == 0) {
                if (n == 0)
                    usage();
                if (get_time_arg(*a, &pl->seek_start) != 0)
                    usage();
                --n;
                ++a;
            }
            else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--loop") == 0) {
                if (n == 0)
                    usage();
                pl->loop_count = strtol(*a, &e, 0);
                if (*e != 0)
                    usage();
                --n;
                ++a;
            }
            else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--frames") == 0) {
                if (n == 0)
                    usage();
                pl->frame_count = strtol(*a, &e, 0);
                if (*e != 0)
                    usage();
                --n;
                ++a;
            }
            else if (strcmp(arg, "-o") == 0) {
                if (n == 0)
                    usage();
                pl->out_name = *a;
                --n;
                ++a;
            }
            else if (strcmp(arg, "--pace-input") == 0) {
                if (n == 0)
                    usage();
                pl->pace_input_hz = strtol(*a, &e, 0);
                if (*e != 0)
                    usage();
                --n;
                ++a;
            }
            else if (strcmp(arg, "--pace-output") == 0) {
                if (n == 0)
                    usage();
                pl->pace_output_mode = player_str_to_output_pace_mode(*a);
                if (pl->pace_output_mode == PLAYER_PACE_INVALID)
                    usage();
                --n;
                ++a;
            }
            else if (strcmp(arg, "--deinterlace") == 0) {
                pl->wants_deinterlace = true;
            }
            else if (strcmp(arg, "--cube") == 0) {
                wants_cube = true;
            }
            else if (strcmp(arg, "--modeset") == 0) {
                pl->wants_modeset = true;
            }
            else if (strcmp(arg, "--ticker") == 0) {
                if (n == 0)
                    usage();
                ticker_text = *a;
                --n;
                ++a;
            }
            else if (strcmp(arg, "--tile") == 0) {
                tile_video = true;
            }
            else if (strcmp(arg, "--") == 0) {
                is_file = true;
            }
            else
                usage();
        }

        if (ple.n == 0)
            usage();
    }

    dpo = drmprime_out_new();
    if (dpo == NULL) {
        fprintf(stderr, "Failed to open drmprime output\n");
        return 1;
    }

    drmprime_out_size(dpo, &screen_width, &screen_height);
    if (tile_video) {
        while (ple.n > tiles_w * tiles_w)
            ++tiles_w;
        tiles_h = (ple.n + tiles_w - 1) / tiles_w;
    }

    // Finalize arguments in playlists
    for (size_t i = 0; i != ple.n; ++i) {
        pl = ple.pla[i];
        if (pl->in_count == 0)
            usage();
        if (pl->loop_count > 0)
            pl->loop_count *= pl->in_count;
        if (pl->w == 0) {
            if (!tile_video && i != 0) {
                fprintf(stderr, "Playlist %zd needs a window", i);
                return -1;
            }
            pl->w = screen_width / tiles_w;
            pl->h = screen_height / tiles_h;
            pl->x = pl->w * (i % tiles_w);
            pl->y = pl->h * (i / tiles_w);
        }

        /* open the file to dump raw data */
        if (pl->out_name != NULL) {
            if ((pl->output_file = fopen(pl->out_name, "w+")) == NULL) {
                fprintf(stderr, "Failed to open output file %s: %s\n", pl->out_name, strerror(errno));
                return -1;
            }
        }
    }

    for (size_t i = 0; i != ple.n; ++i) {
        pl = ple.pla[i];

        /* open the file to dump raw data */
        if (pl->out_name != NULL) {
            if ((pl->output_file = fopen(pl->out_name, "w+")) == NULL) {
                fprintf(stderr, "Failed to open output file %s: %s\n", pl->out_name, strerror(errno));
                return -1;
            }
        }

        if ((pl->pe = player_new(dpo)) == NULL) {
            fprintf(stderr, "Failed to create player");
            return -1;
        }
        if (player_set_hwdevice_by_name(pl->pe, pl->hwdev) != 0)
            return -1;
        player_set_modeset(pl->pe, pl->wants_modeset);
        player_set_rotation(pl->pe, pl->rotation);
        player_set_output_file(pl->pe, pl->output_file);
        player_set_window(pl->pe, pl->x, pl->y, pl->w, pl->h, pl->zpos);
        player_set_output_pace_mode(pl->pe, pl->pace_output_mode);

        playlist_run(pl);
    }

    if (wants_cube)
        drmprime_out_runcube_start(dpo);

    if (ticker_text != NULL)
        drmprime_out_runticker_start(dpo, ticker_text);


    for (size_t i = 0; i != ple.n; ++i) {
        pl = ple.pla[i];

        playlist_wait(pl);
        if (pl->output_file)
            fclose(pl->output_file);
        player_delete(&pl->pe);
    }
    drmprime_out_delete(dpo);

    playlist_env_uninit(&ple);

    return 0;
}
