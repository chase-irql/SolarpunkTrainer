#pragma once

#include <Windows.h>
#include <ImGui/imgui.h>

namespace MenuUI {

    struct AppColors {
        ImVec4 Window;
        ImVec4 ScreenScrim;
        ImVec4 TitleBar;
        ImVec4 Sidebar;
        ImVec4 Surface;
        ImVec4 SurfaceHover;
        ImVec4 SurfaceActive;
        ImVec4 Border;
        ImVec4 BorderSoft;
        ImVec4 Accent;
        ImVec4 AccentStrong;
        ImVec4 AccentMuted;
        ImVec4 TextPrimary;
        ImVec4 TextSecondary;
        ImVec4 TextMuted;
        ImVec4 Success;
        ImVec4 Warning;
        ImVec4 Error;
        ImVec4 ResourceTree;
        ImVec4 ResourceOre;
        ImVec4 ResourcePlant;
        ImVec4 ResourcePickup;
        ImVec4 ResourceDroppedItem;
        ImVec4 ResourceLoot;
        ImVec4 ResourceAnimal;
        ImVec4 ResourceNpc;
    };

    struct AppSpacing {
        float XS;
        float SM;
        float MD;
        float LG;
        float XL;
    };

    struct AppRadius {
        float SM;
        float MD;
        float LG;
    };

    struct AppSizing {
        float TitleBarHeight;
        float SidebarWidth;
        float StatusBarHeight;
        float NavigationRowHeight;
        float ControlRowHeight;
        float ButtonHeight;
        float OverlayGutter;
        float NotificationMinWidth;
        float NotificationMaxWidth;
    };

    struct AppMotion {
        float NotificationEnterDuration;
        float NotificationExitDuration;
        float NotificationStackResponse;
    };

    struct AppTheme {
        AppColors Colors;
        AppSpacing Spacing;
        AppRadius Radius;
        AppSizing Sizing;
        AppMotion Motion;
    };

    struct AppFonts {
        ImFont* Small = nullptr;
        ImFont* Body = nullptr;
        ImFont* Heading = nullptr;
        ImFont* Title = nullptr;
    };

    bool Initialize(HMODULE module);
    const AppTheme& GetTheme();
    const AppFonts& GetFonts();

    ImU32 ColorU32(const ImVec4& color);
    ImVec4 WithAlpha(const ImVec4& color, float alpha);
    float Animate(ImGuiID id, float target, float speed = 14.0f);

} // namespace MenuUI
