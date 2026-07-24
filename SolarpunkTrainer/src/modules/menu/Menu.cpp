#include "Menu.h"

#include "MenuComponents.h"
#include "MenuTheme.h"
#include "Notifications.h"
#include "TrainerSettings.h"
#include "../render/ItemIconCache.h"
#include "../render/Render.h"
#include "../render/WorldRenderer.h"
#include "../solarpunk/FlyHack.h"
#include "../solarpunk/InventoryEditor.h"
#include "../solarpunk/PlayerExploits.h"
#include "../solarpunk/ResourceEsp.h"
#include "../solarpunk/ResourceNames.h"
#include "../solarpunk/WaypointSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

    constexpr ImVec2 DefaultWindowSize(860.0f, 600.0f);
    constexpr ImVec2 MinimumWindowSize(760.0f, 520.0f);
    constexpr ImVec2 MaximumWindowSize(1120.0f, 820.0f);
    constexpr float OverviewCardMinWidth = 280.0f;
    constexpr float RuntimeCardMinWidth = 300.0f;
    constexpr float MetricCardMinWidth = 175.0f;

    int gLastRenderedPage = -1;

    struct FeatureNotificationSnapshot {
        bool CoordinateHud = false;
        bool ProjectionPreview = false;
        bool ResourceMarkers = false;
        bool GodMode = false;
        bool NoHunger = false;
        bool NoThirst = false;
        bool FreeBuilding = false;
        bool FreeCrafting = false;
        bool FreeResearch = false;
        bool FreezeTime = false;
        bool ThirdPerson = false;
        bool Fly = false;
        bool AirshipInfiniteBattery = false;
        bool AirshipNoHullDamage = false;
        bool AirshipAutopilot = false;
        bool RuntimeUpdateFailed = false;
        bool FlyUpdateFailed = false;
    };

    struct ObservedInventoryLock {
        uintptr_t InventorySystem = 0;
        uintptr_t ItemClass = 0;
        int SlotIndex = -1;
        bool Quantity = false;
        bool Durability = false;
        bool WaterLevel = false;
        std::string DisplayName;

        bool Any() const {
            return Quantity || Durability || WaterLevel;
        }
    };

    FeatureNotificationSnapshot
        gFeatureNotificationSnapshot{};
    bool gFeatureNotificationBaselineReady = false;
    std::array<
        ObservedInventoryLock,
        Solarpunk::InventoryEditor::MaxInventorySlots>
        gObservedInventoryLocks{};
    uintptr_t gObservedInventorySystem = 0;
    bool gInventoryLockBaselineReady = false;

    struct CardGrid {
        int Columns;
        float Width;
    };

    CardGrid CalculateCardGrid(
        float availableWidth,
        float minimumCardWidth,
        int maximumColumns) {
        const float gap = MenuUI::GetTheme().Spacing.LG;
        const int fittingColumns = static_cast<int>(
            (availableWidth + gap) / (minimumCardWidth + gap));
        const int columns = std::clamp(fittingColumns, 1, maximumColumns);
        return {
            columns,
            (availableWidth - gap * static_cast<float>(columns - 1))
                / static_cast<float>(columns)
        };
    }

    const char* PageChildId(int page) {
        switch (page) {
        case 0:
            return "##page_content_exploits";
        case 1:
            return "##page_content_visuals";
        case 2:
            return "##page_content_movement";
        case 3:
            return "##page_content_inventory";
        case 4:
            return "##page_content_waypoints";
        case 5:
            return "##page_content_diagnostics";
        default:
            return "##page_content_exploits";
        }
    }

    ImVec2 MeasureText(ImFont* font, const char* text) {
        if (!font)
            return ImGui::CalcTextSize(text);

        return font->CalcTextSizeA(
            font->LegacySize,
            FLT_MAX,
            0.0f,
            text);
    }

    void FormatCoordinate(char* buffer, size_t size, double value) {
        std::snprintf(buffer, size, "%.2f", value);
    }

    void FormatVector(
        char* buffer,
        size_t size,
        const Solarpunk::Vector3& value) {
        std::snprintf(
            buffer,
            size,
            "%.1f  /  %.1f  /  %.1f",
            value.X,
            value.Y,
            value.Z);
    }

    void FormatRotation(
        char* buffer,
        size_t size,
        const Solarpunk::Rotator3& value) {
        std::snprintf(
            buffer,
            size,
            "P %.1f   Y %.1f   R %.1f",
            value.Pitch,
            value.Yaw,
            value.Roll);
    }

    std::string LowerCopy(std::string_view value) {
        std::string lowered;
        lowered.reserve(value.size());
        for (const unsigned char character : value)
            lowered.push_back(static_cast<char>(std::tolower(character)));
        return lowered;
    }

    std::string InventorySlotLabel(int slotIndex) {
        if (slotIndex
            < Solarpunk::InventoryEditor::HotbarSlotCount) {
            return "hotbar slot "
                + std::to_string(slotIndex + 1);
        }
        return "backpack slot "
            + std::to_string(
                slotIndex
                - Solarpunk::InventoryEditor::HotbarSlotCount
                + 1);
    }

    std::string LockedValueNames(
        const ObservedInventoryLock& lock) {
        std::vector<std::string_view> names;
        if (lock.Quantity)
            names.emplace_back("quantity");
        if (lock.Durability)
            names.emplace_back("durability");
        if (lock.WaterLevel)
            names.emplace_back("water level");

        std::string result;
        for (size_t index = 0; index < names.size(); ++index) {
            if (index > 0) {
                result += index + 1 == names.size()
                    ? " and "
                    : ", ";
            }
            result += names[index];
        }
        return result;
    }

    void NotifyFeatureTransition(
        bool previous,
        bool current,
        const char* key,
        const char* label) {
        if (previous == current)
            return;

        Notifications::Push(
            current
                ? Notifications::Kind::Success
                : Notifications::Kind::Info,
            key,
            std::string(label)
                + (current ? " enabled" : " disabled"),
            current
                ? "The feature is now enabled for this session."
                : "The feature has returned to normal game behavior.",
            3.8f);
    }

    FeatureNotificationSnapshot CaptureFeatureNotifications(
        bool coordinateHud,
        bool projectionPreview) {
        FeatureNotificationSnapshot snapshot{};
        snapshot.CoordinateHud = coordinateHud;
        snapshot.ProjectionPreview = projectionPreview;
        snapshot.ResourceMarkers =
            Solarpunk::ResourceEsp::GetSettings().Enabled;

        const auto& exploits =
            Solarpunk::PlayerExploits::GetSettings();
        snapshot.GodMode = exploits.GodMode;
        snapshot.NoHunger = exploits.NoHunger;
        snapshot.NoThirst = exploits.NoThirst;
        snapshot.FreeBuilding = exploits.FreeBuilding;
        snapshot.FreeCrafting = exploits.FreeCrafting;
        snapshot.FreeResearch = exploits.FreeResearch;
        snapshot.FreezeTime = exploits.FreezeTime;
        snapshot.ThirdPerson = exploits.ThirdPerson;
        snapshot.AirshipInfiniteBattery =
            exploits.AirshipInfiniteBattery;
        snapshot.AirshipNoHullDamage =
            exploits.AirshipNoHullDamage;
        snapshot.AirshipAutopilot =
            exploits.AirshipAutopilot;
        snapshot.RuntimeUpdateFailed =
            Solarpunk::PlayerExploits::GetState()
                .RuntimeUpdateFailed;

        snapshot.Fly =
            Solarpunk::FlyHack::GetSettings().Enabled;
        snapshot.FlyUpdateFailed =
            Solarpunk::FlyHack::GetState().CurrentStatus
            == Solarpunk::FlyHack::Status::UpdateFailed;
        return snapshot;
    }

    void NotifyInventoryLockChange(
        const Solarpunk::InventoryEditor::ItemSlot& item,
        std::string_view lockName,
        bool enabled,
        bool succeeded,
        std::string_view value = {}) {
        const std::string key =
            "inventory.lock."
            + std::string(lockName)
            + "."
            + std::to_string(item.Index);
        if (!succeeded) {
            Notifications::Push(
                Notifications::Kind::Error,
                key,
                "Could not change inventory lock",
                "The "
                    + std::string(lockName)
                    + " lock for "
                    + item.DisplayName
                    + " was rejected.",
                5.0f);
            return;
        }

        std::string message =
            item.DisplayName
            + " in "
            + InventorySlotLabel(item.Index);
        if (enabled) {
            message += " will be held";
            if (!value.empty())
                message += " at " + std::string(value);
            message += " until unlocked.";
        }
        else {
            message += " is no longer locked.";
        }

        Notifications::Push(
            enabled
                ? Notifications::Kind::Success
                : Notifications::Kind::Info,
            key,
            std::string(lockName)
                + (enabled
                    ? " lock enabled"
                    : " lock disabled"),
            std::move(message),
            4.25f);
    }

    void DrawMatrix(
        const char* label,
        const Solarpunk::Matrix4x4& matrix) {
        const auto& theme = MenuUI::GetTheme();
        const auto& fonts = MenuUI::GetFonts();

        ImGui::PushFont(fonts.Small);
        ImGui::TextColored(theme.Colors.TextSecondary, "%s", label);
        for (int row = 0; row < 4; ++row) {
            ImGui::TextColored(
                theme.Colors.TextMuted,
                "% .3f   % .3f   % .3f   % .3f",
                matrix.M[row][0],
                matrix.M[row][1],
                matrix.M[row][2],
                matrix.M[row][3]);
        }
        ImGui::PopFont();
    }

} // namespace

bool Menu::bIsOpen = false;
bool Menu::bCoordinatesVisible = true;
bool Menu::bProjectionPreviewVisible = false;
int Menu::currentPage = 0;
float Menu::menuOpacity = 96.0f;
float Menu::hudOpacity = 92.0f;

void Menu::Initialize(HMODULE module) {
    MenuUI::Initialize(module);
    Solarpunk::WaypointSystem::Initialize();
    Notifications::Clear();
    gFeatureNotificationBaselineReady = false;
    gInventoryLockBaselineReady = false;
    gObservedInventorySystem = 0;
    gObservedInventoryLocks.fill({});

    TrainerSettings::PersistentSettings settings{};
    if (!TrainerSettings::Load(settings))
        TrainerSettings::Save(settings);
    ApplySettings(settings);
    TrainerSettings::SetAutosaveBaseline(CaptureSettings());

    gFeatureNotificationSnapshot =
        CaptureFeatureNotifications(
            bCoordinatesVisible,
            bProjectionPreviewVisible);
    gFeatureNotificationBaselineReady = true;
}

TrainerSettings::PersistentSettings Menu::CaptureSettings() {
    TrainerSettings::PersistentSettings settings{};
    settings.CoordinateHud = bCoordinatesVisible;
    settings.ProjectionPreview = bProjectionPreviewVisible;
    settings.MenuOpacity = menuOpacity;
    settings.HudOpacity = hudOpacity;

    const auto& resources = Solarpunk::ResourceEsp::GetSettings();
    settings.ResourceEspEnabled = resources.Enabled;
    settings.ResourceShowTrees = resources.ShowTrees;
    settings.ResourceShowOres = resources.ShowOres;
    settings.ResourceShowPlants = resources.ShowPlants;
    settings.ResourceShowPickups = resources.ShowPickups;
    settings.ResourceShowDroppedItems = resources.ShowDroppedItems;
    settings.ResourceShowLoot = resources.ShowLoot;
    settings.ResourceShowAnimals = resources.ShowAnimals;
    settings.ResourceShowNpcs = resources.ShowNpcs;
    settings.ResourceShowLabels = resources.ShowLabels;
    settings.ResourceShowDistance = resources.ShowDistance;
    settings.ResourceMaxDistanceMeters =
        resources.MaxDistanceMeters;

    const auto& exploits =
        Solarpunk::PlayerExploits::GetSettings();
    settings.GodMode = exploits.GodMode;
    settings.NoHunger = exploits.NoHunger;
    settings.NoThirst = exploits.NoThirst;
    settings.FreeBuilding = exploits.FreeBuilding;
    settings.FreeCrafting = exploits.FreeCrafting;
    settings.FreeResearch = exploits.FreeResearch;
    settings.FreezeTime = exploits.FreezeTime;
    settings.TimeOfDay = exploits.TimeOfDay;
    settings.ThirdPerson = exploits.ThirdPerson;
    settings.ThirdPersonDistance = exploits.ThirdPersonDistance;
    settings.ThirdPersonHeight = exploits.ThirdPersonHeight;
    settings.GameSpeed = exploits.GameSpeed;
    settings.AirshipSpeedMultiplier =
        exploits.AirshipSpeedMultiplier;
    settings.AirshipBoostMultiplier =
        exploits.AirshipBoostMultiplier;
    settings.AirshipInfiniteBattery =
        exploits.AirshipInfiniteBattery;
    settings.AirshipNoHullDamage =
        exploits.AirshipNoHullDamage;

    const auto& fly = Solarpunk::FlyHack::GetSettings();
    settings.FlightMode = static_cast<int>(fly.FlightMode);
    settings.VelocitySpeed = fly.VelocitySpeed;
    settings.NoclipSpeed = fly.NoclipSpeed;
    return settings;
}

void Menu::ApplySettings(
    const TrainerSettings::PersistentSettings& settings) {
    bCoordinatesVisible = settings.CoordinateHud;
    bProjectionPreviewVisible = settings.ProjectionPreview;
    menuOpacity = settings.MenuOpacity;
    hudOpacity = settings.HudOpacity;

    auto& resources = Solarpunk::ResourceEsp::GetSettings();
    resources.Enabled = settings.ResourceEspEnabled;
    resources.ShowTrees = settings.ResourceShowTrees;
    resources.ShowOres = settings.ResourceShowOres;
    resources.ShowPlants = settings.ResourceShowPlants;
    resources.ShowPickups = settings.ResourceShowPickups;
    resources.ShowDroppedItems = settings.ResourceShowDroppedItems;
    resources.ShowLoot = settings.ResourceShowLoot;
    resources.ShowAnimals = settings.ResourceShowAnimals;
    resources.ShowNpcs = settings.ResourceShowNpcs;
    resources.ShowLabels = settings.ResourceShowLabels;
    resources.ShowDistance = settings.ResourceShowDistance;
    resources.MaxDistanceMeters =
        settings.ResourceMaxDistanceMeters;

    auto& exploits = Solarpunk::PlayerExploits::GetSettings();
    exploits.GodMode = settings.GodMode;
    exploits.NoHunger = settings.NoHunger;
    exploits.NoThirst = settings.NoThirst;
    exploits.FreeBuilding = settings.FreeBuilding;
    exploits.FreeCrafting = settings.FreeCrafting;
    exploits.FreeResearch = settings.FreeResearch;
    exploits.FreezeTime = settings.FreezeTime;
    exploits.TimeOfDay = settings.TimeOfDay;
    exploits.ThirdPerson = settings.ThirdPerson;
    exploits.ThirdPersonDistance = settings.ThirdPersonDistance;
    exploits.ThirdPersonHeight = settings.ThirdPersonHeight;
    exploits.GameSpeed = settings.GameSpeed;
    exploits.AirshipSpeedMultiplier =
        settings.AirshipSpeedMultiplier;
    exploits.AirshipBoostMultiplier =
        settings.AirshipBoostMultiplier;
    exploits.AirshipInfiniteBattery =
        settings.AirshipInfiniteBattery;
    exploits.AirshipNoHullDamage =
        settings.AirshipNoHullDamage;
    // Autopilot can move the ship as soon as it is possessed, so it is a
    // deliberate session control rather than an auto-loaded preference.
    exploits.AirshipAutopilot = false;

    auto& fly = Solarpunk::FlyHack::GetSettings();
    // Flight mode and speeds are preferences. The active toggle is a runtime
    // safety control and is deliberately never persisted.
    fly.Enabled = false;
    fly.FlightMode = settings.FlightMode == 1
        ? Solarpunk::FlyHack::Mode::Noclip
        : Solarpunk::FlyHack::Mode::Velocity;
    fly.VelocitySpeed = settings.VelocitySpeed;
    fly.NoclipSpeed = settings.NoclipSpeed;
}

void Menu::Toggle() {
    bIsOpen = !bIsOpen;
}

void Menu::ToggleCoordinateOverlay() {
    bCoordinatesVisible = !bCoordinatesVisible;
}

bool Menu::IsOpen() {
    return bIsOpen;
}

bool Menu::IsCoordinateOverlayVisible() {
    return bCoordinatesVisible;
}

void Menu::Render() {
    Solarpunk::InventoryEditor::Update(
        WorldRenderer::GetFrameState().Player);
    Solarpunk::InventoryEditor::MaintainLocks(
        Render::GetGameWindow());
    Solarpunk::ResourceEsp::Update(
        WorldRenderer::GetFrameState().Player);
    DrawResourceOverlay();
    DrawWaypointOverlay();
    DrawProjectionPreview();
    DrawCoordinateOverlay();

    if (bIsOpen) {
        MenuUI::ScreenScrim();
        DrawApplicationShell();
    }

    UpdateNotifications();
    Notifications::Render();
    TrainerSettings::Autosave(CaptureSettings());
}

void Menu::UpdateNotifications() {
    const FeatureNotificationSnapshot current =
        CaptureFeatureNotifications(
            bCoordinatesVisible,
            bProjectionPreviewVisible);
    if (!gFeatureNotificationBaselineReady) {
        gFeatureNotificationSnapshot = current;
        gFeatureNotificationBaselineReady = true;
    }
    else {
        const auto& previous =
            gFeatureNotificationSnapshot;
        NotifyFeatureTransition(
            previous.CoordinateHud,
            current.CoordinateHud,
            "feature.coordinate_hud",
            "Coordinate HUD");
        NotifyFeatureTransition(
            previous.ProjectionPreview,
            current.ProjectionPreview,
            "feature.projection_preview",
            "Projection preview");
        NotifyFeatureTransition(
            previous.ResourceMarkers,
            current.ResourceMarkers,
            "feature.resource_markers",
            "Resource markers");
        NotifyFeatureTransition(
            previous.GodMode,
            current.GodMode,
            "feature.god_mode",
            "God mode");
        NotifyFeatureTransition(
            previous.NoHunger,
            current.NoHunger,
            "feature.no_hunger",
            "No hunger");
        NotifyFeatureTransition(
            previous.NoThirst,
            current.NoThirst,
            "feature.no_thirst",
            "No thirst");
        NotifyFeatureTransition(
            previous.FreeBuilding,
            current.FreeBuilding,
            "feature.free_building",
            "Free building");
        NotifyFeatureTransition(
            previous.FreeCrafting,
            current.FreeCrafting,
            "feature.free_crafting",
            "Free crafting");
        NotifyFeatureTransition(
            previous.FreeResearch,
            current.FreeResearch,
            "feature.free_research",
            "Free research");
        NotifyFeatureTransition(
            previous.FreezeTime,
            current.FreezeTime,
            "feature.freeze_time",
            "Solar clock freeze");
        NotifyFeatureTransition(
            previous.ThirdPerson,
            current.ThirdPerson,
            "feature.third_person",
            "Third person");
        NotifyFeatureTransition(
            previous.Fly,
            current.Fly,
            "feature.fly",
            "Flight");
        NotifyFeatureTransition(
            previous.AirshipInfiniteBattery,
            current.AirshipInfiniteBattery,
            "feature.airship_battery",
            "Infinite airship battery");
        NotifyFeatureTransition(
            previous.AirshipNoHullDamage,
            current.AirshipNoHullDamage,
            "feature.airship_hull",
            "Airship hull protection");
        NotifyFeatureTransition(
            previous.AirshipAutopilot,
            current.AirshipAutopilot,
            "feature.airship_autopilot",
            "Airship autopilot");

        if (!previous.RuntimeUpdateFailed
            && current.RuntimeUpdateFailed) {
            Notifications::Push(
                Notifications::Kind::Error,
                "runtime.exploit_update_failed",
                "Runtime update failed",
                "One or more requested feature changes could not be applied.",
                5.5f);
        }
        if (!previous.FlyUpdateFailed
            && current.FlyUpdateFailed) {
            Notifications::Push(
                Notifications::Kind::Error,
                "runtime.fly_update_failed",
                "Flight update failed",
                "The movement controller rejected the current flight state.",
                5.5f);
        }
        gFeatureNotificationSnapshot = current;
    }

    const auto& inventory =
        Solarpunk::InventoryEditor::GetState();
    if (!inventory.IsReady())
        return;

    std::array<
        ObservedInventoryLock,
        Solarpunk::InventoryEditor::MaxInventorySlots>
        observed{};
    for (const auto& item : inventory.Items) {
        if (item.Index < 0
            || item.Index
                >= Solarpunk::InventoryEditor::MaxInventorySlots
            || (!item.QuantityLocked
                && !item.DurabilityLocked
                && !item.WaterLevelLocked)) {
            continue;
        }
        auto& lock =
            observed[static_cast<size_t>(item.Index)];
        lock.InventorySystem = inventory.InventorySystem;
        lock.ItemClass = item.ItemClass;
        lock.SlotIndex = item.Index;
        lock.Quantity = item.QuantityLocked;
        lock.Durability = item.DurabilityLocked;
        lock.WaterLevel = item.WaterLevelLocked;
        lock.DisplayName = item.DisplayName;
    }

    if (!gInventoryLockBaselineReady
        || gObservedInventorySystem
            != inventory.InventorySystem) {
        gObservedInventoryLocks = std::move(observed);
        gObservedInventorySystem =
            inventory.InventorySystem;
        gInventoryLockBaselineReady = true;
        return;
    }

    for (const auto& previous : gObservedInventoryLocks) {
        if (!previous.Any())
            continue;

        const auto item = std::find_if(
            inventory.Items.begin(),
            inventory.Items.end(),
            [&](const auto& candidate) {
                return candidate.Index
                        == previous.SlotIndex
                    && candidate.ItemClass
                        == previous.ItemClass;
            });
        if (item != inventory.Items.end()) {
            if (previous.Durability
                && !item->HasDurability) {
                Notifications::Push(
                    Notifications::Kind::Warning,
                    "inventory.lock.capability.durability."
                        + std::to_string(
                            previous.SlotIndex),
                    "Durability lock released",
                    previous.DisplayName
                        + " no longer exposes a durability value.",
                    5.0f);
            }
            if (previous.WaterLevel
                && !item->HasWaterLevel) {
                Notifications::Push(
                    Notifications::Kind::Warning,
                    "inventory.lock.capability.water."
                        + std::to_string(
                            previous.SlotIndex),
                    "Water-level lock released",
                    previous.DisplayName
                        + " no longer exposes a water-level value.",
                    5.0f);
            }
            continue;
        }

        const std::string valueNames =
            LockedValueNames(previous);
        Notifications::Push(
            Notifications::Kind::Warning,
            "inventory.lock.auto_release."
                + std::to_string(previous.SlotIndex),
            "Inventory lock released",
            previous.DisplayName
                + " left "
                + InventorySlotLabel(previous.SlotIndex)
                + "; its "
                + valueNames
                + (valueNames.find(" and ")
                        != std::string::npos
                    || valueNames.find(',')
                        != std::string::npos
                    ? " locks were released."
                    : " lock was released."),
            5.75f);
    }

    gObservedInventoryLocks = std::move(observed);
}

void Menu::DrawApplicationShell() {
    const auto& theme = MenuUI::GetTheme();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowSize(DefaultWindowSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(MinimumWindowSize, MaximumWindowSize);
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!ImGui::Begin("Solarpunk Trainer##main_window", &bIsOpen, flags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    const ImVec2 windowPosition = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 windowMax(
        windowPosition.x + windowSize.x,
        windowPosition.y + windowSize.y);
    const float alpha = std::clamp(menuOpacity / 100.0f, 0.55f, 1.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        windowPosition,
        windowMax,
        MenuUI::ColorU32(MenuUI::WithAlpha(
            theme.Colors.Window,
            theme.Colors.Window.w * alpha)),
        theme.Radius.LG);

    draw->AddRectFilled(
        ImVec2(windowPosition.x, windowPosition.y + theme.Sizing.TitleBarHeight),
        ImVec2(
            windowPosition.x + theme.Sizing.SidebarWidth,
            windowMax.y - theme.Sizing.StatusBarHeight),
        MenuUI::ColorU32(MenuUI::WithAlpha(
            theme.Colors.Sidebar,
            theme.Colors.Sidebar.w * alpha)));

    draw->AddLine(
        ImVec2(
            windowPosition.x + theme.Sizing.SidebarWidth,
            windowPosition.y + theme.Sizing.TitleBarHeight),
        ImVec2(
            windowPosition.x + theme.Sizing.SidebarWidth,
            windowMax.y - theme.Sizing.StatusBarHeight),
        MenuUI::ColorU32(theme.Colors.BorderSoft));
    draw->AddRect(
        windowPosition,
        windowMax,
        MenuUI::ColorU32(theme.Colors.Border),
        theme.Radius.LG);

    DrawTitleBar();
    DrawSidebar();

    ImGui::SetCursorScreenPos(ImVec2(
        windowPosition.x + theme.Sizing.SidebarWidth,
        windowPosition.y + theme.Sizing.TitleBarHeight));
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(theme.Spacing.XL, theme.Spacing.LG));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(
            theme.Spacing.MD,
            theme.Spacing.LG - theme.Spacing.XS));
    const bool pageChanged = currentPage != gLastRenderedPage;
    ImGui::BeginChild(
        PageChildId(currentPage),
        ImVec2(
            windowSize.x - theme.Sizing.SidebarWidth,
            windowSize.y - theme.Sizing.TitleBarHeight - theme.Sizing.StatusBarHeight),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_None);
    if (pageChanged)
        ImGui::SetScrollY(0.0f);
    gLastRenderedPage = currentPage;

    switch (currentPage) {
    case 0:
        DrawExploitsPage();
        break;
    case 1:
        DrawVisualsPage();
        break;
    case 2:
        DrawMovementPage();
        break;
    case 3:
        DrawInventoryPage();
        break;
    case 4:
        DrawWaypointsPage();
        break;
    case 5:
        DrawDiagnosticsPage();
        break;
    default:
        currentPage = 0;
        DrawExploitsPage();
        break;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    DrawStatusBar();

    ImGui::End();
    ImGui::PopStyleVar();
}

void Menu::DrawTitleBar() {
    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    const auto& frame = WorldRenderer::GetFrameState();
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float alpha = std::clamp(menuOpacity / 100.0f, 0.55f, 1.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        position,
        ImVec2(position.x + size.x, position.y + theme.Sizing.TitleBarHeight),
        MenuUI::ColorU32(MenuUI::WithAlpha(
            theme.Colors.TitleBar,
            theme.Colors.TitleBar.w * alpha)),
        theme.Radius.LG,
        ImDrawFlags_RoundCornersTop);
    draw->AddLine(
        ImVec2(position.x, position.y + theme.Sizing.TitleBarHeight),
        ImVec2(position.x + size.x, position.y + theme.Sizing.TitleBarHeight),
        MenuUI::ColorU32(theme.Colors.BorderSoft));

    ImGui::SetCursorScreenPos(ImVec2(position.x, position.y));
    ImGui::InvisibleButton(
        "##window_drag_region",
        ImVec2(size.x - 185.0f, theme.Sizing.TitleBarHeight));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImGui::SetWindowPos(ImVec2(
            position.x + ImGui::GetIO().MouseDelta.x,
            position.y + ImGui::GetIO().MouseDelta.y));
    }

    draw->AddText(
        fonts.Title,
        fonts.Title->LegacySize,
        ImVec2(position.x + theme.Spacing.LG, position.y + 12.0f),
        MenuUI::ColorU32(theme.Colors.TextPrimary),
        "Solarpunk Trainer");
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(position.x + theme.Spacing.LG, position.y + 38.0f),
        MenuUI::ColorU32(theme.Colors.TextMuted),
        "INTERNAL RUNTIME");

    const bool runtimeReady = frame.Player.HasCoordinates();
    const ImVec4& statusColor = runtimeReady
        ? theme.Colors.Success
        : theme.Colors.Warning;
    const char* statusText = runtimeReady ? "CONNECTED" : "WAITING";
    const ImVec2 statusSize = MeasureText(fonts.Small, statusText);
    const float statusX = position.x + size.x - 70.0f - statusSize.x;

    draw->AddCircleFilled(
        ImVec2(statusX - 10.0f, position.y + 30.0f),
        3.0f,
        MenuUI::ColorU32(statusColor));
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(statusX, position.y + 23.0f),
        MenuUI::ColorU32(statusColor),
        statusText);

    ImGui::SetCursorScreenPos(ImVec2(
        position.x + size.x - 48.0f,
        position.y + 16.0f));
    if (MenuUI::CloseButton("##close_menu"))
        bIsOpen = false;
}

void Menu::DrawSidebar() {
    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();

    ImGui::SetCursorScreenPos(ImVec2(
        position.x,
        position.y + theme.Sizing.TitleBarHeight));
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(theme.Spacing.MD, theme.Spacing.LG));
    ImGui::BeginChild(
        "##sidebar",
        ImVec2(
            theme.Sizing.SidebarWidth,
            size.y - theme.Sizing.TitleBarHeight - theme.Sizing.StatusBarHeight),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(theme.Spacing.MD, theme.Spacing.SM));

    ImGui::PushFont(fonts.Small);
    ImGui::TextColored(theme.Colors.TextMuted, "NAVIGATION");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));

    if (MenuUI::SidebarItem("##nav_exploits", "01", "Exploits", currentPage == 0))
        currentPage = 0;
    if (MenuUI::SidebarItem("##nav_visuals", "02", "Visuals", currentPage == 1))
        currentPage = 1;
    if (MenuUI::SidebarItem("##nav_movement", "03", "Movement", currentPage == 2))
        currentPage = 2;
    if (MenuUI::SidebarItem("##nav_inventory", "04", "Inventory", currentPage == 3))
        currentPage = 3;
    if (MenuUI::SidebarItem("##nav_waypoints", "05", "Waypoints", currentPage == 4))
        currentPage = 4;
    if (MenuUI::SidebarItem("##nav_diagnostics", "06", "Diagnostics", currentPage == 5))
        currentPage = 5;

    const float footerY = ImGui::GetWindowHeight() - 62.0f;
    if (ImGui::GetCursorPosY() < footerY)
        ImGui::SetCursorPosY(footerY);

    ImGui::PushFont(fonts.Small);
    ImGui::TextColored(theme.Colors.TextMuted, "SOLARPUNK  /  UE 5.7");
    ImGui::TextColored(theme.Colors.TextMuted, "SDK PROFILE  2026.07");
    ImGui::PopFont();

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void Menu::DrawStatusBar() {
    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    const auto& frame = WorldRenderer::GetFrameState();
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float y = position.y + size.y - theme.Sizing.StatusBarHeight;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        ImVec2(position.x, y),
        ImVec2(position.x + size.x, position.y + size.y),
        MenuUI::ColorU32(theme.Colors.TitleBar),
        theme.Radius.LG,
        ImDrawFlags_RoundCornersBottom);
    draw->AddLine(
        ImVec2(position.x, y),
        ImVec2(position.x + size.x, y),
        MenuUI::ColorU32(theme.Colors.BorderSoft));

    const auto& flyState = Solarpunk::FlyHack::GetState();
    const auto& flySettings = Solarpunk::FlyHack::GetSettings();
    const char* runtimeText = flyState.IsActive()
        ? (flySettings.FlightMode == Solarpunk::FlyHack::Mode::Noclip
            ? "Noclip flight active"
            : "Velocity flight active")
        : (frame.Projection.IsValid
            ? "View projection ready"
            : Solarpunk::GetCameraStatusText(frame.Camera.Status));
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(position.x + theme.Spacing.LG, y + 9.0f),
        MenuUI::ColorU32(
            flyState.IsActive() || frame.Projection.IsValid
            ? theme.Colors.Success
            : theme.Colors.TextMuted),
        runtimeText);

    const char* hotkeys = "INSERT  MENU     F2  HUD     END  UNLOAD";
    const ImVec2 hotkeySize = MeasureText(fonts.Small, hotkeys);
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(position.x + size.x - theme.Spacing.LG - hotkeySize.x, y + 9.0f),
        MenuUI::ColorU32(theme.Colors.TextMuted),
        hotkeys);
}

void Menu::DrawExploitsPage() {
    const auto& theme = MenuUI::GetTheme();
    const auto& frame = WorldRenderer::GetFrameState();
    auto& settings = Solarpunk::PlayerExploits::GetSettings();
    const auto& state = Solarpunk::PlayerExploits::GetState();

    MenuUI::PageHeader(
        "Exploits",
        "Survival, camera, world-time, and piloted-airship overrides.");

    const float available = ImGui::GetContentRegionAvail().x;
    const CardGrid grid = CalculateCardGrid(
        available,
        OverviewCardMinWidth,
        2);

    MenuUI::BeginCard(
        "##survival_exploits_card",
        ImVec2(grid.Width, 372.0f));
    MenuUI::CardHeading(
        "Survival",
        "Settings autosave and reactivate when the local player is ready");
    MenuUI::StatusBadge(
        Solarpunk::PlayerExploits::GetStatusText(
            state.CurrentStatus),
        state.CurrentStatus
            == Solarpunk::PlayerExploits::Status::Ready
        ? MenuUI::StatusKind::Success
        : state.CurrentStatus
            == Solarpunk::PlayerExploits::Status::UpdateFailed
            ? MenuUI::StatusKind::Error
            : MenuUI::StatusKind::Warning);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
    MenuUI::ToggleRow(
        "##exploit_god_mode",
        "Godmode",
        "Block Unreal actor damage and continuously restore maximum health.",
        &settings.GodMode);
    MenuUI::ToggleRow(
        "##exploit_no_hunger",
        "No Hunger",
        "Keep the controller's hunger value at its current maximum.",
        &settings.NoHunger);
    MenuUI::ToggleRow(
        "##exploit_no_thirst",
        "No Thirst",
        "Keep the controller's thirst value at its current maximum.",
        &settings.NoThirst);
    MenuUI::EndCard();

    if (grid.Columns > 1)
        ImGui::SameLine(0.0f, theme.Spacing.LG);
    else
        ImGui::Dummy(ImVec2(0.0f, theme.Spacing.LG));

    MenuUI::BeginCard(
        "##survival_values_card",
        ImVec2(grid.Width, 372.0f));
    MenuUI::CardHeading(
        "Live values",
        "Values are read directly from BP_MainPlayerController_C");
    char health[48]{};
    char hunger[48]{};
    char thirst[48]{};
    std::snprintf(
        health,
        sizeof(health),
        "%d / %d",
        state.Health,
        state.MaxHealth);
    std::snprintf(
        hunger,
        sizeof(hunger),
        "%d / %d",
        state.Hunger,
        state.MaxHunger);
    std::snprintf(
        thirst,
        sizeof(thirst),
        "%d / %d",
        state.Thirst,
        state.MaxThirst);
    MenuUI::Metric(
        "HEALTH",
        frame.Player.PlayerController ? health : "--",
        settings.GodMode ? "Damage blocked" : "Game controlled");
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    MenuUI::Metric(
        "HUNGER",
        frame.Player.PlayerController ? hunger : "--",
        settings.NoHunger ? "Locked at maximum" : "Game controlled");
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    MenuUI::Metric(
        "THIRST",
        frame.Player.PlayerController ? thirst : "--",
        settings.NoThirst ? "Locked at maximum" : "Game controlled");
    MenuUI::EndCard();

    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.LG));
    MenuUI::BeginCard(
        "##world_progression_exploits_card",
        ImVec2(0.0f, 680.0f));
    MenuUI::CardHeading(
        "World and progression",
        "Use the game's native databases and Blueprint systems while removing material costs");
    const bool anyProgressionRequested =
        settings.FreeBuilding
        || settings.FreeCrafting
        || settings.FreeResearch;
    const bool allRequestedProgressionActive =
        (!settings.FreeBuilding || state.FreeBuildingActive)
        && (!settings.FreeCrafting || state.FreeCraftingActive)
        && (!settings.FreeResearch || state.FreeResearchActive);
    MenuUI::StatusBadge(
        state.RuntimeUpdateFailed
            ? "WORLD UPDATE FAILED"
            : anyProgressionRequested
                ? (allRequestedProgressionActive
                    ? "FREE COSTS ACTIVE"
                    : "APPLYING FREE COSTS")
                : state.DayNightCycle
                    ? "WORLD SYSTEMS READY"
                    : "WAITING FOR WORLD",
        state.RuntimeUpdateFailed
            ? MenuUI::StatusKind::Error
            : anyProgressionRequested
                && allRequestedProgressionActive
                ? MenuUI::StatusKind::Success
                : state.DayNightCycle
                    ? MenuUI::StatusKind::Success
                    : MenuUI::StatusKind::Warning);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
    MenuUI::ToggleRow(
        "##exploit_free_building",
        "Free building",
        "Remove construction material costs while preserving the game's normal placement and save flow.",
        &settings.FreeBuilding);
    MenuUI::ToggleRow(
        "##exploit_free_crafting",
        "Free crafting",
        "Craft without owning or consuming ingredients. Reopen a crafting screen after changing this mode.",
        &settings.FreeCrafting);
    MenuUI::ToggleRow(
        "##exploit_free_research",
        "Free research upgrades",
        "Unlock research without owning or consuming required items while preserving the normal save path.",
        &settings.FreeResearch);

    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    ImGui::PushFont(MenuUI::GetFonts().Heading);
    ImGui::TextColored(
        theme.Colors.TextPrimary,
        "Environment");
    ImGui::PopFont();
    MenuUI::WrappedText(
        "Set the solar clock, freeze it at the selected hour, or trigger one of the game's native weather transitions.",
        theme.Colors.TextMuted,
        MenuUI::GetFonts().Small);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
    MenuUI::ToggleRow(
        "##exploit_freeze_time",
        "Freeze solar clock",
        "Hold the day-night actor at the selected time without pausing the rest of the simulation.",
        &settings.FreezeTime);
    MenuUI::SliderRow(
        "##exploit_time_of_day",
        "Time of day",
        "A 24-hour solar-clock value; use Set time for a one-shot change.",
        &settings.TimeOfDay,
        0.0f,
        24.0f,
        "%.2f h");

    const bool worldAvailable = frame.Player.World != 0;
    if (MenuUI::ActionButton(
        "##environment_set_time",
        settings.FreezeTime
            ? "Apply and hold time"
            : "Set time",
        ImVec2(170.0f, theme.Sizing.ButtonHeight),
        MenuUI::ButtonKind::Primary,
        worldAvailable)) {
        Solarpunk::PlayerExploits::RequestTimeOfDay(
            settings.TimeOfDay);
    }
    ImGui::SameLine(0.0f, theme.Spacing.SM);
    char liveTime[80]{};
    if (state.DayNightCycle) {
        std::snprintf(
            liveTime,
            sizeof(liveTime),
            "Live clock  %.2f h%s",
            state.CurrentTimeOfDay,
            state.TimeFrozen ? "  /  FROZEN" : "");
    } else {
        strcpy_s(liveTime, "Solar clock unavailable");
    }
    ImGui::PushFont(MenuUI::GetFonts().Small);
    ImGui::TextColored(
        state.TimeFrozen
            ? theme.Colors.Success
            : theme.Colors.TextMuted,
        "%s",
        liveTime);
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    const float weatherGap = theme.Spacing.SM;
    const float weatherWidth = (std::max)(
        72.0f,
        (ImGui::GetContentRegionAvail().x
            - weatherGap * 3.0f) / 4.0f);
    if (MenuUI::ActionButton(
        "##environment_dynamic",
        "Dynamic",
        ImVec2(weatherWidth, theme.Sizing.ButtonHeight),
        MenuUI::ButtonKind::Secondary,
        worldAvailable)) {
        Solarpunk::PlayerExploits::RequestWeatherPreset(
            Solarpunk::PlayerExploits::WeatherPreset::ResumeDynamic);
    }
    ImGui::SameLine(0.0f, weatherGap);
    if (MenuUI::ActionButton(
        "##environment_sunny",
        "Sunny",
        ImVec2(weatherWidth, theme.Sizing.ButtonHeight),
        MenuUI::ButtonKind::Secondary,
        worldAvailable)) {
        Solarpunk::PlayerExploits::RequestWeatherPreset(
            Solarpunk::PlayerExploits::WeatherPreset::Sunny);
    }
    ImGui::SameLine(0.0f, weatherGap);
    if (MenuUI::ActionButton(
        "##environment_rain",
        "Light rain",
        ImVec2(weatherWidth, theme.Sizing.ButtonHeight),
        MenuUI::ButtonKind::Secondary,
        worldAvailable)) {
        Solarpunk::PlayerExploits::RequestWeatherPreset(
            Solarpunk::PlayerExploits::WeatherPreset::LightRain);
    }
    ImGui::SameLine(0.0f, weatherGap);
    if (MenuUI::ActionButton(
        "##environment_storm",
        "Storm",
        ImVec2(weatherWidth, theme.Sizing.ButtonHeight),
        MenuUI::ButtonKind::Secondary,
        worldAvailable)) {
        Solarpunk::PlayerExploits::RequestWeatherPreset(
            Solarpunk::PlayerExploits::WeatherPreset::Thunderstorm);
    }
    MenuUI::EndCard();

    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.LG));
    MenuUI::BeginCard(
        "##camera_world_exploits_card",
        ImVec2(0.0f, 448.0f));
    MenuUI::CardHeading(
        "Camera and world",
        "On-foot third person is reversible; world speed affects the complete simulation");
    if (state.RuntimeUpdateFailed) {
        MenuUI::StatusBadge(
            "RUNTIME UPDATE FAILED",
            MenuUI::StatusKind::Error);
    }
    else if (state.ThirdPersonActive) {
        MenuUI::StatusBadge(
            "THIRD PERSON ACTIVE",
            MenuUI::StatusKind::Success);
    }
    else if (state.WorldSettings) {
        MenuUI::StatusBadge(
            "WORLD RUNTIME READY",
            MenuUI::StatusKind::Success);
    }
    else {
        MenuUI::StatusBadge(
            "WAITING FOR WORLD",
            MenuUI::StatusKind::Warning);
    }
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
    MenuUI::ToggleRow(
        "##exploit_third_person",
        "Third person",
        "Keep an on-foot chase camera behind the character while compensating interaction reach.",
        &settings.ThirdPerson);

    if (!settings.ThirdPerson)
        ImGui::BeginDisabled();
    MenuUI::SliderRow(
        "##exploit_third_person_distance",
        "Camera distance",
        "Horizontal distance behind the character.",
        &settings.ThirdPersonDistance,
        100.0f,
        800.0f,
        "%.0f cm");
    MenuUI::SliderRow(
        "##exploit_third_person_height",
        "Camera height",
        "Vertical offset above the original first-person camera position.",
        &settings.ThirdPersonHeight,
        0.0f,
        250.0f,
        "%.0f cm");
    if (!settings.ThirdPerson)
        ImGui::EndDisabled();

    MenuUI::SliderRow(
        "##exploit_game_speed",
        "Game speed",
        "Scale global Unreal time dilation; 1.00x restores the game's original value.",
        &settings.GameSpeed,
        0.10f,
        5.0f,
        "%.2fx");
    char currentGameSpeed[96]{};
    std::snprintf(
        currentGameSpeed,
        sizeof(currentGameSpeed),
        "Current engine time scale: %.2fx",
        state.CurrentGameSpeed);
    MenuUI::WrappedText(
        currentGameSpeed,
        theme.Colors.TextMuted,
        MenuUI::GetFonts().Small);
    MenuUI::EndCard();

    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.LG));
    MenuUI::BeginCard(
        "##airship_exploits_card",
        ImVec2(0.0f, 536.0f));
    MenuUI::CardHeading(
        "Airship systems",
        "Performance and protection overrides apply only to the airship you are piloting");
    const bool airshipUpdateFailed =
        state.ControlledAirship && state.RuntimeUpdateFailed;
    MenuUI::StatusBadge(
        airshipUpdateFailed
            ? "AIRSHIP UPDATE FAILED"
            : state.ControlledAirship
            ? "PILOTED AIRSHIP READY"
            : "BOARD THE AIRSHIP TO APPLY",
        airshipUpdateFailed
            ? MenuUI::StatusKind::Error
            : state.ControlledAirship
            ? MenuUI::StatusKind::Success
            : MenuUI::StatusKind::Warning);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
    MenuUI::ToggleRow(
        "##exploit_airship_infinite_battery",
        "Infinite battery",
        "Continuously refill the active airship and refresh its battery display.",
        &settings.AirshipInfiniteBattery);
    MenuUI::ToggleRow(
        "##exploit_airship_no_hull_damage",
        "No hull damage",
        "Repair accumulated damage and keep the piloted airship at zero damage.",
        &settings.AirshipNoHullDamage);
    MenuUI::ToggleRow(
        "##exploit_airship_autopilot",
        "Autopilot",
        "Maintain the ship's native autopilot state; disabling restores its previous state.",
        &settings.AirshipAutopilot);
    MenuUI::SliderRow(
        "##exploit_airship_speed",
        "Ship speed multiplier",
        "Applies only while you are piloting and restores the original tuning on exit.",
        &settings.AirshipSpeedMultiplier,
        1.0f,
        20.0f,
        "%.2fx");
    MenuUI::SliderRow(
        "##exploit_airship_boost",
        "Boost power multiplier",
        "Scale the airship's native boost strength without changing ordinary flight speed.",
        &settings.AirshipBoostMultiplier,
        1.0f,
        10.0f,
        "%.2fx");
    MenuUI::EndCard();

    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.LG));
    MenuUI::BeginCard(
        "##airship_telemetry_card",
        ImVec2(0.0f, 224.0f));
    MenuUI::CardHeading(
        "Airship telemetry",
        "Live values from the currently piloted ship");
    char airshipCurrentSpeed[48]{};
    char airshipMaximumSpeed[48]{};
    char airshipBoost[48]{};
    char airshipBattery[48]{};
    char airshipDamage[48]{};
    std::snprintf(
        airshipCurrentSpeed,
        sizeof(airshipCurrentSpeed),
        state.ControlledAirship ? "%.1f" : "--",
        state.AirshipCurrentSpeed);
    std::snprintf(
        airshipMaximumSpeed,
        sizeof(airshipMaximumSpeed),
        state.ControlledAirship ? "%.1f" : "--",
        state.AirshipMaxSpeed);
    std::snprintf(
        airshipBoost,
        sizeof(airshipBoost),
        state.ControlledAirship ? "%.1f" : "--",
        state.AirshipBoostStrength);
    if (state.ControlledAirship) {
        std::snprintf(
            airshipBattery,
            sizeof(airshipBattery),
            "%d / %d",
            state.AirshipBattery,
            state.AirshipMaxBattery);
        std::snprintf(
            airshipDamage,
            sizeof(airshipDamage),
            "%d / %d",
            state.AirshipDamage,
            state.AirshipMaxDamage);
    }
    else {
        std::snprintf(
            airshipBattery,
            sizeof(airshipBattery),
            "--");
        std::snprintf(
            airshipDamage,
            sizeof(airshipDamage),
            "--");
    }
    if (ImGui::BeginTable(
        "##airship_live_values",
        3,
        ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        MenuUI::Metric(
            "CURRENT SPEED",
            airshipCurrentSpeed,
            "Live movement value");
        ImGui::TableNextColumn();
        MenuUI::Metric(
            "MAX SPEED",
            airshipMaximumSpeed,
            "Live tuned maximum");
        ImGui::TableNextColumn();
        MenuUI::Metric(
            "BOOST STRENGTH",
            airshipBoost,
            state.AirshipBoosting
                ? "Boost active"
                : "Boost standby");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        MenuUI::Metric(
            "BATTERY",
            airshipBattery,
            settings.AirshipInfiniteBattery
                ? "Held at capacity"
                : "Ship controlled");
        ImGui::TableNextColumn();
        MenuUI::Metric(
            "HULL DAMAGE",
            airshipDamage,
            settings.AirshipNoHullDamage
                ? "Held at zero"
                : "Ship controlled");
        ImGui::TableNextColumn();
        MenuUI::Metric(
            "AUTOPILOT",
            state.ControlledAirship
                ? state.AirshipAutopilotActive
                    ? "ON"
                    : "OFF"
                : "--",
            settings.AirshipAutopilot
                ? "Trainer maintained"
                : "Ship controlled");
        ImGui::EndTable();
    }
    MenuUI::EndCard();
}

void Menu::DrawVisualsPage() {
    const auto& theme = MenuUI::GetTheme();
    const auto& frame = WorldRenderer::GetFrameState();
    auto& resourceSettings = Solarpunk::ResourceEsp::GetSettings();
    const auto& resourceState = Solarpunk::ResourceEsp::GetState();

    MenuUI::PageHeader(
        "Visuals",
        "Configure the HUD, resource markers, and world-space diagnostics.");

    MenuUI::BeginCard("##hud_settings_card", ImVec2(0.0f, 204.0f));
    MenuUI::CardHeading("Overlay layers", "Independent, click-through ImGui draw layers");
    MenuUI::ToggleRow(
        "##visual_coordinate_hud",
        "Coordinate HUD",
        "Display live X, Y, and Z values from the local pawn root component.",
        &bCoordinatesVisible);
    MenuUI::ToggleRow(
        "##visual_projection_preview",
        "World projection test",
        "Render RGB world axes 500 cm in front of the camera.",
        &bProjectionPreviewVisible,
        frame.Projection.IsValid);
    MenuUI::EndCard();

    char resourceSummary[128]{};
    std::snprintf(
        resourceSummary,
        sizeof(resourceSummary),
        "%zu entities across %u levels  /  %u foliage instances checked",
        resourceState.MarkerCount(),
        resourceState.LevelsScanned,
        resourceState.FoliageInstancesScanned);

    MenuUI::BeginCard("##resource_markers_card", ImVec2(0.0f, 432.0f));
    MenuUI::CardHeading("Resource markers", resourceSummary);
    MenuUI::StatusKind resourceStatusKind = MenuUI::StatusKind::Neutral;
    if (resourceState.Status == Solarpunk::ResourceEsp::ScanStatus::Ready)
        resourceStatusKind = MenuUI::StatusKind::Success;
    else if (resourceState.Status
        == Solarpunk::ResourceEsp::ScanStatus::NamePoolUnavailable)
        resourceStatusKind = MenuUI::StatusKind::Error;
    else if (resourceState.Status
        == Solarpunk::ResourceEsp::ScanStatus::WaitingForWorld)
        resourceStatusKind = MenuUI::StatusKind::Warning;
    MenuUI::StatusBadge(
        Solarpunk::ResourceEsp::GetStatusText(resourceState.Status),
        resourceStatusKind);
    const std::string resourceLogSummary =
        "FRIENDLY NAMES  "
        + std::to_string(Solarpunk::ResourceNames::MappingCount())
        + " mapped  /  "
        + std::to_string(resourceState.LoggedClassCount)
        + " logged";
    MenuUI::WrappedText(
        resourceLogSummary.c_str(),
        theme.Colors.TextMuted,
        MenuUI::GetFonts().Small);
    if (ImGui::IsItemHovered()
        && !resourceState.ClassLogPath.empty()) {
        ImGui::SetTooltip(
            "%s",
            resourceState.ClassLogPath.c_str());
    }
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
    MenuUI::ToggleRow(
        "##resource_markers_enabled",
        "Resource markers",
        "Scan streamed actors and Solar Punk foliage instances.",
        &resourceSettings.Enabled);
    MenuUI::ToggleRow(
        "##resource_marker_labels",
        "Resource labels",
        "Show friendly names; unknown classes fall back to their raw SDK name.",
        &resourceSettings.ShowLabels,
        resourceSettings.Enabled);
    MenuUI::ToggleRow(
        "##resource_marker_distance",
        "Distance",
        "Append straight-line distance in meters to each label.",
        &resourceSettings.ShowDistance,
        resourceSettings.Enabled && resourceSettings.ShowLabels);
    if (!resourceSettings.Enabled)
        ImGui::BeginDisabled();
    MenuUI::SliderRow(
        "##resource_marker_range",
        "Scan range",
        "Limit actor markers to a practical radius around the player.",
        &resourceSettings.MaxDistanceMeters,
        10.0f,
        1000.0f,
        "%.0f m");
    if (!resourceSettings.Enabled)
        ImGui::EndDisabled();
    MenuUI::EndCard();

    char harvestableSummary[128]{};
    std::snprintf(
        harvestableSummary,
        sizeof(harvestableSummary),
        "Trees %u  /  stone and ore %u  /  plants %u",
        resourceState.TreeCount,
        resourceState.OreCount,
        resourceState.PlantCount);

    MenuUI::BeginCard("##harvestable_classes_card", ImVec2(0.0f, 286.0f));
    MenuUI::CardHeading("Harvestables", harvestableSummary);
    MenuUI::ToggleRow(
        "##resource_trees",
        "Trees",
        "Tree actors and instanced foliage linked to BP_Tree classes.",
        &resourceSettings.ShowTrees,
        resourceSettings.Enabled);
    MenuUI::ToggleRow(
        "##resource_ores",
        "Stone and ore",
        "Ore actors, ore patches, and instanced BP_Ore foliage.",
        &resourceSettings.ShowOres,
        resourceSettings.Enabled);
    MenuUI::ToggleRow(
        "##resource_plants",
        "Plants and berries",
        "Both world plants and runtime _BP_LocalPlant instances.",
        &resourceSettings.ShowPlants,
        resourceSettings.Enabled);
    MenuUI::EndCard();

    char entitySummary[160]{};
    std::snprintf(
        entitySummary,
        sizeof(entitySummary),
        "Pickups %u  /  drops %u  /  loot %u  /  animals %u  /  NPCs %u",
        resourceState.PickupCount,
        resourceState.DroppedItemCount,
        resourceState.LootCount,
        resourceState.AnimalCount,
        resourceState.NpcCount);

    MenuUI::BeginCard("##world_entity_classes_card", ImVec2(0.0f, 404.0f));
    MenuUI::CardHeading("World entities", entitySummary);
    MenuUI::ToggleRow(
        "##resource_pickups",
        "Ground pickups",
        "Loose stone, clay, and sticks using BP_GrabItem_MASTER_C.",
        &resourceSettings.ShowPickups,
        resourceSettings.Enabled);
    MenuUI::ToggleRow(
        "##resource_dropped_items",
        "Dropped items",
        "Spawned inventory actors derived from _BP_ItemActor_MASTER_C.",
        &resourceSettings.ShowDroppedItems,
        resourceSettings.Enabled);
    MenuUI::ToggleRow(
        "##resource_loot",
        "Loot chests",
        "Random world chests across every loot rarity and phase.",
        &resourceSettings.ShowLoot,
        resourceSettings.Enabled);
    MenuUI::ToggleRow(
        "##resource_animals",
        "Animals",
        "Chickens, pigs, sheep, and animal drone actors.",
        &resourceSettings.ShowAnimals,
        resourceSettings.Enabled);
    MenuUI::ToggleRow(
        "##resource_npcs",
        "NPCs and tradebots",
        "Merchant and runtime tradebot actor families.",
        &resourceSettings.ShowNpcs,
        resourceSettings.Enabled);
    MenuUI::EndCard();

    MenuUI::BeginCard("##appearance_card", ImVec2(0.0f, 236.0f));
    MenuUI::CardHeading("Appearance", "Graphite surfaces with adjustable translucency");
    MenuUI::SliderRow(
        "##menu_opacity",
        "Menu opacity",
        "Adjust the main shell and sidebar transparency.",
        &menuOpacity,
        80.0f,
        100.0f,
        "%.0f%%");
    MenuUI::SliderRow(
        "##hud_opacity",
        "HUD opacity",
        "Adjust the compact coordinate card transparency.",
        &hudOpacity,
        55.0f,
        100.0f,
        "%.0f%%");
    MenuUI::EndCard();
}

void Menu::DrawDiagnosticsPage() {
    const auto& theme = MenuUI::GetTheme();
    const auto& frame = WorldRenderer::GetFrameState();

    MenuUI::PageHeader(
        "Diagnostics",
        "SDK offsets, player resolution, camera cache, and world-projection data.");

    const float sessionAvailable = ImGui::GetContentRegionAvail().x;
    const CardGrid sessionGrid = CalculateCardGrid(
        sessionAvailable,
        OverviewCardMinWidth,
        2);

    MenuUI::BeginCard(
        "##diagnostic_session_card",
        ImVec2(sessionGrid.Width, 190.0f));
    MenuUI::CardHeading(
        "Player session",
        "Current local-player resolution state");
    MenuUI::StatusBadge(
        frame.Player.HasCoordinates()
            ? "PLAYER READY"
            : "ACQUIRING PLAYER",
        frame.Player.HasCoordinates()
            ? MenuUI::StatusKind::Success
            : MenuUI::StatusKind::Warning);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    MenuUI::PointerRow("UWorld", frame.Player.World);
    MenuUI::PointerRow("Local player", frame.Player.LocalPlayer);
    MenuUI::EndCard();

    if (sessionGrid.Columns > 1)
        ImGui::SameLine(0.0f, theme.Spacing.LG);

    MenuUI::BeginCard(
        "##diagnostic_projection_card",
        ImVec2(sessionGrid.Width, 190.0f));
    MenuUI::CardHeading(
        "World projection",
        "Camera cache and view-projection matrices");
    MenuUI::StatusBadge(
        frame.Projection.IsValid
            ? "PROJECTION READY"
            : "CAMERA WAITING",
        frame.Projection.IsValid
            ? MenuUI::StatusKind::Success
            : MenuUI::StatusKind::Warning);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    char diagnosticFov[32]{};
    std::snprintf(
        diagnosticFov,
        sizeof(diagnosticFov),
        "%.1f degrees",
        frame.Camera.FieldOfView);
    MenuUI::PointerRow(
        "Camera manager",
        frame.Camera.PlayerCameraManager);
    MenuUI::Metric(
        "FIELD OF VIEW",
        frame.Camera.HasCamera() ? diagnosticFov : "--");
    MenuUI::EndCard();

    const float available = ImGui::GetContentRegionAvail().x;
    const CardGrid grid = CalculateCardGrid(
        available,
        MetricCardMinWidth,
        3);
    char values[3][48]{};

    if (frame.Player.HasCoordinates()) {
        FormatCoordinate(values[0], sizeof(values[0]), frame.Player.Coordinates.X);
        FormatCoordinate(values[1], sizeof(values[1]), frame.Player.Coordinates.Y);
        FormatCoordinate(values[2], sizeof(values[2]), frame.Player.Coordinates.Z);
    }
    else {
        std::snprintf(values[0], sizeof(values[0]), "--");
        std::snprintf(values[1], sizeof(values[1]), "--");
        std::snprintf(values[2], sizeof(values[2]), "--");
    }

    constexpr const char* labels[] = { "WORLD X", "WORLD Y", "WORLD Z" };
    for (int index = 0; index < 3; ++index) {
        char cardId[32]{};
        std::snprintf(cardId, sizeof(cardId), "##coordinate_metric_%d", index);
        MenuUI::BeginCard(cardId, ImVec2(grid.Width, 104.0f));
        MenuUI::Metric(labels[index], values[index], "Unreal units / cm");
        MenuUI::EndCard();
        if (index < 2 && (index + 1) % grid.Columns != 0)
            ImGui::SameLine(0.0f, theme.Spacing.LG);
    }

    MenuUI::BeginCard("##player_chain_card", ImVec2(0.0f, 270.0f));
    MenuUI::CardHeading(
        "Resolved object chain",
        Solarpunk::GetStatusText(frame.Player.Status));
    MenuUI::PointerRow("UWorld", frame.Player.World);
    MenuUI::PointerRow("UGameInstance", frame.Player.GameInstance);
    MenuUI::PointerRow("ULocalPlayer", frame.Player.LocalPlayer);
    MenuUI::PointerRow("APlayerController", frame.Player.PlayerController);
    MenuUI::PointerRow("APawn", frame.Player.Pawn);
    MenuUI::PointerRow("USceneComponent", frame.Player.RootComponent);
    MenuUI::EndCard();

    MenuUI::BeginCard(
        "##sdk_offsets_card",
        ImVec2(0.0f, 352.0f));
    MenuUI::CardHeading(
        "SDK offsets",
        "Solarpunk 5.7.1-0+UE5 profile; image-relative unless noted");
    MenuUI::PointerRow("GObjects", 0x076CA920);
    MenuUI::PointerRow("GWorld", 0x078CCE58);
    MenuUI::PointerRow("GNames", 0x078E50C0);
    MenuUI::PointerRow("ProcessEvent", 0x013B1C38);
    MenuUI::PointerRow(
        "Pawn.InventorySystem",
        0x06F8);
    MenuUI::PointerRow(
        "InventorySystem.Inventory",
        0x00C8);
    MenuUI::PointerRow(
        "PlayerCameraManager.CameraCache",
        0x1550);
    MenuUI::EndCard();

    DrawRuntimeDiagnostics();
}

void Menu::DrawInventoryPage() {
    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    const auto& state = Solarpunk::InventoryEditor::GetState();
    const auto& catalog = Solarpunk::InventoryEditor::GetCatalog();
    const auto catalogStatus =
        Solarpunk::InventoryEditor::GetCatalogStatus();

    struct ItemDraft {
        int Quantity = Solarpunk::InventoryEditor::MinimumQuantity;
        int ObservedQuantity = 0;
        int Durability = Solarpunk::InventoryEditor::MinimumDurability;
        int ObservedDurability = -1;
        double WaterLevel =
            Solarpunk::InventoryEditor::MinimumWaterLevel;
        double ObservedWaterLevel = -1.0;
        uintptr_t ItemClass = 0;
    };

    static uintptr_t lastInventorySystem = 0;
    // Keep this trivially initialized. The trainer is commonly manual-mapped,
    // so function-local STL containers cannot be assumed to have had their
    // runtime constructor invoked before the first populated inventory frame.
    static std::array<
        ItemDraft,
        Solarpunk::InventoryEditor::MaxInventorySlots> drafts{};
    static char search[96]{};
    static char catalogSearch[96]{};
    static uintptr_t selectedCatalogClass = 0;
    static int addQuantity = 1;

    if (lastInventorySystem != state.InventorySystem) {
        drafts.fill({});
        search[0] = '\0';
        lastInventorySystem = state.InventorySystem;
    }

    MenuUI::PageHeader(
        "Inventory editor",
        "Edit and independently lock quantities, tool durability, and watering-can water levels.");

    const Solarpunk::InventoryEditor::CatalogItem* selectedCatalogItem =
        nullptr;
    for (const auto& item : catalog) {
        if (item.ItemClass == selectedCatalogClass) {
            selectedCatalogItem = &item;
            break;
        }
    }
    if (!selectedCatalogItem)
        selectedCatalogClass = 0;

    MenuUI::BeginCard(
        "##inventory_add_item_card",
        ImVec2(0.0f, 478.0f));
    char catalogDescription[160]{};
    std::snprintf(
        catalogDescription,
        sizeof(catalogDescription),
        "%zu loaded item classes  /  %d empty slot%s",
        catalog.size(),
        state.EmptyCount,
        state.EmptyCount == 1 ? "" : "s");
    MenuUI::CardHeading("Add new item", catalogDescription);
    MenuUI::StatusBadge(
        Solarpunk::InventoryEditor::GetCatalogStatusText(
            catalogStatus),
        catalogStatus
            == Solarpunk::InventoryEditor::CatalogStatus::Ready
            ? MenuUI::StatusKind::Success
            : catalogStatus
                == Solarpunk::InventoryEditor::CatalogStatus::Unavailable
                ? MenuUI::StatusKind::Error
                : MenuUI::StatusKind::Warning);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
    MenuUI::SearchBox(
        "##inventory_catalog_search",
        catalogSearch,
        sizeof(catalogSearch),
        "Search the loaded item catalog...");
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));

    std::vector<const Solarpunk::InventoryEditor::CatalogItem*>
        matchingCatalogItems;
    const std::string catalogQuery = LowerCopy(catalogSearch);
    matchingCatalogItems.reserve(catalog.size());
    for (const auto& item : catalog) {
        if (!catalogQuery.empty()) {
            const std::string displayName =
                LowerCopy(item.DisplayName);
            const std::string className =
                LowerCopy(item.ClassName);
            if (displayName.find(catalogQuery)
                    == std::string::npos
                && className.find(catalogQuery)
                    == std::string::npos) {
                continue;
            }
        }
        matchingCatalogItems.push_back(&item);
    }

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(theme.Spacing.XS, theme.Spacing.XS));
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        MenuUI::WithAlpha(theme.Colors.Window, 0.34f));
    ImGui::BeginChild(
        "##inventory_catalog_results",
        ImVec2(0.0f, 174.0f),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (catalogStatus
        == Solarpunk::InventoryEditor::CatalogStatus::Ready) {
        ImGuiListClipper clipper;
        clipper.Begin(
            static_cast<int>(matchingCatalogItems.size()),
            58.0f);
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart;
                 index < clipper.DisplayEnd;
                 ++index) {
                const auto* item =
                    matchingCatalogItems[
                        static_cast<size_t>(index)];
                char rowId[64]{};
                std::snprintf(
                    rowId,
                    sizeof(rowId),
                    "##catalog_item_%llX",
                    static_cast<unsigned long long>(
                        item->ItemClass));
                if (MenuUI::SelectionRow(
                    rowId,
                    item->DisplayName.c_str(),
                    item->ClassName.c_str(),
                    selectedCatalogClass
                        == item->ItemClass,
                    static_cast<ImTextureID>(
                        ItemIconCache::GetTextureId(
                            item->ItemClass)))) {
                    selectedCatalogClass = item->ItemClass;
                    selectedCatalogItem = item;
                }
            }
        }
    }
    else {
        ImGui::Dummy(ImVec2(0.0f, theme.Spacing.LG));
        ImGui::PushFont(fonts.Small);
        ImGui::TextColored(
            theme.Colors.TextSecondary,
            "%s",
            catalogStatus
                == Solarpunk::InventoryEditor::CatalogStatus::Unavailable
                ? "The Blueprint item-class hierarchy could not be resolved."
                : "Scanning loaded Blueprint classes...");
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));

    if (selectedCatalogItem) {
        if (MenuUI::QuantityEditorRow(
            "##inventory_add_selected",
            selectedCatalogItem->DisplayName.c_str(),
            selectedCatalogItem->ClassName.c_str(),
            "NEW ITEM   /   FREE SLOT",
            0,
            &addQuantity,
            Solarpunk::InventoryEditor::MinimumQuantity,
            Solarpunk::InventoryEditor::MaximumQuantity,
            state.LastApplyState
                == Solarpunk::InventoryEditor::ApplyState::Pending
                || state.EmptyCount <= 0,
            "Add",
            nullptr,
            static_cast<ImTextureID>(
                ItemIconCache::GetTextureId(
                    selectedCatalogItem->ItemClass)))) {
            Solarpunk::InventoryEditor::QueueAddItem(
                Render::GetGameWindow(),
                selectedCatalogItem->ItemClass,
                addQuantity);
        }
    }
    else {
        MenuUI::WrappedText(
            "Select an item class to choose its quantity and add it through the game's player-inventory function.",
            theme.Colors.TextMuted,
            fonts.Small);
    }
    MenuUI::EndCard();

    std::vector<const Solarpunk::InventoryEditor::ItemSlot*> visibleItems;
    visibleItems.reserve(state.Items.size());
    const std::string query = LowerCopy(search);
    for (const auto& item : state.Items) {
        if (!query.empty()) {
            const std::string displayName = LowerCopy(item.DisplayName);
            const std::string className = LowerCopy(item.ClassName);
            if (displayName.find(query) == std::string::npos
                && className.find(query) == std::string::npos) {
                continue;
            }
        }
        visibleItems.push_back(&item);
    }

    const size_t durableItemCount = static_cast<size_t>(std::count_if(
        visibleItems.begin(),
        visibleItems.end(),
        [](const Solarpunk::InventoryEditor::ItemSlot* item) {
            return item->HasDurability;
        }));
    const size_t waterItemCount = static_cast<size_t>(std::count_if(
        visibleItems.begin(),
        visibleItems.end(),
        [](const Solarpunk::InventoryEditor::ItemSlot* item) {
            return item->HasWaterLevel;
        }));
    const float listHeight = (std::max)(
        232.0f,
        208.0f
            + static_cast<float>(visibleItems.size()) * 84.0f
            + static_cast<float>(durableItemCount) * 144.0f
            + static_cast<float>(waterItemCount) * 144.0f);
    MenuUI::BeginCard(
        "##inventory_items_card",
        ImVec2(0.0f, listHeight));

    char listDescription[128]{};
    std::snprintf(
        listDescription,
        sizeof(listDescription),
        "%zu occupied stack%s shown; empty slots are intentionally excluded",
        visibleItems.size(),
        visibleItems.size() == 1 ? "" : "s");
    MenuUI::CardHeading("Items", listDescription);
    MenuUI::SearchBox(
        "##inventory_search",
        search,
        sizeof(search),
        "Search item or raw class name...");
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));

    if (!state.IsReady()) {
        ImGui::Dummy(ImVec2(0.0f, theme.Spacing.LG));
        ImGui::PushFont(fonts.Heading);
        ImGui::TextColored(
            theme.Colors.TextPrimary,
            "Inventory is not available yet");
        ImGui::PopFont();
        ImGui::PushFont(fonts.Small);
        ImGui::TextColored(
            theme.Colors.TextSecondary,
            "Enter a playable world with a resolved local character.");
        ImGui::PopFont();
    }
    else if (visibleItems.empty()) {
        ImGui::Dummy(ImVec2(0.0f, theme.Spacing.LG));
        ImGui::PushFont(fonts.Heading);
        ImGui::TextColored(
            theme.Colors.TextPrimary,
            query.empty() ? "No occupied slots" : "No matching items");
        ImGui::PopFont();
        ImGui::PushFont(fonts.Small);
        ImGui::TextColored(
            theme.Colors.TextSecondary,
            query.empty()
                ? "Collect an item and it will appear here automatically."
                : "Try a broader display name or Blueprint class name.");
        ImGui::PopFont();
    }
    else {
        const bool operationPending =
            state.LastApplyState
            == Solarpunk::InventoryEditor::ApplyState::Pending;
        for (const auto* item : visibleItems) {
            if (item->Index < 0
                || item->Index
                    >= Solarpunk::InventoryEditor::MaxInventorySlots) {
                continue;
            }

            ItemDraft& draft =
                drafts[static_cast<size_t>(item->Index)];
            if (draft.ItemClass != item->ItemClass) {
                draft.Quantity = item->Quantity;
                draft.ObservedQuantity = item->Quantity;
                draft.Durability = item->Durability;
                draft.ObservedDurability =
                    item->HasDurability ? item->Durability : -1;
                draft.WaterLevel = item->WaterLevel;
                draft.ObservedWaterLevel =
                    item->HasWaterLevel ? item->WaterLevel : -1.0;
                draft.ItemClass = item->ItemClass;
            }
            else {
                if (item->QuantityLocked) {
                    draft.Quantity = item->LockedQuantity;
                    draft.ObservedQuantity = item->Quantity;
                }
                else if (draft.ObservedQuantity != item->Quantity) {
                    draft.Quantity = item->Quantity;
                    draft.ObservedQuantity = item->Quantity;
                }
                const int observedDurability =
                    item->HasDurability ? item->Durability : -1;
                if (item->DurabilityLocked) {
                    draft.Durability = item->LockedDurability;
                    draft.ObservedDurability = observedDurability;
                }
                else if (draft.ObservedDurability != observedDurability) {
                    draft.Durability = item->Durability;
                    draft.ObservedDurability = observedDurability;
                }
                const double observedWaterLevel =
                    item->HasWaterLevel ? item->WaterLevel : -1.0;
                if (item->WaterLevelLocked) {
                    draft.WaterLevel = item->LockedWaterLevel;
                    draft.ObservedWaterLevel = observedWaterLevel;
                }
                else if (std::fabs(
                    draft.ObservedWaterLevel
                        - observedWaterLevel) > 0.0005) {
                    draft.WaterLevel = item->WaterLevel;
                    draft.ObservedWaterLevel = observedWaterLevel;
                }
            }

            char rowId[48]{};
            char slotLabel[48]{};
            std::snprintf(
                rowId,
                sizeof(rowId),
                "##inventory_item_%d_%llX",
                item->Index,
                static_cast<unsigned long long>(item->ItemClass));
            const char* capability = item->HasWaterLevel
                ? (item->HasDurability
                    ? "   /   TOOL + WATER"
                    : "   /   WATER CAN")
                : (item->HasDurability
                    ? "   /   TOOL"
                    : "");
            std::snprintf(
                slotLabel,
                sizeof(slotLabel),
                "%s %d%s",
                item->IsHotbar() ? "HOTBAR" : "BACKPACK",
                item->DisplaySlot(),
                capability);

            bool quantityLocked = item->QuantityLocked;
            const bool quantityWasLocked = quantityLocked;
            const bool applyQuantity = MenuUI::QuantityEditorRow(
                rowId,
                item->DisplayName.c_str(),
                item->ClassName.c_str(),
                slotLabel,
                item->Quantity,
                &draft.Quantity,
                Solarpunk::InventoryEditor::MinimumQuantity,
                Solarpunk::InventoryEditor::MaximumQuantity,
                operationPending,
                "Apply",
                &quantityLocked,
                static_cast<ImTextureID>(
                    ItemIconCache::GetTextureId(
                        item->ItemClass)));
            if (quantityLocked != quantityWasLocked) {
                const bool changed =
                    Solarpunk::InventoryEditor::SetQuantityLock(
                    item->Index,
                    quantityLocked,
                    draft.Quantity);
                NotifyInventoryLockChange(
                    *item,
                    "Quantity",
                    quantityLocked,
                    changed,
                    quantityLocked
                        ? std::to_string(draft.Quantity)
                        : std::string{});
            }
            else if (applyQuantity) {
                Solarpunk::InventoryEditor::QueueQuantityChange(
                    Render::GetGameWindow(),
                    item->Index,
                    draft.Quantity);
            }

            if (item->HasDurability) {
                char durabilityId[56]{};
                std::snprintf(
                    durabilityId,
                    sizeof(durabilityId),
                    "##inventory_durability_%d_%llX",
                    item->Index,
                    static_cast<unsigned long long>(item->ItemClass));
                bool durabilityLocked = item->DurabilityLocked;
                const bool durabilityWasLocked = durabilityLocked;
                const bool applyDurability =
                    MenuUI::IntegerPropertyEditorRow(
                        durabilityId,
                        "Tool durability",
                        "Saved durability value; all other item data is preserved.",
                        item->Durability,
                        &draft.Durability,
                        Solarpunk::InventoryEditor::MinimumDurability,
                        Solarpunk::InventoryEditor::MaximumDurability,
                        operationPending,
                        &durabilityLocked);
                if (durabilityLocked != durabilityWasLocked) {
                    const bool changed =
                        Solarpunk::InventoryEditor::SetDurabilityLock(
                        item->Index,
                        durabilityLocked,
                        draft.Durability);
                    NotifyInventoryLockChange(
                        *item,
                        "Durability",
                        durabilityLocked,
                        changed,
                        durabilityLocked
                            ? std::to_string(
                                draft.Durability)
                            : std::string{});
                }
                else if (applyDurability) {
                    Solarpunk::InventoryEditor::QueueDurabilityChange(
                        Render::GetGameWindow(),
                        item->Index,
                        draft.Durability);
                }
            }
            if (item->HasWaterLevel) {
                char waterLevelId[56]{};
                std::snprintf(
                    waterLevelId,
                    sizeof(waterLevelId),
                    "##inventory_water_%d_%llX",
                    item->Index,
                    static_cast<unsigned long long>(item->ItemClass));
                bool waterLevelLocked = item->WaterLevelLocked;
                const bool waterLevelWasLocked = waterLevelLocked;
                const bool applyWaterLevel =
                    MenuUI::DoublePropertyEditorRow(
                        waterLevelId,
                        "Water level",
                        "Detected from the saved water-level attribute; all other item data is preserved.",
                        item->WaterLevel,
                        &draft.WaterLevel,
                        Solarpunk::InventoryEditor::MinimumWaterLevel,
                        Solarpunk::InventoryEditor::MaximumWaterLevel,
                        operationPending,
                        &waterLevelLocked);
                if (waterLevelLocked != waterLevelWasLocked) {
                    const bool changed =
                        Solarpunk::InventoryEditor::SetWaterLevelLock(
                        item->Index,
                        waterLevelLocked,
                        draft.WaterLevel);
                    char lockedWater[32]{};
                    std::snprintf(
                        lockedWater,
                        sizeof(lockedWater),
                        "%.1f",
                        draft.WaterLevel);
                    NotifyInventoryLockChange(
                        *item,
                        "Water level",
                        waterLevelLocked,
                        changed,
                        waterLevelLocked
                            ? std::string(lockedWater)
                            : std::string{});
                }
                else if (applyWaterLevel) {
                    Solarpunk::InventoryEditor::QueueWaterLevelChange(
                        Render::GetGameWindow(),
                        item->Index,
                        draft.WaterLevel);
                }
            }
            ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
        }
    }

    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    MenuUI::WrappedText(
        "Locks are session-only and automatically release when an item leaves or changes in its slot. All writes use the game's save-aware inventory function on the UE game thread.",
        theme.Colors.TextMuted,
        fonts.Small);
    MenuUI::EndCard();
}

void Menu::DrawMovementPage() {
    const auto& theme = MenuUI::GetTheme();
    const auto& frame = WorldRenderer::GetFrameState();
    auto& settings = Solarpunk::FlyHack::GetSettings();
    const auto& state = Solarpunk::FlyHack::GetState();

    MenuUI::PageHeader(
        "Movement",
        "Focus-gated camera-relative flight with collision-aware and noclip modes.");

    const float available = ImGui::GetContentRegionAvail().x;
    const CardGrid grid = CalculateCardGrid(
        available,
        OverviewCardMinWidth,
        2);

    MenuUI::BeginCard("##fly_setup_card", ImVec2(grid.Width, 340.0f));
    MenuUI::CardHeading(
        "Fly controls",
        "Movement input is accepted only while the game has focus");
    MenuUI::ToggleRow(
        "##fly_enabled",
        "Enable flight",
        "Hover in place and use the configured camera-relative movement mode.",
        &settings.Enabled);

    int selectedMode = static_cast<int>(settings.FlightMode);
    if (MenuUI::SegmentedRow(
        "##fly_mode",
        "Flight mode",
        "Velocity keeps collision; noclip disables the character capsule.",
        &selectedMode,
        "Velocity",
        "Noclip")) {
        settings.FlightMode = selectedMode == 1
            ? Solarpunk::FlyHack::Mode::Noclip
            : Solarpunk::FlyHack::Mode::Velocity;
    }

    const char* statusLabel = "FLIGHT DISABLED";
    MenuUI::StatusKind statusKind = MenuUI::StatusKind::Neutral;
    switch (state.CurrentStatus) {
    case Solarpunk::FlyHack::Status::Active:
        if (state.InputAllowed) {
            statusLabel = settings.FlightMode == Solarpunk::FlyHack::Mode::Noclip
                ? "NOCLIP ACTIVE"
                : "VELOCITY ACTIVE";
            statusKind = MenuUI::StatusKind::Success;
        }
        else {
            statusLabel = "INPUT PAUSED";
            statusKind = MenuUI::StatusKind::Warning;
        }
        break;
    case Solarpunk::FlyHack::Status::WaitingForPlayer:
        statusLabel = "WAITING FOR PLAYER";
        statusKind = MenuUI::StatusKind::Warning;
        break;
    case Solarpunk::FlyHack::Status::RuntimeUnavailable:
    case Solarpunk::FlyHack::Status::UpdateFailed:
        statusLabel = "MOVEMENT UNAVAILABLE";
        statusKind = MenuUI::StatusKind::Error;
        break;
    default:
        break;
    }
    MenuUI::StatusBadge(statusLabel, statusKind);
    MenuUI::EndCard();

    if (grid.Columns > 1)
        ImGui::SameLine(0.0f, theme.Spacing.LG);

    MenuUI::BeginCard("##fly_speed_card", ImVec2(grid.Width, 340.0f));
    MenuUI::CardHeading(
        "Flight speed",
        "Independent speeds in Unreal units per second");
    MenuUI::SliderRow(
        "##velocity_fly_speed",
        "Velocity speed",
        "Collision-aware flying speed.",
        &settings.VelocitySpeed,
        100.0f,
        5000.0f,
        "%.0f cm/s");
    MenuUI::SliderRow(
        "##noclip_fly_speed",
        "Noclip speed",
        "Collision-free flying speed.",
        &settings.NoclipSpeed,
        100.0f,
        8000.0f,
        "%.0f cm/s");
    MenuUI::EndCard();

    MenuUI::BeginCard("##fly_keyboard_card", ImVec2(0.0f, 184.0f));
    MenuUI::CardHeading(
        "Keyboard",
        frame.Player.HasCoordinates()
        ? "Controls pause when the menu opens or the game loses focus"
        : Solarpunk::GetStatusText(frame.Player.Status));
    MenuUI::KeyHintRow("W / S", "Move forward or backward along the camera");
    MenuUI::KeyHintRow("A / D", "Strafe left or right");
    MenuUI::KeyHintRow("SPACE / CTRL", "Move up or down");
    MenuUI::EndCard();
}

void Menu::DrawWaypointsPage() {
    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    const auto& frame = WorldRenderer::GetFrameState();
    const auto& waypoints =
        Solarpunk::WaypointSystem::GetWaypoints();
    const auto teleport =
        Solarpunk::WaypointSystem::GetTeleportStatus();
    static char nameDraft[
        Solarpunk::WaypointSystem::MaximumNameLength + 1]{};

    MenuUI::PageHeader(
        "Waypoints",
        "Save named locations, choose their world marker style, and teleport through the UE game thread.");

    const float available = ImGui::GetContentRegionAvail().x;
    const CardGrid grid = CalculateCardGrid(
        available,
        OverviewCardMinWidth,
        2);

    MenuUI::BeginCard(
        "##waypoint_create_card",
        ImVec2(grid.Width, 340.0f));
    MenuUI::CardHeading(
        "Add waypoint",
        "Capture the local player's current world position");
    MenuUI::StatusBadge(
        frame.Player.HasCoordinates()
            ? "POSITION READY"
            : "WAITING FOR PLAYER",
        frame.Player.HasCoordinates()
            ? MenuUI::StatusKind::Success
            : MenuUI::StatusKind::Warning);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
    const bool submitted = MenuUI::TextInput(
        "##waypoint_name",
        "WAYPOINT NAME",
        nameDraft,
        sizeof(nameDraft),
        "Home base, mine, crash site...");
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    const bool canAdd =
        frame.Player.HasCoordinates()
        && waypoints.size()
            < Solarpunk::WaypointSystem::MaximumWaypoints;
    const bool addClicked = MenuUI::ActionButton(
        "##add_waypoint_at_player",
        "Add at current position",
        ImVec2(
            ImGui::GetContentRegionAvail().x,
            theme.Sizing.ButtonHeight),
        MenuUI::ButtonKind::Primary,
        canAdd);
    if ((submitted || addClicked)
        && canAdd
        && Solarpunk::WaypointSystem::AddAtPlayer(
            nameDraft,
            frame.Player)) {
        nameDraft[0] = '\0';
    }
    MenuUI::EndCard();

    if (grid.Columns > 1)
        ImGui::SameLine(0.0f, theme.Spacing.LG);

    MenuUI::BeginCard(
        "##waypoint_style_card",
        ImVec2(grid.Width, 340.0f));
    MenuUI::CardHeading(
        "World marker",
        "Switch between a restrained dot and a Minecraft-style beacon");
    int markerStyle =
        Solarpunk::WaypointSystem::GetMarkerStyle()
            == Solarpunk::WaypointSystem::MarkerStyle::Beacon
        ? 1
        : 0;
    if (MenuUI::SegmentedRow(
        "##waypoint_marker_style",
        "Marker style",
        "Applied to every waypoint currently marked for drawing.",
        &markerStyle,
        "Dot",
        "Beacon")) {
        Solarpunk::WaypointSystem::SetMarkerStyle(
            markerStyle == 1
            ? Solarpunk::WaypointSystem::MarkerStyle::Beacon
            : Solarpunk::WaypointSystem::MarkerStyle::Dot);
    }

    const size_t visibleCount = static_cast<size_t>(
        std::count_if(
            waypoints.begin(),
            waypoints.end(),
            [](const auto& waypoint) {
                return waypoint.Draw;
            }));
    char markerSummary[96]{};
    std::snprintf(
        markerSummary,
        sizeof(markerSummary),
        "%zu visible  /  %zu saved",
        visibleCount,
        waypoints.size());
    MenuUI::StatusBadge(
        markerSummary,
        waypoints.empty()
            ? MenuUI::StatusKind::Neutral
            : MenuUI::StatusKind::Success);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    MenuUI::WrappedText(
        "Waypoint names, coordinates, visibility, and marker style save automatically.",
        theme.Colors.TextMuted,
        fonts.Small);
    MenuUI::EndCard();

    MenuUI::BeginCard(
        "##waypoint_list_card",
        ImVec2(0.0f, 510.0f));
    MenuUI::CardHeading(
        "Saved waypoints",
        "Manage drawing, teleporting, and deletion for each location");

    MenuUI::StatusKind teleportKind =
        MenuUI::StatusKind::Neutral;
    if (teleport.State
        == Solarpunk::WaypointSystem::TeleportState::Pending) {
        teleportKind = MenuUI::StatusKind::Warning;
    }
    else if (teleport.State
        == Solarpunk::WaypointSystem::TeleportState::Succeeded) {
        teleportKind = MenuUI::StatusKind::Success;
    }
    else if (teleport.State
        == Solarpunk::WaypointSystem::TeleportState::Failed) {
        teleportKind = MenuUI::StatusKind::Error;
    }
    MenuUI::StatusBadge(
        Solarpunk::WaypointSystem::GetTeleportStateText(
            teleport.State),
        teleportKind);
    if (!teleport.Message.empty()) {
        ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));
        MenuUI::WrappedText(
            teleport.Message.c_str(),
            theme.Colors.TextSecondary,
            fonts.Small);
    }
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(0.0f, theme.Spacing.SM));
    ImGui::BeginChild(
        "##waypoint_scroll_area",
        ImVec2(0.0f, 354.0f),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);

    uint64_t deleteWaypointId = 0;
    if (waypoints.empty()) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 region = ImGui::GetContentRegionAvail();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 center(
            start.x + region.x * 0.5f,
            start.y + region.y * 0.43f);
        draw->AddCircle(
            center,
            20.0f,
            MenuUI::ColorU32(
                MenuUI::WithAlpha(
                    theme.Colors.Accent,
                    0.52f)),
            32,
            1.5f);
        draw->AddCircleFilled(
            center,
            4.0f,
            MenuUI::ColorU32(theme.Colors.Accent));
        const char* emptyTitle = "No waypoints saved";
        const char* emptyBody =
            "Name your current position above to create the first one.";
        const ImVec2 titleSize =
            MeasureText(fonts.Body, emptyTitle);
        const ImVec2 bodySize =
            MeasureText(fonts.Small, emptyBody);
        draw->AddText(
            fonts.Body,
            fonts.Body->LegacySize,
            ImVec2(
                center.x - titleSize.x * 0.5f,
                center.y + 34.0f),
            MenuUI::ColorU32(theme.Colors.TextPrimary),
            emptyTitle);
        draw->AddText(
            fonts.Small,
            fonts.Small->LegacySize,
            ImVec2(
                center.x - bodySize.x * 0.5f,
                center.y + 57.0f),
            MenuUI::ColorU32(theme.Colors.TextSecondary),
            emptyBody);
        ImGui::Dummy(region);
    }
    else {
        for (const auto& waypoint : waypoints) {
            ImGui::PushID(
                reinterpret_cast<void*>(
                    static_cast<uintptr_t>(waypoint.Id)));
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(theme.Spacing.MD, theme.Spacing.MD));
            ImGui::PushStyleVar(
                ImGuiStyleVar_ChildRounding,
                theme.Radius.SM);
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg,
                MenuUI::WithAlpha(
                    theme.Colors.SurfaceActive,
                    0.34f));
            ImGui::BeginChild(
                "##waypoint_row",
                ImVec2(-1.0f, 112.0f),
                ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar
                    | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 rowMin = ImGui::GetWindowPos();
            const ImVec2 rowMax(
                rowMin.x + ImGui::GetWindowWidth(),
                rowMin.y + ImGui::GetWindowHeight());
            ImDrawList* rowDraw = ImGui::GetWindowDrawList();
            rowDraw->AddRect(
                rowMin,
                rowMax,
                MenuUI::ColorU32(theme.Colors.BorderSoft),
                theme.Radius.SM);

            rowDraw->AddCircleFilled(
                ImVec2(
                    rowMin.x + theme.Spacing.MD + 4.0f,
                    rowMin.y + 18.0f),
                3.5f,
                MenuUI::ColorU32(
                    waypoint.Draw
                    ? theme.Colors.Accent
                    : theme.Colors.TextMuted));
            rowDraw->AddText(
                fonts.Body,
                fonts.Body->LegacySize,
                ImVec2(
                    rowMin.x + theme.Spacing.MD + 15.0f,
                    rowMin.y + 10.0f),
                MenuUI::ColorU32(theme.Colors.TextPrimary),
                waypoint.Name.c_str());

            const double dx =
                waypoint.Location.X - frame.Player.Coordinates.X;
            const double dy =
                waypoint.Location.Y - frame.Player.Coordinates.Y;
            const double dz =
                waypoint.Location.Z - frame.Player.Coordinates.Z;
            const double distanceMeters =
                frame.Player.HasCoordinates()
                ? std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0
                : 0.0;
            char metadata[192]{};
            if (frame.Player.HasCoordinates()) {
                std::snprintf(
                    metadata,
                    sizeof(metadata),
                    "%.0f m away  /  X %.0f  Y %.0f  Z %.0f",
                    distanceMeters,
                    waypoint.Location.X,
                    waypoint.Location.Y,
                    waypoint.Location.Z);
            }
            else {
                std::snprintf(
                    metadata,
                    sizeof(metadata),
                    "X %.0f  /  Y %.0f  /  Z %.0f",
                    waypoint.Location.X,
                    waypoint.Location.Y,
                    waypoint.Location.Z);
            }
            rowDraw->AddText(
                fonts.Small,
                fonts.Small->LegacySize,
                ImVec2(
                    rowMin.x + theme.Spacing.MD,
                    rowMin.y + 37.0f),
                MenuUI::ColorU32(theme.Colors.TextSecondary),
                metadata);

            ImGui::SetCursorScreenPos(ImVec2(
                rowMin.x + theme.Spacing.MD,
                rowMin.y + 66.0f));
            if (MenuUI::ActionButton(
                "##toggle_waypoint_draw",
                waypoint.Draw ? "Drawing on" : "Drawing off",
                ImVec2(116.0f, 32.0f),
                waypoint.Draw
                    ? MenuUI::ButtonKind::Primary
                    : MenuUI::ButtonKind::Secondary)) {
                Solarpunk::WaypointSystem::SetDraw(
                    waypoint.Id,
                    !waypoint.Draw);
            }

            ImGui::SameLine(0.0f, theme.Spacing.SM);
            if (MenuUI::ActionButton(
                "##teleport_waypoint",
                "Teleport",
                ImVec2(108.0f, 32.0f),
                MenuUI::ButtonKind::Secondary,
                frame.Player.HasCoordinates()
                    && !teleport.IsPending())) {
                Solarpunk::WaypointSystem::QueueTeleport(
                    Render::GetGameWindow(),
                    waypoint.Id);
            }

            ImGui::SameLine(0.0f, theme.Spacing.SM);
            if (MenuUI::ActionButton(
                "##delete_waypoint",
                "Delete",
                ImVec2(82.0f, 32.0f),
                MenuUI::ButtonKind::Danger,
                !teleport.IsPending()
                    || teleport.WaypointId != waypoint.Id)) {
                deleteWaypointId = waypoint.Id;
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::PopID();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    if (deleteWaypointId)
        Solarpunk::WaypointSystem::Delete(deleteWaypointId);
    MenuUI::EndCard();
}

void Menu::DrawRuntimeDiagnostics() {
    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    const auto& frame = WorldRenderer::GetFrameState();

    const float available = ImGui::GetContentRegionAvail().x;
    const CardGrid grid = CalculateCardGrid(
        available,
        RuntimeCardMinWidth,
        2);
    char cameraLocation[128]{};
    char cameraRotation[128]{};
    FormatVector(
        cameraLocation,
        sizeof(cameraLocation),
        frame.Camera.Location);
    FormatRotation(
        cameraRotation,
        sizeof(cameraRotation),
        frame.Camera.Rotation);

    MenuUI::BeginCard("##camera_data_card", ImVec2(grid.Width, 232.0f));
    MenuUI::CardHeading(
        "Camera cache",
        Solarpunk::GetCameraStatusText(frame.Camera.Status));
    MenuUI::PointerRow("PlayerCameraManager", frame.Camera.PlayerCameraManager);
    MenuUI::PointerRow("FMinimalViewInfo", frame.Camera.PointOfView);
    ImGui::PushFont(fonts.Small);
    ImGui::TextColored(theme.Colors.TextSecondary, "LOCATION");
    ImGui::TextColored(theme.Colors.TextPrimary, "%s", cameraLocation);
    ImGui::TextColored(theme.Colors.TextSecondary, "ROTATION");
    ImGui::TextColored(theme.Colors.TextPrimary, "%s", cameraRotation);
    ImGui::TextColored(
        theme.Colors.TextSecondary,
        "FOV  %.2f       ASPECT  %.3f",
        frame.Camera.FieldOfView,
        frame.Camera.AspectRatio);
    ImGui::PopFont();
    MenuUI::EndCard();

    if (grid.Columns > 1)
        ImGui::SameLine(0.0f, theme.Spacing.LG);

    MenuUI::BeginCard("##hotkeys_card", ImVec2(grid.Width, 232.0f));
    MenuUI::CardHeading("Keyboard", "Global controls while the DLL is active");
    MenuUI::KeyHintRow("INSERT", "Open or close the trainer");
    MenuUI::KeyHintRow("F2", "Toggle the coordinate HUD");
    MenuUI::KeyHintRow("END", "Unload the trainer DLL");
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    MenuUI::StatusBadge(
        frame.Projection.IsValid ? "MATRICES CURRENT" : "MATRICES WAITING",
        frame.Projection.IsValid
        ? MenuUI::StatusKind::Success
        : MenuUI::StatusKind::Warning);
    MenuUI::EndCard();

    MenuUI::BeginCard("##matrix_card", ImVec2(0.0f, 364.0f));
    MenuUI::CardHeading(
        "View-projection matrices",
        "Row-major matrices; ViewProjection = Projection x View");
    DrawMatrix("VIEW", frame.Projection.View);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    DrawMatrix("PROJECTION", frame.Projection.Projection);
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.SM));
    DrawMatrix("VIEW PROJECTION", frame.Projection.ViewProjection);
    MenuUI::EndCard();
}

void Menu::DrawCoordinateOverlay() {
    if (!bCoordinatesVisible)
        return;

    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    const auto& player = WorldRenderer::GetFrameState().Player;
    const float alpha = std::clamp(hudOpacity / 100.0f, 0.55f, 1.0f);
    const float width = 286.0f;
    const float height = player.HasCoordinates() ? 142.0f : 96.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + 18.0f, viewport->WorkPos.y + 18.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##solarpunk_coordinate_hud", nullptr, flags);

    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 panelMax(min.x + width, min.y + height);
    const ImVec2 content(min.x + 16.0f, min.y + 15.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        min,
        panelMax,
        MenuUI::ColorU32(MenuUI::WithAlpha(
            theme.Colors.Window,
            theme.Colors.Window.w * alpha)),
        theme.Radius.MD);
    draw->AddRect(
        min,
        panelMax,
        MenuUI::ColorU32(MenuUI::WithAlpha(theme.Colors.Border, 0.72f)),
        theme.Radius.MD);
    draw->AddRectFilled(
        min,
        ImVec2(min.x + 3.0f, panelMax.y),
        MenuUI::ColorU32(theme.Colors.Accent),
        theme.Radius.MD,
        ImDrawFlags_RoundCornersLeft);

    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        content,
        MenuUI::ColorU32(theme.Colors.TextPrimary),
        "SOLARPUNK");

    const bool ready = player.HasCoordinates();
    const ImVec4& statusColor = ready
        ? theme.Colors.Success
        : theme.Colors.Warning;
    const char* status = ready ? "LIVE" : "WAITING";
    const ImVec2 statusSize = MeasureText(fonts.Small, status);
    const ImVec2 statusPosition(
        panelMax.x - 16.0f - statusSize.x,
        content.y + 1.0f);
    draw->AddCircleFilled(
        ImVec2(statusPosition.x - 9.0f, statusPosition.y + 6.0f),
        3.0f,
        MenuUI::ColorU32(statusColor));
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        statusPosition,
        MenuUI::ColorU32(statusColor),
        status);

    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(content.x, content.y + 24.0f),
        MenuUI::ColorU32(theme.Colors.TextMuted),
        ready ? "PLAYER POSITION" : "PLAYER POSITION UNAVAILABLE");

    if (ready) {
        char values[3][48]{};
        FormatCoordinate(values[0], sizeof(values[0]), player.Coordinates.X);
        FormatCoordinate(values[1], sizeof(values[1]), player.Coordinates.Y);
        FormatCoordinate(values[2], sizeof(values[2]), player.Coordinates.Z);
        constexpr const char* labels[] = { "X", "Y", "Z" };

        for (int index = 0; index < 3; ++index) {
            const float y = content.y + 55.0f + static_cast<float>(index) * 22.0f;
            const ImVec2 valueSize = MeasureText(fonts.Body, values[index]);
            draw->AddText(
                fonts.Small,
                fonts.Small->LegacySize,
                ImVec2(content.x, y + 1.0f),
                MenuUI::ColorU32(theme.Colors.TextSecondary),
                labels[index]);
            draw->AddText(
                fonts.Body,
                fonts.Body->LegacySize,
                ImVec2(panelMax.x - 16.0f - valueSize.x, y),
                MenuUI::ColorU32(theme.Colors.TextPrimary),
                values[index]);
        }
    }
    else {
        draw->AddText(
            fonts.Small,
            fonts.Small->LegacySize,
            ImVec2(content.x, content.y + 57.0f),
            MenuUI::ColorU32(theme.Colors.TextSecondary),
            Solarpunk::GetStatusText(player.Status));
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void Menu::DrawResourceOverlay() {
    const auto& settings = Solarpunk::ResourceEsp::GetSettings();
    const auto& frame = WorldRenderer::GetFrameState();
    if (!settings.Enabled || !frame.Projection.IsValid)
        return;

    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    ImFont* font = fonts.Small ? fonts.Small : ImGui::GetFont();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    const auto resourceColor = [&theme](
        Solarpunk::ResourceEsp::Kind kind) -> ImVec4 {
        switch (kind) {
        case Solarpunk::ResourceEsp::Kind::Tree:
            return theme.Colors.ResourceTree;
        case Solarpunk::ResourceEsp::Kind::Ore:
            return theme.Colors.ResourceOre;
        case Solarpunk::ResourceEsp::Kind::Plant:
            return theme.Colors.ResourcePlant;
        case Solarpunk::ResourceEsp::Kind::Pickup:
            return theme.Colors.ResourcePickup;
        case Solarpunk::ResourceEsp::Kind::DroppedItem:
            return theme.Colors.ResourceDroppedItem;
        case Solarpunk::ResourceEsp::Kind::Loot:
            return theme.Colors.ResourceLoot;
        case Solarpunk::ResourceEsp::Kind::Animal:
            return theme.Colors.ResourceAnimal;
        case Solarpunk::ResourceEsp::Kind::Npc:
            return theme.Colors.ResourceNpc;
        default:
            return theme.Colors.TextSecondary;
        }
    };

    for (const auto& marker : Solarpunk::ResourceEsp::GetMarkers()) {
        if (!Solarpunk::ResourceEsp::IsKindVisible(marker.ResourceKind)
            || marker.DistanceMeters > settings.MaxDistanceMeters) {
            continue;
        }

        Solarpunk::ScreenPoint projected{};
        if (!Solarpunk::WorldToScreen(
            marker.Location,
            frame.Projection,
            projected)
            || !projected.IsOnScreen) {
            continue;
        }

        const ImVec2 point(projected.X, projected.Y);
        const ImVec4 color = resourceColor(marker.ResourceKind);
        const ImU32 colorU32 = MenuUI::ColorU32(color);

        draw->AddCircleFilled(
            point,
            6.5f,
            IM_COL32(4, 5, 7, 205));
        draw->AddCircle(
            point,
            6.0f,
            MenuUI::ColorU32(MenuUI::WithAlpha(color, 0.72f)),
            20,
            1.25f);
        draw->AddCircleFilled(point, 3.4f, colorU32);

        if (!settings.ShowLabels)
            continue;

        std::string label = marker.DisplayName.empty()
            ? marker.RawClassName
            : marker.DisplayName;
        if (settings.ShowDistance) {
            char distance[32]{};
            std::snprintf(
                distance,
                sizeof(distance),
                "  %.0f m",
                marker.DistanceMeters);
            label += distance;
        }

        const ImVec2 textSize = font->CalcTextSizeA(
            font->LegacySize,
            FLT_MAX,
            0.0f,
            label.c_str());
        constexpr float HorizontalGap = 10.0f;
        constexpr float PaddingX = 7.0f;
        constexpr float PaddingY = 4.0f;
        float labelX = point.x + HorizontalGap;
        if (labelX + textSize.x + PaddingX * 2.0f
            > frame.Projection.ViewportWidth - 6.0f) {
            labelX = point.x
                - HorizontalGap
                - textSize.x
                - PaddingX * 2.0f;
        }

        const float labelHeight = textSize.y + PaddingY * 2.0f;
        const float labelY = std::clamp(
            point.y - labelHeight * 0.5f,
            6.0f,
            (std::max)(
                6.0f,
                frame.Projection.ViewportHeight - labelHeight - 6.0f));
        const ImVec2 labelMin(labelX, labelY);
        const ImVec2 labelMax(
            labelX + textSize.x + PaddingX * 2.0f,
            labelY + labelHeight);

        draw->AddRectFilled(
            labelMin,
            labelMax,
            IM_COL32(8, 9, 12, 188),
            5.0f);
        draw->AddRect(
            labelMin,
            labelMax,
            MenuUI::ColorU32(MenuUI::WithAlpha(color, 0.45f)),
            5.0f);
        draw->AddText(
            font,
            font->LegacySize,
            ImVec2(labelMin.x + PaddingX, labelMin.y + PaddingY),
            MenuUI::ColorU32(theme.Colors.TextPrimary),
            label.c_str());
    }
}

void Menu::DrawWaypointOverlay() {
    const auto& frame = WorldRenderer::GetFrameState();
    const auto& waypoints =
        Solarpunk::WaypointSystem::GetWaypoints();
    if (!frame.Projection.IsValid || waypoints.empty())
        return;

    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    ImFont* font = fonts.Body ? fonts.Body : ImGui::GetFont();
    ImFont* smallFont = fonts.Small ? fonts.Small : font;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const auto markerStyle =
        Solarpunk::WaypointSystem::GetMarkerStyle();
    const ImVec4 markerColor = theme.Colors.AccentStrong;
    const ImU32 markerU32 = MenuUI::ColorU32(markerColor);

    for (const auto& waypoint : waypoints) {
        if (!waypoint.Draw)
            continue;

        Solarpunk::ScreenPoint base{};
        if (!Solarpunk::WorldToScreen(
            waypoint.Location,
            frame.Projection,
            base)) {
            continue;
        }

        if (markerStyle
            == Solarpunk::WaypointSystem::MarkerStyle::Beacon) {
            const Solarpunk::Vector3 topWorld{
                waypoint.Location.X,
                waypoint.Location.Y,
                waypoint.Location.Z + 10000.0
            };
            Solarpunk::ScreenPoint top{};
            if (Solarpunk::WorldToScreen(
                topWorld,
                frame.Projection,
                top)) {
                const ImVec2 lineStart(base.X, base.Y);
                const ImVec2 lineEnd(top.X, top.Y);
                draw->AddLine(
                    lineStart,
                    lineEnd,
                    MenuUI::ColorU32(
                        MenuUI::WithAlpha(markerColor, 0.10f)),
                    12.0f);
                draw->AddLine(
                    lineStart,
                    lineEnd,
                    MenuUI::ColorU32(
                        MenuUI::WithAlpha(markerColor, 0.24f)),
                    6.0f);
                draw->AddLine(
                    lineStart,
                    lineEnd,
                    MenuUI::ColorU32(
                        MenuUI::WithAlpha(markerColor, 0.78f)),
                    2.0f);
            }
        }

        if (!base.IsOnScreen)
            continue;

        const ImVec2 point(base.X, base.Y);
        if (markerStyle
            == Solarpunk::WaypointSystem::MarkerStyle::Beacon) {
            draw->AddCircleFilled(
                point,
                10.0f,
                IM_COL32(4, 5, 7, 190));
            draw->AddQuadFilled(
                ImVec2(point.x, point.y - 7.0f),
                ImVec2(point.x + 7.0f, point.y),
                ImVec2(point.x, point.y + 7.0f),
                ImVec2(point.x - 7.0f, point.y),
                markerU32);
            draw->AddQuad(
                ImVec2(point.x, point.y - 10.0f),
                ImVec2(point.x + 10.0f, point.y),
                ImVec2(point.x, point.y + 10.0f),
                ImVec2(point.x - 10.0f, point.y),
                MenuUI::ColorU32(
                    MenuUI::WithAlpha(markerColor, 0.54f)),
                1.25f);
        }
        else {
            draw->AddCircleFilled(
                point,
                8.5f,
                IM_COL32(4, 5, 7, 205));
            draw->AddCircle(
                point,
                8.0f,
                MenuUI::ColorU32(
                    MenuUI::WithAlpha(markerColor, 0.72f)),
                24,
                1.4f);
            draw->AddCircleFilled(
                point,
                4.2f,
                markerU32);
        }

        const double dx =
            waypoint.Location.X - frame.Player.Coordinates.X;
        const double dy =
            waypoint.Location.Y - frame.Player.Coordinates.Y;
        const double dz =
            waypoint.Location.Z - frame.Player.Coordinates.Z;
        const double distanceMeters =
            frame.Player.HasCoordinates()
            ? std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0
            : 0.0;

        char distance[48]{};
        if (frame.Player.HasCoordinates()) {
            std::snprintf(
                distance,
                sizeof(distance),
                "%.0f m",
                distanceMeters);
        }

        const ImVec2 nameSize = font->CalcTextSizeA(
            font->LegacySize,
            FLT_MAX,
            0.0f,
            waypoint.Name.c_str());
        const ImVec2 distanceSize =
            frame.Player.HasCoordinates()
            ? smallFont->CalcTextSizeA(
                smallFont->LegacySize,
                FLT_MAX,
                0.0f,
                distance)
            : ImVec2{};
        constexpr float PaddingX = 9.0f;
        constexpr float PaddingY = 6.0f;
        constexpr float MarkerGap = 13.0f;
        const float labelWidth =
            (std::max)(nameSize.x, distanceSize.x)
            + PaddingX * 2.0f;
        const float labelHeight =
            nameSize.y
            + (frame.Player.HasCoordinates()
                ? distanceSize.y + 2.0f
                : 0.0f)
            + PaddingY * 2.0f;
        float labelX = point.x + MarkerGap;
        if (labelX + labelWidth
            > frame.Projection.ViewportWidth - 6.0f) {
            labelX = point.x - MarkerGap - labelWidth;
        }
        const float labelY = std::clamp(
            point.y - labelHeight * 0.5f,
            6.0f,
            (std::max)(
                6.0f,
                frame.Projection.ViewportHeight
                    - labelHeight
                    - 6.0f));
        const ImVec2 labelMin(labelX, labelY);
        const ImVec2 labelMax(
            labelX + labelWidth,
            labelY + labelHeight);

        draw->AddRectFilled(
            labelMin,
            labelMax,
            IM_COL32(7, 8, 11, 205),
            6.0f);
        draw->AddRect(
            labelMin,
            labelMax,
            MenuUI::ColorU32(
                MenuUI::WithAlpha(markerColor, 0.52f)),
            6.0f);
        draw->AddText(
            font,
            font->LegacySize,
            ImVec2(
                labelMin.x + PaddingX,
                labelMin.y + PaddingY),
            MenuUI::ColorU32(theme.Colors.TextPrimary),
            waypoint.Name.c_str());
        if (frame.Player.HasCoordinates()) {
            draw->AddText(
                smallFont,
                smallFont->LegacySize,
                ImVec2(
                    labelMin.x + PaddingX,
                    labelMin.y
                        + PaddingY
                        + nameSize.y
                        + 2.0f),
                MenuUI::ColorU32(theme.Colors.TextSecondary),
                distance);
        }
    }
}

void Menu::DrawProjectionPreview() {
    if (!bProjectionPreviewVisible || !WorldRenderer::IsReady())
        return;

    const auto& theme = MenuUI::GetTheme();
    const auto& camera = WorldRenderer::GetFrameState().Camera;
    const Solarpunk::Vector3 forward = Solarpunk::GetForwardVector(camera.Rotation);
    const Solarpunk::Vector3 marker{
        camera.Location.X + forward.X * 500.0,
        camera.Location.Y + forward.Y * 500.0,
        camera.Location.Z + forward.Z * 500.0
    };
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    WorldRenderer::DrawAxes(marker, 55.0, draw);
    WorldRenderer::DrawMarker(
        marker,
        "VIEW PROJECTION READY",
        MenuUI::ColorU32(theme.Colors.AccentStrong),
        draw);
}
