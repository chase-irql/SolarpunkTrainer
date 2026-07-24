#pragma once

#include <Windows.h>
#include <ImGui/imgui.h>

namespace TrainerSettings {
    struct PersistentSettings;
}

class Menu {
private:
    static bool bIsOpen;
    static bool bCoordinatesVisible;
    static bool bProjectionPreviewVisible;
    static int currentPage;
    static float menuOpacity;
    static float hudOpacity;

    static void DrawResourceOverlay();
    static void DrawWaypointOverlay();
    static void DrawCoordinateOverlay();
    static void DrawProjectionPreview();
    static void DrawApplicationShell();
    static void DrawTitleBar();
    static void DrawSidebar();
    static void DrawStatusBar();
    static void UpdateNotifications();

    static void DrawExploitsPage();
    static void DrawVisualsPage();
    static void DrawInventoryPage();
    static void DrawMovementPage();
    static void DrawWaypointsPage();
    static void DrawDiagnosticsPage();
    static void DrawRuntimeDiagnostics();

    static TrainerSettings::PersistentSettings CaptureSettings();
    static void ApplySettings(
        const TrainerSettings::PersistentSettings& settings);

public:
    static void Initialize(HMODULE module);
    static void Render();

    static void Toggle();
    static void ToggleCoordinateOverlay();
    static bool IsOpen();
    static bool IsCoordinateOverlayVisible();
};
