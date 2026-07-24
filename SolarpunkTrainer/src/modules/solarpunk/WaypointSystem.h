#pragma once

#include "SolarpunkRuntime.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Solarpunk::WaypointSystem {

    constexpr size_t MaximumNameLength = 80;
    constexpr size_t MaximumWaypoints = 256;

    enum class MarkerStyle {
        Dot,
        Beacon
    };

    enum class TeleportState {
        Idle,
        Pending,
        Succeeded,
        Failed
    };

    struct Waypoint {
        uint64_t Id = 0;
        std::string Name;
        Vector3 Location{};
        bool Draw = true;
    };

    struct TeleportStatus {
        TeleportState State = TeleportState::Idle;
        uint64_t WaypointId = 0;
        std::string Message;

        bool IsPending() const {
            return State == TeleportState::Pending;
        }
    };

    void Initialize();

    const std::vector<Waypoint>& GetWaypoints();
    MarkerStyle GetMarkerStyle();
    TeleportStatus GetTeleportStatus();

    bool AddAtPlayer(
        const std::string& name,
        const PlayerSnapshot& player);
    bool SetDraw(uint64_t waypointId, bool draw);
    bool SetMarkerStyle(MarkerStyle style);
    bool Delete(uint64_t waypointId);

    bool QueueTeleport(HWND gameWindow, uint64_t waypointId);
    UINT GetTeleportMessage();
    void ProcessPendingTeleport();
    void ResetRuntime();

    const char* GetMarkerStyleText(MarkerStyle style);
    const char* GetTeleportStateText(TeleportState state);

} // namespace Solarpunk::WaypointSystem
