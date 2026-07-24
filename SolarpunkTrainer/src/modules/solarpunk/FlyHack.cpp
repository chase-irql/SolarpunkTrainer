#include "FlyHack.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <memory/Memory.h>

namespace {

    namespace Offsets {
        constexpr uintptr_t Actor_CollisionFlags = 0x005D;
        constexpr uint8_t Actor_CollisionMask = 0x01;

        constexpr uintptr_t Character_CharacterMovement = 0x0338;
        constexpr uintptr_t Character_CapsuleComponent = 0x0340;

        constexpr uintptr_t Movement_Velocity = 0x00D8;
        constexpr uintptr_t CharacterMovement_MovementMode = 0x0241;
        constexpr uintptr_t CharacterMovement_CustomMovementMode = 0x0242;
        constexpr uintptr_t CharacterMovement_MaxFlySpeed = 0x0294;
        constexpr uintptr_t CharacterMovement_ForceNextFloorCheckFlags = 0x02F9;
        constexpr uint8_t CharacterMovement_ForceNextFloorCheckMask = 0x10;
        constexpr uintptr_t CharacterMovement_Acceleration = 0x0338;

        constexpr uintptr_t PrimitiveComponent_BodyInstance = 0x0388;
        constexpr uintptr_t BodyInstance_CollisionEnabled = 0x0017;
    }

    constexpr uint8_t MoveFlying = 5;
    constexpr uint8_t NoCollision = 0;

    struct SavedState {
        bool IsCaptured = false;
        uintptr_t Pawn = 0;
        uintptr_t MovementComponent = 0;
        uintptr_t CapsuleComponent = 0;
        bool ActorCollisionEnabled = true;
        uint8_t CapsuleCollisionMode = 0;
        uint8_t MovementMode = 0;
        uint8_t CustomMovementMode = 0;
        float MaxFlySpeed = 0.0f;
    };

    Solarpunk::FlyHack::Settings gSettings{};
    Solarpunk::FlyHack::RuntimeState gRuntime{};
    SavedState gSaved{};

    bool KeyDown(int virtualKey) {
        return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    }

    bool TryReadPointer(uintptr_t address, uintptr_t& value) {
        const auto result = Memory::TryRead<uintptr_t>(address);
        if (!result || !Memory::IsValidPtr(*result)) {
            value = 0;
            return false;
        }

        value = *result;
        return true;
    }

    bool SetActorCollisionBit(uintptr_t pawn, bool enabled) {
        const auto flags = Memory::TryRead<uint8_t>(
            pawn + Offsets::Actor_CollisionFlags);
        if (!flags)
            return false;

        uint8_t next = *flags;
        if (enabled)
            next |= Offsets::Actor_CollisionMask;
        else
            next &= static_cast<uint8_t>(~Offsets::Actor_CollisionMask);

        return next == *flags
            || Memory::Write(pawn + Offsets::Actor_CollisionFlags, next);
    }

    bool SetCapsuleCollision(uintptr_t capsule, uint8_t collisionMode) {
        const uintptr_t address = capsule
            + Offsets::PrimitiveComponent_BodyInstance
            + Offsets::BodyInstance_CollisionEnabled;
        const auto current = Memory::TryRead<uint8_t>(address);
        if (!current)
            return false;

        return *current == collisionMode
            || Memory::Write(address, collisionMode);
    }

    bool SetMovementMode(
        uintptr_t movement,
        uint8_t movementMode,
        uint8_t customMode) {
        return Memory::Write(
            movement + Offsets::CharacterMovement_MovementMode,
            movementMode)
            && Memory::Write(
                movement + Offsets::CharacterMovement_CustomMovementMode,
                customMode);
    }

    bool WriteVelocity(
        uintptr_t movement,
        const Solarpunk::Vector3& velocity) {
        const Solarpunk::Vector3 noAcceleration{};
        return Memory::Write(
            movement + Offsets::Movement_Velocity,
            velocity)
            && Memory::Write(
                movement + Offsets::CharacterMovement_Acceleration,
                noAcceleration);
    }

    void RequestFloorCheck(uintptr_t movement) {
        const uintptr_t address = movement
            + Offsets::CharacterMovement_ForceNextFloorCheckFlags;
        const auto flags = Memory::TryRead<uint8_t>(address);
        if (flags) {
            Memory::Write(
                address,
                static_cast<uint8_t>(
                    *flags | Offsets::CharacterMovement_ForceNextFloorCheckMask));
        }
    }

    bool CaptureState(uintptr_t pawn) {
        SavedState captured{};
        captured.Pawn = pawn;

        if (!TryReadPointer(
            pawn + Offsets::Character_CharacterMovement,
            captured.MovementComponent)
            || !TryReadPointer(
                pawn + Offsets::Character_CapsuleComponent,
                captured.CapsuleComponent)) {
            return false;
        }

        const auto actorFlags = Memory::TryRead<uint8_t>(
            pawn + Offsets::Actor_CollisionFlags);
        const auto capsuleCollision = Memory::TryRead<uint8_t>(
            captured.CapsuleComponent
            + Offsets::PrimitiveComponent_BodyInstance
            + Offsets::BodyInstance_CollisionEnabled);
        const auto movementMode = Memory::TryRead<uint8_t>(
            captured.MovementComponent
            + Offsets::CharacterMovement_MovementMode);
        const auto customMode = Memory::TryRead<uint8_t>(
            captured.MovementComponent
            + Offsets::CharacterMovement_CustomMovementMode);
        const auto maxFlySpeed = Memory::TryRead<float>(
            captured.MovementComponent
            + Offsets::CharacterMovement_MaxFlySpeed);

        if (!actorFlags
            || !capsuleCollision
            || !movementMode
            || !customMode
            || !maxFlySpeed) {
            return false;
        }

        captured.ActorCollisionEnabled =
            (*actorFlags & Offsets::Actor_CollisionMask) != 0;
        captured.CapsuleCollisionMode = *capsuleCollision;
        captured.MovementMode = *movementMode;
        captured.CustomMovementMode = *customMode;
        captured.MaxFlySpeed = *maxFlySpeed;
        captured.IsCaptured = true;
        gSaved = captured;
        return true;
    }

    void RestoreState() {
        if (!gSaved.IsCaptured)
            return;

        if (Memory::IsValidPtr(gSaved.MovementComponent)) {
            WriteVelocity(gSaved.MovementComponent, {});
            Memory::Write(
                gSaved.MovementComponent
                + Offsets::CharacterMovement_MaxFlySpeed,
                gSaved.MaxFlySpeed);
            SetMovementMode(
                gSaved.MovementComponent,
                gSaved.MovementMode,
                gSaved.CustomMovementMode);
            RequestFloorCheck(gSaved.MovementComponent);
        }

        if (Memory::IsValidPtr(gSaved.CapsuleComponent)) {
            SetCapsuleCollision(
                gSaved.CapsuleComponent,
                gSaved.CapsuleCollisionMode);
        }

        if (Memory::IsValidPtr(gSaved.Pawn)) {
            SetActorCollisionBit(
                gSaved.Pawn,
                gSaved.ActorCollisionEnabled);
        }

        gSaved = {};
    }

    Solarpunk::Vector3 BuildInputDirection(
        const Solarpunk::CameraSnapshot& camera,
        bool& hasInput) {
        const double forwardAxis =
            (KeyDown('W') ? 1.0 : 0.0)
            - (KeyDown('S') ? 1.0 : 0.0);
        const double rightAxis =
            (KeyDown('D') ? 1.0 : 0.0)
            - (KeyDown('A') ? 1.0 : 0.0);
        const double verticalAxis =
            (KeyDown(VK_SPACE) ? 1.0 : 0.0)
            - (KeyDown(VK_CONTROL) ? 1.0 : 0.0);

        hasInput = forwardAxis != 0.0
            || rightAxis != 0.0
            || verticalAxis != 0.0;
        if (!hasInput)
            return {};

        const Solarpunk::Vector3 forward =
            Solarpunk::GetForwardVector(camera.Rotation);
        const Solarpunk::Vector3 right =
            Solarpunk::GetRightVector(camera.Rotation);
        Solarpunk::Vector3 direction{
            forward.X * forwardAxis + right.X * rightAxis,
            forward.Y * forwardAxis + right.Y * rightAxis,
            forward.Z * forwardAxis + right.Z * rightAxis + verticalAxis
        };

        const double length = std::sqrt(
            direction.X * direction.X
            + direction.Y * direction.Y
            + direction.Z * direction.Z);
        if (length <= 0.0001) {
            hasInput = false;
            return {};
        }

        direction.X /= length;
        direction.Y /= length;
        direction.Z /= length;
        return direction;
    }

} // namespace

Solarpunk::FlyHack::Settings& Solarpunk::FlyHack::GetSettings() {
    return gSettings;
}

const Solarpunk::FlyHack::RuntimeState& Solarpunk::FlyHack::GetState() {
    return gRuntime;
}

void Solarpunk::FlyHack::Update(
    const PlayerSnapshot& player,
    const CameraSnapshot& camera,
    float deltaTime,
    bool allowInput) {
    gRuntime.InputAllowed = allowInput;
    gRuntime.HasDirectionalInput = false;

    if (!gSettings.Enabled) {
        RestoreState();
        gRuntime = {};
        gRuntime.CurrentStatus = Status::Disabled;
        return;
    }

    if (!player.Pawn
        || !player.RootComponent
        || !camera.HasCamera()) {
        RestoreState();
        gRuntime.CurrentStatus = Status::WaitingForPlayer;
        gRuntime.Pawn = player.Pawn;
        gRuntime.MovementComponent = 0;
        return;
    }

    if (!gSaved.IsCaptured || gSaved.Pawn != player.Pawn) {
        RestoreState();
        if (!CaptureState(player.Pawn)) {
            gRuntime.CurrentStatus = Status::RuntimeUnavailable;
            gRuntime.Pawn = player.Pawn;
            gRuntime.MovementComponent = 0;
            return;
        }
    }

    gRuntime.Pawn = gSaved.Pawn;
    gRuntime.MovementComponent = gSaved.MovementComponent;

    const bool noclip = gSettings.FlightMode == Mode::Noclip;
    const float speed = std::clamp(
        noclip ? gSettings.NoclipSpeed : gSettings.VelocitySpeed,
        100.0f,
        8000.0f);
    const bool stateApplied =
        SetActorCollisionBit(
            gSaved.Pawn,
            noclip ? false : gSaved.ActorCollisionEnabled)
        && SetCapsuleCollision(
            gSaved.CapsuleComponent,
            noclip ? NoCollision : gSaved.CapsuleCollisionMode)
        && SetMovementMode(gSaved.MovementComponent, MoveFlying, 0)
        && Memory::Write(
            gSaved.MovementComponent
            + Offsets::CharacterMovement_MaxFlySpeed,
            speed);

    if (!stateApplied) {
        gRuntime.CurrentStatus = Status::UpdateFailed;
        WriteVelocity(gSaved.MovementComponent, {});
        return;
    }

    Vector3 velocity{};
    if (allowInput) {
        bool hasInput = false;
        const Vector3 direction = BuildInputDirection(camera, hasInput);
        gRuntime.HasDirectionalInput = hasInput;
        if (hasInput) {
            velocity = {
                direction.X * static_cast<double>(speed),
                direction.Y * static_cast<double>(speed),
                direction.Z * static_cast<double>(speed)
            };
        }
    }

    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
        velocity = {};

    if (!WriteVelocity(gSaved.MovementComponent, velocity)) {
        gRuntime.CurrentStatus = Status::UpdateFailed;
        return;
    }

    gRuntime.CurrentStatus = Status::Active;
}

void Solarpunk::FlyHack::SuspendInput() {
    if (gSaved.IsCaptured
        && Memory::IsValidPtr(gSaved.MovementComponent)) {
        WriteVelocity(gSaved.MovementComponent, {});
    }

    gRuntime.InputAllowed = false;
    gRuntime.HasDirectionalInput = false;
}

void Solarpunk::FlyHack::Shutdown() {
    RestoreState();
    gSettings.Enabled = false;
    gRuntime = {};
    gRuntime.CurrentStatus = Status::Disabled;
}

const char* Solarpunk::FlyHack::GetModeText(Mode mode) {
    return mode == Mode::Noclip ? "Noclip" : "Velocity";
}

const char* Solarpunk::FlyHack::GetStatusText(Status status) {
    switch (status) {
    case Status::Disabled:
        return "Fly controls disabled";
    case Status::WaitingForPlayer:
        return "Waiting for player and camera";
    case Status::Active:
        return "Fly controls active";
    case Status::RuntimeUnavailable:
        return "Character movement unavailable";
    case Status::UpdateFailed:
        return "Unable to update movement state";
    default:
        return "Fly controls unavailable";
    }
}
