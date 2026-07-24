#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Solarpunk::GameSchema {

    struct GlobalsLayout {
        std::uintptr_t GObjectsRva = 0;
        std::uintptr_t GNamesRva = 0;
        std::uintptr_t GWorldRva = 0;
        std::uintptr_t ProcessEventRva = 0;
        std::uint32_t ProcessEventIndex = 0;
    };

    struct ObjectArrayLayout {
        bool Chunked = false;
        std::uint32_t Objects = 0;
        std::uint32_t NumElements = 0;
        std::uint32_t MaxElements = 0;
        std::uint32_t NumChunks = 0;
        std::uint32_t MaxChunks = 0;
        std::uint32_t ItemSize = 0;
        std::uint32_t ItemObject = 0;
        std::uint32_t ChunkSize = 0;
    };

    struct NamePoolLayout {
        std::uint32_t BlockOffsetBits = 0;
        std::uint32_t EntryStride = 0;
        std::uint32_t FNameSize = 0;
    };

    struct UObjectLayout {
        std::uint32_t Flags = 0;
        std::uint32_t Index = 0;
        std::uint32_t Class = 0;
        std::uint32_t Name = 0;
        std::uint32_t Outer = 0;
    };

    struct UFieldLayout {
        std::uint32_t Next = 0;
    };

    struct UStructLayout {
        std::uint32_t Super = 0;
        std::uint32_t Children = 0;
        std::uint32_t ChildProperties = 0;
        std::uint32_t Size = 0;
        std::uint32_t MinAlignment = 0;
    };

    struct UFunctionLayout {
        std::uint32_t Flags = 0;
        std::uint32_t NativeFunction = 0;
    };

    struct RuntimeLayout {
        GlobalsLayout Globals;
        ObjectArrayLayout ObjectArray;
        NamePoolLayout NamePool;
        UObjectLayout UObject;
        UFieldLayout UField;
        UStructLayout UStruct;
        UFunctionLayout UFunction;
        std::uint32_t LevelActors = 0;
        std::string Fingerprint;
        std::filesystem::path SchemaPath;
    };

    struct PropertyDescriptor {
        std::uint32_t Offset = 0;
        std::uint32_t Size = 0;
        std::uint32_t ArrayDim = 0;
        std::uint64_t Flags = 0;
        std::uint32_t BoolByteOffset = 0;
        std::uint8_t BoolMask = 0;
        std::string Class;
        std::string CppType;
        std::string ReferencedType;
    };

    // Initializes from the exact local schema produced for the running game.
    // Failure is terminal for this DLL instance so no stale fallback offsets
    // can become write-capable.
    bool Initialize();
    bool Ready();
    const RuntimeLayout& Get();
    std::string Status();

    // Owner may be either a full Unreal path or the reflected short type name.
    // Inherited properties are resolved by walking the dumped super chain.
    const PropertyDescriptor* FindProperty(
        std::string_view owner,
        std::string_view propertyName);
    const PropertyDescriptor* FindPropertyPrefix(
        std::string_view owner,
        std::string_view propertyPrefix);
    std::optional<std::uint32_t> FindTypeSize(
        std::string_view owner);

} // namespace Solarpunk::GameSchema
