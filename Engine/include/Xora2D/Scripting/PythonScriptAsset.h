#pragma once

#include "Xora2D/Scripting/ScriptTypes.h"

#include <filesystem>
#include <string>

namespace Xora2D {

// Loaded Python script source (immutable text resource).
class PythonScriptAsset {
public:
    [[nodiscard]] bool IsValid() const { return m_valid; }
    [[nodiscard]] const std::string& GetSource() const { return m_source; }
    [[nodiscard]] const std::string& GetRelativePath() const { return m_relativePath; }

    static ScriptLoadResult LoadFromFile(const std::filesystem::path& absolutePath,
                                         const std::string& relativePath, PythonScriptAsset& output);

private:
    bool m_valid = false;
    std::string m_source;
    std::string m_relativePath;
};

} // namespace Xora2D