#include "MenuTheme.h"

#include "../../../assets/resource.h"

#include <algorithm>
#include <cmath>

namespace {

    MenuUI::AppFonts gFonts{};
    bool gInitialized = false;

    ImFont* AddEmbeddedFont(
        const void* data,
        int dataSize,
        float pixelSize,
        const char* debugName) {
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        config.OversampleH = 3;
        config.OversampleV = 2;
        config.PixelSnapH = false;
        sprintf_s(config.Name, "%s", debugName);

        return io.Fonts->AddFontFromMemoryTTF(
            const_cast<void*>(data),
            dataSize,
            pixelSize,
            &config,
            io.Fonts->GetGlyphRangesDefault());
    }

    void ApplyStyle() {
        const MenuUI::AppTheme& theme = MenuUI::GetTheme();
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowPadding = ImVec2(0.0f, 0.0f);
        style.FramePadding = ImVec2(theme.Spacing.MD, theme.Spacing.SM);
        style.ItemSpacing = ImVec2(theme.Spacing.MD, theme.Spacing.MD);
        style.ItemInnerSpacing = ImVec2(theme.Spacing.SM, theme.Spacing.SM);
        style.ScrollbarSize = 8.0f;
        style.GrabMinSize = 18.0f;

        style.WindowRounding = theme.Radius.LG;
        style.ChildRounding = theme.Radius.MD;
        style.PopupRounding = theme.Radius.MD;
        style.FrameRounding = theme.Radius.SM;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 8.0f;

        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;

        style.AntiAliasedLines = true;
        style.AntiAliasedLinesUseTex = true;
        style.AntiAliasedFill = true;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = theme.Colors.TextPrimary;
        colors[ImGuiCol_TextDisabled] = theme.Colors.TextMuted;
        colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_PopupBg] = theme.Colors.Surface;
        colors[ImGuiCol_Border] = theme.Colors.BorderSoft;
        colors[ImGuiCol_FrameBg] = theme.Colors.Surface;
        colors[ImGuiCol_FrameBgHovered] = theme.Colors.SurfaceHover;
        colors[ImGuiCol_FrameBgActive] = theme.Colors.SurfaceActive;
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ScrollbarGrab] = theme.Colors.Border;
        colors[ImGuiCol_ScrollbarGrabHovered] = theme.Colors.Accent;
        colors[ImGuiCol_ScrollbarGrabActive] = theme.Colors.AccentStrong;
        colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ResizeGripHovered] = theme.Colors.AccentMuted;
        colors[ImGuiCol_ResizeGripActive] = theme.Colors.Accent;
        colors[ImGuiCol_NavHighlight] = theme.Colors.AccentMuted;
    }

} // namespace

const MenuUI::AppTheme& MenuUI::GetTheme() {
    static const AppTheme theme{
        {
            ImVec4(0.022f, 0.023f, 0.027f, 0.985f),
            ImVec4(0.000f, 0.000f, 0.000f, 0.300f),
            ImVec4(0.014f, 0.015f, 0.018f, 0.995f),
            ImVec4(0.032f, 0.034f, 0.039f, 0.985f),
            ImVec4(0.064f, 0.067f, 0.075f, 0.965f),
            ImVec4(0.094f, 0.098f, 0.108f, 0.985f),
            ImVec4(0.126f, 0.131f, 0.143f, 1.00f),
            ImVec4(0.300f, 0.310f, 0.330f, 0.78f),
            ImVec4(0.255f, 0.265f, 0.285f, 0.36f),
            ImVec4(0.625f, 0.650f, 0.695f, 1.00f),
            ImVec4(0.875f, 0.890f, 0.920f, 1.00f),
            ImVec4(0.625f, 0.650f, 0.695f, 0.22f),
            ImVec4(0.955f, 0.960f, 0.970f, 1.00f),
            ImVec4(0.710f, 0.725f, 0.750f, 1.00f),
            ImVec4(0.535f, 0.550f, 0.580f, 1.00f),
            ImVec4(0.420f, 0.800f, 0.585f, 1.00f),
            ImVec4(0.925f, 0.680f, 0.310f, 1.00f),
            ImVec4(0.925f, 0.370f, 0.400f, 1.00f),
            ImVec4(0.330f, 0.820f, 0.475f, 1.00f),
            ImVec4(0.960f, 0.675f, 0.260f, 1.00f),
            ImVec4(0.900f, 0.370f, 0.675f, 1.00f),
            ImVec4(0.285f, 0.745f, 0.920f, 1.00f),
            ImVec4(0.250f, 0.865f, 0.790f, 1.00f),
            ImVec4(0.980f, 0.785f, 0.285f, 1.00f),
            ImVec4(0.390f, 0.650f, 0.975f, 1.00f),
            ImVec4(0.700f, 0.475f, 0.950f, 1.00f)
        },
        { 4.0f, 8.0f, 12.0f, 20.0f, 28.0f },
        { 6.0f, 10.0f, 14.0f },
        {
            64.0f,
            196.0f,
            34.0f,
            44.0f,
            58.0f,
            38.0f,
            20.0f,
            300.0f,
            380.0f
        },
        { 0.24f, 0.32f, 17.0f }
    };
    return theme;
}

const MenuUI::AppFonts& MenuUI::GetFonts() {
    return gFonts;
}

bool MenuUI::Initialize(HMODULE module) {
    if (gInitialized)
        return gFonts.Body != nullptr;

    gInitialized = true;
    ApplyStyle();

    HRSRC resource = FindResourceW(
        module,
        MAKEINTRESOURCEW(IDR_INTER_VARIABLE),
        RT_RCDATA);
    if (!resource)
        goto fallback;

    {
        HGLOBAL loaded = LoadResource(module, resource);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        const DWORD dataSize = SizeofResource(module, resource);

        if (!data || !dataSize)
            goto fallback;

        gFonts.Small = AddEmbeddedFont(data, static_cast<int>(dataSize), 13.5f, "Inter Small");
        gFonts.Body = AddEmbeddedFont(data, static_cast<int>(dataSize), 15.5f, "Inter Body");
        gFonts.Heading = AddEmbeddedFont(data, static_cast<int>(dataSize), 19.0f, "Inter Heading");
        gFonts.Title = AddEmbeddedFont(data, static_cast<int>(dataSize), 21.5f, "Inter Title");
    }

fallback:
    if (!gFonts.Body)
        gFonts.Body = ImGui::GetIO().Fonts->AddFontDefault();
    if (!gFonts.Small)
        gFonts.Small = gFonts.Body;
    if (!gFonts.Heading)
        gFonts.Heading = gFonts.Body;
    if (!gFonts.Title)
        gFonts.Title = gFonts.Heading;

    ImGui::GetIO().FontDefault = gFonts.Body;
    return gFonts.Body != nullptr;
}

ImU32 MenuUI::ColorU32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImVec4 MenuUI::WithAlpha(const ImVec4& color, float alpha) {
    return ImVec4(color.x, color.y, color.z, std::clamp(alpha, 0.0f, 1.0f));
}

float MenuUI::Animate(ImGuiID id, float target, float speed) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float current = storage->GetFloat(id, target);
    const float delta = 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime);
    current += (target - current) * delta;
    storage->SetFloat(id, current);
    return current;
}
