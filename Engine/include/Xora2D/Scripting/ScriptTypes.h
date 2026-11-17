#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Xora2D {

enum class ScriptPropertyType : std::uint8_t {
    Number = 0,
    Boolean,
    String,
};

struct ScriptProperty {
    std::string Name;
    ScriptPropertyType Type = ScriptPropertyType::Number;
    double NumberValue = 0.0;
    bool BooleanValue = false;
    std::string StringValue;
};

struct ScriptLoadResult {
    bool Success = false;
    std::string Message;
};

struct ScriptReloadResult {
    bool Success = false;
    bool KeptPreviousVersion = false;
    std::string Message;
};

} // namespace Xora2D