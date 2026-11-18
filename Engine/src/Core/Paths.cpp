#include "Xora2D/Core/Paths.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <mutex>
#include <optional>

namespace Xora2D {

namespace {

std::mutex& ContentRootMutex() {
    static std::mutex mutex;
    return mutex;
}

std::optional<std::filesystem::path>& ContentRootOverride() {
    static std::optional<std::filesystem::path> root;
    return root;
}

} // namespace

std::filesystem::path GetResourceRoot() {
    if (const char* environment = std::getenv("XORA2D_RESOURCES")) {
        return std::filesystem::path(environment);
    }
#ifdef XORA2D_RESOURCE_DIR
    if (std::filesystem::is_directory(XORA2D_RESOURCE_DIR)) {
        return std::filesystem::path(XORA2D_RESOURCE_DIR);
    }
#endif
    if (const char* basePath = SDL_GetBasePath()) {
        const std::filesystem::path base(basePath);
        SDL_free(const_cast<char*>(basePath));
        const std::filesystem::path nextToExecutable = base / "Resources";
        if (std::filesystem::is_directory(nextToExecutable)) {
            return nextToExecutable;
        }
        const std::filesystem::path parent = base / ".." / "Resources";
        if (std::filesystem::is_directory(parent)) {
            return std::filesystem::weakly_canonical(parent);
        }
    }
    return std::filesystem::path("Resources");
}

std::filesystem::path ResolveResourcePath(const std::string& relativePath) {
    return GetResourceRoot() / relativePath;
}

std::filesystem::path GetContentRoot() {
    {
        std::scoped_lock lock(ContentRootMutex());
        if (ContentRootOverride().has_value()) {
            return *ContentRootOverride();
        }
    }
    return GetResourceRoot();
}

std::filesystem::path ResolveContentPath(const std::string& relativePath) {
    return GetContentRoot() / relativePath;
}

void SetContentRoot(const std::filesystem::path& root) {
    std::scoped_lock lock(ContentRootMutex());
    ContentRootOverride() = root;
}

void ResetContentRoot() {
    std::scoped_lock lock(ContentRootMutex());
    ContentRootOverride().reset();
}

bool HasContentRootOverride() {
    std::scoped_lock lock(ContentRootMutex());
    return ContentRootOverride().has_value();
}

} // namespace Xora2D
