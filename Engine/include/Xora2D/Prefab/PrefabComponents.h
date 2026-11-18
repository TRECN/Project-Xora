#pragma once

#include "Xora2D/Assets/AssetHandle.h"
#include "Xora2D/Core/UUID.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Xora2D {

// Links a scene entity to the prefab it was instantiated from. Plain data: serialized with the scene, copied into
// Play-mode runtime scenes, never touched by runtime systems.
//
// Overrides are explicit: `Overrides` lists the property paths (see PrefabProperties) whose instance value
// intentionally differs from the prefab. The instance's own component data always holds the current values; the
// list decides what Revert restores and what an explicit Refresh preserves.
struct PrefabInstanceComponent {
    // Source prefab asset (stable registry handle; survives prefab rename / move).
    AssetHandle Prefab{};
    // Shared by every entity created by one instantiation.
    UUID InstanceId{};
    // The entity inside the prefab asset this entity was created from.
    UUID SourceEntity{};
    // Prefab revision the instance was created from / last synchronized with. A lower value than the prefab's
    // current revision means the prefab changed since (shown as "outdated"; refreshed only on request).
    std::uint32_t Revision = 0;
    // Sorted, unique property paths.
    std::vector<std::string> Overrides;
};

} // namespace Xora2D
