#include "Notifications.h"

#include "MenuTheme.h"

#include <ImGui/imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cfloat>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

    using Clock = std::chrono::steady_clock;

    struct ActiveNotification {
        Notifications::Kind Kind = Notifications::Kind::Info;
        std::string Key;
        std::string Title;
        std::string Message;
        Clock::time_point CreatedAt{};
        float DurationSeconds = 4.25f;
        float CurrentY = 0.0f;
        bool HasCurrentY = false;
    };

    constexpr size_t MaximumNotifications = 6;
    constexpr float MinimumDurationSeconds = 1.5f;
    constexpr float MaximumDurationSeconds = 12.0f;

    std::mutex gNotificationMutex;
    std::vector<ActiveNotification> gNotifications;
    uint64_t gAnonymousNotificationId = 0;

    float SecondsBetween(
        Clock::time_point later,
        Clock::time_point earlier) {
        return std::chrono::duration<float>(
            later - earlier).count();
    }

    float Clamp01(float value) {
        return std::clamp(value, 0.0f, 1.0f);
    }

    float SmoothStep(float value) {
        const float clamped = Clamp01(value);
        return clamped * clamped
            * (3.0f - 2.0f * clamped);
    }

    float EaseOutCubic(float value) {
        const float inverse = 1.0f - Clamp01(value);
        return 1.0f - inverse * inverse * inverse;
    }

    ImVec4 FadeColor(const ImVec4& color, float alpha) {
        ImVec4 faded = color;
        faded.w *= Clamp01(alpha);
        return faded;
    }

    ImVec4 KindColor(
        Notifications::Kind kind,
        const MenuUI::AppTheme& theme) {
        switch (kind) {
        case Notifications::Kind::Success:
            return theme.Colors.Success;
        case Notifications::Kind::Warning:
            return theme.Colors.Warning;
        case Notifications::Kind::Error:
            return theme.Colors.Error;
        case Notifications::Kind::Info:
        default:
            return theme.Colors.AccentStrong;
        }
    }

    const char* KindGlyph(Notifications::Kind kind) {
        switch (kind) {
        case Notifications::Kind::Success:
            return "+";
        case Notifications::Kind::Warning:
            return "!";
        case Notifications::Kind::Error:
            return "x";
        case Notifications::Kind::Info:
        default:
            return "i";
        }
    }

    ImVec2 MeasureWrapped(
        ImFont* font,
        const std::string& text,
        float wrapWidth) {
        if (text.empty())
            return ImVec2(0.0f, 0.0f);
        if (!font)
            return ImGui::CalcTextSize(
                text.c_str(),
                nullptr,
                false,
                wrapWidth);
        return font->CalcTextSizeA(
            font->LegacySize,
            FLT_MAX,
            wrapWidth,
            text.c_str());
    }

    void DrawNotification(
        ImDrawList* draw,
        ActiveNotification& notification,
        float age,
        float desiredY,
        float width,
        float height,
        float deltaTime,
        const ImGuiViewport& viewport,
        const MenuUI::AppTheme& theme,
        const MenuUI::AppFonts& fonts) {
        const float enterDuration =
            (std::max)(
                0.01f,
                theme.Motion.NotificationEnterDuration);
        const float exitDuration =
            (std::max)(
                0.01f,
                theme.Motion.NotificationExitDuration);
        const float remainingSeconds =
            notification.DurationSeconds - age;
        const float enter =
            SmoothStep(age / enterDuration);
        const float exit =
            SmoothStep(remainingSeconds / exitDuration);
        const float alpha = (std::min)(enter, exit);

        if (!notification.HasCurrentY) {
            notification.CurrentY =
                desiredY + theme.Spacing.LG;
            notification.HasCurrentY = true;
        }
        const float stackBlend =
            1.0f - std::exp(
                -theme.Motion.NotificationStackResponse
                * (std::max)(0.0f, deltaTime));
        notification.CurrentY +=
            (desiredY - notification.CurrentY)
            * stackBlend;

        const float enterOffset =
            (1.0f - EaseOutCubic(
                age / enterDuration))
            * 34.0f;
        const float exitOffset =
            (1.0f - exit)
            * 20.0f;
        const float right =
            viewport.WorkPos.x
            + viewport.WorkSize.x
            - theme.Sizing.OverlayGutter;
        const float x =
            right - width + enterOffset + exitOffset;
        const float y = notification.CurrentY;
        const ImVec2 minimum(x, y);
        const ImVec2 maximum(x + width, y + height);

        const ImVec4 kindColor =
            KindColor(notification.Kind, theme);
        const float radius = theme.Radius.MD;
        draw->AddRectFilled(
            ImVec2(minimum.x - 5.0f, minimum.y + 5.0f),
            ImVec2(maximum.x + 5.0f, maximum.y + 9.0f),
            MenuUI::ColorU32(FadeColor(
                theme.Colors.ScreenScrim,
                alpha * 0.72f)),
            radius + 3.0f);
        draw->AddRectFilled(
            minimum,
            maximum,
            MenuUI::ColorU32(FadeColor(
                theme.Colors.Surface,
                alpha * 0.985f)),
            radius);
        draw->AddRect(
            minimum,
            maximum,
            MenuUI::ColorU32(FadeColor(
                theme.Colors.BorderSoft,
                alpha)),
            radius,
            0,
            1.0f);

        const float progress =
            Clamp01(
                remainingSeconds
                / notification.DurationSeconds);
        const float railInset = 1.0f;
        const float railHeight = 2.5f;
        draw->AddRectFilled(
            ImVec2(
                minimum.x + railInset,
                minimum.y + railInset),
            ImVec2(
                maximum.x - railInset,
                minimum.y + railInset + railHeight),
            MenuUI::ColorU32(FadeColor(
                theme.Colors.BorderSoft,
                alpha * 0.62f)),
            radius);
        draw->AddRectFilled(
            ImVec2(
                minimum.x + railInset,
                minimum.y + railInset),
            ImVec2(
                minimum.x + railInset
                    + (width - railInset * 2.0f)
                        * progress,
                minimum.y + railInset + railHeight),
            MenuUI::ColorU32(FadeColor(
                kindColor,
                alpha)),
            radius);

        const float iconCenterX =
            minimum.x + theme.Spacing.LG;
        const float iconCenterY =
            minimum.y + height * 0.5f;
        draw->AddCircleFilled(
            ImVec2(iconCenterX, iconCenterY),
            12.0f,
            MenuUI::ColorU32(FadeColor(
                kindColor,
                alpha * 0.16f)));
        draw->AddCircle(
            ImVec2(iconCenterX, iconCenterY),
            11.5f,
            MenuUI::ColorU32(FadeColor(
                kindColor,
                alpha * 0.68f)),
            24,
            1.0f);

        ImFont* bodyFont =
            fonts.Body ? fonts.Body : ImGui::GetFont();
        ImFont* smallFont =
            fonts.Small ? fonts.Small : bodyFont;
        const char* glyph = KindGlyph(notification.Kind);
        const ImVec2 glyphSize =
            bodyFont->CalcTextSizeA(
                bodyFont->LegacySize,
                FLT_MAX,
                0.0f,
                glyph);
        draw->AddText(
            bodyFont,
            bodyFont->LegacySize,
            ImVec2(
                iconCenterX - glyphSize.x * 0.5f,
                iconCenterY - glyphSize.y * 0.5f),
            MenuUI::ColorU32(FadeColor(
                kindColor,
                alpha)),
            glyph);

        const float textX =
            minimum.x + theme.Spacing.LG * 2.0f
            + theme.Spacing.SM;
        const float textWidth =
            maximum.x - theme.Spacing.LG - textX;
        float textY =
            minimum.y + theme.Spacing.MD + 3.0f;
        draw->AddText(
            bodyFont,
            bodyFont->LegacySize,
            ImVec2(textX, textY),
            MenuUI::ColorU32(FadeColor(
                theme.Colors.TextPrimary,
                alpha)),
            notification.Title.c_str());

        if (!notification.Message.empty()) {
            textY += bodyFont->LegacySize
                + theme.Spacing.XS;
            draw->AddText(
                smallFont,
                smallFont->LegacySize,
                ImVec2(textX, textY),
                MenuUI::ColorU32(FadeColor(
                    theme.Colors.TextSecondary,
                    alpha)),
                notification.Message.c_str(),
                nullptr,
                textWidth);
        }
    }

} // namespace

void Notifications::Push(
    Kind kind,
    std::string_view key,
    std::string title,
    std::string message,
    float durationSeconds) {
    if (title.empty())
        return;

    const Clock::time_point now = Clock::now();
    durationSeconds = std::clamp(
        durationSeconds,
        MinimumDurationSeconds,
        MaximumDurationSeconds);

    std::scoped_lock lock(gNotificationMutex);
    std::string resolvedKey(key);
    if (resolvedKey.empty()) {
        resolvedKey =
            "anonymous."
            + std::to_string(++gAnonymousNotificationId);
    }

    const auto existing = std::find_if(
        gNotifications.begin(),
        gNotifications.end(),
        [&](const ActiveNotification& notification) {
            return notification.Key == resolvedKey;
        });
    if (existing != gNotifications.end()) {
        existing->Kind = kind;
        existing->Title = std::move(title);
        existing->Message = std::move(message);
        existing->CreatedAt = now;
        existing->DurationSeconds = durationSeconds;
        return;
    }

    if (gNotifications.size()
        >= MaximumNotifications) {
        gNotifications.erase(gNotifications.begin());
    }

    ActiveNotification notification{};
    notification.Kind = kind;
    notification.Key = std::move(resolvedKey);
    notification.Title = std::move(title);
    notification.Message = std::move(message);
    notification.CreatedAt = now;
    notification.DurationSeconds = durationSeconds;
    gNotifications.push_back(std::move(notification));
}

void Notifications::Render() {
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();
    if (!viewport
        || viewport->WorkSize.x <= 1.0f
        || viewport->WorkSize.y <= 1.0f) {
        return;
    }

    const Clock::time_point now = Clock::now();
    const auto& theme = MenuUI::GetTheme();
    const auto& fonts = MenuUI::GetFonts();
    const float deltaTime =
        (std::max)(0.0f, ImGui::GetIO().DeltaTime);

    std::scoped_lock lock(gNotificationMutex);
    std::erase_if(
        gNotifications,
        [&](const ActiveNotification& notification) {
            return SecondsBetween(
                now,
                notification.CreatedAt)
                >= notification.DurationSeconds;
        });
    if (gNotifications.empty())
        return;

    const float maximumAvailableWidth =
        (std::max)(
            1.0f,
            viewport->WorkSize.x
                - theme.Sizing.OverlayGutter * 2.0f);
    const float width = (std::min)(
        maximumAvailableWidth,
        std::clamp(
            viewport->WorkSize.x * 0.26f,
            theme.Sizing.NotificationMinWidth,
            theme.Sizing.NotificationMaxWidth));
    const float textWidth =
        width
        - theme.Spacing.LG * 3.0f
        - theme.Spacing.SM;
    ImFont* bodyFont =
        fonts.Body ? fonts.Body : ImGui::GetFont();
    ImFont* smallFont =
        fonts.Small ? fonts.Small : bodyFont;

    float stackBottom =
        viewport->WorkPos.y
        + viewport->WorkSize.y
        - theme.Sizing.OverlayGutter;
    ImDrawList* draw =
        ImGui::GetForegroundDrawList();
    for (auto iterator = gNotifications.rbegin();
        iterator != gNotifications.rend();
        ++iterator) {
        const ImVec2 bodySize =
            MeasureWrapped(
                smallFont,
                iterator->Message,
                textWidth);
        const float contentHeight =
            theme.Spacing.MD * 2.0f
            + bodyFont->LegacySize
            + (iterator->Message.empty()
                ? 0.0f
                : theme.Spacing.XS + bodySize.y);
        const float height =
            (std::max)(64.0f, contentHeight);
        const float desiredY =
            stackBottom - height;
        if (desiredY
            < viewport->WorkPos.y
                + theme.Sizing.OverlayGutter) {
            break;
        }
        const float age =
            SecondsBetween(now, iterator->CreatedAt);

        DrawNotification(
            draw,
            *iterator,
            age,
            desiredY,
            width,
            height,
            deltaTime,
            *viewport,
            theme,
            fonts);
        stackBottom =
            desiredY - theme.Spacing.SM;
    }
}

void Notifications::Clear() {
    std::scoped_lock lock(gNotificationMutex);
    gNotifications.clear();
    gAnonymousNotificationId = 0;
}
