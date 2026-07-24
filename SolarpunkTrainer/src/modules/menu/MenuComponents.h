#pragma once

#include "MenuTheme.h"

#include <cstddef>
#include <cstdint>

namespace MenuUI {

    enum class StatusKind {
        Success,
        Warning,
        Error,
        Neutral
    };

    enum class ButtonKind {
        Primary,
        Secondary,
        Ghost,
        Danger
    };

    bool SidebarItem(
        const char* id,
        const char* index,
        const char* label,
        bool selected);

    bool CloseButton(const char* id);
    bool ActionButton(
        const char* id,
        const char* label,
        const ImVec2& size,
        ButtonKind kind = ButtonKind::Secondary,
        bool enabled = true);

    bool SearchBox(
        const char* id,
        char* buffer,
        size_t bufferSize,
        const char* hint);

    bool TextInput(
        const char* id,
        const char* label,
        char* buffer,
        size_t bufferSize,
        const char* hint);

    bool SelectionRow(
        const char* id,
        const char* title,
        const char* metadata,
        bool selected,
        ImTextureID itemIcon = ImTextureID_Invalid);

    bool QuantityEditorRow(
        const char* id,
        const char* title,
        const char* className,
        const char* slotLabel,
        int currentQuantity,
        int* draftQuantity,
        int minimum,
        int maximum,
        bool operationPending = false,
        const char* actionLabel = "Apply",
        bool* locked = nullptr,
        ImTextureID itemIcon = ImTextureID_Invalid);

    bool IntegerPropertyEditorRow(
        const char* id,
        const char* title,
        const char* description,
        int currentValue,
        int* draftValue,
        int minimum,
        int maximum,
        bool operationPending = false,
        bool* locked = nullptr);

    bool DoublePropertyEditorRow(
        const char* id,
        const char* title,
        const char* description,
        double currentValue,
        double* draftValue,
        double minimum,
        double maximum,
        bool operationPending = false,
        bool* locked = nullptr);

    bool ToggleRow(
        const char* id,
        const char* title,
        const char* description,
        bool* value,
        bool enabled = true);

    bool SegmentedRow(
        const char* id,
        const char* title,
        const char* description,
        int* selectedIndex,
        const char* firstLabel,
        const char* secondLabel,
        bool enabled = true);

    bool SliderRow(
        const char* id,
        const char* title,
        const char* description,
        float* value,
        float minimum,
        float maximum,
        const char* format);

    void ScreenScrim();
    void PageHeader(const char* title, const char* description);
    void StatusBadge(const char* label, StatusKind kind);
    float WrappedText(
        const char* text,
        const ImVec4& color,
        ImFont* font = nullptr,
        float wrapWidth = 0.0f);

    void BeginCard(
        const char* id,
        const ImVec2& size,
        ImGuiWindowFlags flags = ImGuiWindowFlags_None);
    void EndCard();

    void CardHeading(const char* title, const char* description = nullptr);
    void Metric(const char* label, const char* value, const char* detail = nullptr);
    void PointerRow(const char* label, uintptr_t value);
    void KeyHintRow(const char* key, const char* action);

} // namespace MenuUI
