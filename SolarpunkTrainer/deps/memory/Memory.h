#pragma once
#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

#pragma comment(lib, "psapi.lib")

namespace seh {

    bool    IsBadPtr(const void* ptr, size_t size);

    template<typename T>
    bool SafeRead(const void* src, T& out) {
        __try {
            out = *static_cast<const T*>(src);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    template<typename T>
    bool SafeWrite(void* dst, const T& val) {
        __try {
            *static_cast<T*>(dst) = val;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

} // namespace seh


class Memory {
public:
    Memory() = delete;
    ~Memory() = delete;

    template<typename T>
    static T Read(uintptr_t address) {
        T out{};
        seh::SafeRead(reinterpret_cast<const void*>(address), out);
        return out;
    }

    template<typename T>
    static std::optional<T> TryRead(uintptr_t address) {
        T out{};
        if (!seh::SafeRead(reinterpret_cast<const void*>(address), out))
            return std::nullopt;
        return out;
    }

    static bool ReadBytes(uintptr_t address, void* buffer, size_t size);
    static bool WriteBytes(uintptr_t address, const void* buffer, size_t size);

    template<typename T>
    static bool Write(uintptr_t address, const T& value) {
        return seh::SafeWrite(reinterpret_cast<void*>(address), value);
    }

    template<typename T>
    static bool WritePatch(uintptr_t address, const T& value) {
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(T),
            PAGE_EXECUTE_READWRITE, &old);
        const bool ok = seh::SafeWrite(reinterpret_cast<void*>(address), value);
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), old, &old);
        return ok;
    }

    static bool Nop(uintptr_t address, size_t count);

    template<typename T>
    static std::optional<T> ResolveChain(uintptr_t base,
        std::initializer_list<uintptr_t> offsets)
    {
        uintptr_t addr = base;
        const auto* it = offsets.begin();
        const auto* end = offsets.end();

        for (; it != end; ++it) {
            addr += *it;
            if (std::next(it) != end) {
                const auto next = TryRead<uintptr_t>(addr);
                if (!next || !*next) return std::nullopt;
                addr = *next;
            }
        }
        return TryRead<T>(addr);
    }

    static uintptr_t ResolveRip(uintptr_t addr, size_t instrLen);

    struct ModuleInfo {
        uintptr_t base = 0;
        size_t    size = 0;
        explicit operator bool() const { return base != 0 && size != 0; }
    };

    static uintptr_t  GetModuleBase(const wchar_t* name = nullptr);
    static ModuleInfo GetModuleInfo(const wchar_t* name = nullptr);

    static uintptr_t              PatternScan(uintptr_t start, size_t regionSize,
        const char* pattern);

    static uintptr_t              PatternScanModule(const wchar_t* moduleName,
        const char* pattern);

    static std::vector<uintptr_t> PatternScanAll(uintptr_t start, size_t regionSize,
        const char* pattern);

    template<typename T>
    static std::vector<uintptr_t> ScanValue(uintptr_t start, size_t regionSize,
        const T& value)
    {
        std::vector<uintptr_t> hits;
        for (size_t i = 0; i + sizeof(T) <= regionSize; ++i) {
            T v{};
            if (seh::SafeRead(reinterpret_cast<const void*>(start + i), v) && v == value)
                hits.push_back(start + i);
        }
        return hits;
    }

    static bool IsValidPtr(uintptr_t address, size_t readSize = sizeof(void*));

private:
    struct ParsedPattern {
        std::vector<uint8_t> bytes;
        std::vector<bool>    mask; // true = must match, false = wildcard
    };

    static ParsedPattern ParsePattern(const char* pattern);
};