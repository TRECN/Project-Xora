#pragma once

#include "Xora2DEditor/EditorPanel.h"

#include <memory>

namespace Xora2DEditor {

std::unique_ptr<EditorPanel> CreateContentBrowserPanel();

} // namespace Xora2DEditor