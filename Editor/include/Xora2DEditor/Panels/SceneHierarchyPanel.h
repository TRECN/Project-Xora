#pragma once

#include "Xora2DEditor/EditorPanel.h"

#include <memory>

namespace Xora2DEditor {

std::unique_ptr<EditorPanel> CreateSceneHierarchyPanel();

} // namespace Xora2DEditor