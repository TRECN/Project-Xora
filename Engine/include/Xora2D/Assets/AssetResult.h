#pragma once

#include "Xora2D/Assets/AssetHandle.h"

#include <string>

namespace Xora2D {

struct AssetResult {
    bool Success = false;
    std::string Message;
    AssetHandle Handle{};
};

} // namespace Xora2D