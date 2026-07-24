#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Loader {

    struct InjectionResult {
        bool Success = false;
        std::string Message;
    };

    DWORD FindSolarpunkProcess();
    std::filesystem::path ResolveTrainerPath();
    std::filesystem::path ResolveSchemaProbePath();
    bool IsTrainerLoaded(
        DWORD processId,
        const std::filesystem::path& trainerPath);
    InjectionResult InjectLibrary(
        DWORD processId,
        const std::filesystem::path& libraryPath,
        bool requireResident,
        std::string_view displayName);
    InjectionResult InjectTrainer(
        DWORD processId,
        const std::filesystem::path& trainerPath);

} // namespace Loader
