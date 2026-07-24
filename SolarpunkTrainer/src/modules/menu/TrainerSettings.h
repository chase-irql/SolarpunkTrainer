#pragma once

namespace TrainerSettings {

    constexpr int CurrentVersion = 1;

    struct PersistentSettings {
        bool CoordinateHud = true;
        bool ProjectionPreview = false;
        float MenuOpacity = 96.0f;
        float HudOpacity = 92.0f;

        bool ResourceEspEnabled = true;
        bool ResourceShowTrees = true;
        bool ResourceShowOres = true;
        bool ResourceShowPlants = true;
        bool ResourceShowPickups = true;
        bool ResourceShowDroppedItems = true;
        bool ResourceShowLoot = true;
        bool ResourceShowAnimals = true;
        bool ResourceShowNpcs = true;
        bool ResourceShowLabels = true;
        bool ResourceShowDistance = true;
        float ResourceMaxDistanceMeters = 250.0f;

        bool GodMode = false;
        bool NoHunger = false;
        bool NoThirst = false;
        bool FreeBuilding = false;
        bool FreeCrafting = false;
        bool FreeResearch = false;
        bool FreezeTime = false;
        float TimeOfDay = 12.0f;
        bool ThirdPerson = false;
        float ThirdPersonDistance = 320.0f;
        float ThirdPersonHeight = 60.0f;
        float GameSpeed = 1.0f;
        float AirshipSpeedMultiplier = 1.0f;
        float AirshipBoostMultiplier = 1.0f;
        bool AirshipInfiniteBattery = false;
        bool AirshipNoHullDamage = false;

        int FlightMode = 0;
        float VelocitySpeed = 1200.0f;
        float NoclipSpeed = 1800.0f;
    };

    bool Load(PersistentSettings& settings);
    bool Save(const PersistentSettings& settings);
    void SetAutosaveBaseline(const PersistentSettings& settings);
    void Autosave(const PersistentSettings& settings);

} // namespace TrainerSettings
