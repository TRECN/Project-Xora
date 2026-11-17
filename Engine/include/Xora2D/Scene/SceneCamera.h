#pragma once

#include "Xora2D/Renderer/OrthographicCamera.h"

namespace Xora2D {

class Scene;

// Resolves the primary camera entity into an OrthographicCamera for rendering.
class SceneCamera {
public:
  enum class Result {
    Ok,
    NoPrimaryCamera,
    MultiplePrimaryCameras,
  };

  static Result BuildPrimaryCamera(const Scene& scene, OrthographicCamera& camera);
};

} // namespace Xora2D