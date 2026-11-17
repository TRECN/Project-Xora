#include "Xora2D/Core/Paths.h"

#include <SDL3/SDL.h>

#include <cstdlib>

namespace Xora2D {

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

} // namespace Xora2D