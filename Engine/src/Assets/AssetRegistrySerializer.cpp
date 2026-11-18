#include "Xora2D/Assets/AssetRegistrySerializer.h"

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Core/Logger.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace Xora2D {
namespace {

AssetRegistrySerializeResult Failure(std::string message) {
    return AssetRegistrySerializeResult{false, std::move(message)};
}

AssetRegistrySerializeResult Success() {
    return AssetRegistrySerializeResult{true, {}};
}

std::string AssetTypeToString(AssetType type) {
    switch (type) {
    case AssetType::Texture2D:
        return "Texture2D";
    case AssetType::Audio:
        return "Audio";
    case AssetType::PythonScript:
        return "PythonScript";
    case AssetType::AnimationClip:
        return "AnimationClip";
    default:
        return "Unknown";
    }
}

std::optional<AssetType> AssetTypeFromString(const std::string& text) {
    if (text == "Texture2D") {
        return AssetType::Texture2D;
    }
    if (text == "Audio") {
        return AssetType::Audio;
    }
    if (text == "PythonScript") {
        return AssetType::PythonScript;
    }
    if (text == "AnimationClip") {
        return AssetType::AnimationClip;
    }
    return std::nullopt;
}

} // namespace

AssetRegistrySerializeResult AssetRegistrySerializer::Load(AssetRegistry& registry,
                                                           const std::filesystem::path& filepath) {
    if (!std::filesystem::exists(filepath)) {
        registry.Clear();
        return Success();
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(filepath.string());
    } catch (const YAML::Exception& exception) {
        return Failure(std::string("Malformed asset registry YAML: ") + exception.what());
    }

    if (!root["Version"]) {
        return Failure("Asset registry missing Version");
    }
    const int version = root["Version"].as<int>();
    if (version != kFormatVersion) {
        return Failure("Unsupported asset registry version");
    }

    AssetRegistry parsed{};
    const YAML::Node assets = root["Assets"];
    if (assets && !assets.IsSequence()) {
        return Failure("Assets must be a sequence");
    }

    std::unordered_set<AssetHandle> seenHandles;
    std::unordered_set<std::string> seenPaths;

    if (assets) {
        for (const YAML::Node& node : assets) {
            if (!node["Handle"] || !node["Type"] || !node["Name"] || !node["RelativePath"]) {
                return Failure("Asset entry missing required fields");
            }

            const std::optional<AssetHandle> handle = AssetHandle::FromString(node["Handle"].as<std::string>());
            if (!handle.has_value()) {
                return Failure("Invalid asset handle in registry");
            }
            if (!seenHandles.insert(*handle).second) {
                return Failure("Duplicate asset handle in registry file");
            }

            const std::optional<AssetType> type = AssetTypeFromString(node["Type"].as<std::string>());
            if (!type.has_value()) {
                return Failure("Invalid asset type in registry file");
            }

            AssetMetadata metadata{};
            metadata.Handle = *handle;
            metadata.Type = *type;
            metadata.Name = node["Name"].as<std::string>();
            const std::optional<std::string> normalized =
                NormalizeAssetRelativePath(node["RelativePath"].as<std::string>());
            if (!normalized.has_value()) {
                return Failure("Unsafe relative path in registry");
            }
            metadata.RelativePath = *normalized;
            if (!seenPaths.insert(metadata.RelativePath).second) {
                return Failure("Duplicate asset path in registry file");
            }
            if (node["Available"]) {
                metadata.Available = node["Available"].as<bool>();
            }

            std::string validationError;
            if (!parsed.ValidateMetadata(metadata, validationError)) {
                return Failure(validationError);
            }
            if (!parsed.RegisterAsset(metadata)) {
                return Failure("Failed to register parsed asset metadata");
            }
        }
    }

    registry = std::move(parsed);
    return Success();
}

AssetRegistrySerializeResult AssetRegistrySerializer::Save(const AssetRegistry& registry,
                                                           const std::filesystem::path& filepath) {
    try {
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "Version" << YAML::Value << kFormatVersion;
        emitter << YAML::Key << "Assets" << YAML::Value << YAML::BeginSeq;

        std::vector<AssetMetadata> assets = registry.ListAssets();
        std::sort(assets.begin(), assets.end(), [](const AssetMetadata& left, const AssetMetadata& right) {
            return left.Handle.ToString() < right.Handle.ToString();
        });

        for (const AssetMetadata& metadata : assets) {
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "Handle" << YAML::Value << metadata.Handle.ToString();
            emitter << YAML::Key << "Type" << YAML::Value << AssetTypeToString(metadata.Type);
            emitter << YAML::Key << "Name" << YAML::Value << metadata.Name;
            emitter << YAML::Key << "RelativePath" << YAML::Value << metadata.RelativePath;
            emitter << YAML::Key << "Available" << YAML::Value << metadata.Available;
            emitter << YAML::EndMap;
        }

        emitter << YAML::EndSeq;
        emitter << YAML::EndMap;

        std::filesystem::create_directories(filepath.parent_path());
        const std::filesystem::path tempPath = filepath.string() + ".tmp";
        {
            std::ofstream output(tempPath);
            if (!output.is_open()) {
                return Failure("Failed to open temporary registry file for writing");
            }
            output << emitter.c_str();
            if (!output.good()) {
                return Failure("Failed while writing temporary registry file");
            }
        }

        std::error_code errorCode;
        std::filesystem::rename(tempPath, filepath, errorCode);
        if (errorCode) {
            std::filesystem::remove(tempPath, errorCode);
            return Failure("Failed to commit asset registry file");
        }
        return Success();
    } catch (const std::exception& exception) {
        return Failure(std::string("Asset registry save failed: ") + exception.what());
    }
}

} // namespace Xora2D