#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetTypes.h"

#include <string>

namespace Xora2D {

struct AssetMetadata {
    AssetHandle Handle{};
    AssetType Type = AssetType::Unknown;
    std::string Name;
    // Relative to Resources/Assets/ (forward slashes, no .. segments).
    std::string RelativePath;
    bool Available = true;
};

} // namespace Xora2D