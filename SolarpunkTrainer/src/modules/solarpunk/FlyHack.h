#pragma once

#include "SolarpunkRuntime.h"

#include <cstdint>

namespace Solarpunk::FlyHack {

    enum class Mode {
        Velocity = 0,
        Noclip = 1
    };

    enum class Status {
        Disabled,
        WaitingForPlayer,
        Active,
        RuntimeUnavailable,
        UpdateFailed
    };

    struct Settings {
        bool Enabled = false;
        Mode FlightMode = Mode::Velocity;
        float VelocitySpeed = 1200.0f;
        float NoclipSpeed = 1800.0f;
    };

    struct RuntimeState {
        Status CurrentStatus = Status::Disabled;
        uintptr_t Pawn = 0;
        uintptr_t MovementComponent = 0;
        bool InputAllowed = false;
        bool HasDirectionalInput = false;

        bool IsActive() const {
            return CurrentStatus == Status::Active;
        }
    };

    Settings& GetSettings();
    const RuntimeState& GetState();

    void Update(
        const PlayerSnapshot& player,
        const CameraSnapshot& camera,
        float deltaTime,
        bool allowInput);

    void SuspendInput();
    void Shutdown();

    const char* GetModeText(Mode mode);
    const char* GetStatusText(Status status);

} // namespace Solarpunk::FlyHack
