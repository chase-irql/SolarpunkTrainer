#include "WorldRenderer.h"

namespace {

    WorldRenderer::FrameState gFrame{};

    ImDrawList* ResolveDrawList(ImDrawList* drawList) {
        return drawList ? drawList : ImGui::GetForegroundDrawList();
    }

} // namespace

void WorldRenderer::BeginFrame(float viewportWidth, float viewportHeight) {
    gFrame.Player = Solarpunk::CapturePlayerSnapshot();
    gFrame.Camera = Solarpunk::CaptureCameraSnapshot(gFrame.Player);
    gFrame.Projection = Solarpunk::BuildProjectionContext(
        gFrame.Camera,
        viewportWidth,
        viewportHeight);
}

const WorldRenderer::FrameState& WorldRenderer::GetFrameState() {
    return gFrame;
}

bool WorldRenderer::IsReady() {
    return gFrame.Projection.IsValid;
}

bool WorldRenderer::Project(const Solarpunk::Vector3& world, ImVec2& screen) {
    Solarpunk::ScreenPoint projected{};
    if (!Solarpunk::WorldToScreen(world, gFrame.Projection, projected))
        return false;

    screen = ImVec2(projected.X, projected.Y);
    return true;
}

bool WorldRenderer::DrawLine3D(
    const Solarpunk::Vector3& start,
    const Solarpunk::Vector3& end,
    ImU32 color,
    float thickness,
    ImDrawList* drawList) {
    ImVec2 screenStart{};
    ImVec2 screenEnd{};
    if (!Project(start, screenStart) || !Project(end, screenEnd))
        return false;

    ResolveDrawList(drawList)->AddLine(
        screenStart,
        screenEnd,
        color,
        thickness);
    return true;
}

bool WorldRenderer::DrawMarker(
    const Solarpunk::Vector3& world,
    const char* label,
    ImU32 color,
    ImDrawList* drawList) {
    ImVec2 screen{};
    if (!Project(world, screen))
        return false;

    ImDrawList* draw = ResolveDrawList(drawList);
    draw->AddCircleFilled(screen, 3.5f, color);
    draw->AddCircle(screen, 7.0f, color, 24, 1.0f);
    if (label && label[0])
        draw->AddText(ImVec2(screen.x + 11.0f, screen.y - 7.0f), color, label);
    return true;
}

void WorldRenderer::DrawAxes(
    const Solarpunk::Vector3& origin,
    double size,
    ImDrawList* drawList) {
    DrawLine3D(
        origin,
        { origin.X + size, origin.Y, origin.Z },
        IM_COL32(225, 92, 92, 235),
        1.5f,
        drawList);
    DrawLine3D(
        origin,
        { origin.X, origin.Y + size, origin.Z },
        IM_COL32(91, 205, 130, 235),
        1.5f,
        drawList);
    DrawLine3D(
        origin,
        { origin.X, origin.Y, origin.Z + size },
        IM_COL32(98, 148, 230, 235),
        1.5f,
        drawList);
}
