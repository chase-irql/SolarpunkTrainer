#pragma once

#include "../solarpunk/SolarpunkRuntime.h"

#include <ImGui/imgui.h>

namespace WorldRenderer {

    struct FrameState {
        Solarpunk::PlayerSnapshot Player{};
        Solarpunk::CameraSnapshot Camera{};
        Solarpunk::ProjectionContext Projection{};
    };

    void BeginFrame(float viewportWidth, float viewportHeight);
    const FrameState& GetFrameState();
    bool IsReady();

    bool Project(const Solarpunk::Vector3& world, ImVec2& screen);
    bool DrawLine3D(
        const Solarpunk::Vector3& start,
        const Solarpunk::Vector3& end,
        ImU32 color,
        float thickness = 1.0f,
        ImDrawList* drawList = nullptr);

    bool DrawMarker(
        const Solarpunk::Vector3& world,
        const char* label,
        ImU32 color,
        ImDrawList* drawList = nullptr);

    void DrawAxes(
        const Solarpunk::Vector3& origin,
        double size,
        ImDrawList* drawList = nullptr);

} // namespace WorldRenderer
