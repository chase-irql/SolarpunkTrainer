#include "Injector.h"

#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <memory>
#include <string_view>

namespace {

    constexpr std::wstring_view GameExecutable =
        L"SolarpunkSteam-Win64-Shipping.exe";
    constexpr DWORD InjectionTimeoutMilliseconds = 15000;

    struct HandleCloser {
        void operator()(HANDLE handle) const {
            if (handle && handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
        }
    };

    using UniqueHandle =
        std::unique_ptr<void, HandleCloser>;

    std::string Win32Message(
        std::string_view prefix,
        DWORD error = GetLastError()) {
        std::array<char, 512> buffer{};
        const DWORD length = FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM
                | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr);

        std::string result(prefix);
        if (length) {
            while (!result.empty()
                && std::isspace(
                    static_cast<unsigned char>(
                        result.back()))) {
                result.pop_back();
            }
            result.append(": ");
            result.append(buffer.data(), length);
            while (!result.empty()
                && std::isspace(
                    static_cast<unsigned char>(
                        result.back()))) {
                result.pop_back();
            }
        }
        else {
            result.append(" (Win32 ");
            result.append(std::to_string(error));
            result.push_back(')');
        }
        return result;
    }

    bool EqualInsensitive(
        std::wstring_view left,
        std::wstring_view right) {
        return left.size() == right.size()
            && std::equal(
                left.begin(),
                left.end(),
                right.begin(),
                [](wchar_t a, wchar_t b) {
                    return std::towlower(a)
                        == std::towlower(b);
                });
    }

    uintptr_t FindRemoteModuleBase(
        DWORD processId,
        std::wstring_view moduleName) {
        UniqueHandle snapshot(
            CreateToolhelp32Snapshot(
                TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                processId));
        if (snapshot.get() == INVALID_HANDLE_VALUE)
            return 0;

        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        if (!Module32FirstW(snapshot.get(), &module))
            return 0;

        do {
            if (EqualInsensitive(
                module.szModule,
                moduleName)) {
                return reinterpret_cast<uintptr_t>(
                    module.modBaseAddr);
            }
        } while (Module32NextW(snapshot.get(), &module));
        return 0;
    }

    bool IsAmd64Image(
        const std::filesystem::path& path) {
        UniqueHandle file(CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        if (file.get() == INVALID_HANDLE_VALUE)
            return false;

        IMAGE_DOS_HEADER dos{};
        DWORD bytesRead = 0;
        if (!ReadFile(
            file.get(),
            &dos,
            sizeof(dos),
            &bytesRead,
            nullptr)
            || bytesRead != sizeof(dos)
            || dos.e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }

        LARGE_INTEGER offset{};
        offset.QuadPart = dos.e_lfanew;
        if (!SetFilePointerEx(
            file.get(),
            offset,
            nullptr,
            FILE_BEGIN)) {
            return false;
        }

        IMAGE_NT_HEADERS64 headers{};
        if (!ReadFile(
            file.get(),
            &headers,
            sizeof(headers),
            &bytesRead,
            nullptr)
            || bytesRead != sizeof(headers)) {
            return false;
        }
        return headers.Signature == IMAGE_NT_SIGNATURE
            && headers.FileHeader.Machine
                == IMAGE_FILE_MACHINE_AMD64
            && headers.OptionalHeader.Magic
                == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    }

} // namespace

DWORD Loader::FindSolarpunkProcess() {
    UniqueHandle snapshot(
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (!Process32FirstW(snapshot.get(), &process))
        return 0;

    do {
        if (EqualInsensitive(
            process.szExeFile,
            GameExecutable)) {
            return process.th32ProcessID;
        }
    } while (Process32NextW(snapshot.get(), &process));
    return 0;
}

std::filesystem::path Loader::ResolveTrainerPath() {
    std::array<wchar_t, 32768> modulePath{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (!length || length >= modulePath.size())
        return {};

    const std::filesystem::path directory =
        std::filesystem::path(
            std::wstring_view(modulePath.data(), length))
            .parent_path();
    const std::filesystem::path primary =
        directory / L"SolarpunkTrainer.dll";
    return primary;
}

std::filesystem::path Loader::ResolveSchemaProbePath() {
    std::array<wchar_t, 32768> modulePath{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (!length || length >= modulePath.size())
        return {};

    return std::filesystem::path(
        std::wstring_view(modulePath.data(), length))
        .parent_path()
        / L"SolarpunkSchemaProbe.dll";
}

bool Loader::IsTrainerLoaded(
    DWORD processId,
    const std::filesystem::path& trainerPath) {
    return processId
        && !trainerPath.empty()
        && FindRemoteModuleBase(
        processId,
        trainerPath.filename().wstring()) != 0;
}

Loader::InjectionResult Loader::InjectLibrary(
    DWORD processId,
    const std::filesystem::path& libraryPath,
    bool requireResident,
    std::string_view displayName) {
    const std::string subject(displayName);
    if (!processId)
        return { false, "Solarpunk is not running." };
    if (libraryPath.empty()
        || !std::filesystem::is_regular_file(libraryPath)) {
        return {
            false,
            subject + " DLL is missing beside the loader."
        };
    }
    if (!IsAmd64Image(libraryPath)) {
        return {
            false,
            subject + " DLL is not a valid x64 image."
        };
    }
    if (requireResident
        && FindRemoteModuleBase(
            processId,
            libraryPath.filename().wstring())) {
        return {
            true,
            subject + " is already loaded."
        };
    }

    UniqueHandle process(OpenProcess(
        PROCESS_CREATE_THREAD
            | PROCESS_QUERY_INFORMATION
            | PROCESS_VM_OPERATION
            | PROCESS_VM_WRITE
            | PROCESS_VM_READ,
        FALSE,
        processId));
    if (!process) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            return {
                false,
                "Access denied. Match the game's privilege level and retry."
            };
        }
        return {
            false,
            Win32Message("Could not open the game", error)
        };
    }

    const std::wstring absolutePath =
        std::filesystem::absolute(libraryPath).wstring();
    const SIZE_T pathBytes =
        (absolutePath.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(
        process.get(),
        nullptr,
        pathBytes,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE);
    if (!remotePath) {
        return {
            false,
            Win32Message("Could not allocate loader memory")
        };
    }

    const auto releaseRemotePath = [&]() {
        VirtualFreeEx(
            process.get(),
            remotePath,
            0,
            MEM_RELEASE);
    };

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(
        process.get(),
        remotePath,
        absolutePath.c_str(),
        pathBytes,
        &bytesWritten)
        || bytesWritten != pathBytes) {
        const std::string message =
            Win32Message(
                "Could not copy the " + subject + " path");
        releaseRemotePath();
        return { false, message };
    }

    HMODULE localKernel32 =
        GetModuleHandleW(L"kernel32.dll");
    const FARPROC localLoadLibrary =
        localKernel32
            ? GetProcAddress(
                localKernel32,
                "LoadLibraryW")
            : nullptr;
    const uintptr_t remoteKernel32 =
        FindRemoteModuleBase(processId, L"kernel32.dll");
    if (!localKernel32
        || !localLoadLibrary
        || !remoteKernel32) {
        releaseRemotePath();
        return {
            false,
            "Could not resolve the target loader routine."
        };
    }

    const uintptr_t loadLibraryRva =
        reinterpret_cast<uintptr_t>(localLoadLibrary)
        - reinterpret_cast<uintptr_t>(localKernel32);
    const auto remoteLoadLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(
            remoteKernel32 + loadLibraryRva);

    UniqueHandle thread(CreateRemoteThread(
        process.get(),
        nullptr,
        0,
        remoteLoadLibrary,
        remotePath,
        0,
        nullptr));
    if (!thread) {
        const std::string message =
            Win32Message("Could not start the " + subject);
        releaseRemotePath();
        return { false, message };
    }

    const DWORD wait = WaitForSingleObject(
        thread.get(),
        InjectionTimeoutMilliseconds);
    if (wait != WAIT_OBJECT_0) {
        // The remote thread still owns the path when it times out, so leave
        // that small allocation in place instead of creating a use-after-free.
        return {
            false,
            wait == WAIT_TIMEOUT
                ? "The game did not finish loading the "
                    + subject + " in time."
                : Win32Message(
                    subject + " load wait failed")
        };
    }

    DWORD moduleResult = 0;
    if (!GetExitCodeThread(thread.get(), &moduleResult)) {
        const DWORD error = GetLastError();
        releaseRemotePath();
        return {
            false,
            Win32Message(
                "Could not read the " + subject + " load result",
                error)
        };
    }
    if (moduleResult == 0) {
        releaseRemotePath();
        return {
            false,
            "The game rejected the " + subject + " DLL."
        };
    }

    releaseRemotePath();
    if (requireResident
        && !FindRemoteModuleBase(
            processId,
            libraryPath.filename().wstring())) {
        return {
            false,
            subject + " unloaded before initialization completed."
        };
    }
    return {
        true,
        requireResident
            ? subject + " loaded successfully."
            : subject + " started successfully."
    };
}

Loader::InjectionResult Loader::InjectTrainer(
    DWORD processId,
    const std::filesystem::path& trainerPath) {
    return InjectLibrary(
        processId,
        trainerPath,
        true,
        "Trainer");
}
