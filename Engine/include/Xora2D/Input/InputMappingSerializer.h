#pragma once

#include "Xora2D/Input/InputMapping.h"

#include <filesystem>
#include <string>

namespace Xora2D {

struct InputMappingSerializeResult {
    bool Success = false;
    std::string Message;
};

class InputMappingSerializer {
public:
    static constexpr int kFormatVersion = 1;

    static InputMappingSerializeResult Load(InputMapping& mapping, const std::filesystem::path& filepath);
    static InputMappingSerializeResult Save(const InputMapping& mapping, const std::filesystem::path& filepath);
};

} // namespace Xora2D