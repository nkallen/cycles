/* SPDX-License-Identifier: Apache-2.0 */
#include "app/plasticity_cycles.h"
#include "app/plasticity_cycles_internal.h"

#include "device/device.h"
#include "scene/light.h"
#include "scene/object.h"
#include "scene/shader_nodes.h"
#include "util/log.h"
#include "util/transform.h"

#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>

const char *plasticity_cycles_error(const char *message) noexcept
{
  thread_local char error[1024];
  std::snprintf(error, sizeof(error), "%s", message);
  return error;
}

const char *plasticity_cycles_create(void **scene)
{
  try {
    *scene = nullptr;
    static std::once_flag init_logging;
    std::call_once(init_logging, [] { ccl::log_init(nullptr); });

    const auto devices = ccl::Device::available_devices(ccl::DEVICE_MASK_CPU);
    if (devices.empty()) {
      return plasticity_cycles_error("Cycles found no CPU device");
    }

    ccl::SessionParams params;
    params.device = devices.front();
    params.denoise_device = params.device;
    params.headless = true;
    params.background = true;
    params.use_auto_tile = false;
    auto result = std::make_unique<PlasticityCycles>(params);
    if (result->session.progress.get_error()) {
      return plasticity_cycles_error(result->session.progress.get_error_message().c_str());
    }
    auto &native_scene = *result->session.scene;
    auto graph = ccl::make_unique<ccl::ShaderGraph>();
    auto *emission = graph->create_node<ccl::EmissionNode>();
    emission->set_color(ccl::make_float3(1.0f, 1.0f, 1.0f));
    emission->set_strength(1.0f);
    graph->connect(emission->output("Emission"), graph->output()->input("Surface"));
    native_scene.default_light->set_graph(std::move(graph));
    native_scene.default_light->tag_update(&native_scene);

    auto *background = native_scene.create_node<ccl::BackgroundLight>();
    background->set_use_mis(true);
    auto *background_object = native_scene.create_node<ccl::Object>();
    background_object->set_geometry(background);

    *scene = result.release();
    return nullptr;
  }
  catch (const std::exception &error) {
    return plasticity_cycles_error(error.what());
  }
  catch (...) {
    return plasticity_cycles_error("Cycles session creation failed");
  }
}

void plasticity_cycles_destroy(void *scene)
{
  delete static_cast<PlasticityCycles *>(scene);
}

const char *plasticity_cycles_add_lights(void *scene,
                                        const size_t count,
                                        const int32_t *types,
                                        const double *positions,
                                        const double *sizes,
                                        const double *powers)
{
  for (size_t i = 0; i < count; ++i) {
    if (types[i] != 1 && types[i] != 2) {
      return plasticity_cycles_error("Unknown Cycles light type");
    }
  }
  try {
    auto &session = static_cast<PlasticityCycles *>(scene)->session;
    auto &native_scene = *session.scene;
    const ccl::thread_scoped_lock lock(native_scene.mutex);
    for (size_t i = 0; i < count; ++i) {
      const float size = static_cast<float>(sizes[i]);
      ccl::Light *light;
      if (types[i] == 1) {
        auto *point = native_scene.create_node<ccl::PointLight>();
        point->set_radius(size);
        light = point;
      }
      else {
        auto *area = native_scene.create_node<ccl::AreaLight>();
        area->set_sizeu(size);
        area->set_sizev(size);
        light = area;
      }
      const float power = static_cast<float>(powers[i]);
      light->set_strength(ccl::make_float3(power, power, power));
      light->set_normalize(true);
      light->set_use_mis(true);

      auto *object = native_scene.create_node<ccl::Object>();
      object->set_geometry(light);
      object->set_visibility(ccl::PATH_RAY_VISIBILITY_ALL & ~ccl::PATH_RAY_VISIBILITY_CAMERA);
      const auto *position = positions + i * 3;
      object->set_tfm(ccl::transform_translate(static_cast<float>(position[0]),
                                               static_cast<float>(position[1]),
                                               static_cast<float>(position[2])));
      light->tag_update(&native_scene);
      object->tag_update(&native_scene);
    }
    return nullptr;
  }
  catch (const std::exception &error) {
    return plasticity_cycles_error(error.what());
  }
  catch (...) {
    return plasticity_cycles_error("Cycles light creation failed");
  }
}
