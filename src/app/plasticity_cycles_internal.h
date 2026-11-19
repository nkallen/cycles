/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "scene/scene.h"
#include "session/session.h"
#include "util/vector.h"

struct PlasticityCycles {
  explicit PlasticityCycles(const ccl::SessionParams &params) : session(params, ccl::SceneParams())
  {
  }

  ccl::Session session;
  ccl::vector<ccl::Shader *> materials;
};

const char *plasticity_cycles_error(const char *message) noexcept;
