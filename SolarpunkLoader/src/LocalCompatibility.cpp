#include "LocalCompatibility.h"

#include "Injector.h"

#include <bcrypt.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace {

    using json = nlohmann::json;
    constexpr auto SchemaTimeout = std::chrono::seconds(180);

    struct HandleCloser {
        void operator()(HANDLE handle) const {
            if (handle && handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
        }
    };

    using UniqueHandle =
        std::unique_ptr<void, HandleCloser>;

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

    bool QueryExecutablePath(
        DWORD processId,
        std::filesystem::path& output) {
        UniqueHandle process(OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId));
        if (!process)
            return false;

        std::array<wchar_t, 32768> path{};
        DWORD length = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(
            process.get(),
            0,
            path.data(),
            &length)
            || !length) {
            return false;
        }
        output = std::filesystem::path(
            std::wstring_view(path.data(), length));
        return true;
    }

    std::filesystem::path CacheRoot() {
        std::array<wchar_t, 32768> localAppData{};
        const DWORD length = GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            localAppData.data(),
            static_cast<DWORD>(localAppData.size()));
        if (!length || length >= localAppData.size())
            return {};
        return std::filesystem::path(
            std::wstring_view(localAppData.data(), length))
            / L"Solarpunk Trainer"
            / L"SchemaCache";
    }

    bool InspectBuild(
        DWORD processId,
        Loader::BuildIdentity& identity,
        std::string& error) {
        if (!QueryExecutablePath(
            processId,
            identity.ExecutablePath)) {
            error = "Could not locate the running game executable.";
            return false;
        }

        std::ifstream stream(
            identity.ExecutablePath,
            std::ios::binary);
        if (!stream) {
            error = "Could not read the installed game executable.";
            return false;
        }

        stream.seekg(0, std::ios::end);
        const std::streamoff fileSize = stream.tellg();
        if (fileSize <= 0) {
            error = "The installed game executable is empty.";
            return false;
        }
        identity.FileSize =
            static_cast<std::uint64_t>(fileSize);
        stream.seekg(0, std::ios::beg);

        IMAGE_DOS_HEADER dos{};
        stream.read(
            reinterpret_cast<char*>(&dos),
            sizeof(dos));
        if (!stream
            || dos.e_magic != IMAGE_DOS_SIGNATURE
            || dos.e_lfanew <= 0) {
            error = "The game executable has an invalid DOS header.";
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
            error = "The game executable is not a valid x64 PE image.";
            return false;
        }
        identity.TimeDateStamp =
            nt.FileHeader.TimeDateStamp;
        identity.SizeOfImage =
            nt.OptionalHeader.SizeOfImage;

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
            if (!stream) {
                error = "Could not read the game section table.";
                return false;
            }
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
            error = "The game executable has no readable .text section.";
            return false;
        }

        std::vector<std::uint8_t> bytes(text.SizeOfRawData);
        stream.seekg(text.PointerToRawData, std::ios::beg);
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!stream
            || !HashSha256(
                bytes.data(),
                bytes.size(),
                identity.TextSha256)) {
            error = "Could not fingerprint the game code section.";
            return false;
        }

        std::ostringstream fingerprint;
        fingerprint
            << std::hex
            << std::setfill('0')
            << std::setw(8)
            << identity.TimeDateStamp
            << '-'
            << std::setw(8)
            << identity.SizeOfImage
            << '-'
            << identity.TextSha256;
        identity.Fingerprint = fingerprint.str();

        const std::filesystem::path root = CacheRoot();
        if (root.empty()) {
            error = "LOCALAPPDATA is unavailable for the schema cache.";
            return false;
        }
        identity.SchemaPath =
            root
            / std::filesystem::path(identity.Fingerprint)
            / L"runtime-schema.json";
        return true;
    }

    bool PositiveInteger(
        const json& value,
        const char* key) {
        const auto iterator = value.find(key);
        return iterator != value.end()
            && iterator->is_number_integer()
            && iterator->get<std::int64_t>() > 0;
    }

    bool ValidateSchema(
        const Loader::BuildIdentity& identity,
        std::string& error) {
        std::ifstream stream(
            identity.SchemaPath,
            std::ios::binary);
        if (!stream) {
            error = "The local analyzer did not create a schema.";
            return false;
        }

        try {
            const json schema = json::parse(stream);
            if (schema.value("format_version", 0) != 1) {
                error = "The cached schema format is unsupported.";
                return false;
            }
            if (!schema.contains("build")
                || schema["build"].value(
                    "fingerprint",
                    std::string{}) != identity.Fingerprint) {
                error = "The cached schema belongs to another game build.";
                return false;
            }
            if (schema.value("type_count", 0) < 100
                || !schema.contains("types")
                || !schema["types"].is_array()
                || schema["types"].size()
                    != schema["type_count"].get<std::size_t>()) {
                error = "The cached reflection snapshot is incomplete.";
                return false;
            }

            const json& globals =
                schema.at("core").at("globals");
            if (!PositiveInteger(globals, "gobjects_rva")
                || !PositiveInteger(globals, "gnames_rva")
                || !PositiveInteger(globals, "gworld_rva")
                || !PositiveInteger(
                    globals,
                    "process_event_rva")
                || !PositiveInteger(
                    globals,
                    "process_event_index")) {
                error =
                    "The analyzer could not validate all Unreal globals.";
                return false;
            }

            const json& objectArray =
                schema.at("core").at("object_array");
            const json& namePool =
                schema.at("core").at("name_pool");
            const json& uobject =
                schema.at("core").at("uobject");
            const json& ufield =
                schema.at("core").at("ufield");
            const json& property =
                schema.at("core").at("property");
            if (!PositiveInteger(objectArray, "item_size")
                || !PositiveInteger(
                    namePool,
                    "block_offset_bits")
                || !PositiveInteger(
                    namePool,
                    "entry_stride")
                || !PositiveInteger(uobject, "class")
                || !PositiveInteger(uobject, "name")
                || !PositiveInteger(ufield, "next")
                || !PositiveInteger(property, "element_size")
                || !PositiveInteger(
                    property,
                    "offset_internal")) {
                error =
                    "The analyzer produced incomplete engine layouts.";
                return false;
            }
        }
        catch (const std::exception&) {
            error = "The cached schema is malformed.";
            return false;
        }
        return true;
    }

    bool ProcessStillRunning(DWORD processId) {
        UniqueHandle process(OpenProcess(
            SYNCHRONIZE,
            FALSE,
            processId));
        return process
            && WaitForSingleObject(process.get(), 0)
                == WAIT_TIMEOUT;
    }

} // namespace

Loader::CompatibilityResult
Loader::EnsureLocalCompatibility(
    DWORD processId,
    const std::filesystem::path& schemaProbePath) {
    CompatibilityResult result{};
    std::string error;
    if (!InspectBuild(processId, result.Build, error)) {
        result.Message = std::move(error);
        return result;
    }

    std::error_code fileError;
    if (std::filesystem::is_regular_file(
        result.Build.SchemaPath,
        fileError)) {
        if (ValidateSchema(result.Build, error)) {
            result.Success = true;
            result.Message =
                "Installed build recognized from the local schema cache.";
            return result;
        }
        std::filesystem::remove(
            result.Build.SchemaPath,
            fileError);
    }

    if (!std::filesystem::is_regular_file(
        schemaProbePath,
        fileError)) {
        result.Message =
            "SolarpunkSchemaProbe.dll is missing beside the loader.";
        return result;
    }

    const InjectionResult injection = InjectLibrary(
        processId,
        schemaProbePath,
        false,
        "Local schema analyzer");
    if (!injection.Success) {
        result.Message = injection.Message;
        return result;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + SchemaTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!ProcessStillRunning(processId)) {
            result.Message =
                "Solarpunk closed during local compatibility analysis.";
            return result;
        }
        fileError.clear();
        if (std::filesystem::is_regular_file(
            result.Build.SchemaPath,
            fileError)) {
            if (ValidateSchema(result.Build, error)) {
                result.Success = true;
                result.Message =
                    "Local game analysis completed. No download was used.";
                return result;
            }
            result.Message = std::move(error);
            return result;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100));
    }

    result.Message =
        "Local game analysis timed out. Retry after the world is loaded.";
    return result;
}
