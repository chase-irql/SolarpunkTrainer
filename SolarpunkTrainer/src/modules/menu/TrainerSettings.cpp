#include "TrainerSettings.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>

namespace {

    constexpr wchar_t SettingsSection[] = L"SolarpunkTrainer";
    constexpr size_t SettingsPathCapacity = 1024;

    TrainerSettings::PersistentSettings gLastSaved{};
    bool gHasAutosaveBaseline = false;

    bool BuildSettingsPath(
        wchar_t (&path)[SettingsPathCapacity],
        bool createDirectory) {
        path[0] = L'\0';
        const DWORD length = GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            path,
            static_cast<DWORD>(SettingsPathCapacity));
        if (!length || length >= SettingsPathCapacity)
            return false;

        if (wcscat_s(
            path,
            SettingsPathCapacity,
            L"\\SolarpunkTrainer") != 0) {
            return false;
        }

        if (createDirectory
            && !CreateDirectoryW(path, nullptr)
            && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }

        return wcscat_s(
            path,
            SettingsPathCapacity,
            L"\\settings.ini") == 0;
    }

    bool ReadBool(
        const wchar_t* path,
        const wchar_t* key,
        bool fallback) {
        return GetPrivateProfileIntW(
            SettingsSection,
            key,
            fallback ? 1 : 0,
            path) != 0;
    }

    int ReadInt(
        const wchar_t* path,
        const wchar_t* key,
        int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(
            SettingsSection,
            key,
            fallback,
            path));
    }

    float ReadFloat(
        const wchar_t* path,
        const wchar_t* key,
        float fallback) {
        wchar_t fallbackText[48]{};
        wchar_t value[48]{};
        swprintf_s(fallbackText, L"%.3f", fallback);
        GetPrivateProfileStringW(
            SettingsSection,
            key,
            fallbackText,
            value,
            static_cast<DWORD>(std::size(value)),
            path);

        wchar_t* end = nullptr;
        const double parsed = std::wcstod(value, &end);
        return end != value && std::isfinite(parsed)
            ? static_cast<float>(parsed)
            : fallback;
    }

    bool WriteText(
        const wchar_t* path,
        const wchar_t* key,
        const wchar_t* value) {
        return WritePrivateProfileStringW(
            SettingsSection,
            key,
            value,
            path) != FALSE;
    }

    bool WriteBool(
        const wchar_t* path,
        const wchar_t* key,
        bool value) {
        return WriteText(path, key, value ? L"1" : L"0");
    }

    bool WriteInt(
        const wchar_t* path,
        const wchar_t* key,
        int value) {
        wchar_t text[32]{};
        swprintf_s(text, L"%d", value);
        return WriteText(path, key, text);
    }

    bool WriteFloat(
        const wchar_t* path,
        const wchar_t* key,
        float value) {
        wchar_t text[48]{};
        swprintf_s(text, L"%.3f", value);
        return WriteText(path, key, text);
    }

    bool Equivalent(
        const TrainerSettings::PersistentSettings& left,
        const TrainerSettings::PersistentSettings& right) {
        constexpr float epsilon = 0.001f;
        return left.CoordinateHud == right.CoordinateHud
            && left.ProjectionPreview == right.ProjectionPreview
            && std::abs(left.MenuOpacity - right.MenuOpacity) < epsilon
            && std::abs(left.HudOpacity - right.HudOpacity) < epsilon
            && left.ResourceEspEnabled == right.ResourceEspEnabled
            && left.ResourceShowTrees == right.ResourceShowTrees
            && left.ResourceShowOres == right.ResourceShowOres
            && left.ResourceShowPlants == right.ResourceShowPlants
            && left.ResourceShowPickups == right.ResourceShowPickups
            && left.ResourceShowDroppedItems
                == right.ResourceShowDroppedItems
            && left.ResourceShowLoot == right.ResourceShowLoot
            && left.ResourceShowAnimals == right.ResourceShowAnimals
            && left.ResourceShowNpcs == right.ResourceShowNpcs
            && left.ResourceShowLabels == right.ResourceShowLabels
            && left.ResourceShowDistance == right.ResourceShowDistance
            && std::abs(
                left.ResourceMaxDistanceMeters
                - right.ResourceMaxDistanceMeters) < epsilon
            && left.GodMode == right.GodMode
            && left.NoHunger == right.NoHunger
            && left.NoThirst == right.NoThirst
            && left.FreeBuilding == right.FreeBuilding
            && left.FreeCrafting == right.FreeCrafting
            && left.FreeResearch == right.FreeResearch
            && left.FreezeTime == right.FreezeTime
            && std::abs(
                left.TimeOfDay - right.TimeOfDay) < epsilon
            && left.ThirdPerson == right.ThirdPerson
            && std::abs(
                left.ThirdPersonDistance
                - right.ThirdPersonDistance) < epsilon
            && std::abs(
                left.ThirdPersonHeight
                - right.ThirdPersonHeight) < epsilon
            && std::abs(
                left.GameSpeed
                - right.GameSpeed) < epsilon
            && std::abs(
                left.AirshipSpeedMultiplier
                - right.AirshipSpeedMultiplier) < epsilon
            && std::abs(
                left.AirshipBoostMultiplier
                - right.AirshipBoostMultiplier) < epsilon
            && left.AirshipInfiniteBattery
                == right.AirshipInfiniteBattery
            && left.AirshipNoHullDamage
                == right.AirshipNoHullDamage
            && left.FlightMode == right.FlightMode
            && std::abs(
                left.VelocitySpeed
                - right.VelocitySpeed) < epsilon
            && std::abs(
                left.NoclipSpeed
                - right.NoclipSpeed) < epsilon;
    }

} // namespace

bool TrainerSettings::Load(PersistentSettings& settings) {
    wchar_t path[SettingsPathCapacity]{};
    if (!BuildSettingsPath(path, false)
        || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES
        || ReadInt(path, L"Version", 0) != CurrentVersion) {
        return false;
    }

    PersistentSettings loaded{};
    loaded.CoordinateHud =
        ReadBool(path, L"CoordinateHud", loaded.CoordinateHud);
    loaded.ProjectionPreview =
        ReadBool(path, L"ProjectionPreview", loaded.ProjectionPreview);
    loaded.MenuOpacity = std::clamp(
        ReadFloat(path, L"MenuOpacity", loaded.MenuOpacity),
        80.0f,
        100.0f);
    loaded.HudOpacity = std::clamp(
        ReadFloat(path, L"HudOpacity", loaded.HudOpacity),
        55.0f,
        100.0f);

    loaded.ResourceEspEnabled =
        ReadBool(path, L"ResourceEspEnabled", loaded.ResourceEspEnabled);
    loaded.ResourceShowTrees =
        ReadBool(path, L"ResourceShowTrees", loaded.ResourceShowTrees);
    loaded.ResourceShowOres =
        ReadBool(path, L"ResourceShowOres", loaded.ResourceShowOres);
    loaded.ResourceShowPlants =
        ReadBool(path, L"ResourceShowPlants", loaded.ResourceShowPlants);
    loaded.ResourceShowPickups =
        ReadBool(path, L"ResourceShowPickups", loaded.ResourceShowPickups);
    loaded.ResourceShowDroppedItems = ReadBool(
        path,
        L"ResourceShowDroppedItems",
        loaded.ResourceShowDroppedItems);
    loaded.ResourceShowLoot =
        ReadBool(path, L"ResourceShowLoot", loaded.ResourceShowLoot);
    loaded.ResourceShowAnimals =
        ReadBool(path, L"ResourceShowAnimals", loaded.ResourceShowAnimals);
    loaded.ResourceShowNpcs =
        ReadBool(path, L"ResourceShowNpcs", loaded.ResourceShowNpcs);
    loaded.ResourceShowLabels =
        ReadBool(path, L"ResourceShowLabels", loaded.ResourceShowLabels);
    loaded.ResourceShowDistance =
        ReadBool(path, L"ResourceShowDistance", loaded.ResourceShowDistance);
    loaded.ResourceMaxDistanceMeters = std::clamp(
        ReadFloat(
            path,
            L"ResourceMaxDistanceMeters",
            loaded.ResourceMaxDistanceMeters),
        10.0f,
        1000.0f);

    loaded.GodMode = ReadBool(path, L"GodMode", loaded.GodMode);
    loaded.NoHunger = ReadBool(path, L"NoHunger", loaded.NoHunger);
    loaded.NoThirst = ReadBool(path, L"NoThirst", loaded.NoThirst);
    loaded.FreeBuilding =
        ReadBool(path, L"FreeBuilding", loaded.FreeBuilding);
    loaded.FreeCrafting =
        ReadBool(path, L"FreeCrafting", loaded.FreeCrafting);
    loaded.FreeResearch =
        ReadBool(path, L"FreeResearch", loaded.FreeResearch);
    loaded.FreezeTime =
        ReadBool(path, L"FreezeTime", loaded.FreezeTime);
    loaded.TimeOfDay = std::clamp(
        ReadFloat(path, L"TimeOfDay", loaded.TimeOfDay),
        0.0f,
        24.0f);
    loaded.ThirdPerson =
        ReadBool(path, L"ThirdPerson", loaded.ThirdPerson);
    loaded.ThirdPersonDistance = std::clamp(
        ReadFloat(
            path,
            L"ThirdPersonDistance",
            loaded.ThirdPersonDistance),
        100.0f,
        800.0f);
    loaded.ThirdPersonHeight = std::clamp(
        ReadFloat(
            path,
            L"ThirdPersonHeight",
            loaded.ThirdPersonHeight),
        0.0f,
        250.0f);
    loaded.GameSpeed = std::clamp(
        ReadFloat(path, L"GameSpeed", loaded.GameSpeed),
        0.10f,
        5.0f);
    loaded.AirshipSpeedMultiplier = std::clamp(
        ReadFloat(
            path,
            L"AirshipSpeedMultiplier",
            loaded.AirshipSpeedMultiplier),
        1.0f,
        20.0f);
    loaded.AirshipBoostMultiplier = std::clamp(
        ReadFloat(
            path,
            L"AirshipBoostMultiplier",
            loaded.AirshipBoostMultiplier),
        1.0f,
        10.0f);
    loaded.AirshipInfiniteBattery = ReadBool(
        path,
        L"AirshipInfiniteBattery",
        loaded.AirshipInfiniteBattery);
    loaded.AirshipNoHullDamage = ReadBool(
        path,
        L"AirshipNoHullDamage",
        loaded.AirshipNoHullDamage);
    loaded.FlightMode = std::clamp(
        ReadInt(path, L"FlightMode", loaded.FlightMode),
        0,
        1);
    loaded.VelocitySpeed = std::clamp(
        ReadFloat(path, L"VelocitySpeed", loaded.VelocitySpeed),
        100.0f,
        6000.0f);
    loaded.NoclipSpeed = std::clamp(
        ReadFloat(path, L"NoclipSpeed", loaded.NoclipSpeed),
        100.0f,
        8000.0f);

    settings = loaded;
    return true;
}

bool TrainerSettings::Save(
    const PersistentSettings& settings) {
    wchar_t path[SettingsPathCapacity]{};
    if (!BuildSettingsPath(path, true))
        return false;

    bool saved = WriteInt(path, L"Version", CurrentVersion);
    saved &= WriteBool(path, L"CoordinateHud", settings.CoordinateHud);
    saved &= WriteBool(
        path,
        L"ProjectionPreview",
        settings.ProjectionPreview);
    saved &= WriteFloat(path, L"MenuOpacity", settings.MenuOpacity);
    saved &= WriteFloat(path, L"HudOpacity", settings.HudOpacity);

    saved &= WriteBool(
        path,
        L"ResourceEspEnabled",
        settings.ResourceEspEnabled);
    saved &= WriteBool(
        path,
        L"ResourceShowTrees",
        settings.ResourceShowTrees);
    saved &= WriteBool(
        path,
        L"ResourceShowOres",
        settings.ResourceShowOres);
    saved &= WriteBool(
        path,
        L"ResourceShowPlants",
        settings.ResourceShowPlants);
    saved &= WriteBool(
        path,
        L"ResourceShowPickups",
        settings.ResourceShowPickups);
    saved &= WriteBool(
        path,
        L"ResourceShowDroppedItems",
        settings.ResourceShowDroppedItems);
    saved &= WriteBool(
        path,
        L"ResourceShowLoot",
        settings.ResourceShowLoot);
    saved &= WriteBool(
        path,
        L"ResourceShowAnimals",
        settings.ResourceShowAnimals);
    saved &= WriteBool(
        path,
        L"ResourceShowNpcs",
        settings.ResourceShowNpcs);
    saved &= WriteBool(
        path,
        L"ResourceShowLabels",
        settings.ResourceShowLabels);
    saved &= WriteBool(
        path,
        L"ResourceShowDistance",
        settings.ResourceShowDistance);
    saved &= WriteFloat(
        path,
        L"ResourceMaxDistanceMeters",
        settings.ResourceMaxDistanceMeters);

    saved &= WriteBool(path, L"GodMode", settings.GodMode);
    saved &= WriteBool(path, L"NoHunger", settings.NoHunger);
    saved &= WriteBool(path, L"NoThirst", settings.NoThirst);
    saved &= WriteBool(
        path,
        L"FreeBuilding",
        settings.FreeBuilding);
    saved &= WriteBool(
        path,
        L"FreeCrafting",
        settings.FreeCrafting);
    saved &= WriteBool(
        path,
        L"FreeResearch",
        settings.FreeResearch);
    saved &= WriteBool(
        path,
        L"FreezeTime",
        settings.FreezeTime);
    saved &= WriteFloat(
        path,
        L"TimeOfDay",
        settings.TimeOfDay);
    saved &= WriteBool(
        path,
        L"ThirdPerson",
        settings.ThirdPerson);
    saved &= WriteFloat(
        path,
        L"ThirdPersonDistance",
        settings.ThirdPersonDistance);
    saved &= WriteFloat(
        path,
        L"ThirdPersonHeight",
        settings.ThirdPersonHeight);
    saved &= WriteFloat(
        path,
        L"GameSpeed",
        settings.GameSpeed);
    saved &= WriteFloat(
        path,
        L"AirshipSpeedMultiplier",
        settings.AirshipSpeedMultiplier);
    saved &= WriteFloat(
        path,
        L"AirshipBoostMultiplier",
        settings.AirshipBoostMultiplier);
    saved &= WriteBool(
        path,
        L"AirshipInfiniteBattery",
        settings.AirshipInfiniteBattery);
    saved &= WriteBool(
        path,
        L"AirshipNoHullDamage",
        settings.AirshipNoHullDamage);
    saved &= WriteInt(path, L"FlightMode", settings.FlightMode);
    saved &= WriteFloat(
        path,
        L"VelocitySpeed",
        settings.VelocitySpeed);
    saved &= WriteFloat(
        path,
        L"NoclipSpeed",
        settings.NoclipSpeed);

    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path);
    return saved;
}

void TrainerSettings::SetAutosaveBaseline(
    const PersistentSettings& settings) {
    gLastSaved = settings;
    gHasAutosaveBaseline = true;
}

void TrainerSettings::Autosave(
    const PersistentSettings& settings) {
    if (gHasAutosaveBaseline
        && Equivalent(settings, gLastSaved)) {
        return;
    }

    if (Save(settings)) {
        gLastSaved = settings;
        gHasAutosaveBaseline = true;
    }
}
