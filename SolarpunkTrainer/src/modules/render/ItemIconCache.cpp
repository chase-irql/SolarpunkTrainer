#include "ItemIconCache.h"

#include <memory/Memory.h>

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace {

    namespace Offsets {
        // BP_SkyGameInstance_C
        constexpr uintptr_t GameInstance_ItemDatabase = 0x03B0;

        // TMap<UClass*, FS_Item>. The sparse-array element is an eight-byte
        // class key followed by the 0x68-byte FS_Item value.
        constexpr size_t ItemMapElementStride = 0x78;
        constexpr uintptr_t ItemMapElement_Class = 0x00;
        constexpr uintptr_t ItemMapElement_Icon = 0x20;

        // UTexture / FTextureResource / FD3D11Texture. The render-thread
        // resource is consumed from Present so it cannot race the game-thread
        // texture resource during a streaming handoff.
        constexpr uintptr_t Texture_PrivateResourceRenderThread = 0x0138;
        constexpr uintptr_t TextureResource_TextureRhi = 0x0010;
        constexpr uintptr_t D3D11Texture_ShaderResourceView = 0x0090;
    }

    constexpr size_t MaximumItemIcons = 512;
    constexpr int32_t MaximumSparseSlots = 4096;

    struct ItemIconRecord {
        uintptr_t ItemClass = 0;
        uintptr_t Texture = 0;
        uintptr_t TextureResource = 0;
        uintptr_t TextureRhi = 0;
        ID3D11ShaderResourceView* ShaderResourceView = nullptr;
    };

    std::array<ItemIconRecord, MaximumItemIcons> gRecords{};
    size_t gRecordCount = 0;
    uintptr_t gGameInstance = 0;
    uintptr_t gMapData = 0;
    int32_t gMapAllocated = 0;
    int32_t gMapCapacity = 0;
    ID3D11Device* gDevice = nullptr;

    bool TryRetainShaderResourceView(
        uintptr_t address,
        ID3D11Device* expectedDevice,
        ID3D11ShaderResourceView*& retained) {
        retained = nullptr;
        if (!address || !expectedDevice)
            return false;

        __try {
            auto* view = reinterpret_cast<
                ID3D11ShaderResourceView*>(address);
            ID3D11Device* ownerDevice = nullptr;
            view->GetDevice(&ownerDevice);
            const bool compatible = ownerDevice == expectedDevice;
            if (ownerDevice)
                ownerDevice->Release();
            if (!compatible)
                return false;

            view->AddRef();
            retained = view;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            retained = nullptr;
            return false;
        }
    }

    void SafeRelease(ID3D11ShaderResourceView*& view) {
        ID3D11ShaderResourceView* releasing = view;
        view = nullptr;
        if (!releasing)
            return;

        __try {
            releasing->Release();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    void ClearRecords() {
        for (size_t index = 0; index < gRecordCount; ++index)
            SafeRelease(gRecords[index].ShaderResourceView);
        gRecords.fill({});
        gRecordCount = 0;
    }

    bool TryReadMapHeader(
        uintptr_t gameInstance,
        uintptr_t& data,
        int32_t& allocated,
        int32_t& capacity,
        uintptr_t& allocationWords,
        int32_t& allocationBits) {
        data = 0;
        allocated = 0;
        capacity = 0;
        allocationWords = 0;
        allocationBits = 0;
        if (!Memory::IsValidPtr(gameInstance))
            return false;

        const uintptr_t map =
            gameInstance + Offsets::GameInstance_ItemDatabase;
        const auto observedData = Memory::TryRead<uintptr_t>(map);
        const auto observedAllocated =
            Memory::TryRead<int32_t>(map + 0x08);
        const auto observedCapacity =
            Memory::TryRead<int32_t>(map + 0x0C);
        const auto secondaryAllocation =
            Memory::TryRead<uintptr_t>(map + 0x20);
        const auto observedBits =
            Memory::TryRead<int32_t>(map + 0x28);
        const auto observedMaxBits =
            Memory::TryRead<int32_t>(map + 0x2C);
        if (!observedData
            || !observedAllocated
            || !observedCapacity
            || !secondaryAllocation
            || !observedBits
            || !observedMaxBits
            || *observedAllocated < 0
            || *observedAllocated > MaximumSparseSlots
            || *observedCapacity < *observedAllocated
            || *observedCapacity > MaximumSparseSlots
            || *observedBits < 0
            || *observedBits > MaximumSparseSlots
            || *observedMaxBits < *observedBits
            || *observedMaxBits > MaximumSparseSlots) {
            return false;
        }

        if (*observedAllocated > 0
            && (!*observedData || *observedBits <= 0)) {
            return false;
        }

        data = *observedData;
        allocated = *observedAllocated;
        capacity = *observedCapacity;
        allocationBits = *observedBits;
        allocationWords = *secondaryAllocation
            ? *secondaryAllocation
            : map + 0x10;
        return true;
    }

    bool RefreshRecord(ItemIconRecord& record) {
        if (!record.ItemClass
            || !record.Texture
            || !gDevice) {
            SafeRelease(record.ShaderResourceView);
            record.TextureResource = 0;
            record.TextureRhi = 0;
            return false;
        }

        const auto textureResource = Memory::TryRead<uintptr_t>(
            record.Texture
                + Offsets::Texture_PrivateResourceRenderThread);
        const auto textureRhi = textureResource
            ? Memory::TryRead<uintptr_t>(
                *textureResource
                    + Offsets::TextureResource_TextureRhi)
            : std::optional<uintptr_t>{};
        const auto shaderResourceView = textureRhi
            ? Memory::TryRead<uintptr_t>(
                *textureRhi
                    + Offsets::D3D11Texture_ShaderResourceView)
            : std::optional<uintptr_t>{};
        if (!textureResource
            || !*textureResource
            || !textureRhi
            || !*textureRhi
            || !shaderResourceView
            || !*shaderResourceView) {
            SafeRelease(record.ShaderResourceView);
            record.TextureResource = 0;
            record.TextureRhi = 0;
            return false;
        }

        if (record.ShaderResourceView
            && record.TextureResource == *textureResource
            && record.TextureRhi == *textureRhi
            && reinterpret_cast<uintptr_t>(
                record.ShaderResourceView) == *shaderResourceView) {
            return true;
        }

        ID3D11ShaderResourceView* retained = nullptr;
        if (!TryRetainShaderResourceView(
            *shaderResourceView,
            gDevice,
            retained)) {
            SafeRelease(record.ShaderResourceView);
            record.TextureResource = 0;
            record.TextureRhi = 0;
            return false;
        }

        SafeRelease(record.ShaderResourceView);
        record.TextureResource = *textureResource;
        record.TextureRhi = *textureRhi;
        record.ShaderResourceView = retained;
        return true;
    }

    ItemIconRecord* FindRecord(uintptr_t itemClass) {
        const auto begin = gRecords.begin();
        const auto end = begin
            + static_cast<ptrdiff_t>(gRecordCount);
        const auto found = std::lower_bound(
            begin,
            end,
            itemClass,
            [](const ItemIconRecord& record, uintptr_t value) {
                return record.ItemClass < value;
            });
        return found != end && found->ItemClass == itemClass
            ? &*found
            : nullptr;
    }

} // namespace

void ItemIconCache::Update(
    uintptr_t gameInstance,
    ID3D11Device* device) {
    if (!gameInstance || !device) {
        Reset();
        return;
    }

    uintptr_t data = 0;
    int32_t allocated = 0;
    int32_t capacity = 0;
    uintptr_t allocationWords = 0;
    int32_t allocationBits = 0;
    if (!TryReadMapHeader(
        gameInstance,
        data,
        allocated,
        capacity,
        allocationWords,
        allocationBits)) {
        Reset();
        return;
    }

    if (gGameInstance == gameInstance
        && gMapData == data
        && gMapAllocated == allocated
        && gMapCapacity == capacity
        && gDevice == device) {
        return;
    }

    Reset();
    gGameInstance = gameInstance;
    gMapData = data;
    gMapAllocated = allocated;
    gMapCapacity = capacity;
    gDevice = device;
    if (allocated == 0)
        return;

    const size_t wordCount =
        (static_cast<size_t>(allocationBits) + 31u) / 32u;
    if (wordCount == 0 || wordCount > 128)
        return;
    std::array<uint32_t, 128> words{};
    if (!Memory::ReadBytes(
        allocationWords,
        words.data(),
        wordCount * sizeof(uint32_t))) {
        return;
    }

    int32_t visited = 0;
    for (int32_t index = 0;
        index < allocationBits
            && visited < allocated
            && gRecordCount < gRecords.size();
        ++index) {
        const uint32_t mask =
            1u << (static_cast<uint32_t>(index) & 31u);
        if ((words[static_cast<size_t>(index) >> 5u] & mask) == 0)
            continue;
        ++visited;

        const uintptr_t element =
            data + static_cast<uintptr_t>(index)
                * Offsets::ItemMapElementStride;
        const auto itemClass = Memory::TryRead<uintptr_t>(
            element + Offsets::ItemMapElement_Class);
        const auto texture = Memory::TryRead<uintptr_t>(
            element + Offsets::ItemMapElement_Icon);
        if (!itemClass
            || !*itemClass
            || !texture
            || !*texture
            || !Memory::IsValidPtr(*itemClass)
            || !Memory::IsValidPtr(*texture)) {
            continue;
        }

        ItemIconRecord& record = gRecords[gRecordCount++];
        record.ItemClass = *itemClass;
        record.Texture = *texture;
    }

    std::sort(
        gRecords.begin(),
        gRecords.begin()
            + static_cast<ptrdiff_t>(gRecordCount),
        [](const ItemIconRecord& left, const ItemIconRecord& right) {
            return left.ItemClass < right.ItemClass;
        });
}

uintptr_t ItemIconCache::GetTextureId(uintptr_t itemClass) {
    ItemIconRecord* record = FindRecord(itemClass);
    if (!record || !RefreshRecord(*record))
        return 0;
    return reinterpret_cast<uintptr_t>(
        record->ShaderResourceView);
}

void ItemIconCache::Reset() {
    ClearRecords();
    gGameInstance = 0;
    gMapData = 0;
    gMapAllocated = 0;
    gMapCapacity = 0;
    gDevice = nullptr;
}
