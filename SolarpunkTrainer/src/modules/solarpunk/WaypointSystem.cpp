#include "WaypointSystem.h"
#include "GameSchema.h"

#include <memory/Memory.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

namespace {

    constexpr wchar_t WaypointSettingsSection[] = L"Waypoints";
    constexpr size_t DataPathCapacity = 1024;
    constexpr int MaxClassDepth = 24;
    constexpr int MaxFunctionFieldsPerClass = 2048;
    constexpr uint32_t MaxNameBlocks = 0x2000;
    constexpr uint32_t MaxNameLength = 1023;

    struct RawFName {
        int32_t ComparisonIndex = 0;
        uint32_t Number = 0;
    };

    struct SetActorLocationParams {
        Solarpunk::Vector3 NewLocation{};
        bool Sweep = false;
        uint8_t Pad19[0x7]{};
        std::array<uint8_t, 0x100> SweepHitResult{};
        bool Teleport = true;
        bool ReturnValue = false;
        uint8_t Pad122[0x6]{};
    };

    struct TeleportRequest {
        uint64_t WaypointId = 0;
        Solarpunk::Vector3 Location{};
        std::string Name;
    };

    static_assert(sizeof(SetActorLocationParams) == 0x128);

    std::vector<Solarpunk::WaypointSystem::Waypoint> gWaypoints;
    Solarpunk::WaypointSystem::MarkerStyle gMarkerStyle =
        Solarpunk::WaypointSystem::MarkerStyle::Dot;
    uint64_t gNextWaypointId = 1;
    bool gInitialized = false;

    std::mutex gTeleportMutex;
    std::optional<TeleportRequest> gPendingTeleport;
    Solarpunk::WaypointSystem::TeleportStatus gTeleportStatus{};

    bool IsFinite(const Solarpunk::Vector3& value) {
        return std::isfinite(value.X)
            && std::isfinite(value.Y)
            && std::isfinite(value.Z);
    }

    bool BuildDataPath(
        wchar_t (&path)[DataPathCapacity],
        bool createDirectory) {
        path[0] = L'\0';
        const DWORD length = GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            path,
            static_cast<DWORD>(DataPathCapacity));
        if (!length || length >= DataPathCapacity)
            return false;

        if (wcscat_s(
            path,
            DataPathCapacity,
            L"\\SolarpunkTrainer") != 0) {
            return false;
        }

        if (createDirectory
            && !CreateDirectoryW(path, nullptr)
            && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }

        return wcscat_s(
            path,
            DataPathCapacity,
            L"\\waypoints.ini") == 0;
    }

    bool WriteText(
        const wchar_t* path,
        const wchar_t* section,
        const wchar_t* key,
        const wchar_t* value) {
        return WritePrivateProfileStringW(
            section,
            key,
            value,
            path) != FALSE;
    }

    bool WriteInteger(
        const wchar_t* path,
        const wchar_t* section,
        const wchar_t* key,
        uint64_t value) {
        wchar_t text[32]{};
        swprintf_s(text, L"%llu", static_cast<unsigned long long>(value));
        return WriteText(path, section, key, text);
    }

    bool WriteDouble(
        const wchar_t* path,
        const wchar_t* section,
        const wchar_t* key,
        double value) {
        wchar_t text[64]{};
        swprintf_s(text, L"%.6f", value);
        return WriteText(path, section, key, text);
    }

    std::wstring Utf8ToWide(std::string_view value) {
        if (value.empty())
            return {};

        const int length = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);
        if (length <= 0)
            return {};

        std::wstring wide(static_cast<size_t>(length), L'\0');
        if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            wide.data(),
            length) != length) {
            return {};
        }
        return wide;
    }

    std::string WideToUtf8(std::wstring_view value) {
        if (value.empty())
            return {};

        const int length = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (length <= 0)
            return {};

        std::string utf8(static_cast<size_t>(length), '\0');
        if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            utf8.data(),
            length,
            nullptr,
            nullptr) != length) {
            return {};
        }
        return utf8;
    }

    std::string NormalizeName(std::string_view input) {
        size_t begin = 0;
        while (begin < input.size()
            && static_cast<unsigned char>(input[begin]) <= ' ') {
            ++begin;
        }

        size_t end = input.size();
        while (end > begin
            && static_cast<unsigned char>(input[end - 1]) <= ' ') {
            --end;
        }

        std::string name(input.substr(begin, end - begin));
        if (name.size() > Solarpunk::WaypointSystem::MaximumNameLength)
            name.resize(Solarpunk::WaypointSystem::MaximumNameLength);
        return name;
    }

    double ReadDouble(
        const wchar_t* path,
        const wchar_t* section,
        const wchar_t* key,
        double fallback) {
        wchar_t fallbackText[64]{};
        wchar_t text[64]{};
        swprintf_s(fallbackText, L"%.6f", fallback);
        GetPrivateProfileStringW(
            section,
            key,
            fallbackText,
            text,
            static_cast<DWORD>(std::size(text)),
            path);

        wchar_t* end = nullptr;
        const double value = std::wcstod(text, &end);
        return end != text && std::isfinite(value)
            ? value
            : fallback;
    }

    uint64_t ReadInteger64(
        const wchar_t* path,
        const wchar_t* section,
        const wchar_t* key,
        uint64_t fallback) {
        wchar_t fallbackText[32]{};
        wchar_t text[32]{};
        swprintf_s(
            fallbackText,
            L"%llu",
            static_cast<unsigned long long>(fallback));
        GetPrivateProfileStringW(
            section,
            key,
            fallbackText,
            text,
            static_cast<DWORD>(std::size(text)),
            path);

        wchar_t* end = nullptr;
        const unsigned long long value = std::wcstoull(text, &end, 10);
        return end != text ? static_cast<uint64_t>(value) : fallback;
    }

    bool SaveWaypoints() {
        wchar_t path[DataPathCapacity]{};
        if (!BuildDataPath(path, true))
            return false;

        const int previousCount = GetPrivateProfileIntW(
            WaypointSettingsSection,
            L"Count",
            0,
            path);
        for (int index = 0; index < previousCount; ++index) {
            wchar_t section[48]{};
            swprintf_s(section, L"Waypoint_%d", index);
            WritePrivateProfileStringW(section, nullptr, nullptr, path);
        }

        bool saved = WriteInteger(
            path,
            WaypointSettingsSection,
            L"Count",
            gWaypoints.size());
        saved &= WriteInteger(
            path,
            WaypointSettingsSection,
            L"MarkerStyle",
            gMarkerStyle
                == Solarpunk::WaypointSystem::MarkerStyle::Beacon
                ? 1
                : 0);
        saved &= WriteInteger(
            path,
            WaypointSettingsSection,
            L"NextId",
            gNextWaypointId);

        for (size_t index = 0; index < gWaypoints.size(); ++index) {
            wchar_t section[48]{};
            swprintf_s(
                section,
                L"Waypoint_%zu",
                index);
            const auto& waypoint = gWaypoints[index];
            const std::wstring wideName = Utf8ToWide(waypoint.Name);
            saved &= WriteInteger(
                path,
                section,
                L"Id",
                waypoint.Id);
            saved &= WriteText(
                path,
                section,
                L"Name",
                wideName.c_str());
            saved &= WriteDouble(
                path,
                section,
                L"X",
                waypoint.Location.X);
            saved &= WriteDouble(
                path,
                section,
                L"Y",
                waypoint.Location.Y);
            saved &= WriteDouble(
                path,
                section,
                L"Z",
                waypoint.Location.Z);
            saved &= WriteInteger(
                path,
                section,
                L"Draw",
                waypoint.Draw ? 1 : 0);
        }

        WritePrivateProfileStringW(nullptr, nullptr, nullptr, path);
        return saved;
    }

    bool LoadWaypoints() {
        wchar_t path[DataPathCapacity]{};
        if (!BuildDataPath(path, false)
            || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }

        const int count = std::clamp(
            static_cast<int>(GetPrivateProfileIntW(
                WaypointSettingsSection,
                L"Count",
                0,
                path)),
            0,
            static_cast<int>(
                Solarpunk::WaypointSystem::MaximumWaypoints));
        gMarkerStyle = GetPrivateProfileIntW(
            WaypointSettingsSection,
            L"MarkerStyle",
            0,
            path) == 1
            ? Solarpunk::WaypointSystem::MarkerStyle::Beacon
            : Solarpunk::WaypointSystem::MarkerStyle::Dot;
        gNextWaypointId = (std::max)(
            uint64_t{ 1 },
            ReadInteger64(
                path,
                WaypointSettingsSection,
                L"NextId",
                1));

        gWaypoints.clear();
        gWaypoints.reserve(static_cast<size_t>(count));
        uint64_t maximumId = 0;
        for (int index = 0; index < count; ++index) {
            wchar_t section[48]{};
            swprintf_s(section, L"Waypoint_%d", index);

            wchar_t wideName[256]{};
            GetPrivateProfileStringW(
                section,
                L"Name",
                L"",
                wideName,
                static_cast<DWORD>(std::size(wideName)),
                path);

            Solarpunk::WaypointSystem::Waypoint waypoint{};
            waypoint.Id = ReadInteger64(
                path,
                section,
                L"Id",
                static_cast<uint64_t>(index + 1));
            waypoint.Name = NormalizeName(WideToUtf8(wideName));
            waypoint.Location.X =
                ReadDouble(path, section, L"X", 0.0);
            waypoint.Location.Y =
                ReadDouble(path, section, L"Y", 0.0);
            waypoint.Location.Z =
                ReadDouble(path, section, L"Z", 0.0);
            waypoint.Draw = GetPrivateProfileIntW(
                section,
                L"Draw",
                1,
                path) != 0;

            if (!waypoint.Id
                || waypoint.Name.empty()
                || !IsFinite(waypoint.Location)) {
                continue;
            }

            maximumId = (std::max)(maximumId, waypoint.Id);
            gWaypoints.emplace_back(std::move(waypoint));
        }

        gNextWaypointId = (std::max)(
            gNextWaypointId,
            maximumId + 1);
        return true;
    }

    bool TryReadPointer(uintptr_t address, uintptr_t& value) {
        const auto pointer = Memory::TryRead<uintptr_t>(address);
        if (!pointer || !Memory::IsValidPtr(*pointer)) {
            value = 0;
            return false;
        }
        value = *pointer;
        return true;
    }

    bool TryReadAnsiName(
        uintptr_t address,
        uint32_t length,
        std::string& output) {
        output.resize(length);
        if (!length)
            return true;
        if (!Memory::ReadBytes(address, output.data(), length)) {
            output.clear();
            return false;
        }
        return true;
    }

    bool TryReadWideName(
        uintptr_t address,
        uint32_t length,
        std::string& output) {
        std::vector<wchar_t> wide(length);
        if (length
            && !Memory::ReadBytes(
                address,
                wide.data(),
                static_cast<size_t>(length) * sizeof(wchar_t))) {
            return false;
        }
        output = WideToUtf8(
            std::wstring_view(wide.data(), wide.size()));
        return length == 0 || !output.empty();
    }

    bool TryResolveName(const RawFName& name, std::string& output) {
        output.clear();
        if (name.ComparisonIndex < 0)
            return false;

        const uintptr_t imageBase = Memory::GetModuleBase(nullptr);
        if (!imageBase)
            return false;

        const auto& schema = Solarpunk::GameSchema::Get();
        const uintptr_t namePool =
            imageBase + schema.Globals.GNamesRva;
        const auto currentBlock =
            Memory::TryRead<uint32_t>(namePool + 0x08);
        const auto currentByteCursor =
            Memory::TryRead<uint32_t>(namePool + 0x0C);
        if (!currentBlock
            || !currentByteCursor
            || *currentBlock >= MaxNameBlocks) {
            return false;
        }

        const uint32_t index =
            static_cast<uint32_t>(name.ComparisonIndex);
        const uint32_t blockIndex =
            index >> schema.NamePool.BlockOffsetBits;
        const uint32_t entryIndex =
            index
            & ((1u << schema.NamePool.BlockOffsetBits) - 1u);
        const uint32_t byteOffset =
            entryIndex * schema.NamePool.EntryStride;
        if (blockIndex > *currentBlock
            || blockIndex >= MaxNameBlocks
            || (blockIndex == *currentBlock
                && byteOffset >= *currentByteCursor)) {
            return false;
        }

        uintptr_t block = 0;
        if (!TryReadPointer(
            namePool + 0x10
                + static_cast<uintptr_t>(blockIndex)
                    * sizeof(uintptr_t),
            block)) {
            return false;
        }

        const uintptr_t entry = block + byteOffset;
        const auto header = Memory::TryRead<uint16_t>(entry);
        if (!header)
            return false;

        const bool isWide = (*header & 1u) != 0;
        const uint32_t length = *header >> 6;
        if (!length || length > MaxNameLength)
            return false;

        const bool read = isWide
            ? TryReadWideName(
                entry + sizeof(uint16_t),
                length,
                output)
            : TryReadAnsiName(
                entry + sizeof(uint16_t),
                length,
                output);
        if (!read)
            return false;

        if (name.Number > 0) {
            output.push_back('_');
            output += std::to_string(name.Number - 1);
        }
        return true;
    }

    bool TryReadObjectName(
        uintptr_t object,
        std::string& output) {
        if (!Memory::IsValidPtr(object))
            return false;
        const auto name =
            Memory::TryRead<RawFName>(
                object + Solarpunk::GameSchema::Get().UObject.Name);
        return name && TryResolveName(*name, output);
    }

    uintptr_t FindFunction(
        uintptr_t object,
        std::string_view functionName) {
        uintptr_t currentClass = 0;
        if (!TryReadPointer(
            object + Solarpunk::GameSchema::Get().UObject.Class,
            currentClass)) {
            return 0;
        }

        for (int depth = 0;
            depth < MaxClassDepth;
            ++depth) {
            if (!Memory::IsValidPtr(currentClass))
                return 0;

            uintptr_t field = 0;
            const auto firstField =
                Memory::TryRead<uintptr_t>(
                    currentClass + Solarpunk::GameSchema::Get().UStruct.Children);
            if (firstField)
                field = *firstField;

            for (int index = 0;
                field && index < MaxFunctionFieldsPerClass;
                ++index) {
                if (!Memory::IsValidPtr(field))
                    break;

                std::string fieldName;
                if (TryReadObjectName(field, fieldName)
                    && fieldName == functionName) {
                    return field;
                }

                const auto next =
                    Memory::TryRead<uintptr_t>(
                        field + Solarpunk::GameSchema::Get().UField.Next);
                if (!next || *next == field)
                    break;
                field = *next;
            }

            const auto super =
                Memory::TryRead<uintptr_t>(
                    currentClass
                        + Solarpunk::GameSchema::Get().UStruct.Super);
            if (!super
                || !*super
                || *super == currentClass) {
                break;
            }
            currentClass = *super;
        }
        return 0;
    }

    bool InvokeSetActorLocation(
        uintptr_t pawn,
        const Solarpunk::Vector3& location) {
        const uintptr_t function =
            FindFunction(pawn, "K2_SetActorLocation");
        const uintptr_t imageBase =
            Memory::GetModuleBase(nullptr);
        if (!function || !imageBase)
            return false;

        using ProcessEventFn =
            void(__fastcall*)(uintptr_t, uintptr_t, void*);
        const auto processEvent =
            reinterpret_cast<ProcessEventFn>(
                imageBase
                    + Solarpunk::GameSchema::Get().Globals.ProcessEventRva);

        SetActorLocationParams params{};
        params.NewLocation = location;
        params.Sweep = false;
        params.Teleport = true;

        __try {
            processEvent(pawn, function, &params);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return params.ReturnValue;
    }

    Solarpunk::WaypointSystem::TeleportStatus
    ApplyTeleport(const TeleportRequest& request) {
        Solarpunk::WaypointSystem::TeleportStatus status{};
        status.State =
            Solarpunk::WaypointSystem::TeleportState::Failed;
        status.WaypointId = request.WaypointId;

        const Solarpunk::PlayerSnapshot player =
            Solarpunk::CapturePlayerSnapshot();
        if (!player.HasCoordinates()) {
            status.Message =
                "Player became unavailable before teleporting";
            return status;
        }

        if (!InvokeSetActorLocation(
            player.Pawn,
            request.Location)) {
            status.Message =
                "The game rejected the waypoint teleport";
            return status;
        }

        const Solarpunk::PlayerSnapshot updated =
            Solarpunk::CapturePlayerSnapshot();
        if (!updated.HasCoordinates()) {
            status.Message =
                "Teleported, but the new player position could not be verified";
            return status;
        }

        const double dx =
            updated.Coordinates.X - request.Location.X;
        const double dy =
            updated.Coordinates.Y - request.Location.Y;
        const double dz =
            updated.Coordinates.Z - request.Location.Z;
        const double errorSquared =
            dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(errorSquared)
            || errorSquared > 2500.0) {
            status.Message =
                "The game did not retain the requested waypoint position";
            return status;
        }

        status.State =
            Solarpunk::WaypointSystem::TeleportState::Succeeded;
        status.Message =
            "Teleported to " + request.Name;
        return status;
    }

} // namespace

void Solarpunk::WaypointSystem::Initialize() {
    if (gInitialized)
        return;

    gInitialized = true;
    if (!LoadWaypoints())
        SaveWaypoints();
}

const std::vector<Solarpunk::WaypointSystem::Waypoint>&
Solarpunk::WaypointSystem::GetWaypoints() {
    return gWaypoints;
}

Solarpunk::WaypointSystem::MarkerStyle
Solarpunk::WaypointSystem::GetMarkerStyle() {
    return gMarkerStyle;
}

Solarpunk::WaypointSystem::TeleportStatus
Solarpunk::WaypointSystem::GetTeleportStatus() {
    std::scoped_lock lock(gTeleportMutex);
    return gTeleportStatus;
}

bool Solarpunk::WaypointSystem::AddAtPlayer(
    const std::string& requestedName,
    const PlayerSnapshot& player) {
    if (!player.HasCoordinates()
        || !IsFinite(player.Coordinates)
        || gWaypoints.size() >= MaximumWaypoints) {
        return false;
    }

    std::string name = NormalizeName(requestedName);
    if (name.empty()) {
        name = "Waypoint "
            + std::to_string(gWaypoints.size() + 1);
    }

    Waypoint waypoint{};
    waypoint.Id = gNextWaypointId++;
    waypoint.Name = std::move(name);
    waypoint.Location = player.Coordinates;
    waypoint.Draw = true;
    gWaypoints.emplace_back(std::move(waypoint));

    if (SaveWaypoints())
        return true;

    gWaypoints.pop_back();
    --gNextWaypointId;
    return false;
}

bool Solarpunk::WaypointSystem::SetDraw(
    uint64_t waypointId,
    bool draw) {
    const auto waypoint = std::find_if(
        gWaypoints.begin(),
        gWaypoints.end(),
        [waypointId](const Waypoint& candidate) {
            return candidate.Id == waypointId;
        });
    if (waypoint == gWaypoints.end())
        return false;
    if (waypoint->Draw == draw)
        return true;

    const bool previous = waypoint->Draw;
    waypoint->Draw = draw;
    if (SaveWaypoints())
        return true;

    waypoint->Draw = previous;
    return false;
}

bool Solarpunk::WaypointSystem::SetMarkerStyle(
    MarkerStyle style) {
    if (gMarkerStyle == style)
        return true;

    const MarkerStyle previous = gMarkerStyle;
    gMarkerStyle = style;
    if (SaveWaypoints())
        return true;

    gMarkerStyle = previous;
    return false;
}

bool Solarpunk::WaypointSystem::Delete(
    uint64_t waypointId) {
    const auto waypoint = std::find_if(
        gWaypoints.begin(),
        gWaypoints.end(),
        [waypointId](const Waypoint& candidate) {
            return candidate.Id == waypointId;
        });
    if (waypoint == gWaypoints.end())
        return false;

    Waypoint removed = *waypoint;
    const size_t index = static_cast<size_t>(
        std::distance(gWaypoints.begin(), waypoint));
    gWaypoints.erase(waypoint);
    if (SaveWaypoints())
        return true;

    gWaypoints.insert(
        gWaypoints.begin() + static_cast<ptrdiff_t>(index),
        std::move(removed));
    return false;
}

bool Solarpunk::WaypointSystem::QueueTeleport(
    HWND gameWindow,
    uint64_t waypointId) {
    if (!gameWindow || !IsWindow(gameWindow))
        return false;

    const auto waypoint = std::find_if(
        gWaypoints.begin(),
        gWaypoints.end(),
        [waypointId](const Waypoint& candidate) {
            return candidate.Id == waypointId;
        });
    if (waypoint == gWaypoints.end())
        return false;

    {
        std::scoped_lock lock(gTeleportMutex);
        if (gPendingTeleport)
            return false;

        gPendingTeleport = TeleportRequest{
            waypoint->Id,
            waypoint->Location,
            waypoint->Name
        };
        gTeleportStatus = {
            TeleportState::Pending,
            waypoint->Id,
            "Teleporting on the game thread"
        };
    }

    SetLastError(ERROR_SUCCESS);
    if (PostMessageW(
        gameWindow,
        GetTeleportMessage(),
        0,
        0)) {
        return true;
    }

    const DWORD error = GetLastError();
    std::scoped_lock lock(gTeleportMutex);
    gPendingTeleport.reset();
    gTeleportStatus = {
        TeleportState::Failed,
        waypointId,
        error
            ? "Could not queue the teleport (Win32 "
                + std::to_string(error) + ")"
            : "The hooked game window rejected the teleport request"
    };
    return false;
}

UINT Solarpunk::WaypointSystem::GetTeleportMessage() {
    constexpr UINT WaypointTeleportMessage = WM_APP + 0x053B;
    return WaypointTeleportMessage;
}

void Solarpunk::WaypointSystem::ProcessPendingTeleport() {
    std::optional<TeleportRequest> request;
    {
        std::scoped_lock lock(gTeleportMutex);
        if (!gPendingTeleport)
            return;
        request = gPendingTeleport;
        gPendingTeleport.reset();
    }

    TeleportStatus status = ApplyTeleport(*request);
    std::scoped_lock lock(gTeleportMutex);
    gTeleportStatus = std::move(status);
}

void Solarpunk::WaypointSystem::ResetRuntime() {
    std::scoped_lock lock(gTeleportMutex);
    gPendingTeleport.reset();
    gTeleportStatus = {};
}

const char* Solarpunk::WaypointSystem::GetMarkerStyleText(
    MarkerStyle style) {
    return style == MarkerStyle::Beacon ? "Beacon" : "Dot";
}

const char* Solarpunk::WaypointSystem::GetTeleportStateText(
    TeleportState state) {
    switch (state) {
    case TeleportState::Pending:
        return "TELEPORTING";
    case TeleportState::Succeeded:
        return "TELEPORT COMPLETE";
    case TeleportState::Failed:
        return "TELEPORT FAILED";
    case TeleportState::Idle:
    default:
        return "WAYPOINTS READY";
    }
}
