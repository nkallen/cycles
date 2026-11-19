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

/* Types are 1 for point and 2 for square area. Positions are xyz triples.
 * Sizes are point radii or area-light side lengths. Powers are scalar. */
PLASTICITY_CYCLES_API const char *plasticity_cycles_add_lights(void *scene,
                                                            size_t count,
                                                            const int32_t *types,
                                                            const double *positions,
                                                            const double *sizes,
                                                            const double *powers);

#ifdef __cplusplus
}
#endif
