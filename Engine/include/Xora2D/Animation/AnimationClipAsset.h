#pragma once

#include "Xora2D/Animation/AnimationTypes.h"
#include "Xora2D/Assets/AssetResult.h"

#include <filesystem>
#include <string>

namespace Xora2D {

class AnimationClipAsset {
public:
    [[nodiscard]] bool IsValid() const { return m_valid; }
    [[nodiscard]] const AnimationClip& GetClip() const { return m_clip; }
    [[nodiscard]] const std::string& GetRelativePath() const { return m_relativePath; }

    static AssetResult LoadFromFile(const std::filesystem::path& absolutePath, const std::string& relativePath,
                                    AnimationClipAsset& output,
                                    const std::filesystem::path& textureImagePath = {});

private:
    bool m_valid = false;
    std::string m_relativePath;
    AnimationClip m_clip;
};

} // namespace Xora2D