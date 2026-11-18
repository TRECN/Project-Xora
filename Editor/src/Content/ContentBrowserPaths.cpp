#include "Xora2DEditor/Content/ContentBrowserPaths.h"

#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Core/Paths.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace Xora2DEditor {
namespace {

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

} // namespace

std::filesystem::path GetResourcesRootPath() {
    std::error_code errorCode;
    const std::filesystem::path root = Xora2D::GetContentRoot();
    std::filesystem::path canonical = std::filesystem::weakly_canonical(root, errorCode);
    if (errorCode) {
        return root;
    }
    return canonical;
}

std::optional<std::string> NormalizeResourceRelativePath(std::string_view path) {
    std::string result;
    if (path.empty()) {
        return result;
    }
    if (path.front() == '/' || path.front() == '\\') {
        return std::nullopt;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view segment = path.substr(start, end - start);
        if (!segment.empty() && segment != ".") {
            if (segment == "..") {
                return std::nullopt;
            }
            for (const char character : segment) {
                if (static_cast<unsigned char>(character) < 0x20 || character == ':') {
                    // Control characters and drive letters / alternate data streams are never valid.
                    return std::nullopt;
                }
            }
            if (!result.empty()) {
                result.push_back('/');
            }
            result.append(segment);
        }
        start = end + 1;
    }
    return result;
}

std::filesystem::path ResolveResourceRelativePath(std::string_view resourceRelativePath) {
    const std::optional<std::string> normalized = NormalizeResourceRelativePath(resourceRelativePath);
    if (!normalized.has_value()) {
        return {};
    }
    const std::filesystem::path root = GetResourcesRootPath();
    const std::filesystem::path absolute = normalized->empty() ? root : root / *normalized;
    // Guards against symlinks inside the project that point outside of it.
    if (!IsPathWithinResources(absolute)) {
        return {};
    }
    return absolute;
}

bool IsPathWithinResources(const std::filesystem::path& absolute) {
    return Xora2D::IsAbsolutePathWithinRoot(absolute, GetResourcesRootPath());
}

std::optional<std::string> AbsoluteToResourceRelative(const std::filesystem::path& absolute) {
    if (!IsPathWithinResources(absolute)) {
        return std::nullopt;
    }
    std::error_code errorCode;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, errorCode);
    if (errorCode) {
        return std::nullopt;
    }
    const std::filesystem::path relative = canonical.lexically_relative(GetResourcesRootPath());
    if (relative.empty()) {
        return std::nullopt;
    }
    const std::string generic = relative.generic_string();
    return NormalizeResourceRelativePath(generic == "." ? std::string{} : generic);
}

std::optional<std::string> AssetRelativePathFromResourcePath(std::string_view resourceRelativeFile) {
    const std::optional<std::string> normalized = NormalizeResourceRelativePath(resourceRelativeFile);
    if (!normalized.has_value()) {
        return std::nullopt;
    }
    const std::string prefix = std::string(kAssetsFolder) + "/";
    if (!normalized->starts_with(prefix) || normalized->size() == prefix.size()) {
        return std::nullopt;
    }
    return Xora2D::NormalizeAssetRelativePath(normalized->substr(prefix.size()));
}

std::string ResourcePathFromAssetRelativePath(std::string_view assetRelativePath) {
    return JoinResourcePath(kAssetsFolder, assetRelativePath);
}

std::string ParentResourceDirectory(std::string_view resourceRelativePath) {
    const std::size_t slash = resourceRelativePath.find_last_of('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    return std::string(resourceRelativePath.substr(0, slash));
}

std::string JoinResourcePath(std::string_view directory, std::string_view name) {
    if (directory.empty()) {
        return std::string(name);
    }
    if (name.empty()) {
        return std::string(directory);
    }
    std::string joined(directory);
    joined.push_back('/');
    joined.append(name);
    return joined;
}

std::string FileNameOf(std::string_view resourceRelativePath) {
    const std::size_t slash = resourceRelativePath.find_last_of('/');
    return std::string(slash == std::string_view::npos ? resourceRelativePath : resourceRelativePath.substr(slash + 1));
}

bool IsResourcePathWithin(std::string_view path, std::string_view ancestor) {
    if (ancestor.empty()) {
        return true;
    }
    if (!path.starts_with(ancestor)) {
        return false;
    }
    return path.size() == ancestor.size() || path[ancestor.size()] == '/';
}

bool IsValidEntryName(std::string_view name) {
    if (name.empty() || name.size() > 255 || name == "." || name == "..") {
        return false;
    }
    if (name.front() == '.' || name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return false;
    }
    constexpr std::string_view kReserved = "/\\:*?\"<>|";
    for (const char character : name) {
        if (static_cast<unsigned char>(character) < 0x20 || kReserved.find(character) != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

bool IsProtectedResourcePath(std::string_view resourceRelativePath) {
    static constexpr std::array<std::string_view, 5> kProtectedRoots = {"Assets", "Scenes", "Shaders", "Input",
                                                                        "Assets/Registry"};
    if (resourceRelativePath.empty()) {
        return true;
    }
    for (const std::string_view root : kProtectedRoots) {
        if (resourceRelativePath == root) {
            return true;
        }
    }
    return IsResourcePathWithin(resourceRelativePath, kRegistryFolder);
}

bool IsHiddenEntryName(std::string_view name) {
    return name.empty() || name.front() == '.';
}

std::string FullExtensionLower(const std::filesystem::path& file) {
    const std::string name = ToLower(file.filename().string());
    for (const std::string_view compound :
         {std::string_view(".scene.yaml"), std::string_view(".anim.yaml"), std::string_view(".prefab.yaml")}) {
        if (name.size() > compound.size() && name.ends_with(compound)) {
            return std::string(compound);
        }
    }
    return ToLower(file.extension().string());
}

ContentItemKind ClassifyContentFile(const std::filesystem::path& file) {
    if (Xora2D::IsSupportedPrefabExtension(file)) {
        return ContentItemKind::Prefab;
    }
    if (Xora2D::IsSupportedSceneExtension(file)) {
        return ContentItemKind::Scene;
    }
    if (Xora2D::IsSupportedAnimationClipExtension(file)) {
        return ContentItemKind::Animation;
    }
    if (Xora2D::IsSupportedTextureExtension(file)) {
        return ContentItemKind::Texture;
    }
    if (Xora2D::IsSupportedAudioExtension(file)) {
        return ContentItemKind::Audio;
    }
    if (Xora2D::IsSupportedPythonScriptExtension(file)) {
        return ContentItemKind::Script;
    }
    return ContentItemKind::Other;
}

Xora2D::AssetType AssetTypeForKind(ContentItemKind kind) {
    switch (kind) {
    case ContentItemKind::Texture:
        return Xora2D::AssetType::Texture2D;
    case ContentItemKind::Audio:
        return Xora2D::AssetType::Audio;
    case ContentItemKind::Script:
        return Xora2D::AssetType::PythonScript;
    case ContentItemKind::Animation:
        return Xora2D::AssetType::AnimationClip;
    case ContentItemKind::Prefab:
        return Xora2D::AssetType::Prefab;
    default:
        return Xora2D::AssetType::Unknown;
    }
}

const char* ContentItemKindLabel(ContentItemKind kind) {
    switch (kind) {
    case ContentItemKind::Folder:
        return "Folder";
    case ContentItemKind::Texture:
        return "Texture";
    case ContentItemKind::Audio:
        return "Audio";
    case ContentItemKind::Script:
        return "Python Script";
    case ContentItemKind::Scene:
        return "Scene";
    case ContentItemKind::Animation:
        return "Animation Clip";
    case ContentItemKind::Prefab:
        return "Prefab";
    default:
        return "File";
    }
}

const char* ContentImportStatusLabel(ContentImportStatus status) {
    switch (status) {
    case ContentImportStatus::Registered:
        return "Imported";
    case ContentImportStatus::NotRegistered:
        return "Not imported";
    case ContentImportStatus::Missing:
        return "Missing";
    default:
        return "-";
    }
}

std::vector<ContentBrowserEntry> ListDirectory(std::string_view resourceRelativeDirectory,
                                               const Xora2D::AssetRegistry& registry, std::string& errorMessage) {
    errorMessage.clear();
    const std::optional<std::string> directory = NormalizeResourceRelativePath(resourceRelativeDirectory);
    if (!directory.has_value()) {
        errorMessage = "Invalid or unsafe folder path";
        return {};
    }
    const std::filesystem::path absolute = ResolveResourceRelativePath(*directory);
    if (absolute.empty()) {
        errorMessage = "Folder is outside the project";
        return {};
    }
    std::error_code errorCode;
    if (!std::filesystem::exists(absolute, errorCode)) {
        errorMessage = "Folder does not exist (it may have been moved or deleted outside the editor)";
        return {};
    }
    if (!std::filesystem::is_directory(absolute, errorCode)) {
        errorMessage = "Path is not a folder";
        return {};
    }

    std::vector<ContentBrowserEntry> entries;
    std::filesystem::directory_iterator iterator(absolute, std::filesystem::directory_options::skip_permission_denied,
                                                 errorCode);
    if (errorCode) {
        errorMessage = "Folder cannot be read: " + errorCode.message();
        return {};
    }
    for (const std::filesystem::directory_iterator end; iterator != end; iterator.increment(errorCode)) {
        if (errorCode) {
            errorMessage = "Failed while reading folder: " + errorCode.message();
            break;
        }
        const std::filesystem::directory_entry& entry = *iterator;
        ContentBrowserEntry item{};
        item.Name = entry.path().filename().string();
        if (IsHiddenEntryName(item.Name)) {
            continue;
        }
        item.ResourceRelativePath = JoinResourcePath(*directory, item.Name);

        std::error_code statError;
        if (entry.is_directory(statError)) {
            // Symlinked folders that escape the project are skipped rather than followed.
            if (!IsPathWithinResources(entry.path())) {
                continue;
            }
            item.Kind = ContentItemKind::Folder;
            item.LastModified = entry.last_write_time(statError);
            entries.push_back(std::move(item));
            continue;
        }
        if (!entry.is_regular_file(statError)) {
            continue;
        }
        item.Kind = ClassifyContentFile(entry.path());
        item.AssetType = AssetTypeForKind(item.Kind);
        item.FileSizeBytes = entry.file_size(statError);
        item.LastModified = entry.last_write_time(statError);
        if (const auto assetRelative = AssetRelativePathFromResourcePath(item.ResourceRelativePath)) {
            if (const Xora2D::AssetMetadata* meta = registry.FindByRelativePath(*assetRelative)) {
                item.RegisteredHandle = meta->Handle;
                item.AssetType = meta->Type;
                item.Status = ContentImportStatus::Registered;
            } else if (item.AssetType != Xora2D::AssetType::Unknown) {
                item.Status = ContentImportStatus::NotRegistered;
            }
        }
        entries.push_back(std::move(item));
    }

    // Ghost entries for registered assets whose files disappeared from this folder.
    if (const auto assetDirectory = AssetRelativePathFromResourcePath(JoinResourcePath(*directory, "x"))) {
        const std::string assetFolder = ParentResourceDirectory(*assetDirectory);
        for (const Xora2D::AssetMetadata& meta : registry.ListAssets()) {
            if (ParentResourceDirectory(meta.RelativePath) != assetFolder) {
                continue;
            }
            const std::string resourcePath = ResourcePathFromAssetRelativePath(meta.RelativePath);
            const bool listed = std::any_of(entries.begin(), entries.end(), [&](const ContentBrowserEntry& item) {
                return item.ResourceRelativePath == resourcePath;
            });
            if (listed) {
                continue;
            }
            ContentBrowserEntry ghost{};
            ghost.Name = FileNameOf(resourcePath);
            ghost.ResourceRelativePath = resourcePath;
            ghost.Kind = ClassifyContentFile(ghost.Name);
            ghost.RegisteredHandle = meta.Handle;
            ghost.AssetType = meta.Type;
            ghost.Status = ContentImportStatus::Missing;
            entries.push_back(std::move(ghost));
        }
    }
    return entries;
}

std::vector<std::string> ListFoldersRecursive(std::string_view resourceRelativeDirectory) {
    std::vector<std::string> folders;
    const std::optional<std::string> directory = NormalizeResourceRelativePath(resourceRelativeDirectory);
    if (!directory.has_value()) {
        return folders;
    }
    const std::filesystem::path absolute = ResolveResourceRelativePath(*directory);
    std::error_code errorCode;
    if (absolute.empty() || !std::filesystem::is_directory(absolute, errorCode)) {
        return folders;
    }
    std::filesystem::recursive_directory_iterator iterator(
        absolute, std::filesystem::directory_options::skip_permission_denied, errorCode);
    for (const std::filesystem::recursive_directory_iterator end; !errorCode && iterator != end;
         iterator.increment(errorCode)) {
        const std::string name = iterator->path().filename().string();
        std::error_code statError;
        if (!iterator->is_directory(statError)) {
            continue;
        }
        if (IsHiddenEntryName(name) || !IsPathWithinResources(iterator->path())) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (const auto relative = AbsoluteToResourceRelative(iterator->path())) {
            folders.push_back(*relative);
        }
    }
    std::sort(folders.begin(), folders.end());
    return folders;
}

std::string FormatFileSize(std::uint64_t bytes) {
    char buffer[32];
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    return buffer;
}

} // namespace Xora2DEditor
