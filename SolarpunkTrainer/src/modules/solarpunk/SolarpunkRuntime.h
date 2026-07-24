#pragma once

#include <cstdint>

namespace Solarpunk {

    struct Vector3 {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    struct Rotator3 {
        double Pitch = 0.0;
        double Yaw = 0.0;
        double Roll = 0.0;
    };

    struct Matrix4x4 {
        double M[4][4]{};
    };

    enum class SnapshotStatus {
        Ready,
        CompatibilityUnavailable,
        ModuleUnavailable,
        WorldUnavailable,
        GameInstanceUnavailable,
        LocalPlayerUnavailable,
        PlayerControllerUnavailable,
        PawnUnavailable,
        RootComponentUnavailable,
        CoordinatesUnavailable
    };

    struct PlayerSnapshot {
        SnapshotStatus Status = SnapshotStatus::ModuleUnavailable;

        uintptr_t World = 0;
        uintptr_t GameInstance = 0;
        uintptr_t LocalPlayer = 0;
        uintptr_t PlayerController = 0;
        uintptr_t Pawn = 0;
        uintptr_t RootComponent = 0;

        Vector3 Coordinates{};

        bool HasCoordinates() const {
            return Status == SnapshotStatus::Ready;
        }
    };

    enum class CameraStatus {
        Ready,
        CompatibilityUnavailable,
        PlayerControllerUnavailable,
        CameraManagerUnavailable,
        CameraDataUnavailable,
        InvalidCameraData
    };

    struct CameraSnapshot {
        CameraStatus Status = CameraStatus::PlayerControllerUnavailable;

        uintptr_t PlayerCameraManager = 0;
        uintptr_t PointOfView = 0;

        Vector3 Location{};
        Rotator3 Rotation{};
        float FieldOfView = 0.0f;
        float AspectRatio = 0.0f;
        float NearClipPlane = 0.0f;

        bool HasCamera() const {
            return Status == CameraStatus::Ready;
        }
    };

    struct ProjectionContext {
        CameraSnapshot Camera{};
        Matrix4x4 View{};
        Matrix4x4 Projection{};
        Matrix4x4 ViewProjection{};
        float ViewportWidth = 0.0f;
        float ViewportHeight = 0.0f;
        bool IsValid = false;
    };

    struct ScreenPoint {
        float X = 0.0f;
        float Y = 0.0f;
        double Depth = 0.0;
        bool IsOnScreen = false;
    };

    PlayerSnapshot CapturePlayerSnapshot();
    CameraSnapshot CaptureCameraSnapshot(const PlayerSnapshot& player);
    ProjectionContext BuildProjectionContext(
        const CameraSnapshot& camera,
        float viewportWidth,
        float viewportHeight);

    bool WorldToScreen(
        const Vector3& world,
        const ProjectionContext& context,
        ScreenPoint& screen);

    Vector3 GetForwardVector(const Rotator3& rotation);
    Vector3 GetRightVector(const Rotator3& rotation);

    const char* GetStatusText(SnapshotStatus status);
    const char* GetCameraStatusText(CameraStatus status);

} // namespace Solarpunk
