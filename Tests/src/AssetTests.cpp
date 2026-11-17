#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Assets/AssetRegistry.h"
#include "Xora2D/Assets/AssetRegistrySerializer.h"
#include "Xora2D/Core/Logger.h"
#include "Xora2D/Core/Paths.h"
#include "Xora2D/Serialization/SceneSerializer.h"
#include "Xora2D/Scene/Components.h"
#include "Xora2D/Scene/Scene.h"

#include <fstream>
#include <filesystem>
#include <string>

namespace {

int g_assetFailures = 0;

void Check(bool condition, const std::string& description, const char* file, int line) {
    if (condition) {
        XORA_LOG_INFO("  pass: {}", description);
        return;
    }
    XORA_LOG_ERROR("  fail: {} ({}:{})", description, file, line);
    ++g_assetFailures;
}

#define CHECK(condition, description) Check(static_cast<bool>(condition), description, __FILE__, __LINE__)

std::filesystem::path TempRegistryPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("xora2d_registry_" + name + ".yaml");
}

void RemoveIfExists(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

} // namespace

int RunAssetTests() {
    const int before = g_assetFailures;
    XORA_LOG_INFO("TEST asset system started");

    const Xora2D::AssetHandle stable = Xora2D::AssetHandle::StableFromRelativePath("Textures/unit.png");
    CHECK(stable.IsValid(), "stable asset handle from relative path");
    CHECK(Xora2D::AssetHandle::FromString(stable.ToString()).value_or(Xora2D::AssetHandle()) == stable,
          "asset handle round-trips through string");

    Xora2D::AssetRegistry registry;
    Xora2D::AssetMetadata metadata{};
    metadata.Handle = stable;
    metadata.Type = Xora2D::AssetType::Texture2D;
    metadata.Name = "Unit";
    metadata.RelativePath = "Textures/unit.png";
    CHECK(registry.RegisterAsset(metadata), "register asset");
    CHECK(registry.Contains(stable), "find asset by handle");
    CHECK(registry.FindByRelativePath("Textures/unit.png") != nullptr, "find asset by relative path");
    CHECK(!registry.RegisterAsset(metadata), "reject duplicate handle");
    CHECK(registry.RemoveAsset(stable), "remove asset");
    CHECK(!registry.Contains(stable), "asset removed");

    metadata.Handle = stable;
    CHECK(registry.RegisterAsset(metadata), "re-register asset");
    CHECK(registry.ListAssets().size() == 1, "enumerate registered assets");

    const std::filesystem::path registryPath = TempRegistryPath("roundtrip");
    RemoveIfExists(registryPath);
    CHECK(Xora2D::AssetRegistrySerializer::Save(registry, registryPath).Success, "save asset registry");
    Xora2D::AssetRegistry loaded{};
    CHECK(Xora2D::AssetRegistrySerializer::Load(loaded, registryPath).Success, "load asset registry");
    CHECK(loaded.Contains(stable), "registry preserves handle across reload");

    {
        const std::filesystem::path malformed = TempRegistryPath("malformed");
        {
            std::ofstream output(malformed);
            output << "{bad yaml";
        }
        const auto result = Xora2D::AssetRegistrySerializer::Load(registry, malformed);
        CHECK(!result.Success, "reject malformed registry");
        CHECK(registry.Contains(stable), "failed registry load leaves in-memory registry unchanged");
        RemoveIfExists(malformed);

        const std::filesystem::path duplicate = TempRegistryPath("duplicate");
        std::ofstream out(duplicate);
        out << "Version: 1\nAssets:\n  - Handle: " << stable.ToString()
            << "\n    Type: Texture2D\n    Name: A\n    RelativePath: Textures/a.png\n  - Handle: "
            << stable.ToString() << "\n    Type: Texture2D\n    Name: B\n    RelativePath: Textures/b.png\n";
        CHECK(!Xora2D::AssetRegistrySerializer::Load(registry, duplicate).Success, "reject duplicate handle in file");
        RemoveIfExists(duplicate);
    }

    CHECK(Xora2D::NormalizeAssetRelativePath("../escape.png") == std::nullopt, "reject path traversal");
    CHECK(Xora2D::NormalizeAssetRelativePath("Textures/ok.png").has_value(), "accept safe relative path");

    const std::filesystem::path logoSource = Xora2D::ResolveResourcePath("Textures/Xora2D_Logo.png");
    Xora2D::AssetManager manager(registry);
    if (std::filesystem::exists(logoSource)) {
        const Xora2D::AssetResult first = manager.ImportTextureFile(logoSource, "Logo");
        CHECK(first.Success, "import valid texture");
        const Xora2D::AssetResult second = manager.ImportTextureFile(logoSource, "Logo");
        CHECK(second.Success && second.Handle == first.Handle, "re-import returns stable handle");
    }

    RemoveIfExists(registryPath);

    if (std::filesystem::exists(logoSource)) {
        auto scene = Xora2D::Scene::Create();
        Xora2D::AssetResult import = manager.ImportTextureFile(logoSource, "Logo");
        Xora2D::Entity sprite = scene->CreateEntity("LogoSprite");
        sprite.AddComponent<Xora2D::SpriteRendererComponent>().TextureAsset = import.Handle;
        scene->SetAssetManager(&manager);
        const std::filesystem::path scenePath = TempRegistryPath("scene_asset");
        RemoveIfExists(scenePath);
        CHECK(Xora2D::SceneSerializer::Serialize(*scene, scenePath).Success, "save scene with texture asset handle");
        auto loaded = Xora2D::Scene::Create();
        loaded->SetAssetManager(&manager);
        CHECK(Xora2D::SceneSerializer::Deserialize(*loaded, scenePath, &manager).Success, "load scene with asset handle");
        const Xora2D::Entity loadedSprite = loaded->FindEntityByName("LogoSprite");
        CHECK(loadedSprite.GetComponent<Xora2D::SpriteRendererComponent>().TextureAsset == import.Handle,
              "scene preserves texture asset handle");
        RemoveIfExists(scenePath);
    }

    const int failed = g_assetFailures - before;
    if (failed == 0) {
        XORA_LOG_INFO("Asset tests passed");
    } else {
        XORA_LOG_ERROR("Asset tests failed ({} checks)", failed);
    }
    return failed;
}