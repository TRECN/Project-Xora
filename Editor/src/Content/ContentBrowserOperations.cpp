#include "Xora2DEditor/Content/ContentBrowserOperations.h"

#include "Xora2DEditor/Content/ContentBrowserImport.h"
#include "Xora2DEditor/Content/ContentBrowserPaths.h"

#include "Xora2D/Animation/AnimationComponent.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Audio/AudioComponents.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Entity.h"
#include "Xora2D/Scene/Scene.h"
#include "Xora2D/Scripting/PythonScriptComponent.h"

#include "Xora2D/Prefab/PrefabComponents.h"
#include "Xora2D/Prefab/Prefab.h"
#include "Xora2D/Prefab/PrefabSerializer.h"
#include "Xora2D/Tilemap/TilemapComponent.h"
#include "Xora2D/Tilemap/TilemapSerializer.h"
#include "Xora2D/Tilemap/Tileset.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Xora2DEditor {
namespace {

struct Relocation {
    Xora2D::AssetHandle Handle{};
    std::string OldAssetPath;
    std::string NewAssetPath;
    std::string OldName;
};

std::string RandomToken() {
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    std::ostringstream stream;
    stream << std::hex << generator();
    return stream.str();
}

FilesystemOperationResult Fail(std::string message) {
    FilesystemOperationResult result{};
    result.Message = std::move(message);
    return result;
}

std::string Trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string Unquote(std::string text) {
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front()) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

bool WriteFileAtomically(const std::filesystem::path& file, const std::string& contents) {
    const std::filesystem::path temporary = file.parent_path() / (std::string(kImportTempPrefix) + RandomToken() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output << contents;
        if (!output.good()) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code errorCode;
    std::filesystem::rename(temporary, file, errorCode);
    if (errorCode) {
        std::filesystem::remove(temporary, errorCode);
        return false;
    }
    return true;
}

// All project files with the given compound extension (".scene.yaml" / ".anim.yaml"), resource-relative.
std::vector<std::string> FindProjectFiles(std::string_view extension) {
    std::vector<std::string> files;
    const std::filesystem::path root = GetResourcesRootPath();
    std::error_code errorCode;
    std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied,
                                                           errorCode);
    for (const std::filesystem::recursive_directory_iterator end; !errorCode && iterator != end;
         iterator.increment(errorCode)) {
        const std::string name = iterator->path().filename().string();
        if (IsHiddenEntryName(name)) {
            iterator.disable_recursion_pending();
            continue;
        }
        std::error_code statError;
        if (!iterator->is_regular_file(statError) || FullExtensionLower(iterator->path()) != extension) {
            continue;
        }
        if (const auto relative = AbsoluteToResourceRelative(iterator->path())) {
            files.push_back(*relative);
        }
    }
    return files;
}

// Points animation clips at a texture's new location. The clip also gets an explicit TextureAssetHandle so it
// resolves the (stable) handle instead of deriving one from the new path.
std::size_t RewriteAnimationTextureReferences(const std::vector<Relocation>& relocations,
                                              const Xora2D::AssetRegistry& registry,
                                              std::vector<std::string>& warnings) {
    std::unordered_map<std::string, const Relocation*> byOldPath;
    for (const Relocation& relocation : relocations) {
        const Xora2D::AssetMetadata* meta = registry.FindByHandle(relocation.Handle);
        if (meta != nullptr && meta->Type == Xora2D::AssetType::Texture2D) {
            byOldPath[relocation.OldAssetPath] = &relocation;
        }
    }
    if (byOldPath.empty()) {
        return 0;
    }

    std::size_t updatedFiles = 0;
    for (const std::string& clipPath : FindProjectFiles(".anim.yaml")) {
        const std::filesystem::path absolute = ResolveResourceRelativePath(clipPath);
        std::ifstream input(absolute, std::ios::binary);
        if (!input) {
            continue;
        }
        std::vector<std::string> lines;
        for (std::string line; std::getline(input, line);) {
            lines.push_back(line);
        }
        input.close();

        bool changed = false;
        bool hasHandleLine = std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
            return Trim(line).starts_with("TextureAssetHandle:");
        });
        for (std::size_t index = 0; index < lines.size(); ++index) {
            const std::string trimmed = Trim(lines[index]);
            constexpr std::string_view kKey = "TextureRelativePath:";
            if (!trimmed.starts_with(kKey)) {
                continue;
            }
            const std::string value = Unquote(Trim(trimmed.substr(kKey.size())));
            const auto found = byOldPath.find(value);
            if (found == byOldPath.end()) {
                continue;
            }
            const std::string indent = lines[index].substr(0, lines[index].find_first_not_of(" \t"));
            lines[index] = indent + std::string(kKey) + " " + found->second->NewAssetPath;
            if (!hasHandleLine) {
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(index),
                             indent + "TextureAssetHandle: " + found->second->Handle.ToString());
                hasHandleLine = true;
                ++index;
            }
            changed = true;
        }
        if (!changed) {
            continue;
        }
        std::string contents;
        for (const std::string& line : lines) {
            contents += line;
            contents += '\n';
        }
        if (WriteFileAtomically(absolute, contents)) {
            ++updatedFiles;
        } else {
            warnings.push_back("Could not update texture path in '" + clipPath + "'");
        }
    }
    return updatedFiles;
}

FilesystemOperationResult RelocateEntry(const std::string& sourcePath, const std::string& destinationPath,
                                        Xora2D::AssetRegistry& registry) {
    const std::optional<std::string> source = NormalizeResourceRelativePath(sourcePath);
    const std::optional<std::string> destination = NormalizeResourceRelativePath(destinationPath);
    if (!source.has_value() || !destination.has_value() || source->empty() || destination->empty()) {
        return Fail("Invalid or unsafe path");
    }
    if (IsProtectedResourcePath(*source)) {
        return Fail("'" + *source + "' is a protected project folder");
    }
    if (IsResourcePathWithin(*destination, kRegistryFolder)) {
        return Fail("Items cannot be moved into the asset registry folder");
    }
    if (*source == *destination) {
        FilesystemOperationResult unchanged{};
        unchanged.Success = true;
        unchanged.ResultPath = *source;
        return unchanged;
    }

    const std::filesystem::path absoluteSource = ResolveResourceRelativePath(*source);
    const std::filesystem::path absoluteDestination = ResolveResourceRelativePath(*destination);
    if (absoluteSource.empty() || absoluteDestination.empty()) {
        return Fail("Path escapes the project folder");
    }
    std::error_code errorCode;
    if (!std::filesystem::exists(absoluteSource, errorCode)) {
        return Fail("'" + *source + "' no longer exists on disk");
    }
    const bool isFolder = std::filesystem::is_directory(absoluteSource, errorCode);
    if (isFolder && IsResourcePathWithin(*destination, *source)) {
        return Fail("A folder cannot be moved into itself");
    }
    if (!std::filesystem::is_directory(absoluteDestination.parent_path(), errorCode)) {
        return Fail("Destination folder does not exist");
    }
    if (std::filesystem::exists(absoluteDestination, errorCode)) {
        // Case-only renames on case-insensitive filesystems resolve to the same file.
        if (!std::filesystem::equivalent(absoluteSource, absoluteDestination, errorCode)) {
            return Fail("An item named '" + FileNameOf(*destination) + "' already exists there");
        }
    }

    // Plan registry changes before touching the disk so conflicts are detected up front.
    std::vector<Relocation> plan;
    std::unordered_set<Xora2D::AssetHandle> planned;
    for (const Xora2D::AssetMetadata& meta : registry.ListAssets()) {
        const std::string resourcePath = ResourcePathFromAssetRelativePath(meta.RelativePath);
        if (!IsResourcePathWithin(resourcePath, *source)) {
            continue;
        }
        const std::string newResourcePath = *destination + resourcePath.substr(source->size());
        const std::optional<std::string> newAssetPath = AssetRelativePathFromResourcePath(newResourcePath);
        if (!newAssetPath.has_value()) {
            return Fail("Registered assets must stay inside the Assets folder ('" + meta.Name + "' would leave it)");
        }
        plan.push_back(Relocation{meta.Handle, meta.RelativePath, *newAssetPath, meta.Name});
        planned.insert(meta.Handle);
    }
    for (const Relocation& relocation : plan) {
        const Xora2D::AssetMetadata* occupant = registry.FindByRelativePath(relocation.NewAssetPath);
        if (occupant != nullptr && !planned.contains(occupant->Handle)) {
            return Fail("Registry already has an asset at '" + relocation.NewAssetPath + "'");
        }
    }

    std::filesystem::rename(absoluteSource, absoluteDestination, errorCode);
    if (errorCode) {
        return Fail("Filesystem rename failed: " + errorCode.message());
    }

    // Apply registry changes; on any failure undo everything (registry first, then disk).
    std::vector<const Relocation*> applied;
    for (const Relocation& relocation : plan) {
        if (!registry.RelocateAssetPath(relocation.Handle, relocation.NewAssetPath)) {
            for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator) {
                (void)registry.RelocateAssetPath((*iterator)->Handle, (*iterator)->OldAssetPath);
            }
            std::error_code undoError;
            std::filesystem::rename(absoluteDestination, absoluteSource, undoError);
            FilesystemOperationResult failed =
                Fail("Asset registry update failed for '" + relocation.OldAssetPath + "'; change was rolled back");
            if (undoError) {
                failed.Message += " (WARNING: the file could not be moved back: " + undoError.message() + ")";
            }
            return failed;
        }
        applied.push_back(&relocation);
    }

    FilesystemOperationResult result{};
    result.Success = true;
    result.ResultPath = *destination;
    result.RelocatedAssetCount = plan.size();
    for (const Relocation& relocation : plan) {
        // Keep display names in sync with file names unless the user customised the name.
        const std::string oldStem = std::filesystem::path(relocation.OldAssetPath).filename().string();
        const std::string newStem = std::filesystem::path(relocation.NewAssetPath).filename().string();
        const auto stemOf = [](const std::string& fileName) {
            return fileName.substr(0, fileName.size() - FullExtensionLower(fileName).size());
        };
        if (relocation.OldName == stemOf(oldStem) && stemOf(oldStem) != stemOf(newStem)) {
            (void)registry.SetAssetName(relocation.Handle, stemOf(newStem));
        }
    }
    result.UpdatedReferenceFiles = RewriteAnimationTextureReferences(plan, registry, result.Warnings);
    return result;
}

void WalkYamlForReferences(const YAML::Node& node, const std::string& context,
                           const std::unordered_map<std::string, Xora2D::AssetHandle>& needles,
                           const std::unordered_map<Xora2D::AssetHandle, std::string>& assetPaths,
                           const std::string& referencedBy, const std::string& entityName,
                           std::vector<AssetReference>& output) {
    if (node.IsScalar()) {
        const std::string value = node.Scalar();
        const auto found = needles.find(value);
        if (found != needles.end()) {
            AssetReference reference{};
            reference.Handle = found->second;
            reference.AssetPath = assetPaths.at(found->second);
            reference.ReferencedBy = referencedBy;
            reference.Detail = entityName.empty() ? context : context + " on '" + entityName + "'";
            output.push_back(std::move(reference));
        }
        return;
    }
    if (node.IsSequence()) {
        for (const YAML::Node& child : node) {
            WalkYamlForReferences(child, context, needles, assetPaths, referencedBy, entityName, output);
        }
        return;
    }
    if (node.IsMap()) {
        for (const auto& pair : node) {
            const std::string key = pair.first.IsScalar() ? pair.first.Scalar() : std::string{};
            const std::string childContext = context.empty() ? key : context + "." + key;
            WalkYamlForReferences(pair.second, childContext, needles, assetPaths, referencedBy, entityName, output);
        }
    }
}

} // namespace

FilesystemOperationResult CreateProjectFolder(const std::string& parentDirectory, const std::string& name) {
    if (!IsValidEntryName(name)) {
        return Fail("Invalid folder name");
    }
    const std::optional<std::string> parent = NormalizeResourceRelativePath(parentDirectory);
    if (!parent.has_value()) {
        return Fail("Invalid parent folder");
    }
    if (IsResourcePathWithin(*parent, kRegistryFolder)) {
        return Fail("Folders cannot be created inside the asset registry folder");
    }
    const std::filesystem::path absoluteParent = ResolveResourceRelativePath(*parent);
    std::error_code errorCode;
    if (absoluteParent.empty() || !std::filesystem::is_directory(absoluteParent, errorCode)) {
        return Fail("Parent folder does not exist");
    }
    const std::filesystem::path folder = absoluteParent / name;
    if (std::filesystem::exists(folder, errorCode)) {
        return Fail("An item named '" + name + "' already exists");
    }
    if (!std::filesystem::create_directory(folder, errorCode) || errorCode) {
        return Fail("Failed to create folder: " + errorCode.message());
    }
    FilesystemOperationResult result{};
    result.Success = true;
    result.ResultPath = JoinResourcePath(*parent, name);
    return result;
}

FilesystemOperationResult RenameResourceEntry(const std::string& resourceRelativePath, const std::string& newName,
                                              Xora2D::AssetRegistry& registry) {
    const std::optional<std::string> source = NormalizeResourceRelativePath(resourceRelativePath);
    if (!source.has_value() || source->empty()) {
        return Fail("Invalid path");
    }
    std::string finalName = newName;
    if (!IsValidEntryName(finalName)) {
        return Fail("Invalid name (avoid / \\ : * ? \" < > | and leading dots)");
    }
    const std::filesystem::path absolute = ResolveResourceRelativePath(*source);
    std::error_code errorCode;
    if (!absolute.empty() && std::filesystem::is_regular_file(absolute, errorCode)) {
        const std::string oldName = FileNameOf(*source);
        const std::string extension = FullExtensionLower(oldName);
        const std::string newExtension = FullExtensionLower(finalName);
        if (newExtension.empty() && !extension.empty()) {
            // "Hero" -> "Hero.png": keep the original extension (with its original casing).
            finalName += oldName.substr(oldName.size() - extension.size());
        } else if (newExtension != extension && (ClassifyContentFile(oldName) != ContentItemKind::Other ||
                                                 ClassifyContentFile(finalName) != ContentItemKind::Other)) {
            return Fail("Changing a file's extension is not supported (it would change the asset type)");
        }
        if (!IsValidEntryName(finalName)) {
            return Fail("Invalid name");
        }
    }
    return RelocateEntry(*source, JoinResourcePath(ParentResourceDirectory(*source), finalName), registry);
}

FilesystemOperationResult MoveResourceEntry(const std::string& resourceRelativePath,
                                            const std::string& destinationDirectory,
                                            Xora2D::AssetRegistry& registry) {
    const std::optional<std::string> source = NormalizeResourceRelativePath(resourceRelativePath);
    const std::optional<std::string> directory = NormalizeResourceRelativePath(destinationDirectory);
    if (!source.has_value() || !directory.has_value() || source->empty()) {
        return Fail("Invalid or unsafe path");
    }
    const std::filesystem::path absoluteDirectory = ResolveResourceRelativePath(*directory);
    std::error_code errorCode;
    if (absoluteDirectory.empty() || !std::filesystem::is_directory(absoluteDirectory, errorCode)) {
        return Fail("Destination folder does not exist");
    }
    if (ParentResourceDirectory(*source) == *directory) {
        FilesystemOperationResult unchanged{};
        unchanged.Success = true;
        unchanged.ResultPath = *source;
        return unchanged;
    }
    return RelocateEntry(*source, JoinResourcePath(*directory, FileNameOf(*source)), registry);
}

std::vector<Xora2D::AssetHandle> CollectAssetsUnder(const std::string& resourceRelativePath,
                                                    const Xora2D::AssetRegistry& registry) {
    std::vector<Xora2D::AssetHandle> handles;
    const std::optional<std::string> path = NormalizeResourceRelativePath(resourceRelativePath);
    if (!path.has_value()) {
        return handles;
    }
    for (const Xora2D::AssetMetadata& meta : registry.ListAssets()) {
        if (IsResourcePathWithin(ResourcePathFromAssetRelativePath(meta.RelativePath), *path)) {
            handles.push_back(meta.Handle);
        }
    }
    return handles;
}

std::vector<AssetReference> FindAssetReferences(const std::vector<Xora2D::AssetHandle>& handles,
                                                const Xora2D::AssetRegistry& registry,
                                                const Xora2D::Scene* openScene) {
    std::vector<AssetReference> references;
    if (handles.empty()) {
        return references;
    }
    std::unordered_map<std::string, Xora2D::AssetHandle> needles;
    std::unordered_map<Xora2D::AssetHandle, std::string> assetPaths;
    std::unordered_set<Xora2D::AssetHandle> handleSet(handles.begin(), handles.end());
    for (const Xora2D::AssetHandle handle : handles) {
        const Xora2D::AssetMetadata* meta = registry.FindByHandle(handle);
        const std::string resourcePath = meta ? ResourcePathFromAssetRelativePath(meta->RelativePath) : handle.ToString();
        assetPaths[handle] = resourcePath;
        needles[handle.ToString()] = handle;
        if (meta != nullptr) {
            // Path-based references: animation TextureRelativePath and legacy sprite TexturePath values.
            needles[meta->RelativePath] = handle;
            needles[resourcePath] = handle;
        }
    }

    for (const std::string_view extension :
         {std::string_view(".scene.yaml"), std::string_view(".anim.yaml"), std::string_view(".prefab.yaml"),
          std::string_view(".tileset.yaml"), std::string_view(".tilemap.yaml")}) {
        for (const std::string& file : FindProjectFiles(extension)) {
            YAML::Node root;
            try {
                root = YAML::LoadFile(ResolveResourceRelativePath(file).string());
            } catch (const YAML::Exception&) {
                continue;
            }
            // Scenes and prefabs share the entity format (scene PrefabInstance blocks reference prefab handles).
            if ((extension == ".scene.yaml" || extension == ".prefab.yaml") && root["Entities"] &&
                root["Entities"].IsSequence()) {
                for (const YAML::Node& entity : root["Entities"]) {
                    if (!entity.IsMap()) {
                        continue;
                    }
                    std::string tag;
                    if (entity["TagComponent"] && entity["TagComponent"]["Tag"]) {
                        tag = entity["TagComponent"]["Tag"].as<std::string>("");
                    }
                    for (const auto& component : entity) {
                        const std::string key = component.first.Scalar();
                        if (key == "Entity" || key == "TagComponent") {
                            continue;
                        }
                        WalkYamlForReferences(component.second, key, needles, assetPaths, file, tag, references);
                    }
                }
            } else {
                WalkYamlForReferences(root, {}, needles, assetPaths, file, {}, references);
            }
        }
    }

    // One entry per asset/file/entity: an animation clip that names its texture by handle *and* by path is a
    // single reference.
    {
        std::unordered_set<std::string> seen;
        std::erase_if(references, [&](const AssetReference& reference) {
            const std::size_t on = reference.Detail.find(" on '");
            const std::string owner = on == std::string::npos ? std::string{} : reference.Detail.substr(on);
            return !seen.insert(reference.Handle.ToString() + "|" + reference.ReferencedBy + "|" + owner).second;
        });
    }

    if (openScene != nullptr) {
        auto& scene = const_cast<Xora2D::Scene&>(*openScene);
        auto add = [&](Xora2D::AssetHandle handle, const char* component, const std::string& tag) {
            if (handle.IsValid() && handleSet.contains(handle)) {
                references.push_back(AssetReference{handle, assetPaths[handle], "<open scene>",
                                                    std::string(component) + " on '" + tag + "'"});
            }
        };
        for (const Xora2D::UUID& uuid : scene.GetEntityUUIDs()) {
            Xora2D::Entity entity = scene.FindEntityByUUID(uuid);
            if (!entity.IsValid()) {
                continue;
            }
            const std::string tag =
                entity.HasComponent<Xora2D::TagComponent>() ? entity.GetComponent<Xora2D::TagComponent>().Tag : "";
            if (entity.HasComponent<Xora2D::SpriteRendererComponent>()) {
                add(entity.GetComponent<Xora2D::SpriteRendererComponent>().TextureAsset, "SpriteRenderer", tag);
            }
            if (entity.HasComponent<Xora2D::AudioSourceComponent>()) {
                add(entity.GetComponent<Xora2D::AudioSourceComponent>().ClipAsset, "AudioSource", tag);
            }
            if (entity.HasComponent<Xora2D::PythonScriptComponent>()) {
                add(entity.GetComponent<Xora2D::PythonScriptComponent>().ScriptAsset, "PythonScript", tag);
            }
            if (entity.HasComponent<Xora2D::PrefabInstanceComponent>()) {
                add(entity.GetComponent<Xora2D::PrefabInstanceComponent>().Prefab, "Prefab instance", tag);
            }
            if (entity.HasComponent<Xora2D::TilemapComponent>()) {
                const auto& tilemap = entity.GetComponent<Xora2D::TilemapComponent>();
                add(tilemap.Tileset, "Tilemap (tileset)", tag);
                add(tilemap.Source, "Tilemap (asset)", tag);
                for (const Xora2D::TilemapLayer& layer : tilemap.Layers) {
                    add(layer.Tileset, "Tilemap layer (tileset)", tag);
                }
            }
            if (entity.HasComponent<Xora2D::AnimationComponent>()) {
                const auto& animation = entity.GetComponent<Xora2D::AnimationComponent>();
                add(animation.DefaultClip, "Animation (default clip)", tag);
                for (const Xora2D::AssetHandle clip : animation.ClipAssets) {
                    if (clip != animation.DefaultClip) {
                        add(clip, "Animation (clip list)", tag);
                    }
                }
            }
        }
    }
    return references;
}

FilesystemOperationResult DeleteResourceEntry(const std::string& resourceRelativePath, Xora2D::AssetManager& assetManager,
                                              const DeleteOptions& options) {
    const std::optional<std::string> target = NormalizeResourceRelativePath(resourceRelativePath);
    if (!target.has_value() || target->empty()) {
        return Fail("Invalid path");
    }
    if (IsProtectedResourcePath(*target)) {
        return Fail("'" + *target + "' is a protected project folder and cannot be deleted");
    }
    const std::filesystem::path absolute = ResolveResourceRelativePath(*target);
    if (absolute.empty()) {
        return Fail("Path escapes the project folder");
    }

    Xora2D::AssetRegistry& registry = assetManager.GetRegistry();
    const std::vector<Xora2D::AssetHandle> handles = CollectAssetsUnder(*target, registry);
    if (!options.AllowReferenced) {
        std::vector<AssetReference> references = FindAssetReferences(handles, registry, options.OpenScene);
        // References from files that are being deleted along with the assets do not count.
        std::erase_if(references, [&](const AssetReference& reference) {
            return IsResourcePathWithin(reference.ReferencedBy, *target);
        });
        if (!references.empty()) {
            FilesystemOperationResult refused = Fail("Referenced by " + std::to_string(references.size()) +
                                                     " scene/asset entr" + (references.size() == 1 ? "y" : "ies"));
            refused.References = std::move(references);
            return refused;
        }
    }

    std::error_code errorCode;
    FilesystemOperationResult result{};
    if (std::filesystem::exists(std::filesystem::symlink_status(absolute, errorCode))) {
        // Step 1: atomically move the item out of the visible project into the hidden trash.
        const std::filesystem::path trashSlot = GetResourcesRootPath() / kTrashFolderName / RandomToken();
        std::filesystem::create_directories(trashSlot, errorCode);
        if (errorCode) {
            return Fail("Could not prepare delete: " + errorCode.message());
        }
        std::filesystem::rename(absolute, trashSlot / absolute.filename(), errorCode);
        if (errorCode) {
            std::error_code ignored;
            std::filesystem::remove(trashSlot, ignored);
            return Fail("Delete failed: " + errorCode.message());
        }
        // Step 3 (after the registry update below): purge. A failure here leaves only hidden trash behind.
        std::filesystem::remove_all(trashSlot, errorCode);
        if (errorCode) {
            result.Warnings.push_back("Deleted, but temporary files remain in " + std::string(kTrashFolderName) +
                                      ": " + errorCode.message());
        } else {
            std::error_code ignored;
            std::filesystem::remove(trashSlot.parent_path(), ignored); // only succeeds when empty
        }
    } else if (handles.empty()) {
        return Fail("'" + *target + "' does not exist");
    }

    // Step 2: drop registry entries (a missing file with registry entries is just "forgotten").
    for (const Xora2D::AssetHandle handle : handles) {
        assetManager.UnloadAsset(handle);
        (void)registry.RemoveAsset(handle);
    }
    result.Success = true;
    result.RelocatedAssetCount = handles.size();
    return result;
}

FilesystemOperationResult RegisterExistingFile(const std::string& resourceRelativePath,
                                               Xora2D::AssetManager& assetManager) {
    const std::optional<std::string> path = NormalizeResourceRelativePath(resourceRelativePath);
    if (!path.has_value() || !AssetRelativePathFromResourcePath(*path).has_value()) {
        return Fail("Only files inside the Assets folder can be registered");
    }
    const std::filesystem::path absolute = ResolveResourceRelativePath(*path);
    std::error_code errorCode;
    if (absolute.empty() || !std::filesystem::is_regular_file(absolute, errorCode)) {
        return Fail("File does not exist");
    }
    if (Xora2D::IsSupportedTilesetExtension(absolute) || Xora2D::IsSupportedTilemapExtension(absolute)) {
        const bool isTileset = Xora2D::IsSupportedTilesetExtension(absolute);
        std::string validation;
        if (isTileset) {
            const Xora2D::TilesetLoadResult check = Xora2D::TilesetSerializer::Load(absolute, &assetManager);
            validation = check.Success ? std::string{} : check.Message;
        } else {
            Xora2D::TilemapComponent data;
            std::string name;
            if (!Xora2D::TilemapSerializer::LoadAsset(absolute, data, name, validation)) {
                validation = validation.empty() ? "invalid tilemap" : validation;
            } else {
                validation.clear();
            }
        }
        if (!validation.empty()) {
            return Fail(std::string(isTileset ? "Invalid tileset: " : "Invalid tilemap: ") + validation);
        }
        const std::string relative = *AssetRelativePathFromResourcePath(*path);
        const Xora2D::AssetResult registered = isTileset ? assetManager.RegisterTilesetAtRelativePath(relative)
                                                         : assetManager.RegisterTilemapAtRelativePath(relative);
        FilesystemOperationResult result{};
        result.Success = registered.Success;
        result.Message = registered.Success ? (isTileset ? "Tileset registered" : "Tilemap registered") : registered.Message;
        result.ResultPath = *path;
        return result;
    }
    if (Xora2D::IsSupportedPrefabExtension(absolute)) {
        // Prefab files already inside the project (copied in, or registry entry lost) are registered in place after
        // validation; the stable path-derived handle is used.
        const Xora2D::PrefabLoadResult check = Xora2D::PrefabSerializer::Load(absolute, &assetManager);
        if (!check.Success) {
            return Fail("Invalid prefab: " + check.Message);
        }
        const Xora2D::AssetResult registered = assetManager.RegisterPrefabAtRelativePath(
            *AssetRelativePathFromResourcePath(*path), check.Loaded->GetName());
        FilesystemOperationResult result{};
        result.Success = registered.Success;
        result.Message = registered.Success ? "Prefab registered" : registered.Message;
        result.ResultPath = *path;
        return result;
    }
    std::string reason;
    const std::optional<ImportCategory> category = ClassifyImportSource(absolute, reason);
    if (!category.has_value() || *category == ImportCategory::Scene) {
        return Fail(reason.empty() ? "This file type is not a registry asset" : reason);
    }
    StagedImport staged{};
    staged.Source = absolute;
    staged.Category = *category;
    if (!ValidateImportFile(absolute, *category, staged.Message, staged.Warning)) {
        return Fail("Validation failed: " + staged.Message);
    }
    staged.Success = true;
    staged.DestinationResourcePath = *path;
    const ContentImportItemResult committed = CommitImport(staged, assetManager);
    FilesystemOperationResult result{};
    result.Success = committed.Success;
    result.Message = committed.Message;
    result.ResultPath = *path;
    if (!committed.Warning.empty()) {
        result.Warnings.push_back(committed.Warning);
    }
    return result;
}

FilesystemOperationResult ForgetMissingAsset(Xora2D::AssetHandle handle, Xora2D::AssetManager& assetManager) {
    const Xora2D::AssetMetadata* meta = assetManager.GetRegistry().FindByHandle(handle);
    if (meta == nullptr) {
        return Fail("Asset is not registered");
    }
    std::error_code errorCode;
    if (std::filesystem::exists(Xora2D::ResolveAssetPath(meta->RelativePath), errorCode)) {
        return Fail("The file exists; use Delete instead");
    }
    assetManager.UnloadAsset(handle);
    (void)assetManager.GetRegistry().RemoveAsset(handle);
    FilesystemOperationResult result{};
    result.Success = true;
    return result;
}

RegistrySyncReport SyncRegistryWithDisk(Xora2D::AssetRegistry& registry) {
    RegistrySyncReport report{};
    for (const Xora2D::AssetMetadata& meta : registry.ListAssets()) {
        std::error_code errorCode;
        const bool exists = std::filesystem::is_regular_file(Xora2D::ResolveAssetPath(meta.RelativePath), errorCode);
        if (exists != meta.Available) {
            registry.SetAssetAvailable(meta.Handle, exists);
            if (exists) {
                ++report.Restored;
            } else {
                ++report.NewlyMissing;
            }
        }
        if (!exists) {
            report.Missing.push_back(meta.Handle);
        }
    }
    return report;
}

void CleanupStaleContentArtifacts() {
    const std::filesystem::path root = GetResourcesRootPath();
    std::error_code errorCode;
    std::filesystem::remove_all(root / kTrashFolderName, errorCode);
    std::vector<std::filesystem::path> stale;
    std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied,
                                                           errorCode);
    for (const std::filesystem::recursive_directory_iterator end; !errorCode && iterator != end;
         iterator.increment(errorCode)) {
        const std::string name = iterator->path().filename().string();
        if (name.starts_with(kImportTempPrefix)) {
            stale.push_back(iterator->path());
        } else if (IsHiddenEntryName(name)) {
            iterator.disable_recursion_pending();
        }
    }
    for (const std::filesystem::path& file : stale) {
        std::filesystem::remove(file, errorCode);
    }
}

} // namespace Xora2DEditor
