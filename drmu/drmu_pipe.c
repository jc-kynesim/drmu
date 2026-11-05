#include "drmu_pipe.h"

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_props.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include <libdrm/drm_mode.h>

#ifndef DRM_MODE_OBJECT_COLOROP
#define DRM_MODE_OBJECT_COLOROP 0xfafafafa

struct drm_color_ctm {
	/*
	 * Conversion matrix in S31.32 sign-magnitude
	 * (not two's complement!) format.
	 *
	 * out   matrix    in
	 * |R|   |0 1 2|   |R|
	 * |G| = |3 4 5| x |G|
	 * |B|   |6 7 8|   |B|
	 */
	__u64 matrix[9];
};

struct drm_color_ctm_3x4 {
	/*
	 * Conversion matrix with 3x4 dimensions in S31.32 sign-magnitude
	 * (not two's complement!) format.
	 *
	 * out   matrix          in
	 * |R|   |0  1  2  3 |   | R |
	 * |G| = |4  5  6  7 | x | G |
	 * |B|   |8  9  10 11|   | B |
	 *                       |1.0|
	 */
	__u64 matrix[12];
};

struct drm_color_lut {
	/*
	 * Values are mapped linearly to 0.0 - 1.0 range, with 0x0 == 0.0 and
	 * 0xffff == 1.0.
	 */
	__u16 red;
	__u16 green;
	__u16 blue;
	__u16 reserved;
};

/*
 * struct drm_color_lut32
 *
 * 32-bit per channel color LUT entry, similar to drm_color_lut.
 */
struct drm_color_lut32 {
	__u32 red;
	__u32 green;
	__u32 blue;
	__u32 reserved;
};

/**
 * enum drm_colorop_type - Type of color operation
 *
 * drm_colorops can be of many different types. Each type behaves differently
 * and defines a different set of properties. This enum defines all types and
 * gives a high-level description.
 */
enum drm_colorop_type {
	/**
	 * @DRM_COLOROP_1D_CURVE:
	 *
	 * enum string "1D Curve"
	 *
	 * A 1D curve that is being applied to all color channels. The
	 * curve is specified via the CURVE_1D_TYPE colorop property.
	 */
	DRM_COLOROP_1D_CURVE,

	/**
	 * @DRM_COLOROP_1D_LUT:
	 *
	 * enum string "1D LUT"
	 *
	 * A simple 1D LUT of uniformly spaced &drm_color_lut32 entries,
	 * packed into a blob via the DATA property. The driver's
	 * expected LUT size is advertised via the SIZE property.
	 *
	 * The DATA blob is an array of struct drm_color_lut32 with size
	 * of "size".
	 */
	DRM_COLOROP_1D_LUT,

	/**
	 * @DRM_COLOROP_CTM_3X4:
	 *
	 * enum string "3x4 Matrix"
	 *
	 * A 3x4 matrix. Its values are specified via the
	 * &drm_color_ctm_3x4 struct provided via the DATA property.
	 *
	 * The DATA blob is a float[12]:
	 * out   matrix          in
	 * | R |   | 0  1  2  3  |   | R |
	 * | G | = | 4  5  6  7  | x | G |
	 * | B |   | 8  9  10 12 |   | B |
	 */
	DRM_COLOROP_CTM_3X4,

	/**
	 * @DRM_COLOROP_MULTIPLIER:
	 *
	 * enum string "Multiplier"
	 *
	 * A simple multiplier, applied to all color values. The
	 * multiplier is specified as a S31.32 via the MULTIPLIER
	 * property.
	 */
	DRM_COLOROP_MULTIPLIER,

	/**
	 * @DRM_COLOROP_3D_LUT:
	 *
	 * enum string "3D LUT"
	 *
	 * A 3D LUT of &drm_color_lut32 entries,
	 * packed into a blob via the DATA property. The driver's expected
	 * LUT size is advertised via the SIZE property, i.e., a 3D LUT with
	 * 17x17x17 entries will have SIZE set to 17.
	 *
	 * The DATA blob is a 3D array of struct drm_color_lut32 with dimension
	 * length of "size".
	 * The LUT elements are traversed like so:
	 *
	 *   for B in range 0..n
	 *     for G in range 0..n
	 *       for R in range 0..n
	 *        index = R + n * (G + n * B)
	 *         color = lut3d[index]
	 */
	DRM_COLOROP_3D_LUT,
};

/**
 * enum drm_colorop_lut3d_interpolation_type - type of 3DLUT interpolation
 */
enum drm_colorop_lut3d_interpolation_type {
	/**
	 * @DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL:
	 *
	 * Tetrahedral 3DLUT interpolation
	 */
	DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL,
};

/**
 * enum drm_colorop_lut1d_interpolation_type - type of interpolation for 1D LUTs
 */
enum drm_colorop_lut1d_interpolation_type {
	/**
	 * @DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR:
	 *
	 * Linear interpolation. Values between points of the LUT will be
	 * linearly interpolated.
	 */
	DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
};
#endif

// MUST be same order as enum
const char * const colorop_type_names[DRMU_COLOROP_TYPE_COUNT] = {
    "????",
    "1D Curve",
    "1D LUT",
    "3x4 Matrix",
    "Multiplier",
    "3D LOT",
};

drmu_colorop_type_t
colorop_type_name_to_n(const char * name)
{
    unsigned int i;

    for (i = 1; i != (unsigned int)DRMU_COLOROP_TYPE_COUNT; ++i)
        if (strcmp(name, colorop_type_names[i]) == 0)
            return (drmu_colorop_type_t)i;
    return DRMU_COLOROP_TYPE_UNKNOWN;
}

struct drmu_colorop_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init

    drmu_colorop_type_t type;
    bool bypass;

    union {
        struct {
            uint64_t curve;
        } curve_1d;
        struct {
            int64_t val;
        } mul;
    } op;
};

static void
colorop_free(drmu_colorop_t * const cop)
{
    switch (cop->type) {
        case DRMU_COLOROP_TYPE_1D_CURVE:
        case DRMU_COLOROP_TYPE_MULTIPLIER:
        case DRMU_COLOROP_TYPE_UNKNOWN:
        case DRMU_COLOROP_TYPE_1D_LUT:
        case DRMU_COLOROP_TYPE_3X4_MATRIX:
        case DRMU_COLOROP_TYPE_3D_LUT:
        case DRMU_COLOROP_TYPE_COUNT:  // Avoid warning
            break;
    }

    free(cop);
}
void
drmu_colorop_unref(drmu_colorop_t ** const ppDco)
{
    drmu_colorop_t * const dco = *ppDco;

    if (dco == NULL)
        return;
    *ppDco = NULL;

    if (atomic_fetch_sub(&dco->ref_count, 1) != 0)
        return;

    colorop_free(dco);
}

drmu_colorop_t *
drmu_colorop_ref(drmu_colorop_t * const dco)
{
    if (dco != NULL)
        atomic_fetch_add(&dco->ref_count, 1);
    return dco;
}


drmu_colorop_t *
drmu_colorop_new(drmu_colorop_type_t type)
{
    drmu_colorop_t * dco = calloc(1, sizeof(*dco));

    if (dco == NULL)
            return NULL;

    dco->type = type;
    dco->bypass = true;

    return dco;
}


struct drmu_pipe_colorop_s {
    drmu_props_t * props;
    drmu_colorop_type_t type;
    uint32_t next_id;
    bool bypass;
    drmu_prop_enum_t * type_enum;

    union {
        struct {
            drmu_prop_enum_t * curve_enum;
        } curve_1d;
        struct {
            drmu_prop_range_t * range;
            uint64_t val;
        } mul;
    } op;
};

static void
pipe_colorop_free(drmu_pipe_colorop_t * const cop)
{
    if (cop == NULL)
        return;

    switch (cop->type) {
        case DRMU_COLOROP_TYPE_1D_CURVE:
            drmu_prop_enum_delete(&cop->op.curve_1d.curve_enum);
            break;
        case DRMU_COLOROP_TYPE_MULTIPLIER:
            drmu_prop_range_delete(&cop->op.mul.range);
            break;
        case DRMU_COLOROP_TYPE_UNKNOWN:
        case DRMU_COLOROP_TYPE_1D_LUT:
        case DRMU_COLOROP_TYPE_3X4_MATRIX:
        case DRMU_COLOROP_TYPE_3D_LUT:
        case DRMU_COLOROP_TYPE_COUNT:  // Avoid warning
            break;
    }

    drmu_props_unref(&cop->props);
    drmu_prop_enum_delete(&cop->type_enum);
    free(cop);
}

static uint32_t
pipe_colorop_next_id(const drmu_pipe_colorop_t * const pco)
{
    return pco->next_id;
}

static drmu_pipe_colorop_t *
pipe_colorop_new(drmu_env_t * const du, const uint32_t pid)
{
    drmu_pipe_colorop_t * cop = calloc(1, sizeof(*cop));
    uint64_t type_val;
    const char * type_name;

    if (cop == NULL)
        return NULL;

    if ((cop->props = drmu_props_new(du, pid, DRM_MODE_OBJECT_ANY)) == NULL) {
        drmu_err(du, "%s: Cannot retrieve props for id %u", __func__, pid);
        goto fail;
    }

    drmu_props_dump(cop->props);

    cop->next_id = (uint32_t)drmu_propinfo_val(drmu_props_name_to_propinfo(cop->props, "NEXT"));
    cop->bypass = drmu_propinfo_val(drmu_props_name_to_propinfo(cop->props, "BYPASS")) != 0;
    type_val = drmu_propinfo_val(drmu_props_name_to_propinfo(cop->props, "TYPE"));
    cop->type_enum = drmu_prop_enum_new(du, drmu_props_name_to_id(cop->props, "TYPE"));
    if ((type_name = drmu_prop_enum_value_name(cop->type_enum, type_val)) == NULL) {
        drmu_err(du, "%s: Cannot get type", __func__);
        goto fail;
    }
    cop->type = colorop_type_name_to_n(type_name);

    switch (cop->type) {
        case DRMU_COLOROP_TYPE_1D_CURVE:
            if ((cop->op.curve_1d.curve_enum = drmu_prop_enum_new(du, drmu_props_name_to_id(cop->props, "CURVE_1D_TYPE"))) == NULL) {
                drmu_err(du, "1d Curve type missing");
                goto fail;
            }
            break;
        case DRMU_COLOROP_TYPE_MULTIPLIER:
            if ((cop->op.mul.range = drmu_prop_range_new(du, drmu_props_name_to_id(cop->props, "MULTIPLIER"))) == NULL) {
                drmu_err(du, "Multiplier range missing");
                goto fail;
            }
            break;
        case DRMU_COLOROP_TYPE_UNKNOWN:
        case DRMU_COLOROP_TYPE_1D_LUT:
        case DRMU_COLOROP_TYPE_3X4_MATRIX:
        case DRMU_COLOROP_TYPE_3D_LUT:
        case DRMU_COLOROP_TYPE_COUNT:  // Avoid warning
            drmu_warn(du, "Unsupported type %"PRId64"->%d %s", type_val, (int)cop->type, type_name);
            break;
    }

    return cop;

fail:
    pipe_colorop_free(cop);
    return NULL;
}

struct drmu_pipe_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init
    drmu_env_t * du;

    unsigned int cop_n;
    unsigned int cop_size;
    drmu_pipe_colorop_t **cops;
};

static void
pipe_free(drmu_pipe_t * pipe)
{
    unsigned int i;

    for (i = 0; i != pipe->cop_n; ++i)
        pipe_colorop_free(pipe->cops[i]);
    free(pipe->cops);
    free(pipe);
}

void
drmu_pipe_unref(drmu_pipe_t ** const ppPipe)
{
    drmu_pipe_t * const pipe = *ppPipe;

    if (pipe == NULL)
        return;
    *ppPipe = NULL;

    if (atomic_fetch_sub(&pipe->ref_count, 1) != 0)
        return;

    pipe_free(pipe);
}

drmu_pipe_t *
drmu_pipe_ref(drmu_pipe_t * const pipe)
{
    if (pipe != NULL)
        atomic_fetch_add(&pipe->ref_count, 1);
    return pipe;
}

drmu_pipe_t *
drmu_pipe_new_pid(struct drmu_env_s * const du, const uint32_t pid0)
{
    unsigned int i;
    drmu_pipe_t * pipe;
    uint32_t pid = pid0;

    if (pid == 0)
        return NULL;

    if ((pipe = calloc(1, sizeof(*pipe))) == NULL) {
        drmu_err(du, "Pipe struct failed");
        return NULL;
    }

    atomic_init(&pipe->ref_count, 0);

    for (i = 0; pid != 0; ++i) {
        if (i >= pipe->cop_size) {
            size_t new_n = pipe->cop_size < 8 ? 8 : pipe->cop_size * 2;
            void * new_a = realloc(pipe->cops, new_n * sizeof(*pipe->cops));
            if (new_a == NULL) {
                drmu_err(du, "Pipe cop alloc failed");
                goto fail;
            }
            pipe->cop_size = new_n;
            pipe->cops = new_a;
        }

        if ((pipe->cops[i] = pipe_colorop_new(du, pid)) == NULL) {
            drmu_err(du, "Pipe colorop[%d] failed creation", i);
            goto fail;
        }
        pipe->cop_n = i;

        pid = pipe_colorop_next_id(pipe->cops[i]);
    }

    return pipe;

fail:
    pipe_free(pipe);
    return NULL;
}

const drmu_pipe_colorop_t *
drmu_pipe_colorop_n(const drmu_pipe_t * const pipe, unsigned int n)
{
    return pipe == NULL || n >= pipe->cop_n ? NULL : pipe->cops[n];
}
