#include "Xora2D/Scripting/PythonScriptAsset.h"

#include "Xora2D/Assets/AssetPaths.h"

#include <fstream>
#include <sstream>

namespace Xora2D {

ScriptLoadResult PythonScriptAsset::LoadFromFile(const std::filesystem::path& absolutePath,
                                                 const std::string& relativePath, PythonScriptAsset& output) {
    ScriptLoadResult result{};
    if (!std::filesystem::exists(absolutePath)) {
        result.Message = "Python script file does not exist";
        return result;
    }
    if (!IsSupportedPythonScriptExtension(absolutePath)) {
        result.Message = "Unsupported Python script extension";
        return result;
    }

    std::ifstream stream(absolutePath, std::ios::binary);
    if (!stream) {
        result.Message = "Failed to open Python script";
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    output.m_source = buffer.str();
    output.m_relativePath = relativePath;
    output.m_valid = !output.m_source.empty();
    result.Success = output.m_valid;
    if (!result.Success) {
        result.Message = "Python script is empty";
    }
    return result;
}

} // namespace Xora2D