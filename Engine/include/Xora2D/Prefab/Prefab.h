#pragma once

#include "Xora2D/Core/UUID.h"
#include "Xora2D/Scene/Entity.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Xora2D {

class Scene;

// A reusable group of entities stored as a `.prefab.yaml` asset. The entities live in a private content Scene (never
// rendered, never simulated) under prefab-local UUIDs; instances refer to them through
// PrefabInstanceComponent::SourceEntity.
class Prefab {
public:
    static constexpr int kFormatVersion = 1;

    Prefab();

    // Deep copy (content scene included) - used to build a new revision without touching the cached one.
    [[nodiscard]] std::shared_ptr<Prefab> Clone() const;

    [[nodiscard]] const std::string& GetName() const { return m_name; }
    void SetName(std::string name) { m_name = std::move(name); }

    // Incremented on every save of the asset (apply from an instance, prefab editor save).
    [[nodiscard]] std::uint32_t GetRevision() const { return m_revision; }
    void SetRevision(std::uint32_t revision) { m_revision = revision; }

    [[nodiscard]] Scene& GetContent() { return *m_content; }
    [[nodiscard]] const Scene& GetContent() const { return *m_content; }
    [[nodiscard]] const std::shared_ptr<Scene>& GetContentScene() const { return m_content; }

    // Entities in file order (also the instantiation and draw order).
    [[nodiscard]] const std::vector<UUID>& GetEntityIds() const { return m_order; }
    [[nodiscard]] std::size_t GetEntityCount() const { return m_order.size(); }
    [[nodiscard]] Entity FindEntity(const UUID& id) const;

    // Adds an empty entity (Tag + Transform) with the given prefab-local id.
    Entity CreateEntity(const std::string& name, const UUID& id);
    // Re-reads the order from the content scene after external edits (prefab editing session). Existing ids keep
    // their position; new ones are appended; destroyed ones dropped.
    void SyncOrderFromContent();

private:
    std::string m_name;
    std::uint32_t m_revision = 1;
    std::shared_ptr<Scene> m_content;
    std::vector<UUID> m_order;
};

} // namespace Xora2D
