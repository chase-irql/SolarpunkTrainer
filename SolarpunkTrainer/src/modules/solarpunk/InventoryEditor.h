#pragma once

#include "SolarpunkRuntime.h"

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Solarpunk::InventoryEditor {

    constexpr int MinimumQuantity = 1;
    constexpr int MaximumQuantity = 9999;
    constexpr int MinimumDurability = 0;
    constexpr int MaximumDurability = 9999;
    constexpr double MinimumWaterLevel = 0.0;
    constexpr double MaximumWaterLevel = 9999.0;
    constexpr int HotbarSlotCount = 8;
    constexpr int MaxInventorySlots = 512;

    enum class ScanStatus {
        Ready,
        WaitingForPlayer,
        InventoryUnavailable,
        InvalidInventory,
        InventoryChanging,
        NamePoolUnavailable
    };

    enum class ApplyState {
        Idle,
        Pending,
        Succeeded,
        Failed
    };

    enum class CatalogStatus {
        WaitingForRuntime,
        Scanning,
        Ready,
        Unavailable
    };

    struct CatalogItem {
        uintptr_t ItemClass = 0;
        std::string ClassName;
        std::string DisplayName;
    };

    struct ItemSlot {
        int Index = -1;
        int Quantity = 0;
        int Durability = 0;
        double WaterLevel = 0.0;
        uintptr_t ItemClass = 0;
        bool HasDurability = false;
        bool HasWaterLevel = false;
        bool QuantityLocked = false;
        bool DurabilityLocked = false;
        bool WaterLevelLocked = false;
        int LockedQuantity = 0;
        int LockedDurability = 0;
        double LockedWaterLevel = 0.0;
        std::string ClassName;
        std::string DisplayName;

        bool IsHotbar() const {
            return Index >= 0 && Index < HotbarSlotCount;
        }

        int DisplaySlot() const {
            return IsHotbar() ? Index + 1 : Index - HotbarSlotCount + 1;
        }
    };

    struct State {
        ScanStatus Status = ScanStatus::WaitingForPlayer;
        ApplyState LastApplyState = ApplyState::Idle;

        uintptr_t InventorySystem = 0;
        uintptr_t InventoryData = 0;
        int SlotCount = 0;
        int Capacity = 0;
        int DeclaredSize = 0;
        int OccupiedCount = 0;
        int EmptyCount = 0;
        int LastEditedSlot = -1;

        std::string LastApplyMessage;
        std::vector<ItemSlot> Items;

        bool IsReady() const {
            return Status == ScanStatus::Ready;
        }
    };

    void Update(const PlayerSnapshot& player);
    const State& GetState();
    const std::vector<CatalogItem>& GetCatalog();
    CatalogStatus GetCatalogStatus();

    bool QueueQuantityChange(
        HWND gameWindow,
        int slotIndex,
        int desiredQuantity);
    bool QueueDurabilityChange(
        HWND gameWindow,
        int slotIndex,
        int desiredDurability);
    bool QueueWaterLevelChange(
        HWND gameWindow,
        int slotIndex,
        double desiredWaterLevel);
    bool QueueAddItem(
        HWND gameWindow,
        uintptr_t itemClass,
        int quantity);

    bool SetQuantityLock(
        int slotIndex,
        bool enabled,
        int lockedQuantity);
    bool SetDurabilityLock(
        int slotIndex,
        bool enabled,
        int lockedDurability);
    bool SetWaterLevelLock(
        int slotIndex,
        bool enabled,
        double lockedWaterLevel);
    void MaintainLocks(HWND gameWindow);

    UINT GetApplyMessage();
    void ProcessPendingEdit();
    void Reset();

    const char* GetStatusText(ScanStatus status);
    const char* GetApplyStateText(ApplyState state);
    const char* GetCatalogStatusText(CatalogStatus status);

} // namespace Solarpunk::InventoryEditor
