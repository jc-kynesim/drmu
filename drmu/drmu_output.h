#ifndef _DRMU_DRMU_OUTPUT_H
#define _DRMU_DRMU_OUTPUT_H

#include "drmu.h"

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_output_s;
typedef struct drmu_output_s drmu_output_t;

drmu_plane_t * drmu_output_plane_ref_primary(drmu_output_t * const dout);
drmu_plane_t * drmu_output_plane_ref_other(drmu_output_t * const dout);
// Find and ref a plane that supports the given format & mod on the current crtc
// Types is a bit field of acceptable plane types (DRMU_PLANE_TYPE_xxx), 0 => any
//
// add_output must be called before this (so we have a crtc to check against)
drmu_plane_t * drmu_output_plane_ref_format(drmu_output_t * const dout, const unsigned int types, const uint32_t format, const uint64_t mod);

// Add all props accumulated on the output to the atomic
int drmu_atomic_output_add_props(drmu_atomic_t * const da, drmu_output_t * const dout);
// Add activate & CRTC connect props - only needed if output started off disconnected
int drmu_atomic_output_add_connect(drmu_atomic_t * const da, drmu_output_t * const dout);

// Set FB info (bit-depth, HDR metadata etc.)
// Only sets properties that are set in the fb - retains previous value otherwise
int drmu_output_fb_info_set(drmu_output_t * const dout, const drmu_fb_t * const fb);
// Unset all FB info
// (set only sets stuff that is set in the fb, so will never clear anything)
void drmu_output_fb_info_unset(drmu_output_t * const dout);

// Set output mode
int drmu_output_mode_id_set(drmu_output_t * const dout, const int mode_id);

// Width/height of the current mode
const drmu_mode_simple_params_t * drmu_output_mode_simple_params(const drmu_output_t * const dout);

typedef int drmu_mode_score_fn(void * v, const drmu_mode_simple_params_t * mode);

int drmu_output_mode_pick_simple(drmu_output_t * const dout, drmu_mode_score_fn * const score_fn, void * const score_v);

// Simple mode picker cb - looks for width / height and then refresh
// If nothing "plausible" defaults to EDID preferred mode
drmu_mode_score_fn drmu_mode_pick_simple_cb;
// As above but may choose an interlaced mode
drmu_mode_score_fn drmu_mode_pick_simple_interlace_cb;
// Just pick a preferred mode
drmu_mode_score_fn drmu_mode_pick_simple_preferred_cb;

// Allow fb max_bpc info to set the output mode (default false)
int drmu_output_max_bpc_allow(drmu_output_t * const dout, const bool allow);

// Allow fb to set modes generally
int drmu_output_modeset_allow(drmu_output_t * const dout, const bool allow);

// Add a CONN/CRTC pair to an output
// If conn_name == NULL then 1st connected connector is used
// If != NULL then 1st conn with prefix-matching name is used
int drmu_output_add_output(drmu_output_t * const dout, const char * const conn_name);

// Allow _add_output2 to add a disconnected connect & asign a compatible CRTC
// Mode select and active will have to happen later

// Search disconnected conns too; but prefer connected
#define DRMU_OUTPUT_FLAG_ADD_DISCONNECTED       1
// Pick the first one we find; connected or disconnected
#define DRMU_OUTPUT_FLAG_ADD_ANY                2
// Only search disconnected
#define DRMU_OUTPUT_FLAG_ADD_DISCONNECTED_ONLY  4
// Only search writeback connectors; otherwise only search output connectors
#define DRMU_OUTPUT_FLAG_ADD_WRITEBACK          8

// Experimental, more flexible version of _add_output
int drmu_output_add_output2(drmu_output_t * const dout, const char * const conn_name, const unsigned int flags);

// Set writeback fb on output - rotation 0
int drmu_atomic_output_add_writeback_fb(drmu_atomic_t * const da_req, drmu_output_t * const dout,
                                    drmu_fb_t * const dfb);

// Set writeback fb on output - also set rotation on conn
int drmu_atomic_output_add_writeback_fb_rotate(drmu_atomic_t * const da_out, drmu_output_t * const dout,
                                    drmu_fb_t * const dfb, const unsigned int rot);

int drmu_atomic_output_add_writeback_fb_callback(drmu_atomic_t * const da_out, drmu_output_t * const dout,
                                    drmu_fb_t * const dfb, const unsigned int rot,
                                    drmu_fb_fence_fd_fn * const fn, void * const v);

// Add a writeback connector & find a crtc for it
int drmu_output_add_writeback(drmu_output_t * const dout);

// Conn & CRTC for when output isn't fine grained enough
drmu_crtc_t * drmu_output_crtc(const drmu_output_t * const dout);
drmu_conn_t * drmu_output_conn(const drmu_output_t * const dout, const unsigned int n);

// Return the in-use drmu environment
drmu_env_t * drmu_output_env(const drmu_output_t * const dout);

// Create a new empty output - has no crtc or conn
// Takes a ref on the env  (released when the output is deleted)
drmu_output_t * drmu_output_new(drmu_env_t * const du);

// Increment ref count on an output - cannot fail
drmu_output_t * drmu_output_ref(drmu_output_t * const dout);
// Unref an output - delete if ref count now zero
void drmu_output_unref(drmu_output_t ** const ppdout);

#ifdef __cplusplus
}
#endif

#endif


