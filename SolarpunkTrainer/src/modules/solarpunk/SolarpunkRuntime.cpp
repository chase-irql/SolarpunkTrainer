#include "SolarpunkRuntime.h"
#include "GameSchema.h"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <memory/Memory.h>

namespace {

    struct RuntimeOffsets {
        uintptr_t WorldOwningGameInstance = 0;
        uintptr_t GameInstanceLocalPlayers = 0;
        uintptr_t PlayerPlayerController = 0;
        uintptr_t ControllerPawn = 0;
        uintptr_t PlayerControllerAcknowledgedPawn = 0;
        uintptr_t PlayerControllerCameraManager = 0;
        uintptr_t ActorRootComponent = 0;
        uintptr_t SceneComponentRelativeLocation = 0;
        uintptr_t CameraManagerCache = 0;
        uintptr_t CameraCachePointOfView = 0;
        bool Valid = false;
    };

    const RuntimeOffsets& GetRuntimeOffsets() {
        static const RuntimeOffsets offsets = [] {
            RuntimeOffsets result{};
            const auto resolve = [](
                std::string_view owner,
                std::string_view property) -> uintptr_t {
                const auto* descriptor =
                    Solarpunk::GameSchema::FindProperty(
                        owner,
                        property);
                return descriptor
                    ? descriptor->Offset
                    : 0;
            };

            result.WorldOwningGameInstance =
                resolve("World", "OwningGameInstance");
            result.GameInstanceLocalPlayers =
                resolve("GameInstance", "LocalPlayers");
            result.PlayerPlayerController =
                resolve("Player", "PlayerController");
            result.ControllerPawn =
                resolve("Controller", "Pawn");
            result.PlayerControllerAcknowledgedPawn =
                resolve("PlayerController", "AcknowledgedPawn");
            result.PlayerControllerCameraManager =
                resolve(
                    "PlayerController",
                    "PlayerCameraManager");
            result.ActorRootComponent =
                resolve("Actor", "RootComponent");
            result.SceneComponentRelativeLocation =
                resolve("SceneComponent", "RelativeLocation");
            result.CameraManagerCache =
                resolve(
                    "PlayerCameraManager",
                    "CameraCachePrivate");
            result.CameraCachePointOfView =
                resolve("CameraCacheEntry", "POV");
            result.Valid =
                result.WorldOwningGameInstance
                && result.GameInstanceLocalPlayers
                && result.PlayerPlayerController
                && result.ControllerPawn
                && result.PlayerControllerAcknowledgedPawn
                && result.PlayerControllerCameraManager
                && result.ActorRootComponent
                && result.SceneComponentRelativeLocation
                && result.CameraManagerCache
                && result.CameraCachePointOfView;
            return result;
        }();
        return offsets;
    }

    struct TArrayHeader {
        uintptr_t Data = 0;
        int32_t Count = 0;
        int32_t Capacity = 0;
    };

    // First 0x60 bytes of FMinimalViewInfo from the generated SDK. The full
    // structure is 0x8D0, but world projection only needs this stable header.
    struct MinimalViewHeader {
        Solarpunk::Vector3 Location;             // 0x00
        Solarpunk::Rotator3 Rotation;             // 0x18
        float FieldOfView;                        // 0x30
        float DesiredFieldOfView;                 // 0x34
        float FirstPersonFieldOfView;             // 0x38
        float FirstPersonScale;                   // 0x3C
        float OrthoWidth;                         // 0x40
        bool AutoCalculateOrthoPlanes;            // 0x44
        uint8_t Pad45[0x3];
        float AutoPlaneShift;                     // 0x48
        bool UpdateOrthoPlanes;                   // 0x4C
        bool UseCameraHeightAsViewTarget;         // 0x4D
        uint8_t Pad4E[0x2];
        float OrthoNearClipPlane;                 // 0x50
        float OrthoFarClipPlane;                  // 0x54
        float PerspectiveNearClipPlane;           // 0x58
        float AspectRatio;                        // 0x5C
    };

    static_assert(sizeof(TArrayHeader) == 0x10);
    static_assert(sizeof(Solarpunk::Vector3) == 0x18);
    static_assert(sizeof(Solarpunk::Rotator3) == 0x18);
    static_assert(sizeof(MinimalViewHeader) == 0x60);
    static_assert(offsetof(MinimalViewHeader, FieldOfView) == 0x30);
    static_assert(offsetof(MinimalViewHeader, PerspectiveNearClipPlane) == 0x58);
    static_assert(offsetof(MinimalViewHeader, AspectRatio) == 0x5C);

    bool TryReadPointer(uintptr_t address, uintptr_t& value) {
        const auto result = Memory::TryRead<uintptr_t>(address);
        if (!result || !Memory::IsValidPtr(*result)) {
            value = 0;
            return false;
        }

        value = *result;
        return true;
    }

    bool IsFinite(const Solarpunk::Vector3& value) {
        return std::isfinite(value.X)
            && std::isfinite(value.Y)
            && std::isfinite(value.Z);
    }

    bool IsFinite(const Solarpunk::Rotator3& value) {
        return std::isfinite(value.Pitch)
            && std::isfinite(value.Yaw)
            && std::isfinite(value.Roll);
    }

    double Dot(
        const Solarpunk::Vector3& left,
        const Solarpunk::Vector3& right) {
        return left.X * right.X
            + left.Y * right.Y
            + left.Z * right.Z;
    }

    Solarpunk::Matrix4x4 Multiply(
        const Solarpunk::Matrix4x4& left,
        const Solarpunk::Matrix4x4& right) {
        Solarpunk::Matrix4x4 result{};

        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                for (int index = 0; index < 4; ++index)
                    result.M[row][column] += left.M[row][index] * right.M[index][column];
            }
        }

        return result;
    }

    struct Vector4 {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        double W = 0.0;
    };

    Vector4 Transform(
        const Solarpunk::Matrix4x4& matrix,
        const Solarpunk::Vector3& point) {
        return {
            matrix.M[0][0] * point.X + matrix.M[0][1] * point.Y
                + matrix.M[0][2] * point.Z + matrix.M[0][3],
            matrix.M[1][0] * point.X + matrix.M[1][1] * point.Y
                + matrix.M[1][2] * point.Z + matrix.M[1][3],
            matrix.M[2][0] * point.X + matrix.M[2][1] * point.Y
                + matrix.M[2][2] * point.Z + matrix.M[2][3],
            matrix.M[3][0] * point.X + matrix.M[3][1] * point.Y
                + matrix.M[3][2] * point.Z + matrix.M[3][3]
        };
    }

    void BuildCameraAxes(
        const Solarpunk::Rotator3& rotation,
        Solarpunk::Vector3& forward,
        Solarpunk::Vector3& right,
        Solarpunk::Vector3& up) {
        constexpr double degreesToRadians = std::numbers::pi / 180.0;
        const double pitch = rotation.Pitch * degreesToRadians;
        const double yaw = rotation.Yaw * degreesToRadians;
        const double roll = rotation.Roll * degreesToRadians;

        const double sinPitch = std::sin(pitch);
        const double cosPitch = std::cos(pitch);
        const double sinYaw = std::sin(yaw);
        const double cosYaw = std::cos(yaw);
        const double sinRoll = std::sin(roll);
        const double cosRoll = std::cos(roll);

        // UE coordinate system: X forward, Y right, Z up.
        forward = {
            cosPitch * cosYaw,
            cosPitch * sinYaw,
            sinPitch
        };
        right = {
            sinRoll * sinPitch * cosYaw - cosRoll * sinYaw,
            sinRoll * sinPitch * sinYaw + cosRoll * cosYaw,
            -sinRoll * cosPitch
        };
        up = {
            -(cosRoll * sinPitch * cosYaw + sinRoll * sinYaw),
            sinRoll * cosYaw - cosRoll * sinPitch * sinYaw,
            cosRoll * cosPitch
        };
    }

} // namespace

Solarpunk::PlayerSnapshot Solarpunk::CapturePlayerSnapshot() {
    PlayerSnapshot snapshot{};
    const RuntimeOffsets& offsets = GetRuntimeOffsets();
    if (!offsets.Valid) {
        snapshot.Status =
            SnapshotStatus::CompatibilityUnavailable;
        return snapshot;
    }

    const uintptr_t imageBase = Memory::GetModuleBase();
    if (!imageBase) {
        snapshot.Status = SnapshotStatus::ModuleUnavailable;
        return snapshot;
    }

    if (!TryReadPointer(
        imageBase + Solarpunk::GameSchema::Get().Globals.GWorldRva,
        snapshot.World)) {
        snapshot.Status = SnapshotStatus::WorldUnavailable;
        return snapshot;
    }

    if (!TryReadPointer(
        snapshot.World + offsets.WorldOwningGameInstance,
        snapshot.GameInstance)) {
        snapshot.Status = SnapshotStatus::GameInstanceUnavailable;
        return snapshot;
    }

    const auto localPlayers = Memory::TryRead<TArrayHeader>(
        snapshot.GameInstance + offsets.GameInstanceLocalPlayers);

    if (!localPlayers
        || localPlayers->Count < 1
        || localPlayers->Count > localPlayers->Capacity
        || localPlayers->Count > 8
        || !Memory::IsValidPtr(localPlayers->Data)) {
        snapshot.Status = SnapshotStatus::LocalPlayerUnavailable;
        return snapshot;
    }

    if (!TryReadPointer(localPlayers->Data, snapshot.LocalPlayer)) {
        snapshot.Status = SnapshotStatus::LocalPlayerUnavailable;
        return snapshot;
    }

    if (!TryReadPointer(
        snapshot.LocalPlayer + offsets.PlayerPlayerController,
        snapshot.PlayerController)) {
        snapshot.Status = SnapshotStatus::PlayerControllerUnavailable;
        return snapshot;
    }

    // AcknowledgedPawn is normally populated for the local player. During
    // possession transitions, AController::Pawn can become valid first.
    if (!TryReadPointer(
        snapshot.PlayerController
            + offsets.PlayerControllerAcknowledgedPawn,
        snapshot.Pawn)
        && !TryReadPointer(
            snapshot.PlayerController + offsets.ControllerPawn,
            snapshot.Pawn)) {
        snapshot.Status = SnapshotStatus::PawnUnavailable;
        return snapshot;
    }

    if (!TryReadPointer(
        snapshot.Pawn + offsets.ActorRootComponent,
        snapshot.RootComponent)) {
        snapshot.Status = SnapshotStatus::RootComponentUnavailable;
        return snapshot;
    }

    const auto coordinates = Memory::TryRead<Vector3>(
        snapshot.RootComponent
            + offsets.SceneComponentRelativeLocation);

    if (!coordinates || !IsFinite(*coordinates)) {
        snapshot.Status = SnapshotStatus::CoordinatesUnavailable;
        return snapshot;
    }

    snapshot.Coordinates = *coordinates;
    snapshot.Status = SnapshotStatus::Ready;
    return snapshot;
}

Solarpunk::CameraSnapshot Solarpunk::CaptureCameraSnapshot(
    const PlayerSnapshot& player) {
    CameraSnapshot camera{};
    const RuntimeOffsets& offsets = GetRuntimeOffsets();
    if (!offsets.Valid) {
        camera.Status =
            CameraStatus::CompatibilityUnavailable;
        return camera;
    }

    if (!player.PlayerController) {
        camera.Status = CameraStatus::PlayerControllerUnavailable;
        return camera;
    }

    if (!TryReadPointer(
        player.PlayerController
            + offsets.PlayerControllerCameraManager,
        camera.PlayerCameraManager)) {
        camera.Status = CameraStatus::CameraManagerUnavailable;
        return camera;
    }

    camera.PointOfView = camera.PlayerCameraManager
        + offsets.CameraManagerCache
        + offsets.CameraCachePointOfView;

    const auto view = Memory::TryRead<MinimalViewHeader>(camera.PointOfView);
    if (!view) {
        camera.Status = CameraStatus::CameraDataUnavailable;
        return camera;
    }

    if (!IsFinite(view->Location)
        || !IsFinite(view->Rotation)
        || !std::isfinite(view->FieldOfView)
        || view->FieldOfView <= 1.0f
        || view->FieldOfView >= 179.0f) {
        camera.Status = CameraStatus::InvalidCameraData;
        return camera;
    }

    camera.Location = view->Location;
    camera.Rotation = view->Rotation;
    camera.FieldOfView = view->FieldOfView;
    camera.AspectRatio = view->AspectRatio;
    camera.NearClipPlane = view->PerspectiveNearClipPlane;
    camera.Status = CameraStatus::Ready;
    return camera;
}

Solarpunk::ProjectionContext Solarpunk::BuildProjectionContext(
    const CameraSnapshot& camera,
    float viewportWidth,
    float viewportHeight) {
    ProjectionContext context{};
    context.Camera = camera;
    context.ViewportWidth = viewportWidth;
    context.ViewportHeight = viewportHeight;

    if (!camera.HasCamera()
        || viewportWidth <= 1.0f
        || viewportHeight <= 1.0f) {
        return context;
    }

    Vector3 forward{};
    Vector3 right{};
    Vector3 up{};
    BuildCameraAxes(camera.Rotation, forward, right, up);

    context.View.M[0][0] = right.X;
    context.View.M[0][1] = right.Y;
    context.View.M[0][2] = right.Z;
    context.View.M[0][3] = -Dot(right, camera.Location);

    context.View.M[1][0] = up.X;
    context.View.M[1][1] = up.Y;
    context.View.M[1][2] = up.Z;
    context.View.M[1][3] = -Dot(up, camera.Location);

    context.View.M[2][0] = forward.X;
    context.View.M[2][1] = forward.Y;
    context.View.M[2][2] = forward.Z;
    context.View.M[2][3] = -Dot(forward, camera.Location);
    context.View.M[3][3] = 1.0;

    constexpr double degreesToRadians = std::numbers::pi / 180.0;
    const double halfFov = static_cast<double>(camera.FieldOfView)
        * 0.5
        * degreesToRadians;
    const double horizontalScale = 1.0 / std::tan(halfFov);
    const double viewportAspect = static_cast<double>(viewportWidth)
        / static_cast<double>(viewportHeight);
    const double nearPlane = camera.NearClipPlane > 0.01f
        ? static_cast<double>(camera.NearClipPlane)
        : 1.0;
    constexpr double farPlane = 100000000.0;

    // Left-handed perspective matrix with forward depth in +Z.
    context.Projection.M[0][0] = horizontalScale;
    context.Projection.M[1][1] = horizontalScale * viewportAspect;
    context.Projection.M[2][2] = farPlane / (farPlane - nearPlane);
    context.Projection.M[2][3] = -(nearPlane * farPlane) / (farPlane - nearPlane);
    context.Projection.M[3][2] = 1.0;

    context.ViewProjection = Multiply(context.Projection, context.View);
    context.IsValid = true;
    return context;
}

bool Solarpunk::WorldToScreen(
    const Vector3& world,
    const ProjectionContext& context,
    ScreenPoint& screen) {
    screen = {};
    if (!context.IsValid || !IsFinite(world))
        return false;

    const Vector4 clip = Transform(context.ViewProjection, world);
    if (!std::isfinite(clip.W) || clip.W <= 0.001)
        return false;

    const double inverseW = 1.0 / clip.W;
    const double normalizedX = clip.X * inverseW;
    const double normalizedY = clip.Y * inverseW;

    screen.X = static_cast<float>(
        (normalizedX + 1.0) * 0.5 * context.ViewportWidth);
    screen.Y = static_cast<float>(
        (1.0 - normalizedY) * 0.5 * context.ViewportHeight);
    screen.Depth = clip.W;
    screen.IsOnScreen = screen.X >= 0.0f
        && screen.X <= context.ViewportWidth
        && screen.Y >= 0.0f
        && screen.Y <= context.ViewportHeight;
    return std::isfinite(screen.X) && std::isfinite(screen.Y);
}

Solarpunk::Vector3 Solarpunk::GetForwardVector(const Rotator3& rotation) {
    Vector3 forward{};
    Vector3 right{};
    Vector3 up{};
    BuildCameraAxes(rotation, forward, right, up);
    return forward;
}

Solarpunk::Vector3 Solarpunk::GetRightVector(const Rotator3& rotation) {
    Vector3 forward{};
    Vector3 right{};
    Vector3 up{};
    BuildCameraAxes(rotation, forward, right, up);
    return right;
}

const char* Solarpunk::GetStatusText(SnapshotStatus status) {
    switch (status) {
    case SnapshotStatus::Ready:
        return "Tracking local player";
    case SnapshotStatus::CompatibilityUnavailable:
        return "Runtime property contract is incompatible";
    case SnapshotStatus::ModuleUnavailable:
        return "Waiting for game module";
    case SnapshotStatus::WorldUnavailable:
        return "Waiting for world";
    case SnapshotStatus::GameInstanceUnavailable:
        return "Waiting for game instance";
    case SnapshotStatus::LocalPlayerUnavailable:
        return "Waiting for local player";
    case SnapshotStatus::PlayerControllerUnavailable:
        return "Waiting for player controller";
    case SnapshotStatus::PawnUnavailable:
        return "Waiting for player pawn";
    case SnapshotStatus::RootComponentUnavailable:
        return "Waiting for root component";
    case SnapshotStatus::CoordinatesUnavailable:
        return "Waiting for coordinates";
    default:
        return "Waiting for player";
    }
}

const char* Solarpunk::GetCameraStatusText(CameraStatus status) {
    switch (status) {
    case CameraStatus::Ready:
        return "Camera cache ready";
    case CameraStatus::CompatibilityUnavailable:
        return "Camera property contract is incompatible";
    case CameraStatus::PlayerControllerUnavailable:
        return "Waiting for player controller";
    case CameraStatus::CameraManagerUnavailable:
        return "Waiting for camera manager";
    case CameraStatus::CameraDataUnavailable:
        return "Waiting for camera data";
    case CameraStatus::InvalidCameraData:
        return "Camera data is invalid";
    default:
        return "Waiting for camera";
    }
}
