/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(PLASTICITY_CYCLES_BUILD)
#    define PLASTICITY_CYCLES_API __declspec(dllexport)
#  else
#    define PLASTICITY_CYCLES_API __declspec(dllimport)
#  endif
#else
#  define PLASTICITY_CYCLES_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Status calls return NULL on success. Error strings are owned by the library. */
PLASTICITY_CYCLES_API const char *plasticity_cycles_create(void **scene);
PLASTICITY_CYCLES_API void plasticity_cycles_destroy(void *scene);

enum PlasticityCyclesLightType {
  PLASTICITY_CYCLES_LIGHT_POINT = 1,
  PLASTICITY_CYCLES_LIGHT_AREA = 2,
};

/* Positions are xyz triples.
 * Sizes are point radii or area-light side lengths. Powers are scalar. */
PLASTICITY_CYCLES_API const char *plasticity_cycles_add_lights(void *scene,
                                                            size_t count,
                                                            const int32_t *types,
                                                            const double *positions,
                                                            const double *sizes,
                                                            const double *powers);

/* Base colors are RGB triples. Material IDs are local to the scene. */
PLASTICITY_CYCLES_API const char *plasticity_cycles_add_materials(void *scene,
                                                               size_t count,
                                                               const double *baseColors,
                                                               const double *metallics,
                                                               const double *roughnesses,
                                                               int32_t *materialIds);

/* Flattened xyz positions and normals share offsets into their float arrays.
 * Indices are mesh-local. Each triangle has one material ID. */
PLASTICITY_CYCLES_API const char *plasticity_cycles_add_meshes(void *scene,
                                                            size_t count,
                                                            const float *positions,
                                                            const float *normals,
                                                            const uint32_t *indices,
                                                            const int32_t *positionOffsets,
                                                            const int32_t *indexOffsets,
                                                            const int32_t *materialIds);

PLASTICITY_CYCLES_API const char *plasticity_cycles_set_environment(void *scene,
                                                                 const char *path,
                                                                 double strength);

/* Column-major Three.js camera-to-world and WebGL projection matrices. */
PLASTICITY_CYCLES_API const char *plasticity_cycles_set_camera_matrices(void *scene,
                                                                      const double *view,
                                                                      const double *projection);

/* The host provides storage for width * height * 4 linear RGBA components. */
PLASTICITY_CYCLES_API const char *plasticity_cycles_render(void *scene,
                                                         int32_t width,
                                                         int32_t height,
                                                         int32_t samples,
                                                         float *pixels);

#ifdef __cplusplus
}
#endif
