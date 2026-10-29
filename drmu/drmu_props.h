#ifndef _DRMU_DRMU_PROPS_H
#define _DRMU_DRMU_PROPS_H

// Internal props helpers - not part of the public API

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_env_s;
struct drmu_atomic_s;

struct drmu_propinfo_s;
typedef struct drmu_propinfo_s drmu_propinfo_t;

struct drmu_props_s;
typedef struct drmu_props_s drmu_props_t;

uint64_t drmu_propinfo_val(const drmu_propinfo_t * const pi);
uint32_t drmu_propinfo_prop_id(const drmu_propinfo_t * const pi);

drmu_props_t * drmu_props_new(struct drmu_env_s * const du, const uint32_t objid, const uint32_t objtype);
void drmu_props_free(drmu_props_t * const props);

const drmu_propinfo_t * drmu_props_name_to_propinfo(const drmu_props_t * const props, const char * const name);
uint32_t drmu_props_name_to_id(const drmu_props_t * const props, const char * const name);
// Data must be freed later
int drmu_props_name_get_blob(const drmu_props_t * const props, const char * const name, void ** const ppdata, size_t * const plen);
void drmu_props_dump(const drmu_props_t * const props);

// Returns number of props on success, -errno on failure
// *ppPropids & *ppValues must be freed later
int drmu_props_get_properties(struct drmu_env_s * const du, const uint32_t objid, const uint32_t objtype,
                              uint32_t ** const ppPropids, uint64_t ** const ppValues);

// Add all non-immutable props to the atomic, copying any blobs
int drmu_atomic_props_add_save(struct drmu_atomic_s * const da, const uint32_t objid, const drmu_props_t * props);

// In drmu.c
// Data alloced here needs freeing later
int drmu_blob_data_read(struct drmu_env_s * const du, uint32_t blob_id, void ** const ppdata, size_t * plen);

#ifdef __cplusplus
}
#endif

#endif
