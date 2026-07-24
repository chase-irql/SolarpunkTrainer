#include "ResourceEsp.h"
#include "ResourceNames.h"
#include "GameSchema.h"

#include <memory/Memory.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

    namespace Offsets {
        constexpr uintptr_t World_PersistentLevel = 0x0030;
        constexpr uintptr_t World_Levels = 0x01C8;

        constexpr uintptr_t Actor_RootComponent = 0x01B8;
        constexpr uintptr_t Actor_InstanceComponents = 0x0288;
        constexpr uintptr_t SceneComponent_RelativeLocation = 0x0148;

        constexpr uintptr_t InstancedStaticMesh_PerInstanceData = 0x0638;
        constexpr uintptr_t FoliageComponent_SpawningActor = 0x0BD8;
    }

    constexpr uint32_t MaxLevelCount = 1024;
    constexpr uint32_t MaxActorsPerLevel = 100000;
    constexpr uint32_t MaxActorsPerScan = 250000;
    constexpr uint32_t MaxComponentsPerActor = 4096;
    constexpr uint32_t MaxInstancesPerComponent = 250000;
    constexpr size_t MaxMarkersPerScan = 8192;
    constexpr uintptr_t InstanceTransformStride = 0x80;
    constexpr uintptr_t InstanceTransformTranslation = 0x60;
    constexpr uint32_t MaxNameBlocks = 0x2000;
    constexpr uint32_t MaxNameLength = 1023;
    constexpr auto ScanInterval = std::chrono::milliseconds(350);
    constexpr size_t LogPathCapacity = 1024;
    constexpr DWORD MaximumExistingLogBytes = 4 * 1024 * 1024;

    struct TArrayHeader {
        uintptr_t Data = 0;
        int32_t Count = 0;
        int32_t Capacity = 0;
    };

    struct RawFName {
        int32_t ComparisonIndex = 0;
        uint32_t Number = 0;
    };

    struct ClassInfo {
        Solarpunk::ResourceEsp::Kind Kind =
            Solarpunk::ResourceEsp::Kind::Tree;
        bool IsResource = false;
        std::string RawName;
    };

    struct ClassFamilyRule {
        const char* BaseClassName;
        Solarpunk::ResourceEsp::Kind Kind;
    };

    // Add another SDK class family here to extend the scanner without touching
    // actor enumeration or rendering.
    constexpr std::array<ClassFamilyRule, 12> ClassFamilyRules{ {
        { "_BP_Tree_MASTER_C", Solarpunk::ResourceEsp::Kind::Tree },
        { "BP_Ore_MASTER_C", Solarpunk::ResourceEsp::Kind::Ore },
        { "BP_Orepatch_MASTER_C", Solarpunk::ResourceEsp::Kind::Ore },
        { "_BP_Plant_MASTER_C", Solarpunk::ResourceEsp::Kind::Plant },
        { "_BP_LocalPlant_MASTER_C", Solarpunk::ResourceEsp::Kind::Plant },
        { "BP_GrabItem_MASTER_C", Solarpunk::ResourceEsp::Kind::Pickup },
        { "_BP_RandomLootchest_MASTER_C", Solarpunk::ResourceEsp::Kind::Loot },
        { "BP_Animal_MASTER_C", Solarpunk::ResourceEsp::Kind::Animal },
        { "BP_AnimalDrone_C", Solarpunk::ResourceEsp::Kind::Animal },
        { "_BP_Tradebot_MASTER_C", Solarpunk::ResourceEsp::Kind::Npc },
        { "BP_Merchant_C", Solarpunk::ResourceEsp::Kind::Npc },
        { "_BP_ItemActor_MASTER_C", Solarpunk::ResourceEsp::Kind::DroppedItem }
    } };

    Solarpunk::ResourceEsp::Settings gSettings{};
    Solarpunk::ResourceEsp::State gState{};
    std::vector<Solarpunk::ResourceEsp::Marker> gMarkers;
    std::unordered_map<uintptr_t, ClassInfo> gClassCache;
    std::unordered_set<std::string> gLoggedClassKeys;
    std::wstring gClassLogPath;
    std::string gClassLogPathUtf8;
    bool gClassLogInitialized = false;
    uintptr_t gLastWorld = 0;
    std::chrono::steady_clock::time_point gNextScan{};

    bool IsFinite(const Solarpunk::Vector3& value) {
        return std::isfinite(value.X)
            && std::isfinite(value.Y)
            && std::isfinite(value.Z);
    }

    bool TryReadPointer(uintptr_t address, uintptr_t& value) {
        const auto pointer = Memory::TryRead<uintptr_t>(address);
        if (!pointer || !Memory::IsValidPtr(*pointer)) {
            value = 0;
            return false;
        }

        value = *pointer;
        return true;
    }

    bool IsValidArray(
        const TArrayHeader& array,
        uint32_t maximumCount) {
        return array.Count >= 0
            && array.Capacity >= array.Count
            && static_cast<uint32_t>(array.Count) <= maximumCount
            && (array.Count == 0 || Memory::IsValidPtr(array.Data));
    }

    const char* KindText(Solarpunk::ResourceEsp::Kind kind) {
        using Solarpunk::ResourceEsp::Kind;
        switch (kind) {
        case Kind::Tree:
            return "Tree";
        case Kind::Ore:
            return "Ore";
        case Kind::Plant:
            return "Plant";
        case Kind::Pickup:
            return "Pickup";
        case Kind::DroppedItem:
            return "DroppedItem";
        case Kind::Loot:
            return "Loot";
        case Kind::Animal:
            return "Animal";
        case Kind::Npc:
            return "Npc";
        default:
            return "Unknown";
        }
    }

    std::string WideToUtf8(std::wstring_view value) {
        if (value.empty())
            return {};

        const int length = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (length <= 0)
            return {};

        std::string utf8(static_cast<size_t>(length), '\0');
        if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            utf8.data(),
            length,
            nullptr,
            nullptr) != length) {
            return {};
        }
        return utf8;
    }

    bool BuildClassLogPath(
        wchar_t (&path)[LogPathCapacity]) {
        path[0] = L'\0';
        const DWORD length = GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            path,
            static_cast<DWORD>(LogPathCapacity));
        if (!length || length >= LogPathCapacity)
            return false;

        if (wcscat_s(
            path,
            LogPathCapacity,
            L"\\SolarpunkTrainer") != 0) {
            return false;
        }

        if (!CreateDirectoryW(path, nullptr)
            && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }

        return wcscat_s(
            path,
            LogPathCapacity,
            L"\\resource_marker_classes.tsv") == 0;
    }

    bool AppendLogText(std::string_view text) {
        if (gClassLogPath.empty() || text.empty())
            return false;

        HANDLE file = CreateFileW(
            gClassLogPath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD written = 0;
        const bool success = WriteFile(
            file,
            text.data(),
            static_cast<DWORD>(text.size()),
            &written,
            nullptr) != FALSE
            && written == text.size();
        CloseHandle(file);
        return success;
    }

    void InitializeClassLog() {
        if (gClassLogInitialized)
            return;
        gClassLogInitialized = true;

        wchar_t path[LogPathCapacity]{};
        if (!BuildClassLogPath(path))
            return;

        gClassLogPath = path;
        gClassLogPathUtf8 = WideToUtf8(gClassLogPath);

        HANDLE file = CreateFileW(
            path,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        const DWORD size = GetFileSize(file, nullptr);
        std::string existing;
        if (size != INVALID_FILE_SIZE
            && size > 0
            && size <= MaximumExistingLogBytes) {
            existing.resize(size);
            DWORD read = 0;
            if (!ReadFile(
                file,
                existing.data(),
                size,
                &read,
                nullptr)) {
                existing.clear();
            }
            else {
                existing.resize(read);
            }
        }
        CloseHandle(file);

        size_t cursor = 0;
        while (cursor < existing.size()) {
            size_t end = existing.find('\n', cursor);
            if (end == std::string::npos)
                end = existing.size();
            std::string line = existing.substr(
                cursor,
                end - cursor);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty()
                && !line.starts_with("Kind\t")) {
                gLoggedClassKeys.emplace(std::move(line));
            }
            cursor = end + 1;
        }

        if (existing.empty())
            AppendLogText("Kind\tRawClassName\r\n");
    }

    void SyncClassLogState() {
        gState.LoggedClassCount =
            static_cast<uint32_t>(gLoggedClassKeys.size());
        gState.ClassLogPath = gClassLogPathUtf8;
    }

    void LogResourceClass(
        Solarpunk::ResourceEsp::Kind kind,
        const std::string& className) {
        InitializeClassLog();
        if (className.empty() || gClassLogPath.empty())
            return;

        std::string key = KindText(kind);
        key.push_back('\t');
        key += className;
        if (gLoggedClassKeys.contains(key)) {
            SyncClassLogState();
            return;
        }

        std::string line = key;
        line += "\r\n";
        if (AppendLogText(line))
            gLoggedClassKeys.emplace(std::move(key));
        SyncClassLogState();
    }

    bool TryReadAnsiName(
        uintptr_t address,
        uint32_t length,
        std::string& output) {
        output.resize(length);
        if (!length)
            return true;

        if (!Memory::ReadBytes(address, output.data(), length)) {
            output.clear();
            return false;
        }

        return true;
    }

    bool TryReadWideName(
        uintptr_t address,
        uint32_t length,
        std::string& output) {
        std::vector<wchar_t> wide(length);
        if (length
            && !Memory::ReadBytes(
                address,
                wide.data(),
                static_cast<size_t>(length) * sizeof(wchar_t))) {
            return false;
        }

        if (!length) {
            output.clear();
            return true;
        }

        const int utf8Length = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide.data(),
            static_cast<int>(length),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (utf8Length <= 0)
            return false;

        output.resize(static_cast<size_t>(utf8Length));
        return WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide.data(),
            static_cast<int>(length),
            output.data(),
            utf8Length,
            nullptr,
            nullptr) == utf8Length;
    }

    bool TryResolveName(const RawFName& name, std::string& output) {
        output.clear();
        if (name.ComparisonIndex < 0)
            return false;

        const uintptr_t imageBase = Memory::GetModuleBase(nullptr);
        if (!imageBase)
            return false;

        const auto& schema = Solarpunk::GameSchema::Get();
        const uintptr_t namePool =
            imageBase + schema.Globals.GNamesRva;
        const auto currentBlock = Memory::TryRead<uint32_t>(namePool + 0x08);
        const auto currentByteCursor =
            Memory::TryRead<uint32_t>(namePool + 0x0C);
        if (!currentBlock
            || !currentByteCursor
            || *currentBlock >= MaxNameBlocks) {
            return false;
        }

        const uint32_t index = static_cast<uint32_t>(name.ComparisonIndex);
        const uint32_t blockIndex =
            index >> schema.NamePool.BlockOffsetBits;
        const uint32_t entryIndex =
            index
            & ((1u << schema.NamePool.BlockOffsetBits) - 1u);
        const uint32_t byteOffset =
            entryIndex * schema.NamePool.EntryStride;
        if (blockIndex > *currentBlock
            || blockIndex >= MaxNameBlocks
            || (blockIndex == *currentBlock
                && byteOffset >= *currentByteCursor)) {
            return false;
        }

        uintptr_t block = 0;
        if (!TryReadPointer(
            namePool + 0x10
                + static_cast<uintptr_t>(blockIndex) * sizeof(uintptr_t),
            block)) {
            return false;
        }

        const uintptr_t entry = block + byteOffset;
        const auto header = Memory::TryRead<uint16_t>(entry);
        if (!header)
            return false;

        const bool isWide = (*header & 1u) != 0;
        const uint32_t length = *header >> 6;
        if (!length || length > MaxNameLength)
            return false;

        const bool read = isWide
            ? TryReadWideName(entry + sizeof(uint16_t), length, output)
            : TryReadAnsiName(entry + sizeof(uint16_t), length, output);
        if (!read)
            return false;

        if (name.Number > 0) {
            output.push_back('_');
            output += std::to_string(name.Number - 1);
        }

        return true;
    }

    bool TryReadObjectName(uintptr_t object, std::string& output) {
        const auto name =
            Memory::TryRead<RawFName>(
                object + Solarpunk::GameSchema::Get().UObject.Name);
        return name && TryResolveName(*name, output);
    }

    bool TryResolveClassInfo(uintptr_t classObject, ClassInfo& info) {
        if (!Memory::IsValidPtr(classObject))
            return false;

        const auto cached = gClassCache.find(classObject);
        if (cached != gClassCache.end()) {
            info = cached->second;
            if (info.IsResource)
                LogResourceClass(info.Kind, info.RawName);
            return true;
        }

        ClassInfo resolved{};
        uintptr_t currentClass = classObject;
        std::unordered_set<uintptr_t> visited;

        for (int depth = 0; depth < 24; ++depth) {
            if (!Memory::IsValidPtr(currentClass)
                || !visited.insert(currentClass).second) {
                break;
            }

            std::string className;
            if (!TryReadObjectName(currentClass, className))
                return false;

            if (depth == 0)
                resolved.RawName = className;

            for (const ClassFamilyRule& rule : ClassFamilyRules) {
                if (className == rule.BaseClassName) {
                    resolved.Kind = rule.Kind;
                    resolved.IsResource = true;
                    break;
                }
            }

            if (resolved.IsResource)
                break;

            const auto superClass = Memory::TryRead<uintptr_t>(
                currentClass + Solarpunk::GameSchema::Get().UStruct.Super);
            if (!superClass || !*superClass)
                break;

            currentClass = *superClass;
        }

        if (resolved.RawName.empty())
            return false;

        // Backpacks reuse the local-plant base for save/growth behavior, but
        // they are not harvestable world plants.
        if (resolved.Kind == Solarpunk::ResourceEsp::Kind::Plant
            && resolved.RawName.rfind("BP_Backpack", 0) == 0) {
            resolved.IsResource = false;
        }

        gClassCache.emplace(classObject, resolved);
        info = std::move(resolved);
        if (info.IsResource)
            LogResourceClass(info.Kind, info.RawName);
        return true;
    }

    double DistanceSquared(
        const Solarpunk::Vector3& a,
        const Solarpunk::Vector3& b) {
        const double x = a.X - b.X;
        const double y = a.Y - b.Y;
        const double z = a.Z - b.Z;
        return x * x + y * y + z * z;
    }

    void CountMarker(Solarpunk::ResourceEsp::Kind kind) {
        using Solarpunk::ResourceEsp::Kind;
        switch (kind) {
        case Kind::Tree:
            ++gState.TreeCount;
            break;
        case Kind::Ore:
            ++gState.OreCount;
            break;
        case Kind::Plant:
            ++gState.PlantCount;
            break;
        case Kind::Pickup:
            ++gState.PickupCount;
            break;
        case Kind::DroppedItem:
            ++gState.DroppedItemCount;
            break;
        case Kind::Loot:
            ++gState.LootCount;
            break;
        case Kind::Animal:
            ++gState.AnimalCount;
            break;
        case Kind::Npc:
            ++gState.NpcCount;
            break;
        }
    }

    void AddMarker(
        uintptr_t source,
        const ClassInfo& classInfo,
        const Solarpunk::Vector3& location,
        const Solarpunk::PlayerSnapshot& player,
        double maximumDistanceSquared) {
        if (gMarkers.size() >= MaxMarkersPerScan
            || !IsFinite(location)) {
            return;
        }

        const double distanceSquared =
            DistanceSquared(location, player.Coordinates);
        if (!std::isfinite(distanceSquared)
            || distanceSquared > maximumDistanceSquared) {
            return;
        }

        Solarpunk::ResourceEsp::Marker marker{};
        marker.Actor = source;
        marker.ResourceKind = classInfo.Kind;
        marker.Location = location;
        marker.DistanceMeters =
            static_cast<float>(std::sqrt(distanceSquared) / 100.0);
        marker.RawClassName = classInfo.RawName;
        marker.DisplayName = Solarpunk::ResourceNames::Resolve(
            marker.RawClassName);
        gMarkers.emplace_back(std::move(marker));
        CountMarker(classInfo.Kind);
    }

    void ScanFoliageActor(
        uintptr_t actor,
        const Solarpunk::PlayerSnapshot& player,
        double maximumDistanceSquared) {
        Solarpunk::Vector3 actorOffset{};
        uintptr_t actorRoot = 0;
        if (TryReadPointer(
            actor + Offsets::Actor_RootComponent,
            actorRoot)) {
            const auto actorLocation =
                Memory::TryRead<Solarpunk::Vector3>(
                    actorRoot
                        + Offsets::SceneComponent_RelativeLocation);
            if (actorLocation && IsFinite(*actorLocation))
                actorOffset = *actorLocation;
        }

        const auto components = Memory::TryRead<TArrayHeader>(
            actor + Offsets::Actor_InstanceComponents);
        if (!components
            || !IsValidArray(*components, MaxComponentsPerActor)) {
            return;
        }

        std::unordered_set<uintptr_t> visitedComponents;
        visitedComponents.reserve(
            static_cast<size_t>(components->Count));

        for (int32_t componentIndex = 0;
            componentIndex < components->Count
                && gMarkers.size() < MaxMarkersPerScan;
            ++componentIndex) {
            const auto component = Memory::TryRead<uintptr_t>(
                components->Data
                    + static_cast<uintptr_t>(componentIndex)
                        * sizeof(uintptr_t));
            if (!component
                || !Memory::IsValidPtr(*component)
                || !visitedComponents.insert(*component).second) {
                continue;
            }

            uintptr_t componentClass = 0;
            if (!TryReadPointer(
                *component + Solarpunk::GameSchema::Get().UObject.Class,
                componentClass)) {
                continue;
            }

            ClassInfo componentInfo{};
            if (!TryResolveClassInfo(componentClass, componentInfo)
                || componentInfo.RawName
                    != "BP_SolarpunkFoliageInstanced_C") {
                continue;
            }

            uintptr_t spawningClass = 0;
            if (!TryReadPointer(
                *component + Offsets::FoliageComponent_SpawningActor,
                spawningClass)) {
                continue;
            }

            ClassInfo spawningInfo{};
            if (!TryResolveClassInfo(spawningClass, spawningInfo)
                || !spawningInfo.IsResource
                || (spawningInfo.Kind
                    != Solarpunk::ResourceEsp::Kind::Tree
                    && spawningInfo.Kind
                    != Solarpunk::ResourceEsp::Kind::Ore)) {
                continue;
            }

            const auto instances = Memory::TryRead<TArrayHeader>(
                *component
                    + Offsets::InstancedStaticMesh_PerInstanceData);
            if (!instances
                || !IsValidArray(
                    *instances,
                    MaxInstancesPerComponent)) {
                continue;
            }

            Solarpunk::Vector3 componentOffset{};
            const auto relativeLocation =
                Memory::TryRead<Solarpunk::Vector3>(
                    *component
                        + Offsets::SceneComponent_RelativeLocation);
            if (relativeLocation && IsFinite(*relativeLocation))
                componentOffset = *relativeLocation;

            for (int32_t instanceIndex = 0;
                instanceIndex < instances->Count
                    && gMarkers.size() < MaxMarkersPerScan;
                ++instanceIndex) {
                const uintptr_t transform =
                    instances->Data
                    + static_cast<uintptr_t>(instanceIndex)
                        * InstanceTransformStride;
                const auto instanceLocation =
                    Memory::TryRead<Solarpunk::Vector3>(
                        transform + InstanceTransformTranslation);
                if (!instanceLocation)
                    continue;

                ++gState.FoliageInstancesScanned;
                const Solarpunk::Vector3 worldLocation{
                    instanceLocation->X
                        + componentOffset.X
                        + actorOffset.X,
                    instanceLocation->Y
                        + componentOffset.Y
                        + actorOffset.Y,
                    instanceLocation->Z
                        + componentOffset.Z
                        + actorOffset.Z
                };
                AddMarker(
                    *component,
                    spawningInfo,
                    worldLocation,
                    player,
                    maximumDistanceSquared);
            }
        }
    }

    void ScanActor(
        uintptr_t actor,
        const Solarpunk::PlayerSnapshot& player,
        double maximumDistanceSquared) {
        uintptr_t classObject = 0;
        if (!TryReadPointer(
            actor + Solarpunk::GameSchema::Get().UObject.Class,
            classObject)) {
            return;
        }

        ClassInfo classInfo{};
        if (!TryResolveClassInfo(classObject, classInfo))
            return;

        if (classInfo.RawName == "InstancedFoliageActor") {
            ScanFoliageActor(
                actor,
                player,
                maximumDistanceSquared);
            return;
        }

        if (!classInfo.IsResource)
            return;

        uintptr_t rootComponent = 0;
        if (!TryReadPointer(
            actor + Offsets::Actor_RootComponent,
            rootComponent)) {
            return;
        }

        const auto location = Memory::TryRead<Solarpunk::Vector3>(
            rootComponent + Offsets::SceneComponent_RelativeLocation);
        if (!location || !IsFinite(*location))
            return;

        AddMarker(
            actor,
            classInfo,
            *location,
            player,
            maximumDistanceSquared);
    }

    void ScanLevel(
        uintptr_t level,
        const Solarpunk::PlayerSnapshot& player,
        double maximumDistanceSquared,
        std::unordered_set<uintptr_t>& visitedActors) {
        const auto actors =
            Memory::TryRead<TArrayHeader>(
                level + Solarpunk::GameSchema::Get().LevelActors);
        if (!actors
            || !IsValidArray(*actors, MaxActorsPerLevel)) {
            return;
        }

        ++gState.LevelsScanned;
        for (int32_t index = 0;
            index < actors->Count
                && gState.ActorsScanned < MaxActorsPerScan
                && gMarkers.size() < MaxMarkersPerScan;
            ++index) {
            const auto actor = Memory::TryRead<uintptr_t>(
                actors->Data
                    + static_cast<uintptr_t>(index) * sizeof(uintptr_t));
            if (!actor
                || !Memory::IsValidPtr(*actor)
                || !visitedActors.insert(*actor).second) {
                continue;
            }

            ++gState.ActorsScanned;
            ScanActor(
                *actor,
                player,
                maximumDistanceSquared);
        }
    }

    bool CollectLevels(
        uintptr_t world,
        std::vector<uintptr_t>& levels) {
        levels.clear();
        std::unordered_set<uintptr_t> uniqueLevels;

        uintptr_t persistentLevel = 0;
        if (TryReadPointer(
            world + Offsets::World_PersistentLevel,
            persistentLevel)
            && uniqueLevels.insert(persistentLevel).second) {
            levels.emplace_back(persistentLevel);
        }

        const auto loadedLevels =
            Memory::TryRead<TArrayHeader>(world + Offsets::World_Levels);
        if (loadedLevels
            && IsValidArray(*loadedLevels, MaxLevelCount)) {
            for (int32_t index = 0; index < loadedLevels->Count; ++index) {
                const auto level = Memory::TryRead<uintptr_t>(
                    loadedLevels->Data
                        + static_cast<uintptr_t>(index)
                            * sizeof(uintptr_t));
                if (level
                    && Memory::IsValidPtr(*level)
                    && uniqueLevels.insert(*level).second) {
                    levels.emplace_back(*level);
                }
            }
        }

        return !levels.empty();
    }

} // namespace

Solarpunk::ResourceEsp::Settings&
Solarpunk::ResourceEsp::GetSettings() {
    return gSettings;
}

const Solarpunk::ResourceEsp::State&
Solarpunk::ResourceEsp::GetState() {
    return gState;
}

const std::vector<Solarpunk::ResourceEsp::Marker>&
Solarpunk::ResourceEsp::GetMarkers() {
    return gMarkers;
}

void Solarpunk::ResourceEsp::Update(
    const PlayerSnapshot& player) {
    InitializeClassLog();
    if (!gSettings.Enabled) {
        if (gState.Status != ScanStatus::Disabled)
            Reset();
        SyncClassLogState();
        return;
    }

    if (!player.World
        || !player.HasCoordinates()
        || !Memory::IsValidPtr(player.World)) {
        gMarkers.clear();
        gState = {};
        gState.Status = ScanStatus::WaitingForWorld;
        gLastWorld = 0;
        gNextScan = {};
        SyncClassLogState();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (player.World != gLastWorld) {
        gMarkers.clear();
        gLastWorld = player.World;
        gNextScan = {};
    }
    else if (now < gNextScan) {
        return;
    }
    gNextScan = now + ScanInterval;

    const uintptr_t imageBase = Memory::GetModuleBase(nullptr);
    if (!imageBase
        || !Memory::IsValidPtr(
            imageBase + Solarpunk::GameSchema::Get().Globals.GNamesRva,
            0x10)) {
        gMarkers.clear();
        gState = {};
        gState.Status = ScanStatus::NamePoolUnavailable;
        SyncClassLogState();
        return;
    }

    const uint64_t nextGeneration = gState.ScanGeneration + 1;
    gMarkers.clear();
    gState = {};
    gState.Status = ScanStatus::Ready;
    gState.ScanGeneration = nextGeneration;
    SyncClassLogState();

    std::vector<uintptr_t> levels;
    if (!CollectLevels(player.World, levels)) {
        gState.Status = ScanStatus::WaitingForWorld;
        return;
    }

    const double maxDistanceCentimeters =
        static_cast<double>(std::clamp(
            gSettings.MaxDistanceMeters,
            10.0f,
            1000.0f)) * 100.0;
    const double maximumDistanceSquared =
        maxDistanceCentimeters * maxDistanceCentimeters;
    std::unordered_set<uintptr_t> visitedActors;
    visitedActors.reserve(32768);

    for (const uintptr_t level : levels) {
        if (gState.ActorsScanned >= MaxActorsPerScan
            || gMarkers.size() >= MaxMarkersPerScan) {
            break;
        }

        ScanLevel(
            level,
            player,
            maximumDistanceSquared,
            visitedActors);
    }
    SyncClassLogState();
}

void Solarpunk::ResourceEsp::Reset() {
    gMarkers.clear();
    gState = {};
    gState.Status = ScanStatus::Disabled;
    SyncClassLogState();
    gLastWorld = 0;
    gNextScan = {};
}

bool Solarpunk::ResourceEsp::IsKindVisible(Kind kind) {
    switch (kind) {
    case Kind::Tree:
        return gSettings.ShowTrees;
    case Kind::Ore:
        return gSettings.ShowOres;
    case Kind::Plant:
        return gSettings.ShowPlants;
    case Kind::Pickup:
        return gSettings.ShowPickups;
    case Kind::DroppedItem:
        return gSettings.ShowDroppedItems;
    case Kind::Loot:
        return gSettings.ShowLoot;
    case Kind::Animal:
        return gSettings.ShowAnimals;
    case Kind::Npc:
        return gSettings.ShowNpcs;
    default:
        return false;
    }
}

const char* Solarpunk::ResourceEsp::GetStatusText(
    ScanStatus status) {
    switch (status) {
    case ScanStatus::Disabled:
        return "RESOURCE MARKERS OFF";
    case ScanStatus::WaitingForWorld:
        return "WAITING FOR WORLD";
    case ScanStatus::NamePoolUnavailable:
        return "NAME POOL UNAVAILABLE";
    case ScanStatus::Ready:
        return "RESOURCE SCAN READY";
    default:
        return "RESOURCE SCAN UNKNOWN";
    }
}
