#pragma once

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace Loader {

    struct BuildIdentity {
        std::uint32_t TimeDateStamp = 0;
        std::uint32_t SizeOfImage = 0;
        std::uint64_t FileSize = 0;
        std::string TextSha256;
        std::string Fingerprint;
        std::filesystem::path ExecutablePath;
        std::filesystem::path SchemaPath;
    };

    struct CompatibilityResult {
        bool Success = false;
        std::string Message;
        BuildIdentity Build;
    };

    CompatibilityResult EnsureLocalCompatibility(
        DWORD processId,
        const std::filesystem::path& schemaProbePath);

} // namespace Loader
