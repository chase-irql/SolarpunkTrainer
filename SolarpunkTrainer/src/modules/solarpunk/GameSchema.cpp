#include "GameSchema.h"

#include <Windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

    using json = nlohmann::json;

    struct TypeDescriptor {
        std::string Name;
        std::string Super;
        std::uint32_t Size = 0;
        std::unordered_map<
            std::string,
            Solarpunk::GameSchema::PropertyDescriptor> Properties;
    };

    Solarpunk::GameSchema::RuntimeLayout gLayout{};
    std::unordered_map<std::string, TypeDescriptor> gTypes;
    std::unordered_map<std::string, std::string> gShortTypePaths;
    std::once_flag gInitializeOnce;
    bool gReady = false;
    std::string gStatus =
        "The local runtime schema has not been initialized.";

    std::string Hex(
        const std::uint8_t* bytes,
        std::size_t count) {
        static constexpr char Digits[] =
            "0123456789abcdef";
        std::string output(count * 2, '0');
        for (std::size_t index = 0; index < count; ++index) {
            output[index * 2] =
                Digits[(bytes[index] >> 4) & 0x0F];
            output[index * 2 + 1] =
                Digits[bytes[index] & 0x0F];
        }
        return output;
    }

    bool HashSha256(
        const std::uint8_t* data,
        std::size_t size,
        std::string& output) {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectLength = 0;
        DWORD resultLength = 0;
        std::vector<std::uint8_t> hashObject;
        std::array<std::uint8_t, 32> digest{};

        if (BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0) < 0) {
            return false;
        }
        const auto closeHandles = [&]() {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        };
        if (BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength),
            &resultLength,
            0) < 0) {
            closeHandles();
            return false;
        }

        hashObject.resize(objectLength);
        if (BCryptCreateHash(
            algorithm,
            &hash,
            hashObject.data(),
            static_cast<ULONG>(hashObject.size()),
            nullptr,
            0,
            0) < 0
            || BCryptHashData(
                hash,
                const_cast<PUCHAR>(data),
                static_cast<ULONG>(size),
                0) < 0
            || BCryptFinishHash(
                hash,
                digest.data(),
                static_cast<ULONG>(digest.size()),
                0) < 0) {
            closeHandles();
            return false;
        }

        output = Hex(digest.data(), digest.size());
        closeHandles();
        return true;
    }

    bool BuildSchemaPath(
        std::filesystem::path& schemaPath,
        std::string& fingerprint,
        std::uint32_t& sizeOfImage) {
        std::array<wchar_t, 32768> executablePath{};
        const DWORD pathLength = GetModuleFileNameW(
            nullptr,
            executablePath.data(),
            static_cast<DWORD>(executablePath.size()));
        if (!pathLength
            || pathLength >= executablePath.size()) {
            gStatus =
                "Could not locate the running game executable.";
            return false;
        }

        std::ifstream stream(
            std::filesystem::path(std::wstring_view(
                executablePath.data(),
                pathLength)),
            std::ios::binary);
        if (!stream) {
            gStatus =
                "Could not read the running game executable.";
            return false;
        }

        stream.seekg(0, std::ios::end);
        if (stream.tellg() <= 0) {
            gStatus = "The running game executable is empty.";
            return false;
        }
        stream.seekg(0, std::ios::beg);

        IMAGE_DOS_HEADER dos{};
        stream.read(
            reinterpret_cast<char*>(&dos),
            sizeof(dos));
        if (!stream
            || dos.e_magic != IMAGE_DOS_SIGNATURE
            || dos.e_lfanew <= 0) {
            gStatus = "The game executable has an invalid PE header.";
            return false;
        }

        stream.seekg(dos.e_lfanew, std::ios::beg);
        IMAGE_NT_HEADERS64 nt{};
        stream.read(
            reinterpret_cast<char*>(&nt),
            sizeof(nt));
        if (!stream
            || nt.Signature != IMAGE_NT_SIGNATURE
            || nt.OptionalHeader.Magic
                != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            gStatus = "The game executable is not a valid x64 image.";
            return false;
        }
        sizeOfImage = nt.OptionalHeader.SizeOfImage;

        stream.seekg(
            dos.e_lfanew
                + sizeof(std::uint32_t)
                + sizeof(IMAGE_FILE_HEADER)
                + nt.FileHeader.SizeOfOptionalHeader,
            std::ios::beg);

        IMAGE_SECTION_HEADER text{};
        bool foundText = false;
        for (std::uint16_t index = 0;
            index < nt.FileHeader.NumberOfSections;
            ++index) {
            IMAGE_SECTION_HEADER section{};
            stream.read(
                reinterpret_cast<char*>(&section),
                sizeof(section));
            if (!stream)
                break;
            const std::string_view name(
                reinterpret_cast<const char*>(section.Name),
                strnlen_s(
                    reinterpret_cast<const char*>(section.Name),
                    IMAGE_SIZEOF_SHORT_NAME));
            if (name == ".text") {
                text = section;
                foundText = true;
                break;
            }
        }
        if (!foundText
            || !text.PointerToRawData
            || !text.SizeOfRawData) {
            gStatus =
                "The game executable has no readable .text section.";
            return false;
        }

        std::vector<std::uint8_t> bytes(text.SizeOfRawData);
        stream.seekg(text.PointerToRawData, std::ios::beg);
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        std::string textHash;
        if (!stream
            || !HashSha256(
                bytes.data(),
                bytes.size(),
                textHash)) {
            gStatus = "Could not fingerprint the game code.";
            return false;
        }

        std::ostringstream value;
        value
            << std::hex
            << std::setfill('0')
            << std::setw(8)
            << nt.FileHeader.TimeDateStamp
            << '-'
            << std::setw(8)
            << nt.OptionalHeader.SizeOfImage
            << '-'
            << textHash;
        fingerprint = value.str();

        std::array<wchar_t, 32768> localAppData{};
        const DWORD localLength = GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            localAppData.data(),
            static_cast<DWORD>(localAppData.size()));
        if (!localLength
            || localLength >= localAppData.size()) {
            gStatus =
                "LOCALAPPDATA is unavailable for the schema cache.";
            return false;
        }
        schemaPath = std::filesystem::path(
            std::wstring_view(
                localAppData.data(),
                localLength))
            / L"Solarpunk Trainer"
            / L"SchemaCache"
            / std::filesystem::path(fingerprint)
            / L"runtime-schema.json";
        return true;
    }

    template <typename T>
    T ReadInteger(
        const json& object,
        const char* key) {
        return static_cast<T>(object.at(key).get<std::uint64_t>());
    }

    bool IsValidCoreRva(
        std::uintptr_t value,
        std::uint32_t sizeOfImage) {
        return value > 0 && value < sizeOfImage;
    }

    void ParseTypes(const json& types) {
        gTypes.reserve(types.size());
        gShortTypePaths.reserve(types.size());

        for (const json& source : types) {
            if (!source.is_object())
                continue;
            TypeDescriptor type{};
            type.Name = source.value("name", std::string{});
            type.Super = source.value("super", std::string{});
            type.Size = source.value("size", 0u);
            const std::string path =
                source.value("path", std::string{});
            if (path.empty() || type.Name.empty())
                continue;

            const auto& properties =
                source.value(
                    "properties",
                    json::array());
            type.Properties.reserve(properties.size());
            for (const json& property : properties) {
                const std::string name =
                    property.value("name", std::string{});
                if (name.empty())
                    continue;

                Solarpunk::GameSchema::PropertyDescriptor
                    descriptor{};
                descriptor.Offset =
                    property.value("offset", 0u);
                descriptor.Size =
                    property.value("size", 0u);
                descriptor.ArrayDim =
                    property.value("array_dim", 0u);
                descriptor.Flags =
                    property.value("flags", std::uint64_t{});
                descriptor.Class =
                    property.value("class", std::string{});
                descriptor.CppType =
                    property.value("cpp_type", std::string{});
                descriptor.ReferencedType =
                    property.value(
                        "referenced_type",
                        std::string{});
                if (property.contains("bool")) {
                    const json& boolInfo = property["bool"];
                    descriptor.BoolByteOffset =
                        boolInfo.value("byte_offset", 0u);
                    descriptor.BoolMask =
                        static_cast<std::uint8_t>(
                            boolInfo.value("field_mask", 0u));
                }
                type.Properties.emplace(
                    name,
                    std::move(descriptor));
            }

            const auto [iterator, inserted] =
                gTypes.emplace(path, std::move(type));
            if (inserted) {
                auto [nameIterator, nameInserted] =
                    gShortTypePaths.emplace(
                        iterator->second.Name,
                        path);
                if (!nameInserted
                    && nameIterator->second != path) {
                    // Ambiguous short names require the full Unreal path.
                    nameIterator->second.clear();
                }
            }
        }
    }

    void InitializeInternal() {
        std::filesystem::path schemaPath;
        std::string fingerprint;
        std::uint32_t sizeOfImage = 0;
        if (!BuildSchemaPath(
            schemaPath,
            fingerprint,
            sizeOfImage)) {
            return;
        }

        std::ifstream stream(schemaPath, std::ios::binary);
        if (!stream) {
            gStatus =
                "No exact local schema exists for this game build.";
            return;
        }

        try {
            const json schema = json::parse(stream);
            if (schema.value("format_version", 0) != 1
                || schema.at("build").value(
                    "fingerprint",
                    std::string{}) != fingerprint
                || !schema.contains("types")
                || !schema["types"].is_array()
                || schema.value("type_count", 0u)
                    != schema["types"].size()) {
                gStatus =
                    "The local schema is incomplete or belongs to another build.";
                return;
            }

            const json& core = schema.at("core");
            const json& globals = core.at("globals");
            gLayout.Globals.GObjectsRva =
                ReadInteger<std::uintptr_t>(
                    globals,
                    "gobjects_rva");
            gLayout.Globals.GNamesRva =
                ReadInteger<std::uintptr_t>(
                    globals,
                    "gnames_rva");
            gLayout.Globals.GWorldRva =
                ReadInteger<std::uintptr_t>(
                    globals,
                    "gworld_rva");
            gLayout.Globals.ProcessEventRva =
                ReadInteger<std::uintptr_t>(
                    globals,
                    "process_event_rva");
            gLayout.Globals.ProcessEventIndex =
                ReadInteger<std::uint32_t>(
                    globals,
                    "process_event_index");

            if (!IsValidCoreRva(
                gLayout.Globals.GObjectsRva,
                sizeOfImage)
                || !IsValidCoreRva(
                    gLayout.Globals.GNamesRva,
                    sizeOfImage)
                || !IsValidCoreRva(
                    gLayout.Globals.GWorldRva,
                    sizeOfImage)
                || !IsValidCoreRva(
                    gLayout.Globals.ProcessEventRva,
                    sizeOfImage)
                || !gLayout.Globals.ProcessEventIndex) {
                gStatus =
                    "The local schema contains invalid Unreal globals.";
                return;
            }

            const json& objects = core.at("object_array");
            gLayout.ObjectArray.Chunked =
                objects.at("chunked").get<bool>();
            gLayout.ObjectArray.Objects =
                ReadInteger<std::uint32_t>(objects, "objects");
            gLayout.ObjectArray.NumElements =
                ReadInteger<std::uint32_t>(
                    objects,
                    "num_elements");
            gLayout.ObjectArray.MaxElements =
                ReadInteger<std::uint32_t>(
                    objects,
                    "max_elements");
            gLayout.ObjectArray.NumChunks =
                ReadInteger<std::uint32_t>(
                    objects,
                    "num_chunks");
            gLayout.ObjectArray.MaxChunks =
                ReadInteger<std::uint32_t>(
                    objects,
                    "max_chunks");
            gLayout.ObjectArray.ItemSize =
                ReadInteger<std::uint32_t>(
                    objects,
                    "item_size");
            gLayout.ObjectArray.ItemObject =
                ReadInteger<std::uint32_t>(
                    objects,
                    "item_object");
            gLayout.ObjectArray.ChunkSize =
                ReadInteger<std::uint32_t>(
                    objects,
                    "chunk_size");

            const json& names = core.at("name_pool");
            gLayout.NamePool.BlockOffsetBits =
                ReadInteger<std::uint32_t>(
                    names,
                    "block_offset_bits");
            gLayout.NamePool.EntryStride =
                ReadInteger<std::uint32_t>(
                    names,
                    "entry_stride");
            gLayout.NamePool.FNameSize =
                ReadInteger<std::uint32_t>(
                    names,
                    "fname_size");

            const json& uobject = core.at("uobject");
            gLayout.UObject.Flags =
                ReadInteger<std::uint32_t>(uobject, "flags");
            gLayout.UObject.Index =
                ReadInteger<std::uint32_t>(uobject, "index");
            gLayout.UObject.Class =
                ReadInteger<std::uint32_t>(uobject, "class");
            gLayout.UObject.Name =
                ReadInteger<std::uint32_t>(uobject, "name");
            gLayout.UObject.Outer =
                ReadInteger<std::uint32_t>(uobject, "outer");

            const json& ufield = core.at("ufield");
            gLayout.UField.Next =
                ReadInteger<std::uint32_t>(ufield, "next");

            const json& ustruct = core.at("ustruct");
            gLayout.UStruct.Super =
                ReadInteger<std::uint32_t>(ustruct, "super");
            gLayout.UStruct.Children =
                ReadInteger<std::uint32_t>(
                    ustruct,
                    "children");
            gLayout.UStruct.ChildProperties =
                ReadInteger<std::uint32_t>(
                    ustruct,
                    "child_properties");
            gLayout.UStruct.Size =
                ReadInteger<std::uint32_t>(ustruct, "size");
            gLayout.UStruct.MinAlignment =
                ReadInteger<std::uint32_t>(
                    ustruct,
                    "min_alignment");

            const json& ufunction = core.at("ufunction");
            gLayout.UFunction.Flags =
                ReadInteger<std::uint32_t>(
                    ufunction,
                    "flags");
            gLayout.UFunction.NativeFunction =
                ReadInteger<std::uint32_t>(
                    ufunction,
                    "native_function");
            gLayout.LevelActors =
                ReadInteger<std::uint32_t>(
                    core.at("insdk"),
                    "level_actors");

            if (!gLayout.ObjectArray.Chunked
                || !gLayout.ObjectArray.ItemSize
                || !gLayout.ObjectArray.ChunkSize
                || !gLayout.NamePool.BlockOffsetBits
                || !gLayout.NamePool.EntryStride
                || !gLayout.UObject.Class
                || !gLayout.UObject.Name
                || !gLayout.UField.Next
                || !gLayout.UStruct.Super
                || !gLayout.UStruct.Children) {
                gStatus =
                    "The local schema contains incomplete engine layouts.";
                return;
            }

            ParseTypes(schema["types"]);
            if (gTypes.size() < 100) {
                gStatus =
                    "The local schema contains too few reflected types.";
                gTypes.clear();
                gShortTypePaths.clear();
                return;
            }

            gLayout.Fingerprint = std::move(fingerprint);
            gLayout.SchemaPath = std::move(schemaPath);
            gReady = true;
            gStatus =
                "Exact local runtime schema loaded and validated.";
        }
        catch (const std::exception&) {
            gStatus = "The local runtime schema is malformed.";
            gTypes.clear();
            gShortTypePaths.clear();
        }
    }

} // namespace

bool Solarpunk::GameSchema::Initialize() {
    std::call_once(gInitializeOnce, InitializeInternal);
    return gReady;
}

bool Solarpunk::GameSchema::Ready() {
    return gReady;
}

const Solarpunk::GameSchema::RuntimeLayout&
Solarpunk::GameSchema::Get() {
    return gLayout;
}

std::string Solarpunk::GameSchema::Status() {
    return gStatus;
}

const Solarpunk::GameSchema::PropertyDescriptor*
Solarpunk::GameSchema::FindProperty(
    std::string_view owner,
    std::string_view propertyName) {
    if (!gReady || owner.empty() || propertyName.empty())
        return nullptr;

    std::string path(owner);
    auto type = gTypes.find(path);
    if (type == gTypes.end()) {
        const auto shortType = gShortTypePaths.find(path);
        if (shortType == gShortTypePaths.end()
            || shortType->second.empty()) {
            return nullptr;
        }
        path = shortType->second;
        type = gTypes.find(path);
    }

    for (int depth = 0;
        type != gTypes.end() && depth < 64;
        ++depth) {
        const auto property =
            type->second.Properties.find(
                std::string(propertyName));
        if (property != type->second.Properties.end())
            return &property->second;
        if (type->second.Super.empty())
            break;
        type = gTypes.find(type->second.Super);
    }
    return nullptr;
}

const Solarpunk::GameSchema::PropertyDescriptor*
Solarpunk::GameSchema::FindPropertyPrefix(
    std::string_view owner,
    std::string_view propertyPrefix) {
    if (!gReady || owner.empty() || propertyPrefix.empty())
        return nullptr;

    std::string path(owner);
    auto type = gTypes.find(path);
    if (type == gTypes.end()) {
        const auto shortType = gShortTypePaths.find(path);
        if (shortType == gShortTypePaths.end()
            || shortType->second.empty()) {
            return nullptr;
        }
        type = gTypes.find(shortType->second);
    }

    for (int depth = 0;
        type != gTypes.end() && depth < 64;
        ++depth) {
        const PropertyDescriptor* match = nullptr;
        for (const auto& [name, property] :
            type->second.Properties) {
            if (!name.starts_with(propertyPrefix))
                continue;
            if (match)
                return nullptr;
            match = &property;
        }
        if (match)
            return match;
        if (type->second.Super.empty())
            break;
        type = gTypes.find(type->second.Super);
    }
    return nullptr;
}

std::optional<std::uint32_t>
Solarpunk::GameSchema::FindTypeSize(
    std::string_view owner) {
    if (!gReady || owner.empty())
        return std::nullopt;

    auto type = gTypes.find(std::string(owner));
    if (type == gTypes.end()) {
        const auto shortType =
            gShortTypePaths.find(std::string(owner));
        if (shortType == gShortTypePaths.end()
            || shortType->second.empty()) {
            return std::nullopt;
        }
        type = gTypes.find(shortType->second);
    }
    if (type == gTypes.end() || !type->second.Size)
        return std::nullopt;
    return type->second.Size;
}
