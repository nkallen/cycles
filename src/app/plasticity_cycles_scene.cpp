/* SPDX-License-Identifier: Apache-2.0 */
#include "app/plasticity_cycles.h"
#include "app/plasticity_cycles_internal.h"

#include "scene/background.h"
#include "scene/camera.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/pass.h"
#include "scene/shader_nodes.h"
#include "session/buffers.h"
#include "session/output_driver.h"
#include "util/colorspace.h"
#include "util/transform.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <unordered_map>

namespace {

class ImageOutput : public ccl::OutputDriver {
 public:
  explicit ImageOutput(float *pixels) : pixels_(pixels) {}

  void write_render_tile(const Tile &tile) override
  {
    if (tile.size == tile.full_size) {
      written = tile.get_pass_pixels("combined", 4, pixels_);
    }
  }

  bool written = false;

 private:
  float *pixels_;
};

}  // namespace

const char *plasticity_cycles_add_materials(void *scene,
                                            const size_t count,
                                            const double *base_colors,
                                            const double *metallics,
                                            const double *roughnesses,
                                            int32_t *material_ids)
{
  try {
    auto &renderer = *static_cast<PlasticityCycles *>(scene);
    auto &native_scene = *renderer.session.scene;
    const ccl::thread_scoped_lock lock(native_scene.mutex);
    renderer.materials.reserve(renderer.materials.size() + count);

    for (size_t i = 0; i < count; ++i) {
      auto graph = ccl::make_unique<ccl::ShaderGraph>();
      auto *material = graph->create_node<ccl::PrincipledBsdfNode>();
      const double *color = base_colors + i * 3;
      material->set_base_color(ccl::make_float3(
          static_cast<float>(color[0]), static_cast<float>(color[1]), static_cast<float>(color[2])));
      material->set_metallic(static_cast<float>(metallics[i]));
      material->set_roughness(static_cast<float>(roughnesses[i]));
      graph->connect(material->output("BSDF"), graph->output()->input("Surface"));

      auto *shader = native_scene.create_node<ccl::Shader>();
      shader->set_graph(std::move(graph));
      shader->tag_update(&native_scene);
      material_ids[i] = static_cast<int32_t>(renderer.materials.size());
      renderer.materials.push_back(shader);
    }
    return nullptr;
  }
  catch (const std::exception &error) {
    return plasticity_cycles_error(error.what());
  }
  catch (...) {
    return plasticity_cycles_error("Cycles material creation failed");
  }
}

const char *plasticity_cycles_add_meshes(void *scene,
                                        const size_t count,
                                        const float *positions,
                                        const float *normals,
                                        const uint32_t *indices,
                                        const int32_t *position_offsets,
                                        const int32_t *index_offsets,
                                        const int32_t *material_ids)
{
  try {
    auto &renderer = *static_cast<PlasticityCycles *>(scene);
    auto &native_scene = *renderer.session.scene;
    const ccl::thread_scoped_lock lock(native_scene.mutex);
    for (int i = 0; i < index_offsets[count] / 3; ++i) {
      if (material_ids[i] < 0 || static_cast<size_t>(material_ids[i]) >= renderer.materials.size()) {
        return plasticity_cycles_error("Unknown Cycles material ID");
      }
    }

    for (size_t i = 0; i < count; ++i) {
      const int position_start = position_offsets[i];
      const int index_start = index_offsets[i];
      const int vertex_count = (position_offsets[i + 1] - position_start) / 3;
      const int triangle_count = (index_offsets[i + 1] - index_start) / 3;
      auto *mesh = native_scene.create_node<ccl::Mesh>();
      mesh->resize_mesh(vertex_count, triangle_count);

      auto *mesh_positions = mesh->get_position_for_write();
      auto *mesh_normals = mesh->attributes.add(ccl::ATTR_STD_VERTEX_NORMAL)
                               ->data_for_write<ccl::packed_normal>();
      for (int vertex = 0; vertex < vertex_count; ++vertex) {
        const int start = position_start + vertex * 3;
        mesh_positions[vertex] = ccl::make_float3(
            positions[start], positions[start + 1], positions[start + 2]);
        mesh_normals[vertex] = ccl::packed_normal(
            ccl::make_float3(normals[start], normals[start + 1], normals[start + 2]));
      }

      auto &triangles = mesh->get_triangles();
      std::copy_n(indices + index_start, triangle_count * 3, triangles.data());
      std::ranges::fill(mesh->get_smooth(), true);

      ccl::array<ccl::Node *> shaders;
      std::unordered_map<int32_t, int> material_slots;
      auto &mesh_shaders = mesh->get_shader();
      for (int triangle = 0; triangle < triangle_count; ++triangle) {
        const int32_t material_id = material_ids[index_start / 3 + triangle];
        const auto [slot, inserted] = material_slots.try_emplace(
            material_id, static_cast<int>(shaders.size()));
        if (inserted) {
          auto *shader = renderer.materials[material_id];
          shader->tag_used(&native_scene);
          shaders.push_back_slow(shader);
        }
        mesh_shaders[triangle] = slot->second;
      }
      mesh->set_used_shaders(shaders);
      mesh->tag_position_modified();
      mesh->tag_triangles_modified();
      mesh->tag_smooth_modified();
      mesh->tag_shader_modified();
      mesh->tag_update(&native_scene, true);

      auto *object = native_scene.create_node<ccl::Object>();
      object->set_geometry(mesh);
      object->set_tfm(ccl::transform_identity());
      object->tag_update(&native_scene);
    }
    return nullptr;
  }
  catch (const std::exception &error) {
    return plasticity_cycles_error(error.what());
  }
  catch (...) {
    return plasticity_cycles_error("Cycles mesh creation failed");
  }
}

const char *plasticity_cycles_set_environment(void *scene, const char *path, const double strength)
{
  try {
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(path))) {
      return plasticity_cycles_error("Cycles environment path is not a file");
    }

    auto &native_scene = *static_cast<PlasticityCycles *>(scene)->session.scene;
    const ccl::thread_scoped_lock lock(native_scene.mutex);
    auto graph = ccl::make_unique<ccl::ShaderGraph>();
    auto *image = graph->create_node<ccl::EnvironmentTextureNode>();
    image->set_filename(ccl::ustring(path));
    image->set_colorspace(ccl::u_colorspace_auto);
    auto *background = graph->create_node<ccl::BackgroundNode>();
    background->set_strength(static_cast<float>(strength));
    graph->connect(image->output("Color"), background->input("Color"));
    graph->connect(background->output("Background"), graph->output()->input("Surface"));

    auto *shader = native_scene.default_background;
    shader->set_graph(std::move(graph));
    shader->tag_update(&native_scene);
    native_scene.background->tag_update(&native_scene);
    return nullptr;
  }
  catch (const std::exception &error) {
    return plasticity_cycles_error(error.what());
  }
  catch (...) {
    return plasticity_cycles_error("Cycles environment update failed");
  }
}

const char *plasticity_cycles_set_camera(void *scene,
                                         const double *transform,
                                         const double field_of_view)
{
  try {
    auto &native_scene = *static_cast<PlasticityCycles *>(scene)->session.scene;
    const ccl::thread_scoped_lock lock(native_scene.mutex);
    ccl::Transform matrix;
    for (int row = 0; row < 3; ++row) {
      const double *values = transform + row * 4;
      matrix[row] = ccl::make_float4(static_cast<float>(values[0]),
                                     static_cast<float>(values[1]),
                                     static_cast<float>(values[2]),
                                     static_cast<float>(values[3]));
    }
    native_scene.camera->set_matrix(matrix);
    native_scene.camera->set_fov(static_cast<float>(field_of_view));
    return nullptr;
  }
  catch (const std::exception &error) {
    return plasticity_cycles_error(error.what());
  }
  catch (...) {
    return plasticity_cycles_error("Cycles camera update failed");
  }
}

const char *plasticity_cycles_render(void *scene,
                                     const int32_t width,
                                     const int32_t height,
                                     const int32_t samples,
                                     float *pixels)
{
  try {
    auto &session = static_cast<PlasticityCycles *>(scene)->session;
    auto &native_scene = *session.scene;
    {
      const ccl::thread_scoped_lock lock(native_scene.mutex);
      if (native_scene.passes.empty()) {
        auto *pass = native_scene.create_node<ccl::Pass>();
        pass->set_name(ccl::ustring("combined"));
        pass->set_type(ccl::PASS_COMBINED);
      }
      native_scene.camera->set_full_width(width);
      native_scene.camera->set_full_height(height);
      native_scene.camera->compute_auto_viewplane();
    }

    ccl::BufferParams buffer;
    buffer.width = buffer.full_width = width;
    buffer.height = buffer.full_height = height;
    ccl::SessionParams params = session.params;
    params.samples = samples;
    auto driver = ccl::make_unique<ImageOutput>(pixels);
    const auto *output = driver.get();
    session.set_output_driver(std::move(driver));
    session.reset(params, buffer);
    session.start();
    session.wait();

    const bool written = output->written;
    session.set_output_driver(nullptr);
    if (session.progress.get_error()) {
      return plasticity_cycles_error(session.progress.get_error_message().c_str());
    }
    if (!written) {
      return plasticity_cycles_error("Cycles did not return an image");
    }
    return nullptr;
  }
  catch (const std::exception &error) {
    return plasticity_cycles_error(error.what());
  }
  catch (...) {
    return plasticity_cycles_error("Cycles rendering failed");
  }
}
