#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Scripting/ScriptTypes.h"

#include <string>
#include <vector>

namespace Xora2D {

// ECS data for Python gameplay scripts. No Python runtime state is stored here.
struct PythonScriptComponent {
    AssetHandle ScriptAsset{};
    std::string ClassName;
    bool Enabled = true;
    std::vector<ScriptProperty> Properties;
};

} // namespace Xora2D