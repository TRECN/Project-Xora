#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Xora2D {

class AssetRegistry;

} // namespace Xora2D

namespace Xora2DEditor {

// Every path handled by the content browser is "resource-relative": relative to the project Resources/ root,
// forward slashes, no "." / ".." segments (e.g. "Assets/Textures/Hero.png"). The asset registry stores paths
// relative to Resources/Assets/, so "Assets/" + AssetMetadata::RelativePath == resource-relative path.
inline constexpr std::string_view kAssetsFolder = "Assets";
inline constexpr std::string_view kScenesFolder = "Scenes";
inline constexpr std::string_view kRegistryFolder = "Assets/Registry";
// Hidden staging folder used to make deletes atomic from the project's point of view.
inline constexpr std::string_view kTrashFolderName = ".xora_trash";
inline constexpr std::string_view kImportTempPrefix = ".xora_import_";

enum class ContentItemKind : std::uint8_t {
    Folder = 0,
    Texture,
    Audio,
    Script,
    Scene,
    Animation,
    Other,
};

enum class ContentImportStatus : std::uint8_t {
    NotApplicable = 0, // folders, scenes and unmanaged files
    Registered,        // present in the asset registry and on disk
    NotRegistered,     // supported asset type on disk but not in the registry (e.g. copied outside the editor)
    Missing,           // registered, but the file no longer exists on disk
};

struct ContentBrowserEntry {
    ContentItemKind Kind = ContentItemKind::Other;
    std::string Name;
    std::string ResourceRelativePath;
    std::optional<Xora2D::AssetHandle> RegisteredHandle;
    Xora2D::AssetType AssetType = Xora2D::AssetType::Unknown;
    ContentImportStatus Status = ContentImportStatus::NotApplicable;
    std::uint64_t FileSizeBytes = 0;
    std::filesystem::file_time_type LastModified{};

    [[nodiscard]] bool IsFolder() const { return Kind == ContentItemKind::Folder; }
    [[nodiscard]] bool IsMissing() const { return Status == ContentImportStatus::Missing; }
};

[[nodiscard]] std::filesystem::path GetResourcesRootPath();

// Normalizes a resource-relative path. Rejects absolute paths, drive letters, ".." segments, NUL/control
// characters. Empty input yields "" (the Resources root itself).
[[nodiscard]] std::optional<std::string> NormalizeResourceRelativePath(std::string_view path);
// Resolves to an absolute path inside the Resources root, or empty on unsafe input / symlink escape.
[[nodiscard]] std::filesystem::path ResolveResourceRelativePath(std::string_view resourceRelativePath);
[[nodiscard]] bool IsPathWithinResources(const std::filesystem::path& absolute);
[[nodiscard]] std::optional<std::string> AbsoluteToResourceRelative(const std::filesystem::path& absolute);

[[nodiscard]] std::optional<std::string> AssetRelativePathFromResourcePath(std::string_view resourceRelativeFile);
[[nodiscard]] std::string ResourcePathFromAssetRelativePath(std::string_view assetRelativePath);

[[nodiscard]] std::string ParentResourceDirectory(std::string_view resourceRelativePath);
[[nodiscard]] std::string JoinResourcePath(std::string_view directory, std::string_view name);
[[nodiscard]] std::string FileNameOf(std::string_view resourceRelativePath);
// True if `path` equals `ancestor` or lies inside it ("" is the ancestor of everything).
[[nodiscard]] bool IsResourcePathWithin(std::string_view path, std::string_view ancestor);

// A single file/folder name the user may create: no separators, reserved characters, leading dots or
// surrounding whitespace.
[[nodiscard]] bool IsValidEntryName(std::string_view name);
// Root folders the engine depends on, and the registry folder, cannot be renamed, moved or deleted.
[[nodiscard]] bool IsProtectedResourcePath(std::string_view resourceRelativePath);
// Hidden entries (dotfiles, import temp files, the trash folder) never appear in listings.
[[nodiscard]] bool IsHiddenEntryName(std::string_view name);

// Extension including compound suffixes such as ".scene.yaml" / ".anim.yaml" (lower-case).
[[nodiscard]] std::string FullExtensionLower(const std::filesystem::path& file);
[[nodiscard]] ContentItemKind ClassifyContentFile(const std::filesystem::path& file);
[[nodiscard]] Xora2D::AssetType AssetTypeForKind(ContentItemKind kind);
[[nodiscard]] const char* ContentItemKindLabel(ContentItemKind kind);
[[nodiscard]] const char* ContentImportStatusLabel(ContentImportStatus status);

// Lists one directory. Registered assets whose files are missing from this directory are appended as
// "ghost" entries with Status == Missing so they stay visible and recoverable.
[[nodiscard]] std::vector<ContentBrowserEntry> ListDirectory(std::string_view resourceRelativeDirectory,
                                                             const Xora2D::AssetRegistry& registry,
                                                             std::string& errorMessage);

// Lists every folder (resource-relative) below `resourceRelativeDirectory`, excluding hidden folders.
[[nodiscard]] std::vector<std::string> ListFoldersRecursive(std::string_view resourceRelativeDirectory);

[[nodiscard]] std::string FormatFileSize(std::uint64_t bytes);

} // namespace Xora2DEditor
