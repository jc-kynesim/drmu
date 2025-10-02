#ifndef _DRMU_TEST_PLAYER_H
#define _DRMU_TEST_PLAYER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct player_env_s;
typedef struct player_env_s player_env_t;

struct drmprime_out_env_s;
struct AVPacket;

typedef enum player_output_pace_mode_e {
    PLAYER_PACE_INVALID = -1,
    PLAYER_PACE_PTS = 0,
    PLAYER_PACE_FREE,
    PLAYER_PACE_VSYNC
} player_output_pace_mode_t;

int player_decode_video_packet(player_env_t * const pe, struct AVPacket * const packet);
int player_read_video_packet(player_env_t * const pe, struct AVPacket * const packet);

// Runs player_read_video_packet then player_decode_video_packet once
// (avoids need to include avlib)
int player_run_one_packet(player_env_t * const pe);
// Sends an empty packet to player_decode_video_packet
int player_run_eos(player_env_t * const pe);

void player_set_write_frame_count(player_env_t * const pe, long frame_count);
void player_set_input_pace_hz(player_env_t * const pe, long hz);
int player_set_rotation(player_env_t * const pe, unsigned int rot);
player_output_pace_mode_t player_str_to_output_pace_mode(const char * const str);
void player_set_output_pace_mode(player_env_t * const pe, const player_output_pace_mode_t mode);
void player_set_modeset(player_env_t * const pe, bool modeset);
void player_set_output_file(player_env_t * const pe, FILE * output_file);
int player_filter_add_deinterlace(player_env_t * const pe);
int player_seek(player_env_t * const pe, uint64_t seek_pos_us);
void player_close_file(player_env_t * const pe);
int player_open_file(player_env_t * const pe, const char * const fname);
int player_set_hwdevice_by_name(player_env_t * const pe, const char * const hwdev);
void player_set_window(player_env_t * const pe, unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int z);
player_env_t * player_new(struct drmprime_out_env_s * const dpo);
void player_delete(player_env_t ** ppPe);

#ifdef __cplusplus
}
#endif

#endif
