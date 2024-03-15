#ifndef CUBE_RUNCUBE_H
#define CUBE_RUNCUBE_H

struct runcube_env_s;
typedef struct runcube_env_s runcube_env_t;

struct drmu_output_s;

runcube_env_t * runcube_drmu_start(struct drmu_output_s * const dout);
void runcube_drmu_stop(runcube_env_t ** const ppRce);

#endif

