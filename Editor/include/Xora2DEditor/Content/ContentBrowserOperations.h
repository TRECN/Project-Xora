#pragma once

#include "Xora2D/Assets/AssetHandle.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Xora2D {

class AssetManager;
class AssetRegistry;
class Scene;

} // namespace Xora2D

namespace Xora2DEditor {

struct AssetReference {
    Xora2D::AssetHandle Handle{};
    std::string AssetPath;      // resource-relative path of the referenced asset
    std::string ReferencedBy;   // resource-relative file, or "<open scene>" for the scene in the editor
    std::string Detail;         // e.g. "SpriteRendererComponent on 'Hero'"
};

struct FilesystemOperationResult {
    bool Success = false;
    std::string Message;
    // Warnings that did not fail the operation (e.g. a reference file that could not be rewritten).
    std::vector<std::string> Warnings;
    // Set by delete when it refused because the assets are still referenced.
    std::vector<AssetReference> References;
    // Path of the item after a successful create/rename/move.
    std::string ResultPath;
    std::size_t RelocatedAssetCount = 0;
    std::size_t UpdatedReferenceFiles = 0;
};

// Creates `name` inside `parentDirectory` (both resource-relative).
[[nodiscard]] FilesystemOperationResult CreateProjectFolder(const std::string& parentDirectory, const std::string& name);

// Renames a file or folder in place. For files, the extension is kept: "Hero" or "Hero.png" both rename
// "Player.png" to "Hero.png"; changing the extension is rejected because it would change the asset type.
// Registered assets keep their AssetHandle, so scenes referencing them by handle stay valid.
[[nodiscard]] FilesystemOperationResult RenameResourceEntry(const std::string& resourceRelativePath,
                                                            const std::string& newName,
                                                            Xora2D::AssetRegistry& registry);

// Moves a file or folder into another folder. Same handle-preservation guarantees as rename.
[[nodiscard]] FilesystemOperationResult MoveResourceEntry(const std::string& resourceRelativePath,
                                                          const std::string& destinationDirectory,
                                                          Xora2D::AssetRegistry& registry);

// Registered assets at or below `resourceRelativePath`.
[[nodiscard]] std::vector<Xora2D::AssetHandle> CollectAssetsUnder(const std::string& resourceRelativePath,
                                                                  const Xora2D::AssetRegistry& registry);

// Finds references to `handles` from scene files (by handle), animation clips (TextureAssetHandle or
// TextureRelativePath) and, optionally, the scene currently open in the editor.
[[nodiscard]] std::vector<AssetReference> FindAssetReferences(const std::vector<Xora2D::AssetHandle>& handles,
                                                              const Xora2D::AssetRegistry& registry,
                                                              const Xora2D::Scene* openScene = nullptr);

struct DeleteOptions {
    // Delete even though references were found (the user confirmed the warning).
    bool AllowReferenced = false;
    const Xora2D::Scene* OpenScene = nullptr;
};

// Deletes a file or folder. The item is first moved into a hidden trash folder (atomic), then registry entries
// for contained assets are removed, then the trash is purged. Without AllowReferenced, deletion of referenced
// assets is refused and the references are returned.
[[nodiscard]] FilesystemOperationResult DeleteResourceEntry(const std::string& resourceRelativePath,
                                                            Xora2D::AssetManager& assetManager,
                                                            const DeleteOptions& options = {});

// Registers a supported file that exists on disk but not in the registry (e.g. copied in outside the editor).
[[nodiscard]] FilesystemOperationResult RegisterExistingFile(const std::string& resourceRelativePath,
                                                             Xora2D::AssetManager& assetManager);

// Removes a registry entry whose file is missing. Refuses if the file actually exists.
[[nodiscard]] FilesystemOperationResult ForgetMissingAsset(Xora2D::AssetHandle handle,
                                                           Xora2D::AssetManager& assetManager);

struct RegistrySyncReport {
    std::vector<Xora2D::AssetHandle> Missing;
    std::size_t NewlyMissing = 0;
    std::size_t Restored = 0;
};

// Updates AssetMetadata::Available from the filesystem; detects files deleted or restored outside the editor.
RegistrySyncReport SyncRegistryWithDisk(Xora2D::AssetRegistry& registry);

// Removes leftover import temp files and the delete trash folder (e.g. after a crash).
void CleanupStaleContentArtifacts();

} // namespace Xora2DEditor
