#include "Xora2D/Prefab/PrefabSerializer.h"

#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Prefab/Prefab.h"
#include "Xora2D/Scene/Scene.h"

#include "../Serialization/EntitySerialization.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace Xora2D {

namespace {

using SerializationDetail::ParsedEntity;

PrefabLoadResult LoadFailure(std::string message) {
    PrefabLoadResult result{};
    result.Message = std::move(message);
    return result;
}

void CheckReference(AssetManager* assets, AssetHandle handle, AssetType expected, const char* what,
                    const std::string& entityName, std::vector<std::string>& missing) {
    if (assets == nullptr || !handle.IsValid()) {
        return;
    }
    const AssetMetadata* meta = assets->GetRegistry().FindByHandle(handle);
    if (meta == nullptr || meta->Type != expected) {
        missing.push_back(entityName + ": " + what + " " + handle.ToString());
    }
}

} // namespace

PrefabLoadResult PrefabSerializer::Parse(const std::string& yamlText, const std::string& sourceName,
                                         AssetManager* assets) {
    YAML::Node root;
    try {
        root = YAML::Load(yamlText);
    } catch (const YAML::Exception& exception) {
        return LoadFailure(sourceName + ": malformed YAML (" + exception.what() + ")");
    }
    if (!root.IsMap() || !root["Prefab"] || !root["Prefab"].IsMap()) {
        return LoadFailure(sourceName + ": not a prefab (missing 'Prefab' header)");
    }

    auto prefab = std::make_shared<Prefab>();
    std::vector<ParsedEntity> entities;
    try {
        const YAML::Node header = root["Prefab"];
        if (!header["FormatVersion"]) {
            return LoadFailure(sourceName + ": missing Prefab.FormatVersion");
        }
        const int version = header["FormatVersion"].as<int>();
        if (version < 1 || version > Prefab::kFormatVersion) {
            return LoadFailure(sourceName + ": unsupported prefab format version " + std::to_string(version) +
                               " (supported: 1-" + std::to_string(Prefab::kFormatVersion) + ")");
        }
        if (!header["Name"] || header["Name"].as<std::string>().empty()) {
            return LoadFailure(sourceName + ": missing Prefab.Name");
        }
        prefab->SetName(header["Name"].as<std::string>());
        const auto revision = header["Revision"] ? header["Revision"].as<std::int64_t>() : 1;
        if (revision < 1 || revision > 0xFFFFFFFFll) {
            return LoadFailure(sourceName + ": invalid Prefab.Revision");
        }
        prefab->SetRevision(static_cast<std::uint32_t>(revision));

        const YAML::Node list = root["Entities"];
        if (!list || !list.IsSequence() || list.size() == 0) {
            return LoadFailure(sourceName + ": a prefab needs a non-empty 'Entities' sequence");
        }
        std::unordered_set<UUID> seen;
        for (const YAML::Node& node : list) {
            ParsedEntity parsed{};
            std::string error;
            if (!SerializationDetail::ParseEntityNode(node, parsed, sourceName, error)) {
                return LoadFailure(error);
            }
            if (!parsed.Id.IsValid() || !seen.insert(parsed.Id).second) {
                return LoadFailure(sourceName + ": duplicate or invalid entity id " + parsed.Id.ToString());
            }
            if (parsed.Tag.empty() || !parsed.Translation.has_value()) {
                return LoadFailure(sourceName + ": entity " + parsed.Id.ToString() + " needs a Tag and a Transform");
            }
            if (parsed.PrefabInstance.has_value()) {
                return LoadFailure(sourceName + ": nested prefabs are not supported (entity " + parsed.Id.ToString() +
                                   " is a prefab instance)");
            }
            entities.push_back(std::move(parsed));
        }
    } catch (const YAML::Exception& exception) {
        return LoadFailure(sourceName + ": invalid value (" + exception.what() + ")");
    }

    PrefabLoadResult result{};
    for (const ParsedEntity& parsed : entities) {
        if (parsed.Sprite.has_value()) {
            CheckReference(assets, parsed.Sprite->TextureAsset, AssetType::Texture2D, "texture", parsed.Tag,
                           result.MissingAssets);
        }
        if (parsed.AudioSource.has_value()) {
            CheckReference(assets, parsed.AudioSource->ClipAsset, AssetType::Audio, "audio clip", parsed.Tag,
                           result.MissingAssets);
        }
        if (parsed.PythonScript.has_value()) {
            CheckReference(assets, parsed.PythonScript->ScriptAsset, AssetType::PythonScript, "script", parsed.Tag,
                           result.MissingAssets);
        }
        if (parsed.Tilemap.has_value()) {
            CheckReference(assets, parsed.Tilemap->Tileset, AssetType::Tileset, "tileset", parsed.Tag,
                           result.MissingAssets);
            for (const TilemapLayer& layer : parsed.Tilemap->Layers) {
                CheckReference(assets, layer.Tileset, AssetType::Tileset, "tileset", parsed.Tag, result.MissingAssets);
            }
        }
        if (parsed.Animation.has_value()) {
            CheckReference(assets, parsed.Animation->DefaultClip, AssetType::AnimationClip, "animation clip",
                           parsed.Tag, result.MissingAssets);
            for (const AssetHandle clip : parsed.Animation->ClipAssets) {
                CheckReference(assets, clip, AssetType::AnimationClip, "animation clip", parsed.Tag,
                               result.MissingAssets);
            }
        }
    }

    try {
        for (const ParsedEntity& parsed : entities) {
            (void)SerializationDetail::ApplyParsedEntity(prefab->GetContent(), parsed, parsed.Id, assets);
        }
    } catch (const std::exception& exception) {
        return LoadFailure(sourceName + ": " + exception.what());
    }
    // Entities were created in file order; SyncOrderFromContent records that order.
    prefab->SyncOrderFromContent();

    result.Success = true;
    result.Message = "Loaded prefab '" + prefab->GetName() + "' (" + std::to_string(prefab->GetEntityCount()) +
                     " entities, revision " + std::to_string(prefab->GetRevision()) + ")";
    result.Loaded = std::move(prefab);
    return result;
}

PrefabLoadResult PrefabSerializer::Load(const std::filesystem::path& file, AssetManager* assets) {
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(file, errorCode)) {
        return LoadFailure("Prefab file not found: " + file.string());
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return LoadFailure("Cannot read prefab file: " + file.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return Parse(buffer.str(), file.filename().string(), assets);
}

std::string PrefabSerializer::Emit(const Prefab& prefab) {
    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "Prefab" << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "FormatVersion" << YAML::Value << Prefab::kFormatVersion;
    emitter << YAML::Key << "Name" << YAML::Value << prefab.GetName();
    emitter << YAML::Key << "Revision" << YAML::Value << prefab.GetRevision();
    emitter << YAML::EndMap;
    emitter << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
    for (const UUID& id : prefab.GetEntityIds()) {
        const Entity entity = prefab.FindEntity(id);
        if (entity.IsValid()) {
            SerializationDetail::EmitEntityNode(emitter, entity, false);
        }
    }
    emitter << YAML::EndSeq;
    emitter << YAML::EndMap;
    return std::string(emitter.c_str()) + "\n";
}

PrefabSaveResult PrefabSerializer::Save(const Prefab& prefab, const std::filesystem::path& file, bool allowOverwrite) {
    PrefabSaveResult result{};
    std::error_code errorCode;
    if (!allowOverwrite && std::filesystem::exists(file, errorCode)) {
        result.Message = "A file already exists at " + file.string();
        return result;
    }
    std::string text;
    try {
        text = Emit(prefab);
    } catch (const std::exception& exception) {
        result.Message = std::string("Cannot serialize prefab: ") + exception.what();
        return result;
    }
    // Validate exactly what will be written before touching the destination.
    const PrefabLoadResult check = Parse(text, file.filename().string(), nullptr);
    if (!check.Success) {
        result.Message = "Prefab data is invalid, nothing was written: " + check.Message;
        return result;
    }

    std::filesystem::create_directories(file.parent_path(), errorCode);
    std::random_device device;
    const std::filesystem::path temporary =
        file.parent_path() / (".xora_prefab_" + std::to_string(device()) + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.Message = "Cannot write to " + file.parent_path().string();
            return result;
        }
        output << text;
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, errorCode);
            result.Message = "Failed while writing " + file.string();
            return result;
        }
    }
    std::filesystem::rename(temporary, file, errorCode);
    if (errorCode) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        result.Message = "Cannot replace " + file.string() + ": " + errorCode.message();
        return result;
    }
    result.Success = true;
    result.Message = "Saved prefab " + file.filename().string();
    return result;
}

} // namespace Xora2D
