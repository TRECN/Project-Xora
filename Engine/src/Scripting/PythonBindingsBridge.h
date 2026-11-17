#pragma once

#include "Xora2D/Core/UUID.h"

#include <pybind11/pybind11.h>

#include <memory>

namespace py = pybind11;

namespace Xora2D {

class Scene;

py::object CreatePyEntityBinding(const std::shared_ptr<Scene>& scene, const UUID& entityId);

} // namespace Xora2D