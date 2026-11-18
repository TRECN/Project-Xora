#include "Xora2D/Animation/AnimationClipAsset.h"

#include "Xora2D/Animation/SpriteSheetLayout.h"
#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetPaths.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

#include <stb_image.h>

namespace Xora2D {
namespace {

bool ReadVec2(const YAML::Node& node, glm::vec2& output) {
    if (!node || !node.IsSequence() || node.size() < 2) {
        return false;
    }
    output.x = node[0].as<float>();
    output.y = node[1].as<float>();
    return true;
}

bool ReadVec4(const YAML::Node& node, glm::vec4& output) {
    if (!node || !node.IsSequence() || node.size() < 4) {
        return false;
    }
    output.x = node[0].as<float>();
    output.y = node[1].as<float>();
    output.z = node[2].as<float>();
    output.w = node[3].as<float>();
    return true;
}

bool BakeGridFrames(AnimationClip& clip, std::uint32_t textureWidth, std::uint32_t textureHeight,
                    std::string& error) {
    const SpriteSheetValidationResult validation =
        ValidateSpriteSheetLayout(clip.SheetLayout, textureWidth, textureHeight);
    if (!validation.Valid) {
        error = validation.Message;
        return false;
    }
    clip.Frames.clear();
    clip.Frames.reserve(static_cast<std::size_t>(clip.SheetLayout.FrameCount));
    const float defaultDuration = 0.1f;
    for (int frameIndex = 0; frameIndex < clip.SheetLayout.FrameCount; ++frameIndex) {
        const std::optional<glm::vec4> uv =
            CalculateSpriteSheetFrameUV(clip.SheetLayout, textureWidth, textureHeight, frameIndex);
        if (!uv.has_value()) {
            error = "Failed to bake sprite sheet UV";
            return false;
        }
        AnimationFrame frame{};
        frame.UVRect = *uv;
        frame.DurationSeconds = defaultDuration;
        clip.Frames.push_back(frame);
    }
    return true;
}

std::optional<std::pair<int, int>> ReadImageDimensions(const std::filesystem::path& texturePath) {
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info(texturePath.string().c_str(), &width, &height, &channels)) {
        return std::nullopt;
    }
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    return std::make_pair(width, height);
}

} // namespace

AssetResult AnimationClipAsset::LoadFromFile(const std::filesystem::path& absolutePath, const std::string& relativePath,
                                               AnimationClipAsset& output, const std::filesystem::path& textureImagePath) {
    AssetResult result{};
    std::ifstream file(absolutePath);
    if (!file) {
        result.Message = "Failed to open animation clip file";
        return result;
    }

    YAML::Node root;
    try {
        root = YAML::Load(file);
    } catch (const YAML::Exception& exception) {
        result.Message = std::string("Invalid animation YAML: ") + exception.what();
        return result;
    }

    if (!root["Version"]) {
        result.Message = "Animation clip missing Version";
        return result;
    }
    const int version = root["Version"].as<int>();
    if (version != 1) {
        result.Message = "Unsupported animation clip version";
        return result;
    }

    AnimationClip clip{};
    clip.Name = root["Name"] ? root["Name"].as<std::string>() : absolutePath.stem().string();

    if (root["TextureAssetHandle"]) {
        const std::optional<AssetHandle> handle =
            AssetHandle::FromString(root["TextureAssetHandle"].as<std::string>());
        if (!handle.has_value()) {
            result.Message = "Invalid TextureAssetHandle";
            return result;
        }
        clip.TextureAsset = *handle;
    }

    if (root["Loop"]) {
        clip.Loop = root["Loop"].as<bool>();
    }
    if (root["PlaybackSpeed"]) {
        clip.DefaultPlaybackSpeed = root["PlaybackSpeed"].as<float>();
    }

    float defaultFrameDuration = 0.1f;
    if (root["DefaultFrameDuration"]) {
        defaultFrameDuration = std::max(root["DefaultFrameDuration"].as<float>(), 0.0001f);
    }

    if (const YAML::Node framesNode = root["Frames"]) {
        if (!framesNode.IsSequence()) {
            result.Message = "Frames must be a sequence";
            return result;
        }
        clip.UsesExplicitFrames = true;
        for (const YAML::Node& frameNode : framesNode) {
            AnimationFrame frame{};
            if (frameNode["UV"]) {
                if (!ReadVec4(frameNode["UV"], frame.UVRect)) {
                    glm::vec2 min{};
                    glm::vec2 max{};
                    if (!ReadVec2(frameNode["UVMin"], min) || !ReadVec2(frameNode["UVMax"], max)) {
                        result.Message = "Frame requires UV or UVMin/UVMax";
                        return result;
                    }
                    frame.UVRect = glm::vec4(min.x, min.y, max.x, max.y);
                }
            }
            frame.DurationSeconds = frameNode["Duration"] ? frameNode["Duration"].as<float>() : defaultFrameDuration;
            frame.DurationSeconds = std::max(frame.DurationSeconds, 0.0001f);
            if (frameNode["Event"]) {
                frame.EventMarker = frameNode["Event"].as<std::string>();
            }
            clip.Frames.push_back(frame);
        }
    }

    if (const YAML::Node sheetNode = root["SpriteSheet"]) {
        SpriteSheetLayout& layout = clip.SheetLayout;
        if (sheetNode["Columns"]) {
            layout.Columns = sheetNode["Columns"].as<int>();
        }
        if (sheetNode["Rows"]) {
            layout.Rows = sheetNode["Rows"].as<int>();
        }
        if (sheetNode["FrameWidth"]) {
            layout.FrameWidthPixels = sheetNode["FrameWidth"].as<float>();
        }
        if (sheetNode["FrameHeight"]) {
            layout.FrameHeightPixels = sheetNode["FrameHeight"].as<float>();
        }
        if (sheetNode["StartFrame"]) {
            layout.StartFrame = sheetNode["StartFrame"].as<int>();
        }
        if (sheetNode["FrameCount"]) {
            layout.FrameCount = sheetNode["FrameCount"].as<int>();
        }
        if (sheetNode["Order"]) {
            const std::string order = sheetNode["Order"].as<std::string>();
            if (order == "ColumnMajor") {
                layout.Order = SpriteSheetFrameOrder::ColumnMajor;
            } else if (order == "Custom") {
                layout.Order = SpriteSheetFrameOrder::CustomSequence;
            } else {
                layout.Order = SpriteSheetFrameOrder::RowMajor;
            }
        }
        if (const YAML::Node seq = sheetNode["CustomSequence"]) {
            if (!seq.IsSequence()) {
                result.Message = "CustomSequence must be a sequence";
                return result;
            }
            for (const YAML::Node& index : seq) {
                layout.CustomSequence.push_back(index.as<int>());
            }
        }
        if (const YAML::Node durations = sheetNode["FrameDurations"]) {
            if (!durations.IsSequence()) {
                result.Message = "FrameDurations must be a sequence";
                return result;
            }
            clip.UsesExplicitFrames = false;
            (void)durations;
        }
    }

    std::uint32_t texW = 0;
    std::uint32_t texH = 0;
    std::filesystem::path texPath = textureImagePath;
    if (root["TextureRelativePath"]) {
        if (const std::optional<std::string> normalized =
                NormalizeAssetRelativePath(root["TextureRelativePath"].as<std::string>())) {
            // An explicit TextureAssetHandle wins: it survives texture renames/moves, while the
            // path-derived handle only matches assets that still live at their original path.
            if (!clip.TextureAsset.IsValid()) {
                clip.TextureAsset = AssetHandle::StableFromRelativePath(*normalized);
            }
            if (texPath.empty()) {
                texPath = ResolveAssetPath(*normalized);
            }
        }
    }
    if (!texPath.empty()) {
        if (const std::optional dims = ReadImageDimensions(texPath)) {
            texW = static_cast<std::uint32_t>(dims->first);
            texH = static_cast<std::uint32_t>(dims->second);
        }
    }

    if (clip.Frames.empty() && clip.SheetLayout.FrameCount > 0) {
        if (texW == 0 || texH == 0) {
            result.Message = "Sprite sheet clip requires texture dimensions (TextureRelativePath)";
            return result;
        }
        std::string bakeError;
        if (!BakeGridFrames(clip, texW, texH, bakeError)) {
            result.Message = bakeError;
            return result;
        }
        if (const YAML::Node durations = root["SpriteSheet"]["FrameDurations"]) {
            if (durations.IsSequence()) {
                for (std::size_t index = 0; index < clip.Frames.size() && index < durations.size(); ++index) {
                    clip.Frames[index].DurationSeconds = std::max(durations[index].as<float>(), 0.0001f);
                }
            }
        } else {
            for (AnimationFrame& frame : clip.Frames) {
                frame.DurationSeconds = defaultFrameDuration;
            }
        }
    }

    if (clip.Frames.empty()) {
        result.Message = "Animation clip has no frames";
        return result;
    }

    output.m_valid = true;
    output.m_relativePath = relativePath;
    output.m_clip = std::move(clip);
    result.Success = true;
    return result;
}

} // namespace Xora2D