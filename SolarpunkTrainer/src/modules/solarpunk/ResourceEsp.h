#pragma once

#include "SolarpunkRuntime.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Solarpunk::ResourceEsp {

    enum class Kind {
        Tree,
        Ore,
        Plant,
        Pickup,
        DroppedItem,
        Loot,
        Animal,
        Npc
    };

    enum class ScanStatus {
        Disabled,
        WaitingForWorld,
        NamePoolUnavailable,
        Ready
    };

    struct Settings {
        bool Enabled = true;
        bool ShowTrees = true;
        bool ShowOres = true;
        bool ShowPlants = true;
        bool ShowPickups = true;
        bool ShowDroppedItems = true;
        bool ShowLoot = true;
        bool ShowAnimals = true;
        bool ShowNpcs = true;
        bool ShowLabels = true;
        bool ShowDistance = true;
        float MaxDistanceMeters = 250.0f;
    };

    struct Marker {
        uintptr_t Actor = 0;
        Kind ResourceKind = Kind::Tree;
        Vector3 Location{};
        float DistanceMeters = 0.0f;
        std::string RawClassName;
        std::string DisplayName;
    };

    struct State {
        ScanStatus Status = ScanStatus::Disabled;
        uint32_t LevelsScanned = 0;
        uint32_t ActorsScanned = 0;
        uint32_t TreeCount = 0;
        uint32_t OreCount = 0;
        uint32_t PlantCount = 0;
        uint32_t PickupCount = 0;
        uint32_t DroppedItemCount = 0;
        uint32_t LootCount = 0;
        uint32_t AnimalCount = 0;
        uint32_t NpcCount = 0;
        uint32_t FoliageInstancesScanned = 0;
        uint32_t LoggedClassCount = 0;
        uint64_t ScanGeneration = 0;
        std::string ClassLogPath;

        size_t MarkerCount() const {
            return static_cast<size_t>(TreeCount)
                + static_cast<size_t>(OreCount)
                + static_cast<size_t>(PlantCount)
                + static_cast<size_t>(PickupCount)
                + static_cast<size_t>(DroppedItemCount)
                + static_cast<size_t>(LootCount)
                + static_cast<size_t>(AnimalCount)
                + static_cast<size_t>(NpcCount);
        }
    };

    Settings& GetSettings();
    const State& GetState();
    const std::vector<Marker>& GetMarkers();

    void Update(const PlayerSnapshot& player);
    void Reset();

    bool IsKindVisible(Kind kind);
    const char* GetStatusText(ScanStatus status);

} // namespace Solarpunk::ResourceEsp
