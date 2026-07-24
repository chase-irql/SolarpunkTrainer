#include "MenuComponents.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

    ImVec2 MeasureText(ImFont* font, const char* text) {
        if (!font)
            return ImGui::CalcTextSize(text);

        return font->CalcTextSizeA(
            font->LegacySize,
            FLT_MAX,
            0.0f,
            text);
    }

    ImVec2 MeasureWrappedText(
        ImFont* font,
        const char* text,
        float wrapWidth) {
        if (!text || !text[0])
            return {};
        if (!font)
            font = ImGui::GetFont();

        return font->CalcTextSizeA(
            font->LegacySize,
            FLT_MAX,
            (std::max)(1.0f, wrapWidth),
            text);
    }

    std::string EllipsizeText(
        ImFont* font,
        const char* text,
        float maximumWidth) {
        if (!text || !text[0] || maximumWidth <= 1.0f)
            return {};
        if (MeasureText(font, text).x <= maximumWidth)
            return text;

        constexpr std::string_view ellipsis = "...";
        const float ellipsisWidth =
            MeasureText(font, ellipsis.data()).x;
        if (ellipsisWidth >= maximumWidth)
            return std::string(ellipsis);

        std::string result(text);
        while (!result.empty()) {
            size_t removeFrom = result.size() - 1;
            while (removeFrom > 0
                && (static_cast<unsigned char>(
                    result[removeFrom]) & 0xC0u) == 0x80u) {
                --removeFrom;
            }
            result.resize(removeFrom);

            std::string candidate = result;
            candidate += ellipsis;
            if (MeasureText(font, candidate.c_str()).x
                <= maximumWidth) {
                return candidate;
            }
        }
        return std::string(ellipsis);
    }

    void DrawWrappedText(
        ImDrawList* draw,
        ImFont* font,
        const ImVec2& position,
        ImU32 color,
        const char* text,
        float wrapWidth,
        const ImVec4* clip = nullptr) {
        if (!text || !text[0])
            return;
        draw->AddText(
            font,
            font->LegacySize,
            position,
            color,
            text,
            nullptr,
            (std::max)(1.0f, wrapWidth),
            clip);
    }

    const ImVec4& StatusColor(MenuUI::StatusKind kind) {
        const auto& colors = MenuUI::GetTheme().Colors;
        switch (kind) {
        case MenuUI::StatusKind::Success:
            return colors.Success;
        case MenuUI::StatusKind::Warning:
            return colors.Warning;
        case MenuUI::StatusKind::Error:
            return colors.Error;
        default:
            return colors.TextSecondary;
        }
    }

    bool StepperButton(
        const char* id,
        const char* label,
        const ImVec2& size,
        bool enabled) {
        const auto& theme = MenuUI::GetTheme();
        const auto& fonts = MenuUI::GetFonts();

        if (!enabled)
            ImGui::BeginDisabled();

        ImGui::InvisibleButton(id, size);
        const bool hovered = enabled && ImGui::IsItemHovered();
        const bool active = enabled && ImGui::IsItemActive();
        const bool clicked = enabled && ImGui::IsItemClicked();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImVec4 background = theme.Colors.SurfaceActive;
        background.w = enabled
            ? (active ? 0.92f : hovered ? 0.78f : 0.58f)
            : 0.24f;
        draw->AddRectFilled(
            min,
            max,
            MenuUI::ColorU32(background),
            theme.Radius.SM);
        draw->AddRect(
            min,
            max,
            MenuUI::ColorU32(MenuUI::WithAlpha(
                theme.Colors.Border,
                enabled ? 0.52f : 0.22f)),
            theme.Radius.SM);

        const ImVec2 textSize = MeasureText(fonts.Body, label);
        draw->AddText(
            fonts.Body,
            fonts.Body->LegacySize,
            ImVec2(
                min.x + (size.x - textSize.x) * 0.5f,
                min.y + (size.y - textSize.y) * 0.5f),
            MenuUI::ColorU32(
                enabled
                ? theme.Colors.TextPrimary
                : theme.Colors.TextMuted),
            label);

        if (!enabled)
            ImGui::EndDisabled();

        return clicked;
    }

} // namespace

void MenuUI::ScreenScrim() {
    const AppTheme& theme = GetTheme();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport)
        return;

    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##solarpunk_screen_scrim", nullptr, flags);
    ImGui::GetWindowDrawList()->AddRectFilled(
        viewport->Pos,
        ImVec2(
            viewport->Pos.x + viewport->Size.x,
            viewport->Pos.y + viewport->Size.y),
        ColorU32(theme.Colors.ScreenScrim));
    ImGui::End();
}

bool MenuUI::SidebarItem(
    const char* id,
    const char* index,
    const char* label,
    bool selected) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, theme.Sizing.NavigationRowHeight);

    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked();
    const bool focused = ImGui::IsItemFocused();
    const ImGuiID itemId = ImGui::GetItemID();

    const float hover = Animate(
        itemId ^ 0x1F31A2u,
        (hovered || selected) ? 1.0f : 0.0f);
    const float press = Animate(
        itemId ^ 0x43B77Cu,
        active ? 1.0f : 0.0f,
        22.0f);
    const float select = Animate(
        itemId ^ 0x719C02u,
        selected ? 1.0f : 0.0f,
        18.0f);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec4 background = theme.Colors.SurfaceHover;
    background.w = (0.08f + 0.54f * select + 0.24f * hover) * (1.0f - press * 0.15f);
    if (background.w > 0.01f)
        draw->AddRectFilled(min, max, ColorU32(background), theme.Radius.SM);
    if (focused) {
        draw->AddRect(
            min,
            max,
            ColorU32(WithAlpha(theme.Colors.Accent, 0.70f)),
            theme.Radius.SM,
            0,
            1.25f);
    }

    if (select > 0.01f) {
        draw->AddRectFilled(
            ImVec2(min.x, min.y + 10.0f),
            ImVec2(min.x + 2.0f + select, max.y - 10.0f),
            ColorU32(WithAlpha(theme.Colors.AccentStrong, select)),
            2.0f);
    }

    const ImVec4 indexColor = selected
        ? theme.Colors.AccentStrong
        : theme.Colors.TextMuted;
    const ImVec4 labelColor = selected
        ? theme.Colors.TextPrimary
        : theme.Colors.TextSecondary;

    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(min.x + theme.Spacing.MD, min.y + 14.0f),
        ColorU32(indexColor),
        index);
    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(min.x + 42.0f, min.y + 12.0f),
        ColorU32(labelColor),
        label);

    return clicked;
}

bool MenuUI::CloseButton(const char* id) {
    const AppTheme& theme = GetTheme();
    const ImVec2 size(36.0f, 32.0f);

    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked();
    const ImGuiID itemId = ImGui::GetItemID();
    const float hover = Animate(itemId ^ 0xA2081u, hovered ? 1.0f : 0.0f);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 center(
        (min.x + max.x) * 0.5f,
        (min.y + max.y) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec4 background = theme.Colors.Error;
    background.w = hover * (active ? 0.34f : 0.20f);
    if (background.w > 0.01f)
        draw->AddRectFilled(min, max, ColorU32(background), theme.Radius.SM);

    const ImU32 lineColor = ColorU32(
        hovered ? theme.Colors.TextPrimary : theme.Colors.TextMuted);
    draw->AddLine(
        ImVec2(center.x - 5.0f, center.y - 5.0f),
        ImVec2(center.x + 5.0f, center.y + 5.0f),
        lineColor,
        1.75f);
    draw->AddLine(
        ImVec2(center.x + 5.0f, center.y - 5.0f),
        ImVec2(center.x - 5.0f, center.y + 5.0f),
        lineColor,
        1.75f);

    return clicked;
}

bool MenuUI::ActionButton(
    const char* id,
    const char* label,
    const ImVec2& size,
    ButtonKind kind,
    bool enabled) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();

    if (!enabled)
        ImGui::BeginDisabled();

    ImGui::InvisibleButton(id, size);
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool active = enabled && ImGui::IsItemActive();
    const bool clicked = enabled && ImGui::IsItemClicked();
    const bool focused = enabled && ImGui::IsItemFocused();
    const float hover = Animate(
        ImGui::GetItemID() ^ 0xBC077u,
        hovered ? 1.0f : 0.0f);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec4 background = theme.Colors.Surface;
    ImVec4 text = theme.Colors.TextSecondary;
    if (kind == ButtonKind::Primary) {
        background = theme.Colors.Accent;
        background.w = 0.70f + hover * 0.24f;
        text = theme.Colors.Window;
    }
    else if (kind == ButtonKind::Secondary) {
        background = theme.Colors.SurfaceHover;
        background.w = 0.62f + hover * 0.28f;
        text = hovered ? theme.Colors.TextPrimary : theme.Colors.TextSecondary;
    }
    else if (kind == ButtonKind::Danger) {
        background = theme.Colors.Error;
        background.w = 0.10f + hover * 0.18f;
        text = theme.Colors.Error;
    }
    else {
        background = theme.Colors.SurfaceHover;
        background.w = hover * 0.60f;
        text = hovered ? theme.Colors.TextPrimary : theme.Colors.TextSecondary;
    }

    if (!enabled) {
        background = theme.Colors.SurfaceActive;
        background.w = 0.28f;
        text = theme.Colors.TextMuted;
    }

    if (active)
        background.w *= 0.78f;

    if (background.w > 0.01f)
        draw->AddRectFilled(min, max, ColorU32(background), theme.Radius.SM);

    if (kind != ButtonKind::Ghost) {
        draw->AddRect(
            min,
            max,
            ColorU32(
                kind == ButtonKind::Danger
                ? WithAlpha(theme.Colors.Error, 0.48f + hover * 0.30f)
                : theme.Colors.BorderSoft),
            theme.Radius.SM);
    }
    if (focused) {
        draw->AddRect(
            min,
            max,
            ColorU32(WithAlpha(theme.Colors.AccentStrong, 0.82f)),
            theme.Radius.SM,
            0,
            1.25f);
    }

    const float textWidth = (std::max)(
        1.0f,
        size.x - theme.Spacing.MD * 2.0f);
    const std::string visibleLabel =
        EllipsizeText(fonts.Body, label, textWidth);
    const ImVec2 textSize =
        MeasureText(fonts.Body, visibleLabel.c_str());
    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(
            min.x + (size.x - textSize.x) * 0.5f,
            min.y + (size.y - textSize.y) * 0.5f),
        ColorU32(text),
        visibleLabel.c_str());

    if (hovered && visibleLabel != label)
        ImGui::SetTooltip("%s", label);

    if (!enabled)
        ImGui::EndDisabled();

    return clicked;
}

bool MenuUI::SearchBox(
    const char* id,
    char* buffer,
    size_t bufferSize,
    const char* hint) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 start = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    ImGui::PushFont(fonts.Body);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(38.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.Radius.SM);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        WithAlpha(theme.Colors.SurfaceActive, 0.58f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        WithAlpha(theme.Colors.SurfaceHover, 0.92f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        theme.Colors.SurfaceActive);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.Colors.BorderSoft);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    const bool changed = ImGui::InputTextWithHint(
        "##search",
        hint,
        buffer,
        bufferSize);

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    ImGui::PopID();

    const ImVec2 iconCenter(start.x + 17.0f, start.y + 18.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircle(
        iconCenter,
        5.0f,
        ColorU32(theme.Colors.TextMuted),
        16,
        1.4f);
    draw->AddLine(
        ImVec2(iconCenter.x + 3.6f, iconCenter.y + 3.6f),
        ImVec2(iconCenter.x + 8.0f, iconCenter.y + 8.0f),
        ColorU32(theme.Colors.TextMuted),
        1.4f);

    return changed;
}

bool MenuUI::TextInput(
    const char* id,
    const char* label,
    char* buffer,
    size_t bufferSize,
    const char* hint) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const float width = ImGui::GetContentRegionAvail().x;

    ImGui::PushID(id);
    ImGui::PushFont(fonts.Small);
    ImGui::TextColored(theme.Colors.TextSecondary, "%s", label);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, theme.Spacing.XS));

    ImGui::SetNextItemWidth(width);
    ImGui::PushFont(fonts.Body);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(theme.Spacing.MD, 10.0f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameRounding,
        theme.Radius.SM);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        WithAlpha(theme.Colors.SurfaceActive, 0.58f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        WithAlpha(theme.Colors.SurfaceHover, 0.92f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        theme.Colors.SurfaceActive);
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        ? theme.Colors.Border
        : theme.Colors.BorderSoft);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        theme.Colors.TextPrimary);
    ImGui::PushStyleColor(
        ImGuiCol_TextDisabled,
        theme.Colors.TextMuted);

    const bool submitted = ImGui::InputTextWithHint(
        "##input",
        hint,
        buffer,
        bufferSize,
        ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
    ImGui::PopID();
    return submitted;
}

bool MenuUI::SelectionRow(
    const char* id,
    const char* title,
    const char* metadata,
    bool selected,
    ImTextureID itemIcon) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 54.0f);

    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    const bool clicked = ImGui::IsItemClicked();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec4 background = selected
        ? theme.Colors.AccentMuted
        : active
            ? theme.Colors.SurfaceActive
            : hovered
                ? theme.Colors.SurfaceHover
                : WithAlpha(theme.Colors.SurfaceActive, 0.22f);
    draw->AddRectFilled(
        min,
        max,
        ColorU32(background),
        theme.Radius.SM);
    if (selected) {
        draw->AddRectFilled(
            ImVec2(min.x, min.y + 8.0f),
            ImVec2(min.x + 3.0f, max.y - 8.0f),
            ColorU32(theme.Colors.Accent),
            1.5f);
    }
    if (focused) {
        draw->AddRect(
            min,
            max,
            ColorU32(WithAlpha(theme.Colors.Accent, 0.62f)),
            theme.Radius.SM);
    }

    const ImVec2 tileMin(
        min.x + theme.Spacing.SM,
        min.y + 8.0f);
    const ImVec2 tileMax(
        tileMin.x + 38.0f,
        tileMin.y + 38.0f);
    draw->AddRectFilled(
        tileMin,
        tileMax,
        ColorU32(WithAlpha(theme.Colors.SurfaceActive, 0.78f)),
        theme.Radius.SM);
    if (itemIcon != ImTextureID_Invalid) {
        draw->AddImageRounded(
            ImTextureRef(itemIcon),
            ImVec2(tileMin.x + 2.0f, tileMin.y + 2.0f),
            ImVec2(tileMax.x - 2.0f, tileMax.y - 2.0f),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            IM_COL32_WHITE,
            (std::max)(0.0f, theme.Radius.SM - 2.0f));
    }
    else {
        char monogram[2]{
            title && title[0] ? title[0] : '?',
            '\0'
        };
        const ImVec2 monogramSize =
            MeasureText(fonts.Body, monogram);
        draw->AddText(
            fonts.Body,
            fonts.Body->LegacySize,
            ImVec2(
                tileMin.x
                    + (38.0f - monogramSize.x) * 0.5f,
                tileMin.y
                    + (38.0f - monogramSize.y) * 0.5f),
            ColorU32(theme.Colors.TextSecondary),
            monogram);
    }
    draw->AddRect(
        tileMin,
        tileMax,
        ColorU32(WithAlpha(theme.Colors.BorderSoft, 0.62f)),
        theme.Radius.SM);

    const float textX = tileMax.x + theme.Spacing.SM;
    const float textWidth =
        (std::max)(
            1.0f,
            max.x - textX - theme.Spacing.MD);
    const std::string visibleTitle =
        EllipsizeText(fonts.Body, title, textWidth);
    const std::string visibleMetadata =
        EllipsizeText(fonts.Small, metadata, textWidth);
    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(textX, min.y + 8.0f),
        ColorU32(
            selected
                ? theme.Colors.TextPrimary
                : theme.Colors.TextSecondary),
        visibleTitle.c_str());
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(textX, min.y + 31.0f),
        ColorU32(theme.Colors.TextMuted),
        visibleMetadata.c_str());
    if (hovered
        && (visibleTitle != title
            || visibleMetadata != metadata)) {
        ImGui::SetTooltip(
            "%s\n%s",
            title,
            metadata);
    }
    return clicked;
}

bool MenuUI::QuantityEditorRow(
    const char* id,
    const char* title,
    const char* className,
    const char* slotLabel,
    int currentQuantity,
    int* draftQuantity,
    int minimum,
    int maximum,
    bool operationPending,
    const char* actionLabel,
    bool* locked,
    ImTextureID itemIcon) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr float height = 76.0f;
    const ImVec2 end(start.x + width, start.y + height);
    const bool hovered = ImGui::IsMouseHoveringRect(start, end);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec4 background = theme.Colors.SurfaceHover;
    background.w = hovered ? 0.42f : 0.20f;
    draw->AddRectFilled(start, end, ColorU32(background), theme.Radius.SM);
    draw->AddRect(
        start,
        end,
        ColorU32(WithAlpha(
            theme.Colors.BorderSoft,
            hovered ? 0.78f : 0.40f)),
        theme.Radius.SM);

    const ImVec2 tileMin(
        start.x + theme.Spacing.MD,
        start.y + theme.Spacing.MD);
    const ImVec2 tileMax(tileMin.x + 50.0f, tileMin.y + 50.0f);
    draw->AddRectFilled(
        tileMin,
        tileMax,
        ColorU32(WithAlpha(theme.Colors.SurfaceActive, 0.84f)),
        theme.Radius.SM);
    draw->AddRect(
        tileMin,
        tileMax,
        ColorU32(WithAlpha(theme.Colors.BorderSoft, 0.68f)),
        theme.Radius.SM);

    if (itemIcon != ImTextureID_Invalid) {
        draw->AddImageRounded(
            ImTextureRef(itemIcon),
            ImVec2(tileMin.x + 2.0f, tileMin.y + 2.0f),
            ImVec2(tileMax.x - 2.0f, tileMax.y - 2.0f),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            IM_COL32_WHITE,
            (std::max)(0.0f, theme.Radius.SM - 2.0f));
    }
    else {
        char monogram[2]{
            title && title[0] ? title[0] : '?',
            '\0'
        };
        const ImVec2 monogramSize =
            MeasureText(fonts.Heading, monogram);
        draw->AddText(
            fonts.Heading,
            fonts.Heading->LegacySize,
            ImVec2(
                tileMin.x
                    + (50.0f - monogramSize.x) * 0.5f,
                tileMin.y
                    + (50.0f - monogramSize.y) * 0.5f),
            ColorU32(theme.Colors.TextPrimary),
            monogram);
    }

    constexpr float stepWidth = 30.0f;
    constexpr float inputWidth = 74.0f;
    constexpr float applyWidth = 62.0f;
    constexpr float lockWidth = 68.0f;
    constexpr float gap = 6.0f;
    constexpr float applyGap = 10.0f;
    constexpr float lockGap = 6.0f;
    const bool hasLock = locked != nullptr;
    const float controlsWidth =
        stepWidth + gap + inputWidth + gap + stepWidth
        + applyGap + applyWidth
        + (hasLock ? lockGap + lockWidth : 0.0f);
    const float controlsX =
        end.x - theme.Spacing.MD - controlsWidth;
    const float controlsY = start.y + 22.0f;
    const float textX = tileMax.x + theme.Spacing.MD;
    const float textClipX = controlsX - theme.Spacing.LG;
    const ImVec4 textClip(start.x, start.y, textClipX, end.y);
    const float availableTitleWidth =
        (std::max)(1.0f, textClipX - textX);
    const std::string visibleTitle =
        EllipsizeText(
            fonts.Body,
            title,
            availableTitleWidth);

    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(textX, start.y + 13.0f),
        ColorU32(theme.Colors.TextPrimary),
        visibleTitle.c_str(),
        nullptr,
        0.0f,
        &textClip);

    std::string metadata = slotLabel;
    metadata += "   /   ";
    metadata += className;
    const std::string visibleMetadata =
        EllipsizeText(
            fonts.Small,
            metadata.c_str(),
            availableTitleWidth);
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(textX, start.y + 39.0f),
        ColorU32(theme.Colors.TextMuted),
        visibleMetadata.c_str(),
        nullptr,
        0.0f,
        &textClip);
    if (hovered
        && (visibleTitle != title
            || visibleMetadata != metadata)) {
        ImGui::SetTooltip(
            "%s\n%s",
            title,
            metadata.c_str());
    }

    ImGui::PushID(id);
    const bool controlsEnabled =
        !operationPending && (!locked || !*locked);

    ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));
    if (StepperButton(
        "##decrement",
        "-",
        ImVec2(stepWidth, 34.0f),
        controlsEnabled && *draftQuantity > minimum)) {
        *draftQuantity = (std::max)(minimum, *draftQuantity - 1);
    }

    ImGui::SetCursorScreenPos(ImVec2(
        controlsX + stepWidth + gap,
        controlsY));
    ImGui::SetNextItemWidth(inputWidth);
    if (!controlsEnabled)
        ImGui::BeginDisabled();
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.Radius.SM);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        WithAlpha(theme.Colors.SurfaceActive, 0.86f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        theme.Colors.SurfaceHover);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        theme.Colors.SurfaceActive);
    const bool submittedFromKeyboard = ImGui::InputInt(
        "##quantity",
        draftQuantity,
        0,
        0,
        ImGuiInputTextFlags_CharsDecimal
            | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    if (!controlsEnabled)
        ImGui::EndDisabled();
    *draftQuantity = std::clamp(
        *draftQuantity,
        minimum,
        maximum);

    ImGui::SetCursorScreenPos(ImVec2(
        controlsX + stepWidth + gap + inputWidth + gap,
        controlsY));
    if (StepperButton(
        "##increment",
        "+",
        ImVec2(stepWidth, 34.0f),
        controlsEnabled && *draftQuantity < maximum)) {
        *draftQuantity = (std::min)(maximum, *draftQuantity + 1);
    }

    const float applyX =
        controlsX + stepWidth + gap + inputWidth + gap
        + stepWidth + applyGap;
    ImGui::SetCursorScreenPos(ImVec2(applyX, controlsY));
    const bool canApply =
        controlsEnabled && *draftQuantity != currentQuantity;
    const bool apply = ActionButton(
        "##apply",
        actionLabel,
        ImVec2(applyWidth, 34.0f),
        ButtonKind::Primary,
        canApply);
    if (locked) {
        ImGui::SetCursorScreenPos(ImVec2(
            applyX + applyWidth + lockGap,
            controlsY));
        if (ActionButton(
            "##lock",
            *locked ? "Unlock" : "Lock",
            ImVec2(lockWidth, 34.0f),
            *locked ? ButtonKind::Primary : ButtonKind::Ghost,
            true)) {
            *locked = !*locked;
        }
    }
    ImGui::PopID();

    ImGui::SetCursorScreenPos(ImVec2(start.x, end.y));
    ImGui::Dummy(ImVec2(width, 0.0f));
    return apply || (submittedFromKeyboard && canApply);
}

bool MenuUI::IntegerPropertyEditorRow(
    const char* id,
    const char* title,
    const char* description,
    int currentValue,
    int* draftValue,
    int minimum,
    int maximum,
    bool operationPending,
    bool* locked) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr float stepWidth = 30.0f;
    constexpr float inputWidth = 74.0f;
    constexpr float applyWidth = 62.0f;
    constexpr float lockWidth = 68.0f;
    constexpr float gap = 6.0f;
    constexpr float applyGap = 10.0f;
    constexpr float lockGap = 6.0f;
    const bool hasLock = locked != nullptr;
    const float controlsWidth =
        stepWidth + gap + inputWidth + gap + stepWidth
        + applyGap + applyWidth
        + (hasLock ? lockGap + lockWidth : 0.0f);
    const float controlsX =
        start.x + width - theme.Spacing.MD - controlsWidth;
    const float textX = start.x + theme.Spacing.MD;
    const float textWidth =
        (std::max)(
            1.0f,
            controlsX - theme.Spacing.LG - textX);
    const ImVec2 descriptionSize =
        MeasureWrappedText(
            fonts.Small,
            description,
            textWidth);
    const float height = (std::max)(
        62.0f,
        34.0f + descriptionSize.y + 10.0f);
    const ImVec2 end(start.x + width, start.y + height);
    const bool hovered = ImGui::IsMouseHoveringRect(start, end);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        start,
        end,
        ColorU32(WithAlpha(
            theme.Colors.SurfaceActive,
            hovered ? 0.52f : 0.30f)),
        theme.Radius.SM);
    draw->AddRectFilled(
        ImVec2(start.x, start.y + 10.0f),
        ImVec2(start.x + 2.0f, end.y - 10.0f),
        ColorU32(WithAlpha(theme.Colors.Warning, 0.82f)),
        1.0f);

    const float controlsY =
        start.y + (height - 34.0f) * 0.5f;
    const ImVec4 textClip(
        start.x,
        start.y,
        controlsX - theme.Spacing.LG,
        end.y);
    const std::string visibleTitle =
        EllipsizeText(fonts.Body, title, textWidth);

    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(textX, start.y + 10.0f),
        ColorU32(theme.Colors.TextPrimary),
        visibleTitle.c_str(),
        nullptr,
        0.0f,
        &textClip);
    DrawWrappedText(
        draw,
        fonts.Small,
        ImVec2(textX, start.y + 34.0f),
        ColorU32(theme.Colors.TextMuted),
        description,
        textWidth,
        &textClip);
    if (hovered && visibleTitle != title)
        ImGui::SetTooltip("%s", title);

    ImGui::PushID(id);
    const bool controlsEnabled =
        !operationPending && (!locked || !*locked);

    ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));
    if (StepperButton(
        "##decrement",
        "-",
        ImVec2(stepWidth, 34.0f),
        controlsEnabled && *draftValue > minimum)) {
        *draftValue = (std::max)(minimum, *draftValue - 1);
    }

    ImGui::SetCursorScreenPos(ImVec2(
        controlsX + stepWidth + gap,
        controlsY));
    ImGui::SetNextItemWidth(inputWidth);
    if (!controlsEnabled)
        ImGui::BeginDisabled();
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.Radius.SM);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        WithAlpha(theme.Colors.SurfaceActive, 0.86f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        theme.Colors.SurfaceHover);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        theme.Colors.SurfaceActive);
    const bool submittedFromKeyboard = ImGui::InputInt(
        "##value",
        draftValue,
        0,
        0,
        ImGuiInputTextFlags_CharsDecimal
            | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    if (!controlsEnabled)
        ImGui::EndDisabled();
    *draftValue = std::clamp(*draftValue, minimum, maximum);

    ImGui::SetCursorScreenPos(ImVec2(
        controlsX + stepWidth + gap + inputWidth + gap,
        controlsY));
    if (StepperButton(
        "##increment",
        "+",
        ImVec2(stepWidth, 34.0f),
        controlsEnabled && *draftValue < maximum)) {
        *draftValue = (std::min)(maximum, *draftValue + 1);
    }

    const float applyX =
        controlsX + stepWidth + gap + inputWidth + gap
        + stepWidth + applyGap;
    ImGui::SetCursorScreenPos(ImVec2(applyX, controlsY));
    const bool canApply =
        controlsEnabled && *draftValue != currentValue;
    const bool apply = ActionButton(
        "##apply",
        "Apply",
        ImVec2(applyWidth, 34.0f),
        ButtonKind::Primary,
        canApply);
    if (locked) {
        ImGui::SetCursorScreenPos(ImVec2(
            applyX + applyWidth + lockGap,
            controlsY));
        if (ActionButton(
            "##lock",
            *locked ? "Unlock" : "Lock",
            ImVec2(lockWidth, 34.0f),
            *locked ? ButtonKind::Primary : ButtonKind::Ghost,
            true)) {
            *locked = !*locked;
        }
    }
    ImGui::PopID();

    ImGui::SetCursorScreenPos(ImVec2(start.x, end.y));
    ImGui::Dummy(ImVec2(width, 0.0f));
    return apply || (submittedFromKeyboard && canApply);
}

bool MenuUI::DoublePropertyEditorRow(
    const char* id,
    const char* title,
    const char* description,
    double currentValue,
    double* draftValue,
    double minimum,
    double maximum,
    bool operationPending,
    bool* locked) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr float stepWidth = 30.0f;
    constexpr float inputWidth = 74.0f;
    constexpr float applyWidth = 62.0f;
    constexpr float lockWidth = 68.0f;
    constexpr float gap = 6.0f;
    constexpr float applyGap = 10.0f;
    constexpr float lockGap = 6.0f;
    const bool hasLock = locked != nullptr;
    const float controlsWidth =
        stepWidth + gap + inputWidth + gap + stepWidth
        + applyGap + applyWidth
        + (hasLock ? lockGap + lockWidth : 0.0f);
    const float controlsX =
        start.x + width - theme.Spacing.MD - controlsWidth;
    const float textX = start.x + theme.Spacing.MD;
    const float textWidth =
        (std::max)(
            1.0f,
            controlsX - theme.Spacing.LG - textX);
    const ImVec2 descriptionSize =
        MeasureWrappedText(
            fonts.Small,
            description,
            textWidth);
    const float height = (std::max)(
        62.0f,
        34.0f + descriptionSize.y + 10.0f);
    const ImVec2 end(start.x + width, start.y + height);
    const bool hovered = ImGui::IsMouseHoveringRect(start, end);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        start,
        end,
        ColorU32(WithAlpha(
            theme.Colors.SurfaceActive,
            hovered ? 0.52f : 0.30f)),
        theme.Radius.SM);
    draw->AddRectFilled(
        ImVec2(start.x, start.y + 10.0f),
        ImVec2(start.x + 2.0f, end.y - 10.0f),
        ColorU32(WithAlpha(theme.Colors.Accent, 0.82f)),
        1.0f);

    const float controlsY =
        start.y + (height - 34.0f) * 0.5f;
    const ImVec4 textClip(
        start.x,
        start.y,
        controlsX - theme.Spacing.LG,
        end.y);
    const std::string visibleTitle =
        EllipsizeText(fonts.Body, title, textWidth);

    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(textX, start.y + 10.0f),
        ColorU32(theme.Colors.TextPrimary),
        visibleTitle.c_str(),
        nullptr,
        0.0f,
        &textClip);
    DrawWrappedText(
        draw,
        fonts.Small,
        ImVec2(textX, start.y + 34.0f),
        ColorU32(theme.Colors.TextMuted),
        description,
        textWidth,
        &textClip);
    if (hovered && visibleTitle != title)
        ImGui::SetTooltip("%s", title);

    ImGui::PushID(id);
    const bool controlsEnabled =
        !operationPending && (!locked || !*locked);

    ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));
    if (StepperButton(
        "##decrement",
        "-",
        ImVec2(stepWidth, 34.0f),
        controlsEnabled && *draftValue > minimum)) {
        *draftValue = (std::max)(minimum, *draftValue - 1.0);
    }

    ImGui::SetCursorScreenPos(ImVec2(
        controlsX + stepWidth + gap,
        controlsY));
    ImGui::SetNextItemWidth(inputWidth);
    if (!controlsEnabled)
        ImGui::BeginDisabled();
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.Radius.SM);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        WithAlpha(theme.Colors.SurfaceActive, 0.86f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        theme.Colors.SurfaceHover);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        theme.Colors.SurfaceActive);
    const bool submittedFromKeyboard = ImGui::InputDouble(
        "##value",
        draftValue,
        0.0,
        0.0,
        "%.2f",
        ImGuiInputTextFlags_CharsDecimal
            | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    if (!controlsEnabled)
        ImGui::EndDisabled();
    if (!std::isfinite(*draftValue))
        *draftValue = currentValue;
    *draftValue = std::clamp(*draftValue, minimum, maximum);

    ImGui::SetCursorScreenPos(ImVec2(
        controlsX + stepWidth + gap + inputWidth + gap,
        controlsY));
    if (StepperButton(
        "##increment",
        "+",
        ImVec2(stepWidth, 34.0f),
        controlsEnabled && *draftValue < maximum)) {
        *draftValue = (std::min)(maximum, *draftValue + 1.0);
    }

    const float applyX =
        controlsX + stepWidth + gap + inputWidth + gap
        + stepWidth + applyGap;
    ImGui::SetCursorScreenPos(ImVec2(applyX, controlsY));
    const bool canApply =
        controlsEnabled
        && std::fabs(*draftValue - currentValue) > 0.0005;
    const bool apply = ActionButton(
        "##apply",
        "Apply",
        ImVec2(applyWidth, 34.0f),
        ButtonKind::Primary,
        canApply);
    if (locked) {
        ImGui::SetCursorScreenPos(ImVec2(
            applyX + applyWidth + lockGap,
            controlsY));
        if (ActionButton(
            "##lock",
            *locked ? "Unlock" : "Lock",
            ImVec2(lockWidth, 34.0f),
            *locked ? ButtonKind::Primary : ButtonKind::Ghost,
            true)) {
            *locked = !*locked;
        }
    }
    ImGui::PopID();

    ImGui::SetCursorScreenPos(ImVec2(start.x, end.y));
    ImGui::Dummy(ImVec2(width, 0.0f));
    return apply || (submittedFromKeyboard && canApply);
}

bool MenuUI::ToggleRow(
    const char* id,
    const char* title,
    const char* description,
    bool* value,
    bool enabled) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const float width = ImGui::GetContentRegionAvail().x;
    const float descriptionWidth =
        (std::max)(1.0f, width - theme.Spacing.MD * 2.0f);
    const ImVec2 descriptionSize =
        MeasureWrappedText(
            fonts.Small,
            description,
            descriptionWidth);
    const float height = (std::max)(
        theme.Sizing.ControlRowHeight,
        31.0f + descriptionSize.y + 10.0f);
    const ImVec2 size(width, height);

    if (!enabled)
        ImGui::BeginDisabled();

    ImGui::InvisibleButton(id, size);
    const bool clicked = enabled && ImGui::IsItemClicked();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool active = enabled && ImGui::IsItemActive();
    const bool focused = enabled && ImGui::IsItemFocused();
    if (clicked)
        *value = !*value;

    const ImGuiID itemId = ImGui::GetItemID();
    const float hover = Animate(itemId ^ 0x14828u, hovered ? 1.0f : 0.0f);
    const float toggle = Animate(itemId ^ 0x76AE4u, *value ? 1.0f : 0.0f, 18.0f);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec4 rowBackground = theme.Colors.SurfaceHover;
    rowBackground.w = enabled
        ? 0.10f + hover * (active ? 0.34f : 0.22f)
        : 0.05f;
    draw->AddRectFilled(min, max, ColorU32(rowBackground), theme.Radius.SM);
    if (focused) {
        draw->AddRect(
            min,
            max,
            ColorU32(WithAlpha(theme.Colors.Accent, 0.72f)),
            theme.Radius.SM,
            0,
            1.25f);
    }

    const ImVec4 titleColor = enabled
        ? theme.Colors.TextPrimary
        : theme.Colors.TextMuted;
    const ImVec4 descriptionColor = enabled
        ? theme.Colors.TextSecondary
        : WithAlpha(theme.Colors.TextMuted, 0.65f);
    const ImVec2 switchSize(48.0f, 24.0f);
    const ImVec2 switchMin(
        max.x - theme.Spacing.MD - switchSize.x,
        min.y + 8.0f);
    const ImVec2 switchMax(
        switchMin.x + switchSize.x,
        switchMin.y + switchSize.y);
    const char* stateText = *value ? "ON" : "OFF";
    const ImVec2 stateSize = MeasureText(fonts.Small, stateText);
    const float stateX = switchMin.x - theme.Spacing.MD - stateSize.x;
    const float titleX = min.x + theme.Spacing.MD;
    const float titleWidth =
        (std::max)(1.0f, stateX - theme.Spacing.LG - titleX);
    const std::string visibleTitle =
        EllipsizeText(fonts.Body, title, titleWidth);
    const ImVec4 textClip(
        min.x,
        min.y,
        stateX - theme.Spacing.LG,
        min.y + 30.0f);

    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(titleX, min.y + 9.0f),
        ColorU32(titleColor),
        visibleTitle.c_str(),
        nullptr,
        0.0f,
        &textClip);
    const ImVec4 descriptionClip(
        min.x,
        min.y + 30.0f,
        max.x,
        max.y);
    DrawWrappedText(
        draw,
        fonts.Small,
        ImVec2(min.x + theme.Spacing.MD, min.y + 31.0f),
        ColorU32(descriptionColor),
        description,
        descriptionWidth,
        &descriptionClip);

    ImVec4 track = theme.Colors.SurfaceActive;
    track.x += (theme.Colors.AccentStrong.x - track.x) * toggle;
    track.y += (theme.Colors.AccentStrong.y - track.y) * toggle;
    track.z += (theme.Colors.AccentStrong.z - track.z) * toggle;
    track.w = enabled ? 1.0f : 0.40f;
    draw->AddRectFilled(
        switchMin,
        switchMax,
        ColorU32(track),
        switchSize.y * 0.5f);
    draw->AddRect(
        switchMin,
        switchMax,
        ColorU32(WithAlpha(theme.Colors.Border, enabled ? 0.72f : 0.30f)),
        switchSize.y * 0.5f);

    const float knobX = switchMin.x + 12.0f + toggle * 24.0f;
    ImVec4 knob = theme.Colors.TextSecondary;
    knob.x += (theme.Colors.Window.x - knob.x) * toggle;
    knob.y += (theme.Colors.Window.y - knob.y) * toggle;
    knob.z += (theme.Colors.Window.z - knob.z) * toggle;
    knob.w = enabled ? 1.0f : 0.50f;
    draw->AddCircleFilled(
        ImVec2(knobX, switchMin.y + 12.0f),
        8.5f,
        ColorU32(knob));

    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(
            stateX,
            switchMin.y
                + (switchSize.y - stateSize.y) * 0.5f),
        ColorU32(*value ? theme.Colors.TextPrimary : theme.Colors.TextMuted),
        stateText);

    if (hovered && visibleTitle != title)
        ImGui::SetTooltip("%s", title);

    if (!enabled)
        ImGui::EndDisabled();

    return clicked;
}

bool MenuUI::SegmentedRow(
    const char* id,
    const char* title,
    const char* description,
    int* selectedIndex,
    const char* firstLabel,
    const char* secondLabel,
    bool enabled) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    constexpr float PreferredControlWidth = 194.0f;
    constexpr float ControlHeight = 34.0f;
    const float horizontalTextWidth =
        rowWidth
        - theme.Spacing.MD * 2.0f
        - PreferredControlWidth
        - theme.Spacing.LG;
    const bool stacked = horizontalTextWidth < 190.0f;
    const float descriptionWidth = (std::max)(
        1.0f,
        stacked
            ? rowWidth - theme.Spacing.MD * 2.0f
            : horizontalTextWidth);
    const ImVec2 descriptionSize = MeasureWrappedText(
        fonts.Small,
        description,
        descriptionWidth);
    const float rowHeight = stacked
        ? 31.0f
            + descriptionSize.y
            + theme.Spacing.SM
            + ControlHeight
            + theme.Spacing.MD
        : (std::max)(
            theme.Sizing.ControlRowHeight,
            31.0f + descriptionSize.y + 10.0f);
    const ImVec2 rowSize(rowWidth, rowHeight);
    const ImVec2 rowMax(
        start.x + rowSize.x,
        start.y + rowSize.y);
    const ImVec2 controlSize(
        stacked
            ? rowWidth - theme.Spacing.MD * 2.0f
            : PreferredControlWidth,
        ControlHeight);
    const ImVec2 controlMin(
        stacked
            ? start.x + theme.Spacing.MD
            : rowMax.x - theme.Spacing.MD - controlSize.x,
        stacked
            ? start.y
                + 31.0f
                + descriptionSize.y
                + theme.Spacing.SM
            : start.y + 10.0f);
    const ImVec2 controlMax(
        controlMin.x + controlSize.x,
        controlMin.y + controlSize.y);

    if (!enabled)
        ImGui::BeginDisabled();

    ImGui::SetCursorScreenPos(controlMin);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##segmented_selector", controlSize);
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool active = enabled && ImGui::IsItemActive();
    const bool focused = enabled && ImGui::IsItemFocused();
    const bool clicked = enabled && ImGui::IsItemClicked();
    const ImGuiID itemId = ImGui::GetItemID();
    bool changed = false;

    int nextIndex = std::clamp(*selectedIndex, 0, 1);
    if (clicked) {
        nextIndex = ImGui::GetIO().MousePos.x
            < controlMin.x + controlSize.x * 0.5f
            ? 0
            : 1;
    }
    if (focused && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        nextIndex = 0;
    if (focused && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        nextIndex = 1;
    if (nextIndex != *selectedIndex) {
        *selectedIndex = nextIndex;
        changed = true;
    }

    const float selection = Animate(
        itemId ^ 0x63C1A5u,
        *selectedIndex == 0 ? 0.0f : 1.0f,
        18.0f);
    const float hover = Animate(
        itemId ^ 0x9A54D2u,
        hovered ? 1.0f : 0.0f);
    ImGui::PopID();

    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(rowSize);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec4 rowBackground = theme.Colors.SurfaceHover;
    rowBackground.w = enabled ? 0.10f + hover * 0.16f : 0.05f;
    draw->AddRectFilled(
        start,
        rowMax,
        ColorU32(rowBackground),
        theme.Radius.SM);

    const float textX = start.x + theme.Spacing.MD;
    const float labelClipX = stacked
        ? rowMax.x - theme.Spacing.MD
        : controlMin.x - theme.Spacing.LG;
    const float titleWidth =
        (std::max)(1.0f, labelClipX - textX);
    const std::string visibleTitle =
        EllipsizeText(fonts.Body, title, titleWidth);
    const ImVec4 titleClip(
        start.x,
        start.y,
        labelClipX,
        start.y + 30.0f);
    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(textX, start.y + 9.0f),
        ColorU32(enabled ? theme.Colors.TextPrimary : theme.Colors.TextMuted),
        visibleTitle.c_str(),
        nullptr,
        0.0f,
        &titleClip);
    const ImVec4 descriptionClip(
        textX,
        start.y + 30.0f,
        textX + descriptionWidth,
        stacked
            ? controlMin.y - theme.Spacing.SM
            : rowMax.y);
    DrawWrappedText(
        draw,
        fonts.Small,
        ImVec2(textX, start.y + 31.0f),
        ColorU32(
            enabled
            ? theme.Colors.TextSecondary
            : WithAlpha(theme.Colors.TextMuted, 0.65f)),
        description,
        descriptionWidth,
        &descriptionClip);

    draw->AddRectFilled(
        controlMin,
        controlMax,
        ColorU32(WithAlpha(
            theme.Colors.SurfaceActive,
            enabled ? 0.88f : 0.40f)),
        theme.Radius.SM);
    const float segmentWidth = controlSize.x * 0.5f;
    const float selectedMinX = controlMin.x + segmentWidth * selection;
    draw->AddRectFilled(
        ImVec2(selectedMinX, controlMin.y),
        ImVec2(selectedMinX + segmentWidth, controlMax.y),
        ColorU32(WithAlpha(
            theme.Colors.Accent,
            enabled ? (active ? 0.78f : 0.62f) : 0.22f)),
        theme.Radius.SM);
    draw->AddRect(
        controlMin,
        controlMax,
        ColorU32(WithAlpha(
            theme.Colors.Border,
            focused ? 0.95f : 0.62f)),
        theme.Radius.SM,
        0,
        focused ? 1.25f : 1.0f);

    const ImVec2 firstSize = MeasureText(fonts.Small, firstLabel);
    const ImVec2 secondSize = MeasureText(fonts.Small, secondLabel);
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(
            controlMin.x + (segmentWidth - firstSize.x) * 0.5f,
            controlMin.y + (controlSize.y - firstSize.y) * 0.5f),
        ColorU32(
            enabled && *selectedIndex == 0
            ? theme.Colors.TextPrimary
            : theme.Colors.TextMuted),
        firstLabel);
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(
            controlMin.x + segmentWidth
                + (segmentWidth - secondSize.x) * 0.5f,
            controlMin.y + (controlSize.y - secondSize.y) * 0.5f),
        ColorU32(
            enabled && *selectedIndex == 1
            ? theme.Colors.TextPrimary
            : theme.Colors.TextMuted),
        secondLabel);

    if (hovered && visibleTitle != title)
        ImGui::SetTooltip("%s", title);

    if (!enabled)
        ImGui::EndDisabled();

    return changed;
}

bool MenuUI::SliderRow(
    const char* id,
    const char* title,
    const char* description,
    float* value,
    float minimum,
    float maximum,
    const char* format) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const float width = ImGui::GetContentRegionAvail().x;
    const float descriptionWidth =
        (std::max)(1.0f, width - theme.Spacing.MD * 2.0f);
    const ImVec2 descriptionSize = MeasureWrappedText(
        fonts.Small,
        description,
        descriptionWidth);
    const float height = (std::max)(
        72.0f,
        34.0f + descriptionSize.y + 24.0f);
    const ImVec2 size(width, height);

    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    bool changed = false;

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float trackMinX = min.x + theme.Spacing.MD;
    const float trackMaxX = max.x - theme.Spacing.MD;

    if (active) {
        const float normalized = std::clamp(
            (ImGui::GetIO().MousePos.x - trackMinX) / (trackMaxX - trackMinX),
            0.0f,
            1.0f);
        const float next = minimum + normalized * (maximum - minimum);
        changed = next != *value;
        *value = next;
    }

    const float normalized = std::clamp(
        (*value - minimum) / (maximum - minimum),
        0.0f,
        1.0f);
    const float hover = Animate(
        ImGui::GetItemID() ^ 0x3C106u,
        hovered ? 1.0f : 0.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec4 rowBackground = theme.Colors.SurfaceHover;
    rowBackground.w = hover * 0.20f;
    if (rowBackground.w > 0.01f)
        draw->AddRectFilled(min, max, ColorU32(rowBackground), theme.Radius.SM);
    if (focused) {
        draw->AddRect(
            min,
            max,
            ColorU32(WithAlpha(theme.Colors.Accent, 0.70f)),
            theme.Radius.SM,
            0,
            1.25f);
    }

    char valueText[32]{};
    std::snprintf(valueText, sizeof(valueText), format, *value);
    const ImVec2 valueSize = MeasureText(fonts.Small, valueText);
    const float titleWidth = (std::max)(
        1.0f,
        max.x
            - theme.Spacing.MD
            - valueSize.x
            - theme.Spacing.LG
            - (min.x + theme.Spacing.MD));
    const std::string visibleTitle =
        EllipsizeText(fonts.Body, title, titleWidth);

    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        ImVec2(min.x + theme.Spacing.MD, min.y + 10.0f),
        ColorU32(theme.Colors.TextPrimary),
        visibleTitle.c_str());
    const ImVec4 descriptionClip(
        min.x + theme.Spacing.MD,
        min.y + 33.0f,
        max.x - theme.Spacing.MD,
        max.y - 20.0f);
    DrawWrappedText(
        draw,
        fonts.Small,
        ImVec2(min.x + theme.Spacing.MD, min.y + 34.0f),
        ColorU32(theme.Colors.TextSecondary),
        description,
        descriptionWidth,
        &descriptionClip);
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(max.x - theme.Spacing.MD - valueSize.x, min.y + 12.0f),
        ColorU32(theme.Colors.TextPrimary),
        valueText);

    const float trackY = max.y - 12.0f;
    draw->AddRectFilled(
        ImVec2(trackMinX, trackY - 2.0f),
        ImVec2(trackMaxX, trackY + 2.0f),
        ColorU32(theme.Colors.SurfaceActive),
        2.0f);
    draw->AddRectFilled(
        ImVec2(trackMinX, trackY - 2.0f),
        ImVec2(trackMinX + (trackMaxX - trackMinX) * normalized, trackY + 2.0f),
        ColorU32(theme.Colors.Accent),
        2.0f);
    draw->AddCircleFilled(
        ImVec2(trackMinX + (trackMaxX - trackMinX) * normalized, trackY),
        active ? 6.0f : 5.0f,
        ColorU32(theme.Colors.AccentStrong));

    if (hovered && visibleTitle != title)
        ImGui::SetTooltip("%s", title);

    return changed;
}

void MenuUI::PageHeader(const char* title, const char* description) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddText(
        fonts.Heading,
        fonts.Heading->LegacySize,
        start,
        ColorU32(theme.Colors.TextPrimary),
        title);

    float height = fonts.Heading->LegacySize;
    if (description && description[0]) {
        const float descriptionY =
            start.y + fonts.Heading->LegacySize + theme.Spacing.XS;
        const ImVec2 descriptionSize =
            MeasureWrappedText(
                fonts.Small,
                description,
                width);
        DrawWrappedText(
            draw,
            fonts.Small,
            ImVec2(start.x, descriptionY),
            ColorU32(theme.Colors.TextSecondary),
            description,
            width);
        height = fonts.Heading->LegacySize
            + theme.Spacing.XS
            + descriptionSize.y;
    }

    ImGui::Dummy(ImVec2(width, height));
}

void MenuUI::StatusBadge(const char* label, StatusKind kind) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec4& color = StatusColor(kind);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float availableWidth =
        ImGui::GetContentRegionAvail().x;
    const float maximumTextWidth = (std::max)(
        1.0f,
        availableWidth
            - theme.Spacing.MD * 2.0f
            - 10.0f);
    const std::string visibleLabel =
        EllipsizeText(
            fonts.Small,
            label,
            maximumTextWidth);
    const ImVec2 textSize =
        MeasureText(fonts.Small, visibleLabel.c_str());
    const ImVec2 size(
        textSize.x + theme.Spacing.MD * 2.0f + 10.0f,
        25.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        start,
        ImVec2(start.x + size.x, start.y + size.y),
        ColorU32(WithAlpha(color, 0.13f)),
        theme.Radius.SM);
    draw->AddCircleFilled(
        ImVec2(start.x + theme.Spacing.MD, start.y + size.y * 0.5f),
        3.0f,
        ColorU32(color));
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(start.x + theme.Spacing.MD + 9.0f, start.y + 5.0f),
        ColorU32(color),
        visibleLabel.c_str());

    ImGui::Dummy(size);
    if (ImGui::IsItemHovered() && visibleLabel != label)
        ImGui::SetTooltip("%s", label);
}

float MenuUI::WrappedText(
    const char* text,
    const ImVec4& color,
    ImFont* font,
    float wrapWidth) {
    if (!text || !text[0])
        return 0.0f;
    if (!font)
        font = GetFonts().Small
            ? GetFonts().Small
            : ImGui::GetFont();
    if (wrapWidth <= 0.0f)
        wrapWidth = ImGui::GetContentRegionAvail().x;
    wrapWidth = (std::max)(1.0f, wrapWidth);

    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 size =
        MeasureWrappedText(font, text, wrapWidth);
    DrawWrappedText(
        ImGui::GetWindowDrawList(),
        font,
        start,
        ColorU32(color),
        text,
        wrapWidth);
    ImGui::Dummy(ImVec2(wrapWidth, size.y));
    return size.y;
}

void MenuUI::BeginCard(
    const char* id,
    const ImVec2& size,
    ImGuiWindowFlags flags) {
    const AppTheme& theme = GetTheme();

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(theme.Spacing.LG, theme.Spacing.LG));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.Radius.MD);
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(theme.Spacing.MD, theme.Spacing.XS));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild(
        id,
        size,
        ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
        flags | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 cardMax(
        min.x + ImGui::GetWindowWidth(),
        min.y + ImGui::GetWindowHeight());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, cardMax, ColorU32(theme.Colors.Surface), theme.Radius.MD);
    draw->AddRect(min, cardMax, ColorU32(theme.Colors.BorderSoft), theme.Radius.MD);
}

void MenuUI::EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void MenuUI::CardHeading(const char* title, const char* description) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddText(
        fonts.Body,
        fonts.Body->LegacySize,
        start,
        ColorU32(theme.Colors.TextPrimary),
        title);

    float height = fonts.Body->LegacySize;
    if (description && description[0]) {
        const float descriptionY =
            start.y + fonts.Body->LegacySize + theme.Spacing.XS;
        const ImVec2 descriptionSize =
            MeasureWrappedText(
                fonts.Small,
                description,
                width);
        DrawWrappedText(
            draw,
            fonts.Small,
            ImVec2(start.x, descriptionY),
            ColorU32(theme.Colors.TextSecondary),
            description,
            width);
        height = fonts.Body->LegacySize
            + theme.Spacing.XS
            + descriptionSize.y;
    }

    ImGui::Dummy(ImVec2(width, height));
}

void MenuUI::Metric(const char* label, const char* value, const char* detail) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();

    ImGui::PushFont(fonts.Small);
    ImGui::TextColored(theme.Colors.TextSecondary, "%s", label);
    ImGui::PopFont();

    ImGui::PushFont(fonts.Heading);
    ImGui::TextColored(theme.Colors.TextPrimary, "%s", value);
    ImGui::PopFont();

    if (detail && detail[0]) {
        WrappedText(
            detail,
            theme.Colors.TextMuted,
            fonts.Small);
    }
}

void MenuUI::PointerRow(const char* label, uintptr_t value) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr float height = 27.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    char valueText[32]{};
    if (value)
        std::snprintf(valueText, sizeof(valueText), "0x%llX", static_cast<unsigned long long>(value));
    else
        std::snprintf(valueText, sizeof(valueText), "Unavailable");

    const ImVec2 valueSize = MeasureText(fonts.Small, valueText);
    const float labelX = start.x + 15.0f;
    const float labelWidth = (std::max)(
        1.0f,
        start.x
            + width
            - valueSize.x
            - theme.Spacing.LG
            - labelX);
    const std::string visibleLabel =
        EllipsizeText(fonts.Small, label, labelWidth);
    draw->AddCircleFilled(
        ImVec2(start.x + 4.0f, start.y + 9.0f),
        2.5f,
        ColorU32(value ? theme.Colors.Success : theme.Colors.Warning));
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(labelX, start.y + 2.0f),
        ColorU32(theme.Colors.TextSecondary),
        visibleLabel.c_str());
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(start.x + width - valueSize.x, start.y + 2.0f),
        ColorU32(value ? theme.Colors.TextPrimary : theme.Colors.TextMuted),
        valueText);

    ImGui::Dummy(ImVec2(width, height));
    if (ImGui::IsItemHovered() && visibleLabel != label)
        ImGui::SetTooltip("%s", label);
}

void MenuUI::KeyHintRow(const char* key, const char* action) {
    const AppTheme& theme = GetTheme();
    const AppFonts& fonts = GetFonts();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 keySize = MeasureText(fonts.Small, key);
    const ImVec2 chipSize(keySize.x + 16.0f, 24.0f);
    const float actionX =
        start.x + chipSize.x + theme.Spacing.MD;
    const float actionWidth = (std::max)(
        1.0f,
        start.x + width - actionX);
    const std::string visibleAction =
        EllipsizeText(fonts.Small, action, actionWidth);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        start,
        ImVec2(start.x + chipSize.x, start.y + chipSize.y),
        ColorU32(theme.Colors.SurfaceActive),
        theme.Radius.SM);
    draw->AddRect(
        start,
        ImVec2(start.x + chipSize.x, start.y + chipSize.y),
        ColorU32(theme.Colors.BorderSoft),
        theme.Radius.SM);
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(start.x + 8.0f, start.y + 4.0f),
        ColorU32(theme.Colors.TextPrimary),
        key);
    draw->AddText(
        fonts.Small,
        fonts.Small->LegacySize,
        ImVec2(actionX, start.y + 4.0f),
        ColorU32(theme.Colors.TextSecondary),
        visibleAction.c_str());

    ImGui::Dummy(ImVec2(width, 30.0f));
    if (ImGui::IsItemHovered() && visibleAction != action)
        ImGui::SetTooltip("%s", action);
}
