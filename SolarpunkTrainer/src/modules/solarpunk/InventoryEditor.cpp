#include "InventoryEditor.h"
#include "GameSchema.h"

#include <memory/Memory.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

    struct InventoryOffsets {
        uintptr_t CharacterInventorySystem = 0;
        uintptr_t Inventory = 0;
        uintptr_t InventorySize = 0;
        bool Valid = false;
    };

    const InventoryOffsets& GetInventoryOffsets() {
        static const InventoryOffsets offsets = [] {
            InventoryOffsets result{};
            const auto resolve = [](
                std::string_view owner,
                std::string_view name,
                std::string_view expectedClass) -> uintptr_t {
                const auto* property =
                    Solarpunk::GameSchema::FindProperty(
                        owner,
                        name);
                return property
                    && property->Class == expectedClass
                    ? property->Offset
                    : 0;
            };
            result.CharacterInventorySystem = resolve(
                "BP_MainPlayerCharacter_C",
                "InventorySystem",
                "ObjectProperty");
            result.Inventory = resolve(
                "BC_InventorySystem_C",
                "Inventory",
                "ArrayProperty");
            result.InventorySize = resolve(
                "BC_InventorySystem_C",
                "InventorySize",
                "IntProperty");
            result.Valid =
                result.CharacterInventorySystem
                && result.Inventory
                && result.InventorySize;
            return result;
        }();
        return offsets;
    }

    constexpr size_t InventorySlotStride = 0x20;
    constexpr uintptr_t InventorySlot_ItemClass = 0x00;
    constexpr uintptr_t InventorySlot_Quantity = 0x08;
    constexpr int CatalogScanBudget = 4096;
    constexpr int MaximumObjectCount = 5000000;
    constexpr int MaxClassDepth = 24;
    constexpr int MaxFunctionFieldsPerClass = 2048;
    constexpr uint32_t MaxNameBlocks = 0x2000;
    constexpr uint32_t MaxNameLength = 1023;

    struct TArrayHeader {
        uintptr_t Data = 0;
        int32_t Count = 0;
        int32_t Capacity = 0;
    };

    struct GlobalObjectArray {
        uintptr_t Chunks = 0;
        uintptr_t Pad08 = 0;
        int32_t MaxElements = 0;
        int32_t NumElements = 0;
        int32_t MaxChunks = 0;
        int32_t NumChunks = 0;
    };

    struct RawFName {
        int32_t ComparisonIndex = 0;
        uint32_t Number = 0;
    };

    struct RawFString {
        uintptr_t Data = 0;
        int32_t Count = 0;
        int32_t Capacity = 0;
    };

    struct RawInventorySlot {
        uintptr_t ItemClass = 0;
        int32_t Quantity = 0;
        uint32_t Pad0C = 0;
        RawFString AdditionalSaveData{};
    };

    struct OverwriteAndSaveParams {
        RawInventorySlot NewItem{};
        int32_t Index = -1;
        uint32_t Pad24 = 0;
    };

    enum class EditKind {
        Quantity,
        Durability,
        WaterLevel,
        AddItem
    };

    struct EditRequest {
        EditKind Kind = EditKind::Quantity;
        uintptr_t InventorySystem = 0;
        uintptr_t ItemClass = 0;
        int SlotIndex = -1;
        int ExpectedQuantity = 0;
        int ExpectedValue = 0;
        int DesiredValue = 0;
        double ExpectedWaterLevel = 0.0;
        double DesiredWaterLevel = 0.0;
        bool FromLock = false;
    };

    struct SlotLocks {
        uintptr_t InventorySystem = 0;
        uintptr_t ItemClass = 0;
        bool QuantityEnabled = false;
        bool DurabilityEnabled = false;
        bool WaterLevelEnabled = false;
        int Quantity = 0;
        int Durability = 0;
        double WaterLevel = 0.0;

        bool AnyEnabled() const {
            return QuantityEnabled
                || DurabilityEnabled
                || WaterLevelEnabled;
        }
    };

    struct AddItemForPlayerParams {
        RawInventorySlot Item{};
        bool SaveAfterDone = true;
        bool PlayPlopSound = false;
        bool HasLeftover = false;
        uint8_t Pad23[0x5]{};
        RawInventorySlot Leftover{};
        std::array<uint8_t, 0x3C8> BlueprintLocals{};
    };

    enum class CatalogPhase {
        WaitingForRuntime,
        FindItemMaster,
        EnumerateItems,
        Ready,
        Unavailable
    };

    struct EditResult {
        Solarpunk::InventoryEditor::ApplyState State =
            Solarpunk::InventoryEditor::ApplyState::Idle;
        int SlotIndex = -1;
        std::string Message;
    };

    static_assert(sizeof(TArrayHeader) == 0x10);
    static_assert(sizeof(GlobalObjectArray) == 0x20);
    static_assert(sizeof(RawFString) == 0x10);
    static_assert(sizeof(RawInventorySlot) == InventorySlotStride);
    static_assert(sizeof(OverwriteAndSaveParams) == 0x28);
    static_assert(sizeof(AddItemForPlayerParams) == 0x410);

    Solarpunk::InventoryEditor::State gState{};
    std::unordered_map<uintptr_t, std::string> gItemNameCache;
    uintptr_t gLastInventorySystem = 0;

    std::vector<Solarpunk::InventoryEditor::CatalogItem> gCatalog;
    CatalogPhase gCatalogPhase = CatalogPhase::WaitingForRuntime;
    int gCatalogCursor = 0;
    int gCatalogObjectCount = 0;
    uintptr_t gItemMasterClass = 0;
    uintptr_t gBlueprintGeneratedClassMeta = 0;

    std::mutex gEditMutex;
    std::optional<EditRequest> gPendingEdit;
    EditResult gLastEditResult{};
    std::array<
        SlotLocks,
        Solarpunk::InventoryEditor::MaxInventorySlots> gSlotLocks{};
    ULONGLONG gNextLockMaintenanceTick = 0;

    bool TryReadPointer(uintptr_t address, uintptr_t& value) {
        const auto pointer = Memory::TryRead<uintptr_t>(address);
        if (!pointer || !Memory::IsValidPtr(*pointer)) {
            value = 0;
            return false;
        }

        value = *pointer;
        return true;
    }

    bool IsValidArray(const TArrayHeader& array) {
        return array.Count >= 0
            && array.Capacity >= array.Count
            && array.Count <= Solarpunk::InventoryEditor::MaxInventorySlots
            && (array.Count == 0 || Memory::IsValidPtr(array.Data));
    }

    bool TryReadFString(
        const RawFString& value,
        std::wstring& output) {
        output.clear();
        if (value.Count == 0)
            return true;

        constexpr int32_t MaxSavedDataCharacters = 16384;
        if (value.Count < 0
            || value.Capacity < value.Count
            || value.Count > MaxSavedDataCharacters
            || !Memory::IsValidPtr(value.Data)) {
            return false;
        }

        std::vector<wchar_t> buffer(
            static_cast<size_t>(value.Count));
        if (!Memory::ReadBytes(
            value.Data,
            buffer.data(),
            buffer.size() * sizeof(wchar_t))) {
            return false;
        }

        size_t length = buffer.size();
        if (length && buffer.back() == L'\0')
            --length;
        output.assign(buffer.data(), length);
        return true;
    }

    bool EqualsInsensitive(
        std::wstring_view left,
        std::wstring_view right) {
        if (left.size() != right.size())
            return false;

        for (size_t index = 0; index < left.size(); ++index) {
            if (std::towlower(left[index])
                != std::towlower(right[index])) {
                return false;
            }
        }
        return true;
    }

    bool FindJsonIntegerField(
        std::wstring_view json,
        std::wstring_view field,
        size_t& valueBegin,
        size_t& valueEnd,
        int& value) {
        valueBegin = 0;
        valueEnd = 0;
        value = 0;

        size_t cursor = 0;
        while (cursor < json.size()) {
            const size_t keyBegin = json.find(L'"', cursor);
            if (keyBegin == std::wstring_view::npos)
                return false;

            const size_t keyEnd = json.find(L'"', keyBegin + 1);
            if (keyEnd == std::wstring_view::npos)
                return false;

            cursor = keyEnd + 1;
            if (!EqualsInsensitive(
                json.substr(keyBegin + 1, keyEnd - keyBegin - 1),
                field)) {
                continue;
            }

            size_t separator = cursor;
            while (separator < json.size()
                && std::iswspace(json[separator])) {
                ++separator;
            }
            if (separator >= json.size() || json[separator] != L':')
                continue;

            valueBegin = separator + 1;
            while (valueBegin < json.size()
                && std::iswspace(json[valueBegin])) {
                ++valueBegin;
            }

            bool negative = false;
            if (valueBegin < json.size() && json[valueBegin] == L'-') {
                negative = true;
                ++valueBegin;
            }

            valueEnd = valueBegin;
            int64_t parsed = 0;
            while (valueEnd < json.size()
                && std::iswdigit(json[valueEnd])) {
                parsed = parsed * 10
                    + static_cast<int64_t>(json[valueEnd] - L'0');
                if (parsed
                    > static_cast<int64_t>(
                        (std::numeric_limits<int>::max)())) {
                    return false;
                }
                ++valueEnd;
            }
            if (valueEnd == valueBegin)
                return false;

            if (negative)
                --valueBegin;
            value = static_cast<int>(negative ? -parsed : parsed);
            return true;
        }

        return false;
    }

    bool TryGetDurability(
        const RawInventorySlot& slot,
        int& durability) {
        std::wstring savedData;
        if (!TryReadFString(slot.AdditionalSaveData, savedData))
            return false;

        size_t valueBegin = 0;
        size_t valueEnd = 0;
        return FindJsonIntegerField(
            savedData,
            L"durability",
            valueBegin,
            valueEnd,
            durability);
    }

    bool TryBuildDurabilitySavedData(
        const RawInventorySlot& slot,
        int desiredDurability,
        std::wstring& savedData,
        int& currentDurability) {
        if (!TryReadFString(slot.AdditionalSaveData, savedData))
            return false;

        size_t valueBegin = 0;
        size_t valueEnd = 0;
        if (!FindJsonIntegerField(
            savedData,
            L"durability",
            valueBegin,
            valueEnd,
            currentDurability)) {
            return false;
        }

        savedData.replace(
            valueBegin,
            valueEnd - valueBegin,
            std::to_wstring(desiredDurability));
        return true;
    }

    constexpr std::array<std::wstring_view, 4> WaterLevelFields{
        L"Water Level",
        L"WaterLevel",
        L"Water_Level",
        L"CurWaterLevel"
    };

    constexpr double WaterLevelEpsilon = 0.0005;

    bool WaterLevelsEqual(double left, double right) {
        return std::fabs(left - right) <= WaterLevelEpsilon;
    }

    bool FindJsonNumberField(
        std::wstring_view json,
        std::wstring_view field,
        size_t& valueBegin,
        size_t& valueEnd,
        double& value) {
        valueBegin = 0;
        valueEnd = 0;
        value = 0.0;

        size_t cursor = 0;
        while (cursor < json.size()) {
            const size_t keyBegin = json.find(L'"', cursor);
            if (keyBegin == std::wstring_view::npos)
                return false;

            const size_t keyEnd = json.find(L'"', keyBegin + 1);
            if (keyEnd == std::wstring_view::npos)
                return false;

            cursor = keyEnd + 1;
            if (!EqualsInsensitive(
                json.substr(keyBegin + 1, keyEnd - keyBegin - 1),
                field)) {
                continue;
            }

            size_t separator = cursor;
            while (separator < json.size()
                && std::iswspace(json[separator])) {
                ++separator;
            }
            if (separator >= json.size() || json[separator] != L':')
                continue;

            valueBegin = separator + 1;
            while (valueBegin < json.size()
                && std::iswspace(json[valueBegin])) {
                ++valueBegin;
            }

            valueEnd = valueBegin;
            if (valueEnd < json.size()
                && (json[valueEnd] == L'-'
                    || json[valueEnd] == L'+')) {
                ++valueEnd;
            }

            bool hasDigits = false;
            while (valueEnd < json.size()
                && std::iswdigit(json[valueEnd])) {
                hasDigits = true;
                ++valueEnd;
            }

            if (valueEnd < json.size() && json[valueEnd] == L'.') {
                ++valueEnd;
                while (valueEnd < json.size()
                    && std::iswdigit(json[valueEnd])) {
                    hasDigits = true;
                    ++valueEnd;
                }
            }
            if (!hasDigits)
                return false;

            if (valueEnd < json.size()
                && (json[valueEnd] == L'e'
                    || json[valueEnd] == L'E')) {
                size_t exponentEnd = valueEnd + 1;
                if (exponentEnd < json.size()
                    && (json[exponentEnd] == L'-'
                        || json[exponentEnd] == L'+')) {
                    ++exponentEnd;
                }

                const size_t exponentDigits = exponentEnd;
                while (exponentEnd < json.size()
                    && std::iswdigit(json[exponentEnd])) {
                    ++exponentEnd;
                }
                if (exponentEnd == exponentDigits)
                    return false;
                valueEnd = exponentEnd;
            }

            const std::wstring token(
                json.substr(valueBegin, valueEnd - valueBegin));
            wchar_t* parsedEnd = nullptr;
            errno = 0;
            const double parsed = std::wcstod(
                token.c_str(),
                &parsedEnd);
            if (errno == ERANGE
                || !std::isfinite(parsed)
                || parsedEnd != token.c_str() + token.size()) {
                return false;
            }

            value = parsed;
            return true;
        }

        return false;
    }

    bool FindWaterLevelField(
        std::wstring_view savedData,
        size_t& valueBegin,
        size_t& valueEnd,
        double& waterLevel) {
        for (const std::wstring_view field : WaterLevelFields) {
            if (FindJsonNumberField(
                savedData,
                field,
                valueBegin,
                valueEnd,
                waterLevel)) {
                return true;
            }
        }
        return false;
    }

    std::wstring FormatWaterLevel(double waterLevel) {
        char buffer[64]{};
        const auto [end, error] = std::to_chars(
            buffer,
            buffer + sizeof(buffer),
            waterLevel,
            std::chars_format::fixed,
            6);
        if (error != std::errc{})
            return L"0";

        std::string formatted(buffer, end);
        while (!formatted.empty() && formatted.back() == '0')
            formatted.pop_back();
        if (!formatted.empty() && formatted.back() == '.')
            formatted.pop_back();
        if (formatted.empty())
            return L"0";
        return std::wstring(formatted.begin(), formatted.end());
    }

    bool TryGetWaterLevel(
        const RawInventorySlot& slot,
        double& waterLevel) {
        std::wstring savedData;
        if (!TryReadFString(slot.AdditionalSaveData, savedData))
            return false;

        size_t valueBegin = 0;
        size_t valueEnd = 0;
        return FindWaterLevelField(
            savedData,
            valueBegin,
            valueEnd,
            waterLevel);
    }

    bool TryBuildWaterLevelSavedData(
        const RawInventorySlot& slot,
        double desiredWaterLevel,
        std::wstring& savedData,
        double& currentWaterLevel) {
        if (!TryReadFString(slot.AdditionalSaveData, savedData))
            return false;

        size_t valueBegin = 0;
        size_t valueEnd = 0;
        if (!FindWaterLevelField(
            savedData,
            valueBegin,
            valueEnd,
            currentWaterLevel)) {
            return false;
        }

        savedData.replace(
            valueBegin,
            valueEnd - valueBegin,
            FormatWaterLevel(desiredWaterLevel));
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

        if (!length) {
            output.clear();
            return true;
        }

        const int utf8Length = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide.data(),
            static_cast<int>(length),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (utf8Length <= 0)
            return false;

        output.resize(static_cast<size_t>(utf8Length));
        return WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide.data(),
            static_cast<int>(length),
            output.data(),
            utf8Length,
            nullptr,
            nullptr) == utf8Length;
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
        const auto currentBlock = Memory::TryRead<uint32_t>(namePool + 0x08);
        const auto currentByteCursor =
            Memory::TryRead<uint32_t>(namePool + 0x0C);
        if (!currentBlock
            || !currentByteCursor
            || *currentBlock >= MaxNameBlocks) {
            return false;
        }

        const uint32_t index = static_cast<uint32_t>(name.ComparisonIndex);
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
                + static_cast<uintptr_t>(blockIndex) * sizeof(uintptr_t),
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
            ? TryReadWideName(entry + sizeof(uint16_t), length, output)
            : TryReadAnsiName(entry + sizeof(uint16_t), length, output);
        if (!read)
            return false;

        if (name.Number > 0) {
            output.push_back('_');
            output += std::to_string(name.Number - 1);
        }

        return true;
    }

    bool TryReadObjectName(uintptr_t object, std::string& output) {
        if (!Memory::IsValidPtr(object))
            return false;

        const auto name =
            Memory::TryRead<RawFName>(
                object + Solarpunk::GameSchema::Get().UObject.Name);
        return name && TryResolveName(*name, output);
    }

    std::string MakeDisplayName(const std::string& className) {
        std::string trimmed = className;
        if (trimmed.rfind("BP_", 0) == 0)
            trimmed.erase(0, 3);

        constexpr std::array<std::string_view, 3> suffixes{
            "_Item_C",
            "_Item",
            "_C"
        };
        for (const std::string_view suffix : suffixes) {
            if (trimmed.size() >= suffix.size()
                && trimmed.compare(
                    trimmed.size() - suffix.size(),
                    suffix.size(),
                    suffix) == 0) {
                trimmed.erase(trimmed.size() - suffix.size());
                break;
            }
        }

        std::string display;
        display.reserve(trimmed.size() + 8);
        for (size_t index = 0; index < trimmed.size(); ++index) {
            const unsigned char current =
                static_cast<unsigned char>(trimmed[index]);
            if (current == '_') {
                if (!display.empty() && display.back() != ' ')
                    display.push_back(' ');
                continue;
            }

            if (index > 0
                && std::isupper(current)
                && std::islower(
                    static_cast<unsigned char>(trimmed[index - 1]))
                && !display.empty()
                && display.back() != ' ') {
                display.push_back(' ');
            }

            display.push_back(static_cast<char>(current));
        }

        return display.empty() ? className : display;
    }

    bool TryGetItemClassName(uintptr_t itemClass, std::string& className) {
        const auto cached = gItemNameCache.find(itemClass);
        if (cached != gItemNameCache.end()) {
            className = cached->second;
            return true;
        }

        if (!TryReadObjectName(itemClass, className))
            return false;

        gItemNameCache.emplace(itemClass, className);
        return true;
    }

    bool IsValidObjectArray(const GlobalObjectArray& objects) {
        return Memory::IsValidPtr(objects.Chunks)
            && objects.NumElements > 0
            && objects.NumElements <= objects.MaxElements
            && objects.NumElements <= MaximumObjectCount
            && objects.NumChunks > 0
            && objects.NumChunks <= objects.MaxChunks
            && objects.NumChunks
                <= (MaximumObjectCount
                    + static_cast<int>(
                        Solarpunk::GameSchema::Get().ObjectArray.ChunkSize)
                    - 1)
                    / static_cast<int>(
                        Solarpunk::GameSchema::Get().ObjectArray.ChunkSize);
    }

    bool TryReadGlobalObjectArray(
        uintptr_t address,
        GlobalObjectArray& output) {
        const auto& layout = Solarpunk::GameSchema::Get().ObjectArray;
        const auto chunks = Memory::TryRead<uintptr_t>(
            address + layout.Objects);
        const auto maxElements = Memory::TryRead<int32_t>(
            address + layout.MaxElements);
        const auto numElements = Memory::TryRead<int32_t>(
            address + layout.NumElements);
        const auto maxChunks = Memory::TryRead<int32_t>(
            address + layout.MaxChunks);
        const auto numChunks = Memory::TryRead<int32_t>(
            address + layout.NumChunks);
        if (!chunks
            || !maxElements
            || !numElements
            || !maxChunks
            || !numChunks) {
            return false;
        }

        output.Chunks = *chunks;
        output.MaxElements = *maxElements;
        output.NumElements = *numElements;
        output.MaxChunks = *maxChunks;
        output.NumChunks = *numChunks;
        return IsValidObjectArray(output);
    }

    bool TryGetGlobalObject(
        const GlobalObjectArray& objects,
        int index,
        uintptr_t& object) {
        object = 0;
        if (index < 0 || index >= objects.NumElements)
            return false;

        const auto& layout = Solarpunk::GameSchema::Get().ObjectArray;
        const int chunkIndex =
            index / static_cast<int>(layout.ChunkSize);
        const int withinChunk =
            index % static_cast<int>(layout.ChunkSize);
        if (chunkIndex >= objects.NumChunks)
            return false;

        uintptr_t chunk = 0;
        if (!TryReadPointer(
            objects.Chunks
                + static_cast<uintptr_t>(chunkIndex)
                    * sizeof(uintptr_t),
            chunk)) {
            return false;
        }

        return TryReadPointer(
            chunk
                + static_cast<uintptr_t>(withinChunk)
                    * layout.ItemSize
                + layout.ItemObject,
            object);
    }

    bool IsDerivedFrom(
        uintptr_t candidate,
        uintptr_t baseClass) {
        uintptr_t current = candidate;
        for (int depth = 0; depth < MaxClassDepth; ++depth) {
            if (!Memory::IsValidPtr(current))
                return false;
            if (current == baseClass)
                return true;

            const auto super = Memory::TryRead<uintptr_t>(
                current + Solarpunk::GameSchema::Get().UStruct.Super);
            if (!super || !*super || *super == current)
                return false;
            current = *super;
        }
        return false;
    }

    bool IsConcreteCatalogName(std::string_view className) {
        if (className.empty()
            || className.find("MASTER") != std::string_view::npos
            || className.find("Master") != std::string_view::npos
            || className.find("Default__") != std::string_view::npos) {
            return false;
        }

        return className.size() > 2
            && className.ends_with("_C");
    }

    void ResetCatalogScan() {
        gCatalog.clear();
        gCatalogPhase = CatalogPhase::FindItemMaster;
        gCatalogCursor = 0;
        gCatalogObjectCount = 0;
        gItemMasterClass = 0;
        gBlueprintGeneratedClassMeta = 0;
    }

    void UpdateCatalog() {
        const uintptr_t imageBase = Memory::GetModuleBase(nullptr);
        if (!imageBase) {
            gCatalogPhase = CatalogPhase::WaitingForRuntime;
            return;
        }

        GlobalObjectArray objects{};
        if (!TryReadGlobalObjectArray(
            imageBase + Solarpunk::GameSchema::Get().Globals.GObjectsRva,
            objects)) {
            gCatalogPhase = CatalogPhase::Unavailable;
            return;
        }

        if (gCatalogPhase == CatalogPhase::WaitingForRuntime
            || gCatalogPhase == CatalogPhase::Unavailable) {
            ResetCatalogScan();
        }

        if (gCatalogPhase == CatalogPhase::Ready) {
            return;
        }

        int budget = CatalogScanBudget;
        if (gCatalogPhase == CatalogPhase::FindItemMaster) {
            while (budget-- > 0
                && gCatalogCursor < objects.NumElements) {
                uintptr_t object = 0;
                if (TryGetGlobalObject(objects, gCatalogCursor, object)) {
                    std::string objectName;
                    if (TryReadObjectName(object, objectName)
                        && objectName == "_BP_ItemActor_MASTER_C") {
                        uintptr_t metaClass = 0;
                        if (!TryReadPointer(
                            object + Solarpunk::GameSchema::Get().UObject.Class,
                            metaClass)) {
                            gCatalogPhase = CatalogPhase::Unavailable;
                            return;
                        }

                        gItemMasterClass = object;
                        gBlueprintGeneratedClassMeta = metaClass;
                        gCatalogCursor = 0;
                        gCatalogPhase = CatalogPhase::EnumerateItems;
                        break;
                    }
                }
                ++gCatalogCursor;
            }

            if (gCatalogPhase == CatalogPhase::FindItemMaster
                && gCatalogCursor >= objects.NumElements) {
                gCatalogPhase = CatalogPhase::Unavailable;
                return;
            }
        }

        if (gCatalogPhase != CatalogPhase::EnumerateItems)
            return;

        budget = CatalogScanBudget;
        while (budget-- > 0
            && gCatalogCursor < objects.NumElements) {
            uintptr_t object = 0;
            if (TryGetGlobalObject(objects, gCatalogCursor, object)) {
                const auto metaClass = Memory::TryRead<uintptr_t>(
                    object + Solarpunk::GameSchema::Get().UObject.Class);
                if (metaClass
                    && *metaClass == gBlueprintGeneratedClassMeta
                    && object != gItemMasterClass
                    && IsDerivedFrom(object, gItemMasterClass)) {
                    std::string className;
                    if (TryGetItemClassName(object, className)
                        && IsConcreteCatalogName(className)) {
                        Solarpunk::InventoryEditor::CatalogItem item{};
                        item.ItemClass = object;
                        item.ClassName = className;
                        item.DisplayName = MakeDisplayName(className);
                        gCatalog.push_back(std::move(item));
                    }
                }
            }
            ++gCatalogCursor;
        }

        if (gCatalogCursor < objects.NumElements)
            return;

        std::sort(
            gCatalog.begin(),
            gCatalog.end(),
            [](const auto& left, const auto& right) {
                if (left.DisplayName == right.DisplayName)
                    return left.ClassName < right.ClassName;
                return left.DisplayName < right.DisplayName;
            });
        gCatalog.erase(
            std::unique(
                gCatalog.begin(),
                gCatalog.end(),
                [](const auto& left, const auto& right) {
                    return left.ItemClass == right.ItemClass;
                }),
            gCatalog.end());

        gCatalogObjectCount = objects.NumElements;
        gCatalogPhase = CatalogPhase::Ready;
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

        for (int depth = 0; depth < MaxClassDepth; ++depth) {
            if (!Memory::IsValidPtr(currentClass))
                return 0;

            uintptr_t field = 0;
            const auto firstField = Memory::TryRead<uintptr_t>(
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

                const auto next = Memory::TryRead<uintptr_t>(
                    field + Solarpunk::GameSchema::Get().UField.Next);
                if (!next || *next == field)
                    break;
                field = *next;
            }

            const auto super = Memory::TryRead<uintptr_t>(
                currentClass + Solarpunk::GameSchema::Get().UStruct.Super);
            if (!super || !*super || *super == currentClass)
                break;
            currentClass = *super;
        }

        return 0;
    }

    bool InvokeProcessEvent(
        uintptr_t inventorySystem,
        uintptr_t function,
        void* params) {
        const uintptr_t imageBase = Memory::GetModuleBase(nullptr);
        if (!imageBase)
            return false;

        using ProcessEventFn =
            void(__fastcall*)(uintptr_t, uintptr_t, void*);
        const auto processEvent = reinterpret_cast<ProcessEventFn>(
            imageBase
                + Solarpunk::GameSchema::Get().Globals.ProcessEventRva);

        __try {
            processEvent(inventorySystem, function, params);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    int CountEmptySlots(const TArrayHeader& inventory) {
        int emptySlots = 0;
        for (int index = 0; index < inventory.Count; ++index) {
            const auto slot = Memory::TryRead<RawInventorySlot>(
                inventory.Data
                    + static_cast<uintptr_t>(index)
                        * InventorySlotStride);
            if (slot
                && (!slot->ItemClass || slot->Quantity <= 0)) {
                ++emptySlots;
            }
        }
        return emptySlots;
    }

    int64_t GetTotalQuantityForClass(
        const TArrayHeader& inventory,
        uintptr_t itemClass) {
        int64_t total = 0;
        for (int index = 0; index < inventory.Count; ++index) {
            const auto slot = Memory::TryRead<RawInventorySlot>(
                inventory.Data
                    + static_cast<uintptr_t>(index)
                        * InventorySlotStride);
            if (slot
                && slot->ItemClass == itemClass
                && slot->Quantity > 0) {
                total += slot->Quantity;
            }
        }
        return total;
    }

    EditResult ApplyEdit(const EditRequest& request) {
        EditResult result{};
        result.State = Solarpunk::InventoryEditor::ApplyState::Failed;
        result.SlotIndex = request.SlotIndex;
        if (!GetInventoryOffsets().Valid) {
            result.Message =
                "Inventory property contract is incompatible with this build";
            return result;
        }

        const Solarpunk::PlayerSnapshot player =
            Solarpunk::CapturePlayerSnapshot();
        if (!player.HasCoordinates()) {
            result.Message = "Player became unavailable before the edit";
            return result;
        }

        uintptr_t inventorySystem = 0;
        if (!TryReadPointer(
            player.Pawn
                + GetInventoryOffsets().CharacterInventorySystem,
            inventorySystem)
            || inventorySystem != request.InventorySystem) {
            result.Message = "Inventory changed; refresh and try again";
            return result;
        }

        const auto inventory = Memory::TryRead<TArrayHeader>(
            inventorySystem + GetInventoryOffsets().Inventory);
        if (!inventory || !IsValidArray(*inventory)) {
            result.Message = "The player inventory is no longer valid";
            return result;
        }

        if (request.Kind == EditKind::AddItem) {
            if (!Memory::IsValidPtr(request.ItemClass)
                || request.DesiredValue < 1
                || request.DesiredValue
                    > Solarpunk::InventoryEditor::MaximumQuantity
                || !gItemMasterClass
                || !IsDerivedFrom(
                    request.ItemClass,
                    gItemMasterClass)) {
                result.Message =
                    "The selected item class is no longer available";
                return result;
            }

            if (CountEmptySlots(*inventory) <= 0) {
                result.Message =
                    "No empty inventory slot is available for a new item";
                return result;
            }

            const int64_t quantityBefore =
                GetTotalQuantityForClass(
                    *inventory,
                    request.ItemClass);
            const uintptr_t function = FindFunction(
                inventorySystem,
                "AddItemForPlayer");
            if (!function) {
                result.Message =
                    "Could not resolve the native add-item function";
                return result;
            }

            // AddItemForPlayer validates the item class, generates default
            // AdditionalSavedata (including tool durability), chooses a free
            // slot/stack, and saves the inventory.
            AddItemForPlayerParams params{};
            params.Item.ItemClass = request.ItemClass;
            params.Item.Quantity = request.DesiredValue;
            params.SaveAfterDone = true;
            params.PlayPlopSound = false;
            if (!InvokeProcessEvent(
                inventorySystem,
                function,
                &params)) {
                result.Message = "The game rejected the add-item request";
                return result;
            }

            const auto updatedInventory = Memory::TryRead<TArrayHeader>(
                inventorySystem + GetInventoryOffsets().Inventory);
            if (!updatedInventory
                || !IsValidArray(*updatedInventory)) {
                result.Message =
                    "The item was added but verification failed";
                return result;
            }

            const int64_t quantityAfter =
                GetTotalQuantityForClass(
                    *updatedInventory,
                    request.ItemClass);
            if (quantityAfter <= quantityBefore) {
                result.Message = params.HasLeftover
                    ? "The game could not fit the requested item quantity"
                    : "The game did not retain the added item";
                return result;
            }

            result.State =
                Solarpunk::InventoryEditor::ApplyState::Succeeded;
            result.Message =
                "Item added through the game's save-aware player inventory path";
            return result;
        }

        if (request.SlotIndex < 0
            || request.SlotIndex >= inventory->Count) {
            result.Message = "The selected inventory slot is no longer valid";
            return result;
        }

        const uintptr_t slotAddress =
            inventory->Data
            + static_cast<uintptr_t>(request.SlotIndex)
                * InventorySlotStride;
        const auto currentSlot =
            Memory::TryRead<RawInventorySlot>(slotAddress);
        if (!currentSlot
            || currentSlot->ItemClass != request.ItemClass
            || currentSlot->Quantity != request.ExpectedQuantity) {
            result.Message = "The item changed in-game; review its current values";
            return result;
        }

        std::wstring updatedSavedData;
        if (request.Kind == EditKind::Quantity) {
            if (currentSlot->Quantity != request.ExpectedValue) {
                result.Message =
                    "The stack changed in-game; review its new amount";
                return result;
            }
        }
        else if (request.Kind == EditKind::Durability) {
            int currentDurability = 0;
            if (!TryBuildDurabilitySavedData(
                *currentSlot,
                request.DesiredValue,
                updatedSavedData,
                currentDurability)) {
                result.Message =
                    "This item no longer has a durability attribute";
                return result;
            }
            if (currentDurability != request.ExpectedValue) {
                result.Message =
                    "Durability changed in-game; review its new value";
                return result;
            }
        }
        else {
            double currentWaterLevel = 0.0;
            if (!TryBuildWaterLevelSavedData(
                *currentSlot,
                request.DesiredWaterLevel,
                updatedSavedData,
                currentWaterLevel)) {
                result.Message =
                    "This item no longer has a water-level attribute";
                return result;
            }
            if (!WaterLevelsEqual(
                currentWaterLevel,
                request.ExpectedWaterLevel)) {
                result.Message =
                    "Water level changed in-game; review its new value";
                return result;
            }
        }

        const uintptr_t function = FindFunction(
            inventorySystem,
            "OverwriteAndSaveItemAtIndex");
        if (!function) {
            result.Message = "Could not resolve the inventory update function";
            return result;
        }

        OverwriteAndSaveParams params{};
        params.NewItem = *currentSlot;
        if (request.Kind == EditKind::Quantity) {
            params.NewItem.Quantity = request.DesiredValue;
        }
        else {
            params.NewItem.AdditionalSaveData.Data =
                reinterpret_cast<uintptr_t>(updatedSavedData.data());
            params.NewItem.AdditionalSaveData.Count =
                static_cast<int32_t>(updatedSavedData.size() + 1);
            params.NewItem.AdditionalSaveData.Capacity =
                params.NewItem.AdditionalSaveData.Count;
        }
        params.Index = request.SlotIndex;
        if (!InvokeProcessEvent(
            inventorySystem,
            function,
            &params)) {
            result.Message = "The game rejected the inventory update";
            return result;
        }

        const auto updatedInventory = Memory::TryRead<TArrayHeader>(
            inventorySystem + GetInventoryOffsets().Inventory);
        if (!updatedInventory
            || !IsValidArray(*updatedInventory)
            || request.SlotIndex >= updatedInventory->Count) {
            result.Message = "Inventory updated but verification failed";
            return result;
        }

        const uintptr_t updatedSlotAddress =
            updatedInventory->Data
            + static_cast<uintptr_t>(request.SlotIndex)
                * InventorySlotStride;
        const auto updatedSlot =
            Memory::TryRead<RawInventorySlot>(updatedSlotAddress);
        if (!updatedSlot
            || updatedSlot->ItemClass != request.ItemClass) {
            result.Message = "Inventory updated but the item moved";
            return result;
        }

        if (request.Kind == EditKind::Quantity) {
            if (updatedSlot->Quantity != request.DesiredValue) {
                result.Message =
                    "The game did not retain the requested amount";
                return result;
            }
        }
        else if (request.Kind == EditKind::Durability) {
            int updatedDurability = 0;
            if (!TryGetDurability(*updatedSlot, updatedDurability)
                || updatedDurability != request.DesiredValue) {
                result.Message =
                    "The game did not retain the requested durability";
                return result;
            }
        }
        else {
            double updatedWaterLevel = 0.0;
            if (!TryGetWaterLevel(*updatedSlot, updatedWaterLevel)
                || !WaterLevelsEqual(
                    updatedWaterLevel,
                    request.DesiredWaterLevel)) {
                result.Message =
                    "The game did not retain the requested water level";
                return result;
            }
        }

        result.State =
            Solarpunk::InventoryEditor::ApplyState::Succeeded;
        if (request.Kind == EditKind::Quantity) {
            result.Message = request.FromLock
                ? "Quantity lock restored the saved value"
                : "Quantity applied through the save-aware inventory path";
        }
        else if (request.Kind == EditKind::Durability) {
            result.Message = request.FromLock
                ? "Durability lock restored the saved value"
                : "Durability applied through the save-aware inventory path";
        }
        else {
            result.Message = request.FromLock
                ? "Water-level lock restored the saved value"
                : "Water level applied through the save-aware inventory path";
        }
        return result;
    }

    void ReconcileLocks(Solarpunk::InventoryEditor::State& state) {
        if (state.Status
            != Solarpunk::InventoryEditor::ScanStatus::Ready) {
            return;
        }

        std::array<
            Solarpunk::InventoryEditor::ItemSlot*,
            Solarpunk::InventoryEditor::MaxInventorySlots> itemsBySlot{};
        for (auto& item : state.Items) {
            if (item.Index >= 0
                && item.Index
                    < Solarpunk::InventoryEditor::MaxInventorySlots) {
                itemsBySlot[static_cast<size_t>(item.Index)] = &item;
            }
        }

        std::scoped_lock lock(gEditMutex);
        for (size_t index = 0; index < gSlotLocks.size(); ++index) {
            SlotLocks& slotLocks = gSlotLocks[index];
            if (!slotLocks.AnyEnabled())
                continue;

            auto* item = itemsBySlot[index];
            if (!item
                || slotLocks.InventorySystem != state.InventorySystem
                || slotLocks.ItemClass != item->ItemClass) {
                slotLocks = {};
                continue;
            }

            if (!item->HasDurability)
                slotLocks.DurabilityEnabled = false;
            if (!item->HasWaterLevel)
                slotLocks.WaterLevelEnabled = false;
            if (!slotLocks.AnyEnabled()) {
                slotLocks = {};
                continue;
            }

            item->QuantityLocked = slotLocks.QuantityEnabled;
            item->DurabilityLocked = slotLocks.DurabilityEnabled;
            item->WaterLevelLocked = slotLocks.WaterLevelEnabled;
            item->LockedQuantity = slotLocks.Quantity;
            item->LockedDurability = slotLocks.Durability;
            item->LockedWaterLevel = slotLocks.WaterLevel;
        }
    }

    bool IsLockRequestActive(const EditRequest& request) {
        if (!request.FromLock)
            return true;
        if (request.SlotIndex < 0
            || request.SlotIndex
                >= Solarpunk::InventoryEditor::MaxInventorySlots) {
            return false;
        }

        const SlotLocks& slotLocks =
            gSlotLocks[static_cast<size_t>(request.SlotIndex)];
        if (slotLocks.InventorySystem != request.InventorySystem
            || slotLocks.ItemClass != request.ItemClass) {
            return false;
        }

        switch (request.Kind) {
        case EditKind::Quantity:
            return slotLocks.QuantityEnabled
                && slotLocks.Quantity == request.DesiredValue;
        case EditKind::Durability:
            return slotLocks.DurabilityEnabled
                && slotLocks.Durability == request.DesiredValue;
        case EditKind::WaterLevel:
            return slotLocks.WaterLevelEnabled
                && WaterLevelsEqual(
                    slotLocks.WaterLevel,
                    request.DesiredWaterLevel);
        default:
            return false;
        }
    }

    void ClearIdentityIfUnused(SlotLocks& slotLocks) {
        if (!slotLocks.AnyEnabled())
            slotLocks = {};
    }

    void MergeEditState() {
        std::scoped_lock lock(gEditMutex);
        if (gPendingEdit) {
            gState.LastApplyState =
                Solarpunk::InventoryEditor::ApplyState::Pending;
            gState.LastEditedSlot = gPendingEdit->SlotIndex;
            gState.LastApplyMessage =
                "Applying inventory change on the game thread";
            return;
        }

        gState.LastApplyState = gLastEditResult.State;
        gState.LastEditedSlot = gLastEditResult.SlotIndex;
        gState.LastApplyMessage = gLastEditResult.Message;
    }

} // namespace

void Solarpunk::InventoryEditor::Update(
    const PlayerSnapshot& player) {
    State next{};
    if (!GetInventoryOffsets().Valid) {
        next.Status = ScanStatus::InventoryUnavailable;
        gState = std::move(next);
        MergeEditState();
        return;
    }
    UpdateCatalog();

    if (!player.HasCoordinates()) {
        next.Status = ScanStatus::WaitingForPlayer;
        gState = std::move(next);
        MergeEditState();
        return;
    }

    uintptr_t inventorySystem = 0;
    if (!TryReadPointer(
        player.Pawn
            + GetInventoryOffsets().CharacterInventorySystem,
        inventorySystem)) {
        next.Status = ScanStatus::InventoryUnavailable;
        gState = std::move(next);
        MergeEditState();
        return;
    }

    if (inventorySystem != gLastInventorySystem) {
        gItemNameCache.clear();
        gLastInventorySystem = inventorySystem;
    }

    const auto inventory = Memory::TryRead<TArrayHeader>(
        inventorySystem + GetInventoryOffsets().Inventory);
    const auto declaredSize = Memory::TryRead<int32_t>(
        inventorySystem + GetInventoryOffsets().InventorySize);
    if (!inventory
        || !declaredSize
        || !IsValidArray(*inventory)
        || *declaredSize < 0
        || *declaredSize
            > Solarpunk::InventoryEditor::MaxInventorySlots) {
        next.Status = ScanStatus::InvalidInventory;
        next.InventorySystem = inventorySystem;
        gState = std::move(next);
        MergeEditState();
        return;
    }

    next.Status = ScanStatus::Ready;
    next.InventorySystem = inventorySystem;
    next.InventoryData = inventory->Data;
    next.SlotCount = inventory->Count;
    next.Capacity = inventory->Capacity;
    next.DeclaredSize = *declaredSize;
    next.Items.reserve(static_cast<size_t>(inventory->Count));

    for (int index = 0; index < inventory->Count; ++index) {
        const uintptr_t slotAddress =
            inventory->Data
            + static_cast<uintptr_t>(index) * InventorySlotStride;
        const auto slot =
            Memory::TryRead<RawInventorySlot>(slotAddress);
        if (!slot
            || !slot->ItemClass
            || slot->Quantity <= 0
            || !Memory::IsValidPtr(slot->ItemClass)) {
            continue;
        }

        std::string className;
        if (!TryGetItemClassName(slot->ItemClass, className)) {
            next.Status = ScanStatus::NamePoolUnavailable;
            continue;
        }

        ItemSlot item{};
        item.Index = index;
        item.Quantity = slot->Quantity;
        item.ItemClass = slot->ItemClass;
        item.HasDurability =
            TryGetDurability(*slot, item.Durability);
        item.HasWaterLevel =
            TryGetWaterLevel(*slot, item.WaterLevel);
        item.ClassName = className;
        item.DisplayName = MakeDisplayName(className);
        next.Items.push_back(std::move(item));
    }

    // Inventory replication can replace/reallocate the slot array while the
    // render thread is reading it. Publish only a snapshot whose header stayed
    // identical for the full scan; the next frame will retry a changing array.
    const auto verifiedInventory = Memory::TryRead<TArrayHeader>(
        inventorySystem + GetInventoryOffsets().Inventory);
    if (!verifiedInventory
        || !IsValidArray(*verifiedInventory)
        || verifiedInventory->Data != inventory->Data
        || verifiedInventory->Count != inventory->Count
        || verifiedInventory->Capacity != inventory->Capacity) {
        next.Status = ScanStatus::InventoryChanging;
        next.Items.clear();
    }

    ReconcileLocks(next);
    next.OccupiedCount = static_cast<int>(next.Items.size());
    next.EmptyCount = (std::max)(
        0,
        next.SlotCount - next.OccupiedCount);
    gState = std::move(next);
    MergeEditState();
}

const Solarpunk::InventoryEditor::State&
Solarpunk::InventoryEditor::GetState() {
    return gState;
}

const std::vector<Solarpunk::InventoryEditor::CatalogItem>&
Solarpunk::InventoryEditor::GetCatalog() {
    return gCatalog;
}

Solarpunk::InventoryEditor::CatalogStatus
Solarpunk::InventoryEditor::GetCatalogStatus() {
    switch (gCatalogPhase) {
    case CatalogPhase::WaitingForRuntime:
        return CatalogStatus::WaitingForRuntime;
    case CatalogPhase::FindItemMaster:
    case CatalogPhase::EnumerateItems:
        return CatalogStatus::Scanning;
    case CatalogPhase::Ready:
        return CatalogStatus::Ready;
    case CatalogPhase::Unavailable:
    default:
        return CatalogStatus::Unavailable;
    }
}

bool Solarpunk::InventoryEditor::QueueQuantityChange(
    HWND gameWindow,
    int slotIndex,
    int desiredQuantity) {
    if (!gameWindow
        || !IsWindow(gameWindow)
        || !gState.IsReady()
        || desiredQuantity < MinimumQuantity
        || desiredQuantity > MaximumQuantity) {
        std::scoped_lock lock(gEditMutex);
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = slotIndex;
        gLastEditResult.Message = !gameWindow || !IsWindow(gameWindow)
            ? "The hooked game window is not available"
            : "The requested quantity is no longer valid";
        return false;
    }

    const auto item = std::find_if(
        gState.Items.begin(),
        gState.Items.end(),
        [slotIndex](const ItemSlot& candidate) {
            return candidate.Index == slotIndex;
        });
    if (item == gState.Items.end()
        || item->Quantity == desiredQuantity) {
        return false;
    }

    EditRequest request{};
    request.Kind = EditKind::Quantity;
    request.InventorySystem = gState.InventorySystem;
    request.ItemClass = item->ItemClass;
    request.SlotIndex = item->Index;
    request.ExpectedQuantity = item->Quantity;
    request.ExpectedValue = item->Quantity;
    request.DesiredValue = desiredQuantity;

    {
        std::scoped_lock lock(gEditMutex);
        if (gPendingEdit)
            return false;

        gPendingEdit = request;
        gLastEditResult = {};
    }

    const UINT message = GetApplyMessage();
    SetLastError(ERROR_SUCCESS);
    if (!PostMessageW(gameWindow, message, 0, 0)) {
        const DWORD error = GetLastError();
        std::scoped_lock lock(gEditMutex);
        gPendingEdit.reset();
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = slotIndex;
        gLastEditResult.Message = error
            ? "Could not queue the edit on the hooked game window (Win32 "
                + std::to_string(error) + ")"
            : "The hooked game window rejected the edit message without an extended Win32 error";
        return false;
    }

    gState.LastApplyState = ApplyState::Pending;
    gState.LastEditedSlot = slotIndex;
    gState.LastApplyMessage = "Applying quantity on the game thread";
    return true;
}

bool Solarpunk::InventoryEditor::QueueDurabilityChange(
    HWND gameWindow,
    int slotIndex,
    int desiredDurability) {
    if (!gameWindow
        || !IsWindow(gameWindow)
        || !gState.IsReady()
        || desiredDurability < MinimumDurability
        || desiredDurability > MaximumDurability) {
        std::scoped_lock lock(gEditMutex);
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = slotIndex;
        gLastEditResult.Message = !gameWindow || !IsWindow(gameWindow)
            ? "The hooked game window is not available"
            : "The requested durability is no longer valid";
        return false;
    }

    const auto item = std::find_if(
        gState.Items.begin(),
        gState.Items.end(),
        [slotIndex](const ItemSlot& candidate) {
            return candidate.Index == slotIndex;
        });
    if (item == gState.Items.end()
        || !item->HasDurability
        || item->Durability == desiredDurability) {
        return false;
    }

    EditRequest request{};
    request.Kind = EditKind::Durability;
    request.InventorySystem = gState.InventorySystem;
    request.ItemClass = item->ItemClass;
    request.SlotIndex = item->Index;
    request.ExpectedQuantity = item->Quantity;
    request.ExpectedValue = item->Durability;
    request.DesiredValue = desiredDurability;

    {
        std::scoped_lock lock(gEditMutex);
        if (gPendingEdit)
            return false;

        gPendingEdit = request;
        gLastEditResult = {};
    }

    const UINT message = GetApplyMessage();
    SetLastError(ERROR_SUCCESS);
    if (!PostMessageW(gameWindow, message, 0, 0)) {
        const DWORD error = GetLastError();
        std::scoped_lock lock(gEditMutex);
        gPendingEdit.reset();
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = slotIndex;
        gLastEditResult.Message = error
            ? "Could not queue the edit on the hooked game window (Win32 "
                + std::to_string(error) + ")"
            : "The hooked game window rejected the edit message without an extended Win32 error";
        return false;
    }

    gState.LastApplyState = ApplyState::Pending;
    gState.LastEditedSlot = slotIndex;
    gState.LastApplyMessage =
        "Applying durability on the game thread";
    return true;
}

bool Solarpunk::InventoryEditor::QueueWaterLevelChange(
    HWND gameWindow,
    int slotIndex,
    double desiredWaterLevel) {
    if (!gameWindow
        || !IsWindow(gameWindow)
        || !gState.IsReady()
        || !std::isfinite(desiredWaterLevel)
        || desiredWaterLevel < MinimumWaterLevel
        || desiredWaterLevel > MaximumWaterLevel) {
        std::scoped_lock lock(gEditMutex);
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = slotIndex;
        gLastEditResult.Message = !gameWindow || !IsWindow(gameWindow)
            ? "The hooked game window is not available"
            : "The requested water level is no longer valid";
        return false;
    }

    const auto item = std::find_if(
        gState.Items.begin(),
        gState.Items.end(),
        [slotIndex](const ItemSlot& candidate) {
            return candidate.Index == slotIndex;
        });
    if (item == gState.Items.end()
        || !item->HasWaterLevel
        || WaterLevelsEqual(
            item->WaterLevel,
            desiredWaterLevel)) {
        return false;
    }

    EditRequest request{};
    request.Kind = EditKind::WaterLevel;
    request.InventorySystem = gState.InventorySystem;
    request.ItemClass = item->ItemClass;
    request.SlotIndex = item->Index;
    request.ExpectedQuantity = item->Quantity;
    request.ExpectedWaterLevel = item->WaterLevel;
    request.DesiredWaterLevel = desiredWaterLevel;

    {
        std::scoped_lock lock(gEditMutex);
        if (gPendingEdit)
            return false;

        gPendingEdit = request;
        gLastEditResult = {};
    }

    const UINT message = GetApplyMessage();
    SetLastError(ERROR_SUCCESS);
    if (!PostMessageW(gameWindow, message, 0, 0)) {
        const DWORD error = GetLastError();
        std::scoped_lock lock(gEditMutex);
        gPendingEdit.reset();
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = slotIndex;
        gLastEditResult.Message = error
            ? "Could not queue the water-level edit (Win32 "
                + std::to_string(error) + ")"
            : "The hooked game window rejected the water-level edit";
        return false;
    }

    gState.LastApplyState = ApplyState::Pending;
    gState.LastEditedSlot = slotIndex;
    gState.LastApplyMessage =
        "Applying water level on the game thread";
    return true;
}

bool Solarpunk::InventoryEditor::QueueAddItem(
    HWND gameWindow,
    uintptr_t itemClass,
    int quantity) {
    const auto catalogItem = std::find_if(
        gCatalog.begin(),
        gCatalog.end(),
        [itemClass](const CatalogItem& candidate) {
            return candidate.ItemClass == itemClass;
        });
    if (!gameWindow
        || !IsWindow(gameWindow)
        || !gState.IsReady()
        || gState.EmptyCount <= 0
        || GetCatalogStatus() != CatalogStatus::Ready
        || catalogItem == gCatalog.end()
        || quantity < MinimumQuantity
        || quantity > MaximumQuantity) {
        std::scoped_lock lock(gEditMutex);
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = -1;
        if (!gameWindow || !IsWindow(gameWindow)) {
            gLastEditResult.Message =
                "The hooked game window is not available";
        }
        else if (gState.EmptyCount <= 0) {
            gLastEditResult.Message =
                "No empty inventory slot is available";
        }
        else {
            gLastEditResult.Message =
                "The selected catalog item is no longer valid";
        }
        return false;
    }

    EditRequest request{};
    request.Kind = EditKind::AddItem;
    request.InventorySystem = gState.InventorySystem;
    request.ItemClass = itemClass;
    request.SlotIndex = -1;
    request.DesiredValue = quantity;

    {
        std::scoped_lock lock(gEditMutex);
        if (gPendingEdit)
            return false;

        gPendingEdit = request;
        gLastEditResult = {};
    }

    const UINT message = GetApplyMessage();
    SetLastError(ERROR_SUCCESS);
    if (!PostMessageW(gameWindow, message, 0, 0)) {
        const DWORD error = GetLastError();
        std::scoped_lock lock(gEditMutex);
        gPendingEdit.reset();
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = -1;
        gLastEditResult.Message = error
            ? "Could not queue the add-item request (Win32 "
                + std::to_string(error) + ")"
            : "The hooked game window rejected the add-item request";
        return false;
    }

    gState.LastApplyState = ApplyState::Pending;
    gState.LastEditedSlot = -1;
    gState.LastApplyMessage =
        "Adding the selected item on the game thread";
    return true;
}

bool Solarpunk::InventoryEditor::SetQuantityLock(
    int slotIndex,
    bool enabled,
    int lockedQuantity) {
    if (slotIndex < 0 || slotIndex >= MaxInventorySlots)
        return false;

    const auto item = std::find_if(
        gState.Items.begin(),
        gState.Items.end(),
        [slotIndex](const ItemSlot& candidate) {
            return candidate.Index == slotIndex;
        });
    if (enabled
        && (!gState.IsReady()
            || item == gState.Items.end()
            || lockedQuantity < MinimumQuantity
            || lockedQuantity > MaximumQuantity)) {
        return false;
    }

    std::scoped_lock lock(gEditMutex);
    SlotLocks& slotLocks =
        gSlotLocks[static_cast<size_t>(slotIndex)];
    if (!enabled) {
        slotLocks.QuantityEnabled = false;
        ClearIdentityIfUnused(slotLocks);
        return true;
    }

    if (slotLocks.AnyEnabled()
        && (slotLocks.InventorySystem != gState.InventorySystem
            || slotLocks.ItemClass != item->ItemClass)) {
        slotLocks = {};
    }
    slotLocks.InventorySystem = gState.InventorySystem;
    slotLocks.ItemClass = item->ItemClass;
    slotLocks.QuantityEnabled = true;
    slotLocks.Quantity = lockedQuantity;
    return true;
}

bool Solarpunk::InventoryEditor::SetDurabilityLock(
    int slotIndex,
    bool enabled,
    int lockedDurability) {
    if (slotIndex < 0 || slotIndex >= MaxInventorySlots)
        return false;

    const auto item = std::find_if(
        gState.Items.begin(),
        gState.Items.end(),
        [slotIndex](const ItemSlot& candidate) {
            return candidate.Index == slotIndex;
        });
    if (enabled
        && (!gState.IsReady()
            || item == gState.Items.end()
            || !item->HasDurability
            || lockedDurability < MinimumDurability
            || lockedDurability > MaximumDurability)) {
        return false;
    }

    std::scoped_lock lock(gEditMutex);
    SlotLocks& slotLocks =
        gSlotLocks[static_cast<size_t>(slotIndex)];
    if (!enabled) {
        slotLocks.DurabilityEnabled = false;
        ClearIdentityIfUnused(slotLocks);
        return true;
    }

    if (slotLocks.AnyEnabled()
        && (slotLocks.InventorySystem != gState.InventorySystem
            || slotLocks.ItemClass != item->ItemClass)) {
        slotLocks = {};
    }
    slotLocks.InventorySystem = gState.InventorySystem;
    slotLocks.ItemClass = item->ItemClass;
    slotLocks.DurabilityEnabled = true;
    slotLocks.Durability = lockedDurability;
    return true;
}

bool Solarpunk::InventoryEditor::SetWaterLevelLock(
    int slotIndex,
    bool enabled,
    double lockedWaterLevel) {
    if (slotIndex < 0 || slotIndex >= MaxInventorySlots)
        return false;

    const auto item = std::find_if(
        gState.Items.begin(),
        gState.Items.end(),
        [slotIndex](const ItemSlot& candidate) {
            return candidate.Index == slotIndex;
        });
    if (enabled
        && (!gState.IsReady()
            || item == gState.Items.end()
            || !item->HasWaterLevel
            || !std::isfinite(lockedWaterLevel)
            || lockedWaterLevel < MinimumWaterLevel
            || lockedWaterLevel > MaximumWaterLevel)) {
        return false;
    }

    std::scoped_lock lock(gEditMutex);
    SlotLocks& slotLocks =
        gSlotLocks[static_cast<size_t>(slotIndex)];
    if (!enabled) {
        slotLocks.WaterLevelEnabled = false;
        ClearIdentityIfUnused(slotLocks);
        return true;
    }

    if (slotLocks.AnyEnabled()
        && (slotLocks.InventorySystem != gState.InventorySystem
            || slotLocks.ItemClass != item->ItemClass)) {
        slotLocks = {};
    }
    slotLocks.InventorySystem = gState.InventorySystem;
    slotLocks.ItemClass = item->ItemClass;
    slotLocks.WaterLevelEnabled = true;
    slotLocks.WaterLevel = lockedWaterLevel;
    return true;
}

void Solarpunk::InventoryEditor::MaintainLocks(HWND gameWindow) {
    if (!gameWindow
        || !IsWindow(gameWindow)
        || !gState.IsReady()) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (now < gNextLockMaintenanceTick)
        return;
    gNextLockMaintenanceTick = now + 75;

    std::optional<EditRequest> request;
    {
        std::scoped_lock lock(gEditMutex);
        if (gPendingEdit)
            return;

        for (const ItemSlot& item : gState.Items) {
            if (item.Index < 0 || item.Index >= MaxInventorySlots)
                continue;

            const SlotLocks& slotLocks =
                gSlotLocks[static_cast<size_t>(item.Index)];
            if (!slotLocks.AnyEnabled()
                || slotLocks.InventorySystem != gState.InventorySystem
                || slotLocks.ItemClass != item.ItemClass) {
                continue;
            }

            EditRequest next{};
            next.InventorySystem = gState.InventorySystem;
            next.ItemClass = item.ItemClass;
            next.SlotIndex = item.Index;
            next.ExpectedQuantity = item.Quantity;
            next.FromLock = true;

            if (slotLocks.QuantityEnabled
                && item.Quantity != slotLocks.Quantity) {
                next.Kind = EditKind::Quantity;
                next.ExpectedValue = item.Quantity;
                next.DesiredValue = slotLocks.Quantity;
            }
            else if (slotLocks.DurabilityEnabled
                && item.HasDurability
                && item.Durability != slotLocks.Durability) {
                next.Kind = EditKind::Durability;
                next.ExpectedValue = item.Durability;
                next.DesiredValue = slotLocks.Durability;
            }
            else if (slotLocks.WaterLevelEnabled
                && item.HasWaterLevel
                && !WaterLevelsEqual(
                    item.WaterLevel,
                    slotLocks.WaterLevel)) {
                next.Kind = EditKind::WaterLevel;
                next.ExpectedWaterLevel = item.WaterLevel;
                next.DesiredWaterLevel = slotLocks.WaterLevel;
            }
            else {
                continue;
            }

            request = next;
            gPendingEdit = next;
            gLastEditResult = {};
            break;
        }
    }

    if (!request)
        return;

    SetLastError(ERROR_SUCCESS);
    if (!PostMessageW(gameWindow, GetApplyMessage(), 0, 0)) {
        const DWORD error = GetLastError();
        std::scoped_lock lock(gEditMutex);
        if (gPendingEdit
            && gPendingEdit->FromLock
            && gPendingEdit->SlotIndex == request->SlotIndex
            && gPendingEdit->Kind == request->Kind) {
            gPendingEdit.reset();
        }
        gLastEditResult.State = ApplyState::Failed;
        gLastEditResult.SlotIndex = request->SlotIndex;
        gLastEditResult.Message = error
            ? "Could not queue locked-value maintenance (Win32 "
                + std::to_string(error) + ")"
            : "The hooked game window rejected locked-value maintenance";
        return;
    }

    gState.LastApplyState = ApplyState::Pending;
    gState.LastEditedSlot = request->SlotIndex;
    gState.LastApplyMessage =
        "Maintaining a locked inventory value";
}

UINT Solarpunk::InventoryEditor::GetApplyMessage() {
    // Keep this inside the private WM_APP range. RegisterWindowMessageW can
    // return zero in manually mapped modules even though the hooked HWND is
    // valid, which made an otherwise valid edit impossible to dispatch.
    // UE's raw-input helper uses WM_APP + 1/+2 on a different HWND.
    constexpr UINT InventoryEditMessage = WM_APP + 0x053A;
    return InventoryEditMessage;
}

void Solarpunk::InventoryEditor::ProcessPendingEdit() {
    std::optional<EditRequest> request;
    {
        std::scoped_lock lock(gEditMutex);
        if (!gPendingEdit)
            return;

        if (!IsLockRequestActive(*gPendingEdit)) {
            gPendingEdit.reset();
            return;
        }
        request = gPendingEdit;
        gPendingEdit.reset();
    }

    EditResult result = ApplyEdit(*request);
    {
        std::scoped_lock lock(gEditMutex);
        gLastEditResult = std::move(result);
    }
}

void Solarpunk::InventoryEditor::Reset() {
    gState = {};
    gItemNameCache.clear();
    gLastInventorySystem = 0;
    gCatalog.clear();
    gCatalogPhase = CatalogPhase::WaitingForRuntime;
    gCatalogCursor = 0;
    gCatalogObjectCount = 0;
    gItemMasterClass = 0;
    gBlueprintGeneratedClassMeta = 0;

    std::scoped_lock lock(gEditMutex);
    gPendingEdit.reset();
    gLastEditResult = {};
    gSlotLocks.fill({});
    gNextLockMaintenanceTick = 0;
}

const char* Solarpunk::InventoryEditor::GetStatusText(
    ScanStatus status) {
    switch (status) {
    case ScanStatus::Ready:
        return "INVENTORY READY";
    case ScanStatus::WaitingForPlayer:
        return "WAITING FOR PLAYER";
    case ScanStatus::InventoryUnavailable:
        return "INVENTORY UNAVAILABLE";
    case ScanStatus::InvalidInventory:
        return "INVALID INVENTORY DATA";
    case ScanStatus::InventoryChanging:
        return "INVENTORY UPDATING";
    case ScanStatus::NamePoolUnavailable:
        return "ITEM NAMES UNAVAILABLE";
    default:
        return "INVENTORY UNAVAILABLE";
    }
}

const char* Solarpunk::InventoryEditor::GetApplyStateText(
    ApplyState state) {
    switch (state) {
    case ApplyState::Idle:
        return "NO PENDING EDIT";
    case ApplyState::Pending:
        return "APPLYING";
    case ApplyState::Succeeded:
        return "EDIT APPLIED";
    case ApplyState::Failed:
        return "EDIT FAILED";
    default:
        return "NO PENDING EDIT";
    }
}

const char* Solarpunk::InventoryEditor::GetCatalogStatusText(
    CatalogStatus status) {
    switch (status) {
    case CatalogStatus::WaitingForRuntime:
        return "WAITING FOR RUNTIME";
    case CatalogStatus::Scanning:
        return "SCANNING ITEM CLASSES";
    case CatalogStatus::Ready:
        return "ITEM CATALOG READY";
    case CatalogStatus::Unavailable:
        return "ITEM CATALOG UNAVAILABLE";
    default:
        return "UNKNOWN";
    }
}
